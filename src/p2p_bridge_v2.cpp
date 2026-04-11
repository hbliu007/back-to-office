/**
 * @file p2p_bridge_v2.cpp
 * @brief BTO P2P 桥接层实现 — 单例 P2PClient + 多通道架构
 */

#include "p2p_bridge_v2.hpp"
#include "daemon/session_lifecycle.hpp"
#include "observability/error_codes.hpp"
#include "p2p/utils/structured_log.hpp"

#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>
#include <system_error>
#include <vector>

namespace bto {

using boost::asio::ip::tcp;

namespace {

auto bridge_event(const ConnectBridge& bridge,
                  std::string_view component,
                  std::string_view event) -> p2p::utils::Json {
    auto payload = p2p::utils::make_structured_event("bto-bridge", component, event);
    p2p::utils::put_if_not_empty(payload, "trace_id", bridge.trace_id());
    p2p::utils::put_if_not_empty(payload, "did", bridge.did());
    p2p::utils::put_if_not_empty(payload, "peer_did", bridge.peer_did());
    return payload;
}

}  // namespace

// ─── TunnelSession ─────────────────────────────────────────────

TunnelSession::TunnelSession(int id,
                             boost::asio::io_context& ioc,
                             std::shared_ptr<tcp::socket> socket,
                             std::weak_ptr<ConnectBridge> bridge)
    : id_(id)
    , ioc_(ioc)
    , tcp_socket_(std::move(socket))
    , bridge_(std::move(bridge))
{}

TunnelSession::~TunnelSession() {
    close();
}

void TunnelSession::start() {
    spdlog::info("[bto] #{} 会话启动 (channel={})", id_, channel_id_);
    read_tcp();
}

void TunnelSession::close() {
    if (closed_) return;
    closed_ = true;

    spdlog::info("[bto] #{} 关闭会话 (channel={})", id_, channel_id_);

    if (tcp_socket_ && tcp_socket_->is_open()) {
        boost::system::error_code ec;
        tcp_socket_->shutdown(tcp::socket::shutdown_both, ec);
        tcp_socket_->close(ec);
    }

    if (on_cleanup_) {
        on_cleanup_(id_);
    }
}

void TunnelSession::on_p2p_data(const std::vector<uint8_t>& data) {
    if (closed_) return;
    if (!tcp_socket_ || !tcp_socket_->is_open()) return;

    spdlog::debug("[bto] #{} P2P 收到 {} 字节 (channel={})", id_, data.size(), channel_id_);

    bool start_write = false;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (write_queue_.size() >= kMaxWriteQueueSize) {
            spdlog::error("[bto] #{} write queue overflow (channel={}), closing", id_, channel_id_);
            boost::asio::post(ioc_, [self = shared_from_this()]() { self->close(); });
            return;
        }
        write_queue_.push_back(std::make_shared<std::vector<uint8_t>>(data));
        if (!writing_) {
            writing_ = true;
            start_write = true;
        }
    }
    if (start_write) {
        do_write();
    }
}

void TunnelSession::read_tcp() {
    if (closed_) return;
    if (!tcp_socket_ || !tcp_socket_->is_open()) return;

    auto self = shared_from_this();
    auto buf = std::make_shared<std::vector<uint8_t>>(kTcpReadBufferSize);

    tcp_socket_->async_read_some(
        boost::asio::buffer(*buf),
        [self, buf](const boost::system::error_code& ec, std::size_t n) {
            if (ec) {
                if (ec != boost::asio::error::eof &&
                    ec != boost::asio::error::operation_aborted) {
                    spdlog::warn("[bto] #{} TCP 读取错误: {}", self->id_, ec.message());
                    if (auto bridge = self->bridge_.lock()) {
                        auto event =
                            bridge_event(*bridge, "session", "session.tcp.read_failed");
                        event["channel_id"] = self->channel_id_;
                        event["error_code"] = observability::code::kBridgeTcpReadFailed;
                        event["error_detail"] = ec.message();
                        p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
                    }
                }
                self->close();
                return;
            }

            spdlog::debug("[bto] #{} TCP 收到 {} 字节 (channel={})", self->id_, n, self->channel_id_);

            if (n > 0 && self->channel_id_ >= 0) {
                buf->resize(n);
                if (auto bridge = self->bridge_.lock()) {
                    bridge->send_to_p2p(self->channel_id_, *buf);
                } else {
                    self->close();
                    return;
                }
            }

            self->read_tcp();
        }
    );
}

void TunnelSession::do_write() {
    if (closed_) return;

    std::shared_ptr<std::vector<uint8_t>> data;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (write_queue_.empty()) {
            writing_ = false;
            return;
        }
        data = write_queue_.front();
        write_queue_.pop_front();
    }

    auto self = shared_from_this();
    boost::asio::async_write(
        *tcp_socket_,
        boost::asio::buffer(*data),
        [self, data](const boost::system::error_code& ec, std::size_t) {
            if (ec) {
                spdlog::warn("[bto] #{} TCP 写入错误: {}", self->id_, ec.message());
                if (auto bridge = self->bridge_.lock()) {
                    auto event = bridge_event(*bridge, "session", "session.tcp.write_failed");
                    event["channel_id"] = self->channel_id_;
                    event["error_code"] = observability::code::kBridgeTcpWriteFailed;
                    event["error_detail"] = ec.message();
                    p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
                }
                self->close();
                return;
            }

            self->do_write();
        }
    );
}

// ─── ConnectBridge ─────────────────────────────────────────────

ConnectBridge::ConnectBridge(boost::asio::io_context& ioc,
                             const std::string& did,
                             const std::string& peer_did,
                             const p2p::core::P2PConfig& config,
                             uint16_t listen_port)
    : ioc_(ioc)
    , did_(did)
    , peer_did_(peer_did)
    , config_(config)
    , listen_port_(listen_port)
    , acceptor_(ioc)
    , reconnect_timer_(ioc)
{}

ConnectBridge::~ConnectBridge() {
    stop();
}

bool ConnectBridge::start() {
    try {
        tcp::endpoint endpoint(tcp::v4(), listen_port_);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        spdlog::info("[bto] 监听端口 {}", listen_port_);
        auto event = bridge_event(*this, "bridge", "bridge.listen.ready");
        event["local_port"] = listen_port_;
        p2p::utils::emit_structured_log(spdlog::level::info, std::move(event));

        initialize_p2p_client();
        do_accept();

        return true;
    } catch (const std::exception& e) {
        spdlog::error("[bto] 启动失败: {}", e.what());
        auto event = bridge_event(*this, "bridge", "bridge.start_failed");
        event["local_port"] = listen_port_;
        event["error_code"] = observability::code::kBridgeStartFailed;
        event["error_detail"] = e.what();
        p2p::utils::emit_structured_log(spdlog::level::err, std::move(event));
        return false;
    }
}

void ConnectBridge::stop() {
    if (stopping_) return;
    stopping_ = true;
    p2p_connected_ = false;
    reconnecting_ = false;
    reconnect_timer_.cancel();

    spdlog::info("[bto] 停止桥接服务...");
    auto event = bridge_event(*this, "bridge", "bridge.stop");
    event["local_port"] = listen_port_;
    event["active_sessions"] = active_session_count();
    p2p::utils::emit_structured_log(spdlog::level::info, std::move(event));

    boost::system::error_code ec;
    acceptor_.close(ec);

    std::map<int, std::shared_ptr<TunnelSession>> sessions_to_close;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_to_close.swap(sessions_);
        channel_to_session_.clear();
    }

    for (auto& [id, session] : sessions_to_close) {
        session->close();
    }

    reset_p2p_client();

    auto on_stopped = on_stopped_;
    if (on_stopped) {
        boost::asio::post(ioc_, [on_stopped]() {
            on_stopped();
        });
    }
}

std::size_t ConnectBridge::active_session_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.size();
}

void ConnectBridge::set_ssh_hint(const std::string& user, const std::string& key) {
    ssh_user_ = user;
    ssh_key_ = key;
}

void ConnectBridge::initialize_p2p_client() {
    p2p_client_ = std::make_shared<p2p::core::P2PClient>(ioc_, did_, config_);

    auto self = shared_from_this();

    p2p_client_->on_connected([self]() {
        spdlog::info("[bto] P2P 已连接 (DID={})", self->did_);
        spdlog::info("[bto] 路径: {}",
                  p2p::core::P2PClient::ToString(self->p2p_client_->active_path()));
        self->reseed_existing_channels_for_reconnect();
        self->p2p_connected_ = true;
        self->ever_connected_ = true;
        self->reconnecting_ = false;
        self->reconnect_attempts_ = 0;
        std::vector<int> channels_needing_probe;
        {
            std::lock_guard<std::mutex> lock(self->sessions_mutex_);
            for (auto& [channel_id, handshake] : self->channel_handshakes_) {
                if (self->uses_relay_channel_handshake()) {
                    if (handshake.needs_open_probe()) {
                        channels_needing_probe.push_back(channel_id);
                    }
                } else {
                    handshake.mark_remote_ready();
                }
            }
        }
        for (int channel_id : channels_needing_probe) {
            self->maybe_send_open_probe(channel_id);
        }
        if (!self->last_disconnect_detail_.empty()) {
            spdlog::info("[bto] 已恢复连接，上次断开原因: {}", self->last_disconnect_detail_);
        }
        auto event = bridge_event(*self, "bridge", "p2p.connect.ready");
        event["local_port"] = self->listen_port_;
        event["active_sessions"] = self->active_session_count();
        event["path"] = p2p::core::P2PClient::ToString(self->p2p_client_->active_path());
        p2p::utils::emit_structured_log(spdlog::level::info, std::move(event));
        self->flush_pending_channel_data();
    });

    p2p_client_->on_disconnected([self]() {
        self->p2p_connected_ = false;
        const auto reason =
            self->p2p_client_
                ? (std::string(
                       p2p::core::P2PClient::ToString(self->p2p_client_->last_failure_reason())) +
                   " — " + self->p2p_client_->last_failure_detail())
                : std::string("unknown");
        self->last_disconnect_detail_ = reason;
        spdlog::warn("[bto] P2P 断开连接: {}，当前会话数={}", reason, self->active_session_count());
        auto event = bridge_event(*self, "bridge", "p2p.connect.disconnected");
        event["local_port"] = self->listen_port_;
        event["active_sessions"] = self->active_session_count();
        event["error_code"] = observability::code::kBridgeDisconnected;
        event["error_detail"] = reason;
        p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
        const auto action =
            bto::daemon::disconnect_action_for_bridge(self->stopping_, self->active_session_count());
        if (action == bto::daemon::DisconnectAction::reconnect) {
            self->schedule_reconnect(reason);
            return;
        }
        self->stop();
    });

    p2p_client_->on_data([self](int channel_id, const std::vector<uint8_t>& data) {
        self->on_p2p_data(channel_id, data);
    });

    p2p_client_->on_error([self](const std::error_code& ec, const std::string& msg) {
        spdlog::warn("[bto] P2P 错误: {} ({})", msg, ec.message());
        auto event = bridge_event(*self, "bridge", "p2p.connect.error");
        event["error_code"] = observability::code::kBridgeDisconnected;
        event["error_detail"] = msg + " (" + ec.message() + ")";
        p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
    });

    spdlog::info("[bto] 初始化 P2PClient (DID={})", did_);
    p2p_client_->initialize([self](const std::error_code& ec) {
        if (ec) {
            auto message = "P2PClient initialize failed: " + ec.message();
            spdlog::error("[bto] P2PClient 初始化失败: {}", ec.message());
            auto event = bridge_event(*self, "bridge", "p2p.initialize.failed");
            event["error_code"] = observability::code::kBridgeInitFailed;
            event["error_detail"] = ec.message();
            p2p::utils::emit_structured_log(spdlog::level::err, std::move(event));
            if (self->ever_connected_ || self->reconnecting_) {
                self->schedule_reconnect(message);
                return;
            }
            if (self->on_failed_) {
                self->on_failed_(message);
            }
            self->stop();
            return;
        }

        spdlog::info("[bto] 连接到 {} ...", self->peer_did_);
        self->p2p_client_->connect(self->peer_did_, [self](const std::error_code& ec) {
            if (ec) {
                auto message = "P2P connect failed: " + ec.message();
                spdlog::error("[bto] P2P 连接失败: {}", ec.message());
                auto event = bridge_event(*self, "bridge", "p2p.connect.failed");
                event["error_code"] = observability::code::kBridgeConnectFailed;
                event["error_detail"] = ec.message();
                p2p::utils::emit_structured_log(spdlog::level::err, std::move(event));
                if (self->ever_connected_ || self->reconnecting_) {
                    self->schedule_reconnect(message);
                    return;
                }
                if (self->on_failed_) {
                    self->on_failed_(message);
                }
                self->stop();
                return;
            }
            spdlog::info("[bto] P2P 连接成功，等待 SSH 连入...");
            if (self->on_ready_) self->on_ready_();
        });
    });
}

void ConnectBridge::schedule_reconnect(const std::string& reason) {
    if (stopping_ || reconnecting_) {
        return;
    }
    reconnecting_ = true;
    ++reconnect_attempts_;
    spdlog::warn("[bto] 准备重连 relay（第 {} 次），原因: {}", reconnect_attempts_, reason);
    auto event = bridge_event(*this, "bridge", "p2p.reconnect.scheduled");
    event["attempt"] = reconnect_attempts_;
    event["error_detail"] = reason;
    p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
    // Exponential backoff: 2s, 4s, 8s, 16s, 32s, 60s max
    const auto delay_sec = std::min<std::size_t>(
        std::size_t{2} << std::min(reconnect_attempts_ - 1, std::size_t{5}), 60);
    spdlog::warn("[bto] 将在 {}s 后重连...", delay_sec);
    reconnect_timer_.expires_after(std::chrono::seconds(delay_sec));
    auto self = shared_from_this();
    reconnect_timer_.async_wait([self](const boost::system::error_code& ec) {
        if (ec || self->stopping_) {
            return;
        }
        self->reconnecting_ = false;
        self->reset_p2p_client();
        self->initialize_p2p_client();
    });
}

void ConnectBridge::reset_p2p_client() {
    if (!p2p_client_) {
        return;
    }
    p2p_client_->on_connected(nullptr);
    p2p_client_->on_disconnected(nullptr);
    p2p_client_->on_data(nullptr);
    p2p_client_->on_error(nullptr);
    p2p_client_->close();
    p2p_client_.reset();
}

auto ConnectBridge::uses_relay_channel_handshake() const -> bool {
    return p2p_client_ &&
           bto::daemon::should_use_channel_handshake(p2p_client_->active_path());
}

void ConnectBridge::reseed_existing_channels_for_reconnect() {
    if (!p2p_client_) {
        return;
    }
    int max_channel_id = 0;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (const auto& [channel_id, session_id] : channel_to_session_) {
            (void)session_id;
            max_channel_id = std::max(max_channel_id, channel_id);
        }
        for (auto& [channel_id, handshake] : channel_handshakes_) {
            (void)channel_id;
            if (uses_relay_channel_handshake()) {
                handshake.on_reconnect();
            } else {
                handshake.mark_remote_ready();
            }
        }
    }
    for (int next = 0; next < max_channel_id; ++next) {
        (void)p2p_client_->create_channel();
    }
}

auto ConnectBridge::queue_pending_channel_data(int channel_id, const std::vector<uint8_t>& data)
    -> bool {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    const auto buffered = pending_channel_bytes_[channel_id];
    const auto next_total = buffered + data.size();
    if (next_total > kMaxBufferedBytesPerChannel) {
        auto event = bridge_event(*this, "bridge", "session.buffer.overflow");
        event["channel_id"] = channel_id;
        event["buffered_bytes"] = next_total;
        event["error_code"] = observability::code::kBridgeBufferOverflow;
        p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
        return false;
    }
    pending_channel_data_[channel_id].push_back(data);
    pending_channel_bytes_[channel_id] = next_total;
    return true;
}

void ConnectBridge::close_session_for_channel(int channel_id, const std::string& reason) {
    std::shared_ptr<TunnelSession> session;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        const auto channel_it = channel_to_session_.find(channel_id);
        if (channel_it == channel_to_session_.end()) {
            return;
        }
        const auto session_it = sessions_.find(channel_it->second);
        if (session_it == sessions_.end()) {
            return;
        }
        session = session_it->second;
    }
    spdlog::warn("[bto] 关闭会话，原因: {} (channel={})", reason, channel_id);
    auto event = bridge_event(*this, "session", "session.closed");
    event["channel_id"] = channel_id;
    event["error_detail"] = reason;
    p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
    session->close();
}

void ConnectBridge::flush_pending_channel_data() {
    std::map<int, std::deque<std::vector<uint8_t>>> pending;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        pending.swap(pending_channel_data_);
        pending_channel_bytes_.clear();
    }

    for (auto& [channel_id, queue] : pending) {
        for (auto& frame : queue) {
            send_to_p2p(channel_id, frame);
        }
    }
}

void ConnectBridge::flush_pending_channel_data(int channel_id) {
    std::deque<std::vector<uint8_t>> pending;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = pending_channel_data_.find(channel_id);
        if (it == pending_channel_data_.end()) {
            return;
        }
        pending.swap(it->second);
        pending_channel_data_.erase(it);
        pending_channel_bytes_.erase(channel_id);
    }

    for (auto& frame : pending) {
        send_to_p2p(channel_id, frame);
    }
}

void ConnectBridge::maybe_send_open_probe(int channel_id) {
    if (!p2p_client_ || !p2p_connected_ || !uses_relay_channel_handshake()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = channel_handshakes_.find(channel_id);
        if (it == channel_handshakes_.end() || !it->second.needs_open_probe()) {
            return;
        }
        it->second.mark_open_probe_sent();
    }

    auto weak_self = weak_from_this();
    p2p_client_->send_data(channel_id, {}, [weak_self, channel_id](const std::error_code& ec) {
        auto self = weak_self.lock();
        if (!self || !ec) {
            return;
        }
        if (ec == std::make_error_code(std::errc::not_connected)) {
            std::lock_guard<std::mutex> lock(self->sessions_mutex_);
            auto it = self->channel_handshakes_.find(channel_id);
            if (it != self->channel_handshakes_.end()) {
                it->second.on_reconnect();
            }
            return;
        }

        auto event = bridge_event(*self, "session", "session.open_probe.failed");
        event["channel_id"] = channel_id;
        event["error_detail"] = ec.message();
        p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
        self->close_session_for_channel(
            channel_id,
            "failed to send relay channel open probe: " + ec.message());
    });
}

void ConnectBridge::do_accept() {
    if (stopping_) return;

    auto socket = std::make_shared<tcp::socket>(ioc_);
    auto self = shared_from_this();

    acceptor_.async_accept(*socket, [self, socket](const boost::system::error_code& ec) {
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                spdlog::error("[bto] accept 错误: {}", ec.message());
                auto event = bridge_event(*self, "bridge", "bridge.accept.failed");
                event["local_port"] = self->listen_port_;
                event["error_code"] = observability::code::kBridgeAcceptFailed;
                event["error_detail"] = ec.message();
                p2p::utils::emit_structured_log(spdlog::level::err, std::move(event));
            }
            return;
        }

        if (!self->p2p_connected_) {
            spdlog::warn("[bto] P2P 未连接，拒绝 SSH 连入");
            auto event = bridge_event(*self, "session", "session.accept.rejected");
            event["local_port"] = self->listen_port_;
            event["error_code"] = observability::code::kBridgeDisconnected;
            event["error_detail"] = "p2p not connected";
            p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
            socket->close();
            self->do_accept();
            return;
        }

        // 创建通道
        int channel_id = self->p2p_client_->create_channel();
        if (channel_id < 0) {
            spdlog::error("[bto] create_channel 失败");
            socket->close();
            self->do_accept();
            return;
        }

        // 创建会话
        int session_id = self->next_session_id_++;
        auto session = std::make_shared<TunnelSession>(
            session_id, self->ioc_, socket, self->weak_from_this());
        session->set_channel_id(channel_id);

        session->set_cleanup_callback([self](int id) {
            self->remove_session(id);
        });

        {
            std::lock_guard<std::mutex> lock(self->sessions_mutex_);
            self->sessions_[session_id] = session;
            self->channel_to_session_[channel_id] = session_id;
            if (self->uses_relay_channel_handshake()) {
                self->channel_handshakes_[channel_id] = bto::daemon::ChannelHandshake{};
            }
        }
        if (self->on_session_count_changed_) {
            self->on_session_count_changed_(self->active_session_count());
        }

        spdlog::info("[bto] #{} 新连接 (channel={})", session_id, channel_id);

        if (!self->ssh_user_.empty()) {
            if (!self->ssh_key_.empty()) {
                spdlog::info("[bto] SSH 连接命令: ssh -i {} {}@localhost -p {}",
                             self->ssh_key_, self->ssh_user_, self->listen_port_);
            } else {
                spdlog::info("[bto] SSH 连接命令: ssh {}@localhost -p {}",
                             self->ssh_user_, self->listen_port_);
            }
        }

        self->maybe_send_open_probe(channel_id);
        session->start();
        self->do_accept();
    });
}

void ConnectBridge::remove_session(int id) {
    std::size_t remaining = 0;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;

        int channel_id = it->second->channel_id();
        if (channel_id >= 0) {
            channel_to_session_.erase(channel_id);
            channel_handshakes_.erase(channel_id);
            pending_channel_data_.erase(channel_id);
            pending_channel_bytes_.erase(channel_id);
            if (p2p_client_) {
                p2p_client_->close_channel(channel_id);
            }
        }

        sessions_.erase(it);
        remaining = sessions_.size();
    }

    spdlog::info("[bto] #{} 会话已清理 (剩余 {} 个)", id, remaining);
    if (on_session_count_changed_) {
        on_session_count_changed_(remaining);
    }
}

void ConnectBridge::send_to_p2p(int channel_id, const std::vector<uint8_t>& data) {
    bool should_buffer_for_handshake = false;
    if (uses_relay_channel_handshake()) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = channel_handshakes_.find(channel_id);
        should_buffer_for_handshake =
            it != channel_handshakes_.end() && it->second.should_buffer_tcp_payload();
    }
    if (should_buffer_for_handshake) {
        if (!queue_pending_channel_data(channel_id, data)) {
            close_session_for_channel(
                channel_id,
                "channel handshake backlog exceeded local safety limit");
        }
        return;
    }

    if (!p2p_client_ || !p2p_connected_) {
        if (!queue_pending_channel_data(channel_id, data)) {
            close_session_for_channel(channel_id, "relay reconnect backlog exceeded local safety limit");
        }
        return;
    }

    auto weak_self = weak_from_this();
    p2p_client_->send_data(channel_id, data, [weak_self, channel_id, data](const std::error_code& ec) {
        if (ec) {
            if (auto self = weak_self.lock();
                self && ec == std::make_error_code(std::errc::not_connected)) {
                if (!self->queue_pending_channel_data(channel_id, data)) {
                    self->close_session_for_channel(
                        channel_id,
                        "relay reconnect backlog exceeded local safety limit");
                }
                return;
            }
            spdlog::warn("[bto] P2P 发送失败 (channel={}): {}", channel_id, ec.message());
        }
    });
}

void ConnectBridge::on_p2p_data(int channel_id, const std::vector<uint8_t>& data) {
    spdlog::debug("[bto] P2P 收到数据 channel={} size={}", channel_id, data.size());

    std::shared_ptr<TunnelSession> session;
    bool handshake_completed = false;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        auto it = channel_to_session_.find(channel_id);
        if (it == channel_to_session_.end()) {
            spdlog::warn("[bto] 收到未知 channel 数据: {}", channel_id);
            return;
        }

        auto handshake_it = channel_handshakes_.find(channel_id);
        if (handshake_it != channel_handshakes_.end() &&
            handshake_it->second.should_buffer_tcp_payload()) {
            handshake_it->second.mark_remote_ready();
            handshake_completed = true;
        }

        int session_id = it->second;
        auto session_it = sessions_.find(session_id);
        if (session_it == sessions_.end()) {
            spdlog::warn("[bto] 会话 #{} 不存在", session_id);
            return;
        }
        session = session_it->second;
    }

    if (handshake_completed) {
        flush_pending_channel_data(channel_id);
    }

    if (data.empty()) {
        return;
    }

    session->on_p2p_data(data);
}

}  // namespace bto
