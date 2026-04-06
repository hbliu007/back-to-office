/**
 * @file p2p_bridge.hpp
 * @brief BTO P2P 桥接层 — 多会话架构
 *
 * 每个 SSH 连入创建独立的 TunnelSession（独立 P2PClient + 独立 DID）。
 * ConnectBridge 管理 TCP acceptor 和所有活跃会话。
 *
 * 架构:
 *   TCP Acceptor (port 2222, 始终监听)
 *     ├── Session 1: SSH ↔ P2PClient("did-session-1") ↔ relay ↔ peer sshd
 *     ├── Session 2: SSH ↔ P2PClient("did-session-2") ↔ relay ↔ peer sshd
 *     └── Session N: ...
 */

#pragma once

#include "p2p/core/p2p_client.hpp"
#include <boost/asio.hpp>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace bto {

/**
 * @brief 单个 SSH ↔ P2P 隧道会话
 *
 * 每个 SSH 连接对应一个独立的 P2PClient（独立 DID、独立 relay 连接）。
 * 会话生命周期: create → start() → bridge → close() → cleanup
 */
class TunnelSession : public std::enable_shared_from_this<TunnelSession> {
public:
    using CleanupCallback = std::function<void(int)>;

    TunnelSession(int id,
                  std::string session_did,
                  boost::asio::io_context& ioc,
                  const p2p::core::P2PConfig& config,
                  std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    void start(const std::string& peer_did);
    void close();

    void set_cleanup_callback(CleanupCallback cb) { on_cleanup_ = std::move(cb); }
    int id() const { return id_; }

private:
    void read_tcp();
    void do_write();  // 串行写入队列处理

    static constexpr std::size_t kTcpReadBufferSize = 8192;

    int id_;
    std::string session_did_;
    // ioc_ 的生命周期必须长于本 session —— 由 ConnectBridge 在
    // 栈上 (bto.cpp cmd_connect) 持有，session 在 ioc_.run() 期间存活。
    boost::asio::io_context& ioc_;
    std::shared_ptr<p2p::core::P2PClient> client_;
    std::shared_ptr<boost::asio::ip::tcp::socket> tcp_socket_;
    int channel_id_ = -1;
    bool closed_ = false;
    CleanupCallback on_cleanup_;

    // P2P→TCP 写入队列 —— 保证同一时刻只有一个 async_write
    std::deque<std::shared_ptr<std::vector<uint8_t>>> write_queue_;
    bool writing_ = false;
};

/**
 * @brief 多会话 SSH 隧道管理器
 *
 * 启动后立即监听本地端口，每个 SSH 连入创建一个独立 TunnelSession。
 * 会话之间互不影响，单个会话断开不影响其他。
 *
 * 生命周期安全: 通过 shared_from_this() 保证异步回调不会访问已销毁对象。
 */
class ConnectBridge : public std::enable_shared_from_this<ConnectBridge> {
public:
    ConnectBridge(boost::asio::io_context& ioc,
                  const std::string& did,
                  const std::string& peer_did,
                  const p2p::core::P2PConfig& config,
                  uint16_t listen_port);

    ~ConnectBridge();

    bool start();
    void stop();

    /// 设置 SSH 连接提示信息（用户名、密钥路径）
    void set_ssh_hint(const std::string& user, const std::string& key = "");

private:
    void do_accept();
    void remove_session(int id);

    boost::asio::io_context& ioc_;
    std::string base_did_;
    std::string peer_did_;
    p2p::core::P2PConfig config_;
    uint16_t listen_port_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::map<int, std::shared_ptr<TunnelSession>> sessions_;
    int next_session_id_ = 0;
    bool stopping_ = false;
    std::string ssh_user_;
    std::string ssh_key_;
};

}  // namespace bto
