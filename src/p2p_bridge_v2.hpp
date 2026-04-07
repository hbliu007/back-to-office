/**
 * @file p2p_bridge_v2.hpp
 * @brief BTO P2P 桥接层 — 单例 P2PClient + 多通道架构
 *
 * 架构改进:
 *   - ConnectBridge 管理单例 P2PClient（单个 DID，单个 relay 连接）
 *   - 每个 SSH 连入创建 TunnelSession（持有 channel_id，不持有 P2PClient）
 *   - 数据路由: TCP ↔ Session ↔ Bridge ↔ P2PClient
 *
 * 优势:
 *   - 支持多个并发 SSH 连接（通过 channel 复用）
 *   - 减少 relay 连接数（N 个 SSH 只需 1 个 relay）
 *   - 符合 P2PClient 设计意图（单连接多通道）
 */

#pragma once

#include "p2p/core/p2p_client.hpp"
#include <boost/asio.hpp>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

namespace bto {

class ConnectBridge;

/**
 * @brief 单个 SSH ↔ P2P 隧道会话（轻量级，仅持有 channel_id）
 *
 * 职责:
 *   - 管理 TCP socket 生命周期
 *   - 通过 ConnectBridge 路由数据到 P2PClient
 *   - 不持有 P2PClient（由 Bridge 统一管理）
 */
class TunnelSession : public std::enable_shared_from_this<TunnelSession> {
public:
    using CleanupCallback = std::function<void(int)>;

    TunnelSession(int id,
                  boost::asio::io_context& ioc,
                  std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                  ConnectBridge* bridge);

    ~TunnelSession();

    void start();
    void close();

    void set_cleanup_callback(CleanupCallback cb) { on_cleanup_ = std::move(cb); }
    int id() const { return id_; }
    int channel_id() const { return channel_id_; }
    void set_channel_id(int ch) { channel_id_ = ch; }

    /// 从 P2P 接收数据，写入 TCP
    void on_p2p_data(const std::vector<uint8_t>& data);

private:
    void read_tcp();
    void do_write();

    static constexpr std::size_t kTcpReadBufferSize = 8192;

    int id_;
    boost::asio::io_context& ioc_;
    std::shared_ptr<boost::asio::ip::tcp::socket> tcp_socket_;
    ConnectBridge* bridge_;  // 非拥有指针，生命周期由 Bridge 保证
    int channel_id_ = -1;
    bool closed_ = false;
    CleanupCallback on_cleanup_;

    // P2P→TCP 写入队列
    std::deque<std::shared_ptr<std::vector<uint8_t>>> write_queue_;
    bool writing_ = false;
    std::mutex write_mutex_;
};

/**
 * @brief 多会话 SSH 隧道管理器（单例 P2PClient + 多通道路由）
 *
 * 职责:
 *   - 管理单例 P2PClient（单个 DID，单个 relay 连接）
 *   - 管理多个 TunnelSession（每个持有独立 channel_id）
 *   - 路由数据: channel_id → session
 *   - 监听本地端口，接受新 SSH 连接
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

    void set_ssh_hint(const std::string& user, const std::string& key = "");

    /// P2P 连接就绪回调
    using ReadyCallback = std::function<void()>;
    void on_ready(ReadyCallback cb) { on_ready_ = std::move(cb); }

    /// 从 TCP 接收数据，通过 P2PClient 发送
    void send_to_p2p(int channel_id, const std::vector<uint8_t>& data);

private:
    void initialize_p2p_client();
    void do_accept();
    void remove_session(int id);

    /// P2PClient 数据回调 → 路由到对应 session
    void on_p2p_data(int channel_id, const std::vector<uint8_t>& data);

    boost::asio::io_context& ioc_;
    std::string did_;
    std::string peer_did_;
    p2p::core::P2PConfig config_;
    uint16_t listen_port_;
    boost::asio::ip::tcp::acceptor acceptor_;

    // 单例 P2PClient
    std::shared_ptr<p2p::core::P2PClient> p2p_client_;
    bool p2p_connected_ = false;

    // 会话管理
    std::map<int, std::shared_ptr<TunnelSession>> sessions_;
    std::map<int, int> channel_to_session_;  // channel_id → session_id
    std::mutex sessions_mutex_;
    int next_session_id_ = 0;
    bool stopping_ = false;

    std::string ssh_user_;
    std::string ssh_key_;
    ReadyCallback on_ready_;
};

}  // namespace bto
