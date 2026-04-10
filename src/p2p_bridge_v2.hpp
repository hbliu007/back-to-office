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
    using ReadyCallback = std::function<void()>;
    using FailedCallback = std::function<void(const std::string&)>;
    using StoppedCallback = std::function<void()>;
    using SessionCountCallback = std::function<void(std::size_t)>;

    ConnectBridge(boost::asio::io_context& ioc,
                  const std::string& did,
                  const std::string& peer_did,
                  const p2p::core::P2PConfig& config,
                  uint16_t listen_port);

    ~ConnectBridge();

    bool start();
    void stop();
    std::size_t active_session_count() const;
    uint16_t listen_port() const { return listen_port_; }
    bool is_ready() const { return p2p_connected_; }
    const std::string& trace_id() const { return trace_id_; }
    const std::string& did() const { return did_; }
    const std::string& peer_did() const { return peer_did_; }

    void set_ssh_hint(const std::string& user, const std::string& key = "");
    void set_trace_id(std::string trace_id) { trace_id_ = std::move(trace_id); }

    void on_ready(ReadyCallback cb) { on_ready_ = std::move(cb); }
    void on_failed(FailedCallback cb) { on_failed_ = std::move(cb); }
    void on_stopped(StoppedCallback cb) { on_stopped_ = std::move(cb); }
    void on_session_count_changed(SessionCountCallback cb) {
        on_session_count_changed_ = std::move(cb);
    }

    /// 从 TCP 接收数据，通过 P2PClient 发送
    void send_to_p2p(int channel_id, const std::vector<uint8_t>& data);

private:
    void initialize_p2p_client();
    void schedule_reconnect(const std::string& reason);
    void reset_p2p_client();
    void reseed_existing_channels_for_reconnect();
    void flush_pending_channel_data();
    auto queue_pending_channel_data(int channel_id, const std::vector<uint8_t>& data) -> bool;
    void close_session_for_channel(int channel_id, const std::string& reason);
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
    bool ever_connected_ = false;
    bool reconnecting_ = false;
    std::size_t reconnect_attempts_ = 0;
    boost::asio::steady_timer reconnect_timer_;
    std::string last_disconnect_detail_;

    // 会话管理
    std::map<int, std::shared_ptr<TunnelSession>> sessions_;
    std::map<int, int> channel_to_session_;  // channel_id → session_id
    std::map<int, std::deque<std::vector<uint8_t>>> pending_channel_data_;
    std::map<int, std::size_t> pending_channel_bytes_;
    mutable std::mutex sessions_mutex_;
    int next_session_id_ = 0;
    bool stopping_ = false;

    std::string ssh_user_;
    std::string ssh_key_;
    std::string trace_id_;
    ReadyCallback on_ready_;
    FailedCallback on_failed_;
    StoppedCallback on_stopped_;
    SessionCountCallback on_session_count_changed_;

    static constexpr std::size_t kMaxBufferedBytesPerChannel = 256 * 1024;
};

}  // namespace bto
