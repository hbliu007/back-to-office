/**
 * @file p2p_bridge_v2.cpp
 * @brief BTO P2P 桥接层实现 — 单例 P2PClient + 多通道架构
 */

#include "p2p_bridge_v2.hpp"
#include <iostream>
#include <vector>

namespace bto {

using boost::asio::ip::tcp;

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

        initialize_p2p_client();
        do_accept();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[bto] 启动失败: " << e.what() << std::endl;
        return false;
    }
}

void ConnectBridge::stop() {
    if (stopping_) return;
    stopping_ = true;

    std::cout << "[bto] 停止桥接服务..." << std::endl;

    boost::system::error_code ec;
    acceptor_.close(ec);

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        session->close();
    }
    sessions_.clear();
    channel_to_session_.clear();

    if (p2p_client_) {
        p2p_client_->close();
        p2p_client_.reset();
    }
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
        self->p2p_connected_ = true;
    });

    p2p_client_->on_disconnected([self]() {
        std::cerr << "[bto] P2P 断开连接" << std::endl;
        self->p2p_connected_ = false;
        self->stop();
    });

    p2p_client_->on_data([self](int channel_id, const std::vector<uint8_t>& data) {
        self->on_p2p_data(channel_id, data);
    });

    p2p_client_->on_error([self](const std::error_code& ec, const std::string& msg) {
        std::cerr << "[bto] P2P 错误: " << msg << " (" << ec.message() << ")" << std::endl;
    });

    std::cout << "[bto] 初始化 P2PClient (DID=" << did_ << ")" << std::endl;
    p2p_client_->initialize([self](const std::error_code& ec) {
        if (ec) {
            std::cerr << "[bto] P2PClient 初始化失败: " << ec.message() << std::endl;
            self->stop();
            return;
        }

        std::cout << "[bto] 连接到 " << self->peer_did_ << " ..." << std::endl;
        self->p2p_client_->connect(self->peer_did_, [self](const std::error_code& ec) {
            if (ec) {
                std::cerr << "[bto] P2P 连接失败: " << ec.message() << std::endl;
                self->stop();
                return;
            }
            std::cout << "[bto] P2P 连接成功，等待 SSH 连入..." << std::endl;
            if (self->on_ready_) self->on_ready_();
        });
    });
}

void ConnectBridge::do_accept() {
    if (stopping_) return;

    auto socket = std::make_shared<tcp::socket>(ioc_);
    auto self = shared_from_this();

    acceptor_.async_accept(*socket, [self, socket](const boost::system::error_code& ec) {
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                std::cerr << "[bto] accept 错误: " << ec.message() << std::endl;
            }
            return;
        }

        if (!self->p2p_connected_) {
            std::cerr << "[bto] P2P 未连接，拒绝 SSH 连入" << std::endl;
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
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;

    int channel_id = it->second->channel_id();
    if (channel_id >= 0) {
        channel_to_session_.erase(channel_id);
        if (p2p_client_) {
            p2p_client_->close_channel(channel_id);
        }
    }

    sessions_.erase(it);
    std::cout << "[bto] #" << id << " 会话已清理 (剩余 " << sessions_.size() << " 个)" << std::endl;
}

void ConnectBridge::send_to_p2p(int channel_id, const std::vector<uint8_t>& data) {
    if (!p2p_client_ || !p2p_connected_) return;

    p2p_client_->send_data(channel_id, data, [channel_id](const std::error_code& ec) {
        if (ec) {
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
