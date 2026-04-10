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
#include <iostream>
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
                             ConnectBridge* bridge)
    : id_(id)
    , ioc_(ioc)
    , tcp_socket_(std::move(socket))
    , bridge_(bridge)
{}

TunnelSession::~TunnelSession() {
    close();
}

void TunnelSession::start() {
    std::cout << "[bto] #" << id_ << " 会话启动 (channel=" << channel_id_ << ")" << std::endl;
    read_tcp();
}

void TunnelSession::close() {
    if (closed_) return;
    closed_ = true;

    std::cout << "[bto] #" << id_ << " 关闭会话 (channel=" << channel_id_ << ")" << std::endl;

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

    std::cout << "[bto] #" << id_ << " P2P 收到 " << data.size()
              << " 字节 (channel=" << channel_id_ << ")" << std::endl;

    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push_back(std::make_shared<std::vector<uint8_t>>(data));
    if (!writing_) {
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
                    std::cerr << "[bto] #" << self->id_
                              << " TCP 读取错误: " << ec.message() << std::endl;
                    if (self->bridge_) {
                        auto event =
                            bridge_event(*self->bridge_, "session", "session.tcp.read_failed");
                        event["channel_id"] = self->channel_id_;
                        event["error_code"] = observability::code::kBridgeTcpReadFailed;
                        event["error_detail"] = ec.message();
                        p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
                    }
                }
                self->close();
                return;
            }

            std::cout << "[bto] #" << self->id_ << " TCP 收到 " << n
                      << " 字节 (channel=" << self->channel_id_ << ")" << std::endl;

            if (n > 0 && self->channel_id_ >= 0) {
                buf->resize(n);
                self->bridge_->send_to_p2p(self->channel_id_, *buf);
            }

            self->read_tcp();
        }
    );
}

void TunnelSession::do_write() {
    if (closed_) return;
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    writing_ = true;
    auto data = write_queue_.front();
    write_queue_.pop_front();

    auto self = shared_from_this();
    boost::asio::async_write(
        *tcp_socket_,
        boost::asio::buffer(*data),
        [self, data](const boost::system::error_code& ec, std::size_t) {
            if (ec) {
                std::cerr << "[bto] #" << self->id_
                          << " TCP 写入错误: " << ec.message() << std::endl;
                if (self->bridge_) {
                    auto event = bridge_event(*self->bridge_, "session", "session.tcp.write_failed");
                    event["channel_id"] = self->channel_id_;
                    event["error_code"] = observability::code::kBridgeTcpWriteFailed;
                    event["error_detail"] = ec.message();
                    p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
                }
                self->close();
                return;
            }

            std::lock_guard<std::mutex> lock(self->write_mutex_);
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

        std::cout << "[bto] 监听端口 " << listen_port_ << std::endl;
        auto event = bridge_event(*this, "bridge", "bridge.listen.ready");
        event["local_port"] = listen_port_;
        p2p::utils::emit_structured_log(spdlog::level::info, std::move(event));

        initialize_p2p_client();
        do_accept();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[bto] 启动失败: " << e.what() << std::endl;
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

    std::cout << "[bto] 停止桥接服务..." << std::endl;
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
        std::cout << "[bto] P2P 已连接 (DID=" << self->did_ << ")" << std::endl;
        std::cout << "[bto] 路径: "
                  << p2p::core::P2PClient::ToString(self->p2p_client_->active_path())
                  << std::endl;
        self->reseed_existing_channels_for_reconnect();
        self->p2p_connected_ = true;
        self->ever_connected_ = true;
        self->reconnecting_ = false;
        self->reconnect_attempts_ = 0;
        if (!self->last_disconnect_detail_.empty()) {
            std::cout << "[bto] 已恢复连接，上次断开原因: " << self->last_disconnect_detail_
                      << std::endl;
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
        std::cerr << "[bto] P2P 断开连接: " << reason
                  << "，当前会话数=" << self->active_session_count() << std::endl;
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
        std::cerr << "[bto] P2P 错误: " << msg << " (" << ec.message() << ")" << std::endl;
        auto event = bridge_event(*self, "bridge", "p2p.connect.error");
        event["error_code"] = observability::code::kBridgeDisconnected;
        event["error_detail"] = msg + " (" + ec.message() + ")";
        p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
    });

    std::cout << "[bto] 初始化 P2PClient (DID=" << did_ << ")" << std::endl;
    p2p_client_->initialize([self](const std::error_code& ec) {
        if (ec) {
            auto message = "P2PClient initialize failed: " + ec.message();
            std::cerr << "[bto] P2PClient 初始化失败: " << ec.message() << std::endl;
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

        std::cout << "[bto] 连接到 " << self->peer_did_ << " ..." << std::endl;
        self->p2p_client_->connect(self->peer_did_, [self](const std::error_code& ec) {
            if (ec) {
                auto message = "P2P connect failed: " + ec.message();
                std::cerr << "[bto] P2P 连接失败: " << ec.message() << std::endl;
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
            std::cout << "[bto] P2P 连接成功，等待 SSH 连入..." << std::endl;
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
    std::cerr << "[bto] 准备重连 relay（第 " << reconnect_attempts_
              << " 次），原因: " << reason << std::endl;
    auto event = bridge_event(*this, "bridge", "p2p.reconnect.scheduled");
    event["attempt"] = reconnect_attempts_;
    event["error_detail"] = reason;
    p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
    reconnect_timer_.expires_after(std::chrono::seconds(2));
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
    std::cerr << "[bto] 关闭会话，原因: " << reason
              << " (channel=" << channel_id << ")" << std::endl;
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

void ConnectBridge::do_accept() {
    if (stopping_) return;

    auto socket = std::make_shared<tcp::socket>(ioc_);
    auto self = shared_from_this();

    acceptor_.async_accept(*socket, [self, socket](const boost::system::error_code& ec) {
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "[bto] accept 错误: " << ec.message() << std::endl;
                auto event = bridge_event(*self, "bridge", "bridge.accept.failed");
                event["local_port"] = self->listen_port_;
                event["error_code"] = observability::code::kBridgeAcceptFailed;
                event["error_detail"] = ec.message();
                p2p::utils::emit_structured_log(spdlog::level::err, std::move(event));
            }
            return;
        }

        if (!self->p2p_connected_) {
            std::cerr << "[bto] P2P 未连接，拒绝 SSH 连入" << std::endl;
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
            std::cerr << "[bto] create_channel 失败" << std::endl;
            socket->close();
            self->do_accept();
            return;
        }

        // 创建会话
        int session_id = self->next_session_id_++;
        auto session = std::make_shared<TunnelSession>(
            session_id, self->ioc_, socket, self.get());
        session->set_channel_id(channel_id);

        session->set_cleanup_callback([self](int id) {
            self->remove_session(id);
        });

        {
            std::lock_guard<std::mutex> lock(self->sessions_mutex_);
            self->sessions_[session_id] = session;
            self->channel_to_session_[channel_id] = session_id;
        }
        if (self->on_session_count_changed_) {
            self->on_session_count_changed_(self->active_session_count());
        }

        std::cout << "[bto] #" << session_id << " 新连接 (channel=" << channel_id << ")" << std::endl;

        if (!self->ssh_user_.empty()) {
            std::cout << "[bto] SSH 连接命令: ssh ";
            if (!self->ssh_key_.empty()) {
                std::cout << "-i " << self->ssh_key_ << " ";
            }
            std::cout << self->ssh_user_ << "@localhost -p " << self->listen_port_ << std::endl;
        }

        session->start();
        self->do_accept();
    });
}

void ConnectBridge::remove_session(int id) {
    std::size_t remaining = 0;
    int channel_id_to_clear = -1;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;

        int channel_id = it->second->channel_id();
        channel_id_to_clear = channel_id;
        if (channel_id >= 0) {
            channel_to_session_.erase(channel_id);
            if (p2p_client_) {
                p2p_client_->close_channel(channel_id);
            }
        }

        sessions_.erase(it);
        remaining = sessions_.size();
    }

    if (channel_id_to_clear >= 0) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        pending_channel_data_.erase(channel_id_to_clear);
        pending_channel_bytes_.erase(channel_id_to_clear);
    }

    std::cout << "[bto] #" << id << " 会话已清理 (剩余 " << remaining << " 个)" << std::endl;
    if (on_session_count_changed_) {
        on_session_count_changed_(remaining);
    }
}

void ConnectBridge::send_to_p2p(int channel_id, const std::vector<uint8_t>& data) {
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
            std::cerr << "[bto] P2P 发送失败 (channel=" << channel_id
                      << "): " << ec.message() << std::endl;
        }
    });
}

void ConnectBridge::on_p2p_data(int channel_id, const std::vector<uint8_t>& data) {
    std::cout << "[bto] P2P 收到数据 channel=" << channel_id
              << " size=" << data.size() << std::endl;

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    auto it = channel_to_session_.find(channel_id);
    if (it == channel_to_session_.end()) {
        std::cerr << "[bto] 收到未知 channel 数据: " << channel_id << std::endl;
        return;
    }

    int session_id = it->second;
    auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
        std::cerr << "[bto] 会话 #" << session_id << " 不存在" << std::endl;
        return;
    }

    session_it->second->on_p2p_data(data);
}

}  // namespace bto
