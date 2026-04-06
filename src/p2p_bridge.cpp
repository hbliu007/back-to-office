/**
 * @file p2p_bridge.cpp
 * @brief BTO P2P 桥接层实现 — 多会话架构
 *
 * 每个 SSH 连入 → 独立 TunnelSession → 独立 P2PClient → 独立 relay 会话
 */

#include "p2p_bridge.hpp"
#include <iostream>
#include <vector>

namespace bto {

using boost::asio::ip::tcp;

// ─── TunnelSession ─────────────────────────────────────────────

TunnelSession::TunnelSession(int id,
                             std::string session_did,
                             boost::asio::io_context& ioc,
                             const p2p::core::P2PConfig& config,
                             std::shared_ptr<tcp::socket> socket)
    : id_(id)
    , session_did_(std::move(session_did))
    , ioc_(ioc)
    , client_(std::make_shared<p2p::core::P2PClient>(ioc, session_did_, config))
    , tcp_socket_(std::move(socket))
{}

void TunnelSession::start(const std::string& peer_did) {
    auto self = shared_from_this();

    client_->on_connected([self]() {
        std::cout << "[bto] #" << self->id_ << " P2P 已连接 ("
                  << self->session_did_ << ")" << std::endl;
        std::cout << "[bto] #" << self->id_ << " 路径: "
                  << p2p::core::P2PClient::ToString(self->client_->active_path())
                  << std::endl;
        self->channel_id_ = self->client_->create_channel();
        if (self->channel_id_ < 0) {
            std::cerr << "[bto] #" << self->id_
                      << " create_channel 失败" << std::endl;
            self->close();
            return;
        }
        self->read_tcp();
    });

    client_->on_disconnected([self]() {
        std::cerr << "[bto] #" << self->id_ << " P2P 断开" << std::endl;
        self->close();
    });

    client_->on_data([self](int channel_id, const std::vector<uint8_t>& data) {
        if (self->closed_) return;
        if (channel_id != self->channel_id_) return;
        if (!self->tcp_socket_ || !self->tcp_socket_->is_open()) return;

        self->write_queue_.push_back(
            std::make_shared<std::vector<uint8_t>>(data));
        if (!self->writing_) {
            self->do_write();
        }
    });

    client_->on_error([self](const std::error_code& ec, const std::string& msg) {
        std::cerr << "[bto] #" << self->id_ << " 错误: " << msg
                  << " (" << ec.message() << ")" << std::endl;
    });

    std::cout << "[bto] #" << id_ << " 初始化 DID=" << session_did_ << std::endl;
    client_->initialize([self, peer_did](const std::error_code& ec) {
        if (ec) {
            std::cerr << "[bto] #" << self->id_
                      << " 初始化失败: " << ec.message() << std::endl;
            self->close();
            return;
        }
        std::cout << "[bto] #" << self->id_ << " 连接 " << peer_did << " ..." << std::endl;
        self->client_->connect(peer_did, [self](const std::error_code& ec) {
            if (ec) {
                std::cerr << "[bto] #" << self->id_
                          << " 连接失败: " << ec.message() << std::endl;
                std::cerr << "[bto] #" << self->id_ << " 原因: "
                          << p2p::core::P2PClient::ToString(
                                 self->client_->last_failure_reason())
                          << " — " << self->client_->last_failure_detail()
                          << std::endl;
                self->close();
            }
        });
    });
}

void TunnelSession::close() {
    if (closed_) return;
    closed_ = true;

    if (tcp_socket_ && tcp_socket_->is_open()) {
        boost::system::error_code ec;
        tcp_socket_->close(ec);
    }
    client_->close();

    // 延迟回调，避免在当前调用栈中删除自身
    // post 保证 on_cleanup_ 在当前回调返回后执行
    if (on_cleanup_) {
        auto cleanup = on_cleanup_;
        auto id = id_;
        boost::asio::post(ioc_, [cleanup, id]() { cleanup(id); });
    }
}

void TunnelSession::do_write() {
    if (closed_ || write_queue_.empty()) {
        writing_ = false;
        return;
    }
    writing_ = true;
    auto self = shared_from_this();
    auto buf = write_queue_.front();
    write_queue_.pop_front();

    boost::asio::async_write(*tcp_socket_, boost::asio::buffer(*buf),
        [self, buf](const boost::system::error_code& ec, std::size_t) {
            if (ec) {
                std::cerr << "[bto] #" << self->id_
                          << " TCP 写入错误: " << ec.message() << std::endl;
                self->close();
                return;
            }
            self->do_write();  // 继续处理队列中的下一个
        });
}

void TunnelSession::read_tcp() {
    auto self = shared_from_this();
    auto buf = std::make_shared<std::vector<uint8_t>>(kTcpReadBufferSize);

    tcp_socket_->async_read_some(boost::asio::buffer(*buf),
        [self, buf](const boost::system::error_code& ec, std::size_t n) {
            if (self->closed_) return;
            if (ec) {
                if (ec != boost::asio::error::eof) {
                    std::cerr << "[bto] #" << self->id_
                              << " TCP 读取错误: " << ec.message() << std::endl;
                }
                self->close();
                return;
            }
            buf->resize(n);
            self->client_->send_data(self->channel_id_, *buf,
                [self](const std::error_code& ec) {
                    if (ec) {
                        std::cerr << "[bto] #" << self->id_
                                  << " P2P 发送错误: " << ec.message() << std::endl;
                        self->close();
                        return;
                    }
                    // 背压控制：P2P 发送完成后才读下一块 TCP 数据
                    self->read_tcp();
                });
        });
}

// ─── ConnectBridge ─────────────────────────────────────────────

ConnectBridge::ConnectBridge(boost::asio::io_context& ioc,
                             const std::string& did,
                             const std::string& peer_did,
                             const p2p::core::P2PConfig& config,
                             uint16_t listen_port)
    : ioc_(ioc)
    , base_did_(did)
    , peer_did_(peer_did)
    , config_(config)
    , listen_port_(listen_port)
    , acceptor_(ioc)
{}

ConnectBridge::~ConnectBridge() {
    stop();
}

void ConnectBridge::set_ssh_hint(const std::string& user, const std::string& key) {
    ssh_user_ = user;
    ssh_key_ = key;
}

bool ConnectBridge::start() {
    boost::system::error_code ec;

    tcp::endpoint ep(tcp::v4(), listen_port_);
    acceptor_.open(ep.protocol(), ec);
    if (ec) {
        std::cerr << "[bto] 打开端口失败: " << ec.message() << std::endl;
        return false;
    }
    acceptor_.set_option(tcp::acceptor::reuse_address(true), ec);
    acceptor_.bind(ep, ec);
    if (ec) {
        std::cerr << "[bto] 端口 " << listen_port_ << " 被占用" << std::endl;
        std::cerr << "提示: 使用 --listen <其他端口> 或关闭占用端口的进程" << std::endl;
        std::cerr << "      查找占用: lsof -i :" << listen_port_ << std::endl;
        return false;
    }
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        std::cerr << "[bto] listen 失败: " << ec.message() << std::endl;
        return false;
    }

    std::cout << "[bto] 监听 127.0.0.1:" << listen_port_ << std::endl;
    std::cout << "[bto] 每个 SSH 连入将创建独立 P2P 隧道" << std::endl;

    // 构造精确的 SSH 命令提示
    std::string ssh_cmd = "ssh -p " + std::to_string(listen_port_);
    if (!ssh_key_.empty()) ssh_cmd += " -i " + ssh_key_;
    ssh_cmd += " " + (ssh_user_.empty() ? "user" : ssh_user_) + "@127.0.0.1";
    std::cout << "[bto] 使用: " << ssh_cmd << std::endl;

    do_accept();
    return true;
}

void ConnectBridge::stop() {
    stopping_ = true;

    boost::system::error_code ec;
    acceptor_.close(ec);

    auto sessions_copy = sessions_;
    for (auto& [id, session] : sessions_copy) {
        session->close();
    }
    sessions_.clear();
}

void ConnectBridge::do_accept() {
    auto self = shared_from_this();
    acceptor_.async_accept([self](const boost::system::error_code& ec,
                                  boost::asio::ip::tcp::socket socket) {
        if (ec || self->stopping_) return;

        int id = ++self->next_session_id_;
        std::string session_did = self->base_did_ + "-session-" + std::to_string(id);

        std::cout << "[bto] SSH 连入 #" << id << " ("
                  << socket.remote_endpoint().address().to_string() << ":"
                  << socket.remote_endpoint().port() << ")" << std::endl;

        auto tcp_sock = std::make_shared<boost::asio::ip::tcp::socket>(std::move(socket));
        auto session = std::make_shared<TunnelSession>(
            id, session_did, self->ioc_, self->config_, tcp_sock);

        // weak_ptr 保护：cleanup 回调在 post() 中延迟执行，
        // 此时 ConnectBridge 可能已析构，用 weak_ptr 安全检查。
        std::weak_ptr<ConnectBridge> weak_self = self;
        session->set_cleanup_callback([weak_self](int sid) {
            if (auto bridge = weak_self.lock()) {
                bridge->remove_session(sid);
            }
        });
        self->sessions_[id] = session;

        session->start(self->peer_did_);
        self->do_accept();
    });
}

void ConnectBridge::remove_session(int id) {
    sessions_.erase(id);
    std::cout << "[bto] #" << id << " 已清理 (活跃会话: "
              << sessions_.size() << ")" << std::endl;
}

}  // namespace bto
