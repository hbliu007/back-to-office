#pragma once

#include "daemon/common.hpp"
#include "daemon/local_endpoint_manager.hpp"

#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace bto::daemon {

class PeerlinkService {
public:
    using ReplyFn = std::function<void(Json)>;

    explicit PeerlinkService(boost::asio::io_context& ioc);

    void handle_request(const Json& request, ReplyFn reply);
    void set_shutdown_handler(std::function<void()> handler) {
        shutdown_handler_ = std::move(handler);
    }

private:
    struct SessionRecord {
        std::string session_id;
        std::string connection_id;
        std::string target_name;
        std::string target_did;
        uint16_t local_port = 0;
        std::string ssh_user;
        std::string ssh_key;
    };

    class TargetConnection;

    void handle_status(ReplyFn reply);
    void handle_list_connections(ReplyFn reply);
    void handle_list_targets(ReplyFn reply);
    void handle_create_session(const Json& request, ReplyFn reply);
    void handle_close_session(const Json& request, ReplyFn reply);
    void handle_close_target(const Json& request, ReplyFn reply);
    void handle_shutdown(ReplyFn reply);

    void on_connection_stopped(const std::string& connection_id);
    void release_session(const std::string& session_id);

    auto next_session_id() -> std::string;
    auto connection_key(const CreateSessionRequest& request) const -> std::string;
    auto make_session_view(const SessionRecord& record, const std::string& state) const
        -> SessionView;

    boost::asio::io_context& ioc_;
    LocalEndpointManager endpoint_manager_;
    std::map<std::string, std::shared_ptr<TargetConnection>> connections_;
    std::map<std::string, SessionRecord> sessions_;
    std::map<std::string, std::unique_ptr<boost::asio::steady_timer>> session_timers_;
    std::function<void()> shutdown_handler_;
    uint64_t next_session_id_ = 1;
};

class PeerlinkDaemonServer {
public:
    PeerlinkDaemonServer(boost::asio::io_context& ioc, std::string socket_path);
    ~PeerlinkDaemonServer();

    auto start() -> bool;
    void stop();

private:
    class ClientSession;

    void do_accept();

    boost::asio::io_context& ioc_;
    std::string socket_path_;
    std::string lock_path_;
    boost::asio::local::stream_protocol::acceptor acceptor_;
    PeerlinkService service_;
    bool started_ = false;
    int lock_fd_ = -1;
};

}  // namespace bto::daemon
