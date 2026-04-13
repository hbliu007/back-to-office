#include "daemon/peerlink_service.hpp"

#include "config/config.hpp"
#include "daemon/session_lifecycle.hpp"
#include "observability/error_codes.hpp"
#include "p2p_bridge_v2.hpp"
#include "p2p/utils/structured_log.hpp"

#include <chrono>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <filesystem>
#include <set>
#include <thread>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace bto::daemon {

namespace {

enum class ConnectionState {
    Starting,
    Ready,
    Failed,
    Stopped,
};

auto connection_state_name(ConnectionState state) -> std::string {
    switch (state) {
        case ConnectionState::Starting: return "starting";
        case ConnectionState::Ready: return "ready";
        case ConnectionState::Failed: return "failed";
        case ConnectionState::Stopped: return "stopped";
    }
    return "unknown";
}

auto build_p2p_config(const std::string& relay_host, uint16_t relay_port,
                      const std::string& relay_token = "")
    -> p2p::core::P2PConfig {
    p2p::core::P2PConfig cfg;
    cfg.stun_server = "";
    cfg.stun_port = 0;
    cfg.relay_server = relay_host;
    cfg.relay_port = relay_port;
    cfg.signaling_server = relay_host;
    cfg.signaling_port = 8080;
    cfg.tcp_relay_server = relay_host;
    cfg.tcp_relay_port = relay_port;
    cfg.relay_mode = p2p::core::RelayMode::RELAY_ONLY;
    cfg.relay_auth_token = relay_token;
    return cfg;
}

auto daemon_event(std::string_view component, std::string_view event) -> p2p::utils::Json {
    return p2p::utils::make_structured_event("peerlinkd", component, event);
}

void add_request_context(p2p::utils::Json& event, const CreateSessionRequest& request) {
    p2p::utils::put_if_not_empty(event, "trace_id", request.trace_id);
    p2p::utils::put_if_not_empty(event, "target_name", request.target_name);
    p2p::utils::put_if_not_empty(event, "did", request.local_did);
    p2p::utils::put_if_not_empty(event, "peer_did", request.target_did);
    if (!request.relay_host.empty()) {
        event["relay"] = request.relay_host + ":" + std::to_string(request.relay_port);
    }
}

auto error_details_for_trace(const std::string& trace_id) -> Json {
    Json details = Json::object();
    if (!trace_id.empty()) {
        details["trace_id"] = trace_id;
    }
    return details;
}

}  // namespace

class PeerlinkService::TargetConnection
    : public std::enable_shared_from_this<PeerlinkService::TargetConnection> {
public:
    using ReadyWaiter = std::function<void(bool, const std::string&)>;

    TargetConnection(PeerlinkService& service,
                     boost::asio::io_context& ioc,
                     std::string connection_id,
                     CreateSessionRequest request,
                     uint16_t local_port)
        : service_(service)
        , ioc_(ioc)
        , connection_id_(std::move(connection_id))
        , request_(std::move(request))
        , local_port_(local_port)
        , idle_timer_(ioc) {}

    void start() {
        bridge_ = std::make_shared<ConnectBridge>(
            ioc_,
            request_.local_did,
            request_.target_did,
            build_p2p_config(request_.relay_host, request_.relay_port, request_.relay_token),
            local_port_);
        bridge_->set_ssh_hint(request_.ssh_user, request_.ssh_key);
        bridge_->set_trace_id(request_.trace_id);

        auto start_event = daemon_event("connection", "connection.starting");
        add_request_context(start_event, request_);
        start_event["connection_id"] = connection_id_;
        start_event["local_port"] = local_port_;
        p2p::utils::emit_structured_log(spdlog::level::info, std::move(start_event));

        auto self = shared_from_this();
        bridge_->on_ready([self]() {
            if (self->state_ == ConnectionState::Stopped) {
                return;
            }
            self->state_ = ConnectionState::Ready;
            self->last_error_.clear();
            auto event = daemon_event("connection", "connection.ready");
            add_request_context(event, self->request_);
            event["connection_id"] = self->connection_id_;
            event["local_port"] = self->local_port_;
            event["live_tunnels"] = self->live_tunnel_count();
            p2p::utils::emit_structured_log(spdlog::level::info, std::move(event));
            auto waiters = std::move(self->waiters_);
            self->waiters_.clear();
            for (auto& waiter : waiters) {
                waiter(true, "");
            }
        });
        bridge_->on_failed([self](const std::string& message) {
            self->state_ = ConnectionState::Failed;
            self->last_error_ = message;
            auto event = daemon_event("connection", "connection.failed");
            add_request_context(event, self->request_);
            event["connection_id"] = self->connection_id_;
            event["error_code"] = bto::observability::code::kDaemonConnectFailed;
            event["error_detail"] = message;
            p2p::utils::emit_structured_log(spdlog::level::err, std::move(event));
            self->fail_waiters(message);
        });
        bridge_->on_session_count_changed([self](std::size_t count) {
            if (count == 0) {
                self->maybe_schedule_idle_stop();
            }
        });
        bridge_->on_stopped([self]() {
            self->fail_waiters("connection stopped");
            self->state_ = ConnectionState::Stopped;
            self->idle_timer_.cancel();
            auto event = daemon_event("connection", "connection.stopped");
            add_request_context(event, self->request_);
            event["connection_id"] = self->connection_id_;
            event["live_tunnels"] = self->live_tunnel_count();
            p2p::utils::emit_structured_log(spdlog::level::warn, std::move(event));
            self->service_.on_connection_stopped(self->connection_id_);
        });

        if (!bridge_->start()) {
            state_ = ConnectionState::Failed;
            last_error_ = "Bridge start failed";
            auto event = daemon_event("connection", "connection.start_failed");
            add_request_context(event, request_);
            event["connection_id"] = connection_id_;
            event["error_code"] = bto::observability::code::kBridgeStartFailed;
            event["error_detail"] = last_error_;
            p2p::utils::emit_structured_log(spdlog::level::err, std::move(event));
            auto waiters = std::move(waiters_);
            waiters_.clear();
            for (auto& waiter : waiters) {
                waiter(false, last_error_);
            }
            service_.on_connection_stopped(connection_id_);
        }
    }

    void add_waiter(ReadyWaiter waiter) {
        if (state_ == ConnectionState::Ready) {
            waiter(true, "");
            return;
        }
        if (state_ == ConnectionState::Failed || state_ == ConnectionState::Stopped) {
            waiter(false, last_error_.empty() ? "connection unavailable" : last_error_);
            return;
        }
        waiters_.push_back(std::move(waiter));
    }

    void add_session(const std::string& session_id) {
        session_ids_.insert(session_id);
        idle_timer_.cancel();
    }

    void release_session(const std::string& session_id) {
        session_ids_.erase(session_id);
        maybe_schedule_idle_stop();
    }

    void stop() {
        idle_timer_.cancel();
        fail_waiters("connection stopped");
        if (bridge_) {
            bridge_->stop();
        } else {
            state_ = ConnectionState::Stopped;
            service_.on_connection_stopped(connection_id_);
        }
    }

    auto view() const -> ConnectionView {
        ConnectionView view;
        view.connection_id = connection_id_;
        view.trace_id = request_.trace_id;
        view.target_name = request_.target_name;
        view.target_did = request_.target_did;
        view.local_did = request_.local_did;
        view.relay = request_.relay_host + ":" + std::to_string(request_.relay_port);
        view.local_port = local_port_;
        view.active_sessions = session_ids_.size();
        view.live_tunnels = bridge_ ? bridge_->active_session_count() : 0;
        view.state = connection_state_name(state_);
        view.last_error = last_error_;
        return view;
    }

    auto local_port() const -> uint16_t { return local_port_; }
    auto state_name() const -> std::string { return connection_state_name(state_); }
    auto connection_id() const -> const std::string& { return connection_id_; }
    auto target_name() const -> const std::string& { return request_.target_name; }
    auto target_did() const -> const std::string& { return request_.target_did; }
    auto live_tunnel_count() const -> std::size_t {
        return bridge_ ? bridge_->active_session_count() : 0;
    }
    auto can_use_port(std::optional<uint16_t> port) const -> bool {
        return !port || *port == local_port_;
    }

private:
    void fail_waiters(const std::string& message) {
        auto waiters = std::move(waiters_);
        waiters_.clear();
        for (auto& waiter : waiters) {
            waiter(false, message);
        }
    }

    void maybe_schedule_idle_stop() {
        if (state_ != ConnectionState::Ready) {
            return;
        }
        if (!session_ids_.empty()) {
            return;
        }
        if (bridge_ && bridge_->active_session_count() > 0) {
            return;
        }

        auto self = shared_from_this();
        idle_timer_.expires_after(std::chrono::seconds(300));
        idle_timer_.async_wait([self](const boost::system::error_code& ec) {
            if (ec) {
                return;
            }
            if (!self->session_ids_.empty()) {
                return;
            }
            if (self->bridge_ && self->bridge_->active_session_count() > 0) {
                return;
            }
            self->stop();
        });
    }

    PeerlinkService& service_;
    boost::asio::io_context& ioc_;
    std::string connection_id_;
    CreateSessionRequest request_;
    uint16_t local_port_ = 0;
    std::shared_ptr<ConnectBridge> bridge_;
    ConnectionState state_ = ConnectionState::Starting;
    std::string last_error_;
    std::set<std::string> session_ids_;
    boost::asio::steady_timer idle_timer_;
    std::vector<ReadyWaiter> waiters_;
};

class PeerlinkDaemonServer::ClientSession
    : public std::enable_shared_from_this<PeerlinkDaemonServer::ClientSession> {
public:
    ClientSession(boost::asio::local::stream_protocol::socket socket,
                  PeerlinkService& service)
        : socket_(std::move(socket))
        , service_(service) {}

    void start() {
        read_request();
    }

private:
    void read_request() {
        auto self = shared_from_this();
        boost::asio::async_read_until(socket_, buffer_, '\n',
            [self](const boost::system::error_code& ec, std::size_t) {
                if (ec) {
                    return;
                }

                std::istream input(&self->buffer_);
                std::string line;
                std::getline(input, line);
                if (line.empty()) {
                    self->write_response(make_error(
                        bto::observability::code::kDaemonInvalidRequest,
                        "empty request"));
                    return;
                }

                try {
                    auto request = Json::parse(line);
                    self->service_.handle_request(request, [self](Json response) {
                        self->write_response(std::move(response));
                    });
                } catch (const std::exception& ex) {
                    self->write_response(make_error(
                        bto::observability::code::kDaemonInvalidJson,
                        ex.what()));
                }
            });
    }

    void write_response(Json response) {
        auto payload = std::make_shared<std::string>(response.dump() + "\n");
        auto self = shared_from_this();
        boost::asio::async_write(socket_, boost::asio::buffer(*payload),
            [self, payload](const boost::system::error_code&, std::size_t) {
                boost::system::error_code ignored;
                self->socket_.shutdown(boost::asio::local::stream_protocol::socket::shutdown_both,
                                       ignored);
                self->socket_.close(ignored);
            });
    }

    boost::asio::local::stream_protocol::socket socket_;
    boost::asio::streambuf buffer_;
    PeerlinkService& service_;
};

PeerlinkService::PeerlinkService(boost::asio::io_context& ioc)
    : ioc_(ioc) {}

void PeerlinkService::handle_request(const Json& request, ReplyFn reply) {
    const auto action = request.value("action", "");
    if (action == "status") {
        handle_status(std::move(reply));
        return;
    }
    if (action == "list_connections") {
        handle_list_connections(std::move(reply));
        return;
    }
    if (action == "list_targets") {
        handle_list_targets(std::move(reply));
        return;
    }
    if (action == "create_session") {
        handle_create_session(request, std::move(reply));
        return;
    }
    if (action == "close_session") {
        handle_close_session(request, std::move(reply));
        return;
    }
    if (action == "close_target") {
        handle_close_target(request, std::move(reply));
        return;
    }
    if (action == "shutdown") {
        handle_shutdown(std::move(reply));
        return;
    }
    reply(make_error(
        bto::observability::code::kDaemonUnknownAction,
        "unknown action: " + action));
}

void PeerlinkService::handle_status(ReplyFn reply) {
    reply(make_ok({
        {"connections", connections_.size()},
        {"sessions", sessions_.size()},
        {"socket_path", config::default_daemon_socket_path()},
    }));
}

void PeerlinkService::handle_list_connections(ReplyFn reply) {
    Json connections = Json::array();
    for (const auto& [key, connection] : connections_) {
        connections.push_back(connection->view());
    }
    reply(make_ok({
        {"connections", std::move(connections)},
    }));
}

void PeerlinkService::handle_list_targets(ReplyFn reply) {
    Json targets = Json::array();
    auto config_path = config::default_config_path();
    auto loaded = config::Config::load(config_path).value_or(config::Config{});

    for (const auto& [name, peer] : loaded.peers) {
        targets.push_back(Json{
            {"name", name},
            {"did", peer.did},
            {"user", peer.user},
            {"key", peer.key},
            {"port", peer.port},
        });
    }

    reply(make_ok({
        {"local_did", loaded.did},
        {"relay", loaded.relay},
        {"targets", std::move(targets)},
    }));
}

void PeerlinkService::handle_create_session(const Json& request, ReplyFn reply) {
    CreateSessionRequest create_request;
    create_request.trace_id = request.value("trace_id", p2p::utils::make_trace_id("session"));
    create_request.target_name = request.value("target_name", "");
    create_request.target_did = request.value("target_did", "");
    create_request.local_did = request.value("local_did", "");
    create_request.relay_host = request.value("relay_host", "");
    create_request.relay_port = static_cast<uint16_t>(request.value("relay_port", 9700));
    create_request.relay_token = request.value("relay_token", "");
    create_request.ssh_user = request.value("ssh_user", "");
    create_request.ssh_key = request.value("ssh_key", "");
    if (request.contains("requested_port") && !request["requested_port"].is_null()) {
        create_request.requested_port =
            static_cast<uint16_t>(request["requested_port"].get<int>());
    }

    auto requested = daemon_event("session", "session.create.requested");
    add_request_context(requested, create_request);
    p2p::utils::emit_structured_log(spdlog::level::info, std::move(requested));

    if (create_request.target_did.empty()) {
        reply(make_error(
            bto::observability::code::kDaemonInvalidRequest,
            "target_did is required",
            error_details_for_trace(create_request.trace_id)));
        return;
    }
    if (create_request.local_did.empty()) {
        reply(make_error(
            bto::observability::code::kDaemonInvalidRequest,
            "local_did is required",
            error_details_for_trace(create_request.trace_id)));
        return;
    }
    if (create_request.relay_host.empty()) {
        reply(make_error(
            bto::observability::code::kDaemonInvalidRequest,
            "relay_host is required",
            error_details_for_trace(create_request.trace_id)));
        return;
    }
    if (create_request.target_name.empty()) {
        create_request.target_name = create_request.target_did;
    }

    const auto key = connection_key(create_request);
    auto existing = connections_.find(key);
    if (existing != connections_.end() && !existing->second->can_use_port(create_request.requested_port)) {
        reply(make_error(
            bto::observability::code::kDaemonPortConflict,
            "target already attached to a different local port",
            error_details_for_trace(create_request.trace_id)));
        return;
    }

    std::shared_ptr<TargetConnection> connection;
    bool needs_start = false;
    if (existing == connections_.end()) {
        auto port = endpoint_manager_.acquire(key, create_request.requested_port);
        if (!port) {
            reply(make_error(
                bto::observability::code::kDaemonPortUnavailable,
                "no available local port",
                error_details_for_trace(create_request.trace_id)));
            return;
        }

        connection = std::make_shared<TargetConnection>(*this, ioc_, key, create_request, *port);
        connections_[key] = connection;
        needs_start = true;
    } else {
        connection = existing->second;
    }

    connection->add_waiter([this, connection, create_request, reply](bool ok, const std::string& error) mutable {
        if (!ok) {
            auto failed = daemon_event("session", "session.create.failed");
            add_request_context(failed, create_request);
            failed["connection_id"] = connection->connection_id();
            failed["error_code"] = bto::observability::code::kDaemonConnectFailed;
            failed["error_detail"] = error;
            p2p::utils::emit_structured_log(spdlog::level::err, std::move(failed));
            reply(make_error(
                bto::observability::code::kDaemonConnectFailed,
                error,
                Json{{"trace_id", create_request.trace_id}, {"connection_id", connection->connection_id()}}));
            return;
        }

        SessionRecord record;
        record.trace_id = create_request.trace_id;
        record.session_id = next_session_id();
        record.connection_id = connection->connection_id();
        record.target_name = create_request.target_name;
        record.target_did = create_request.target_did;
        record.local_port = connection->local_port();
        record.ssh_user = create_request.ssh_user;
        record.ssh_key = create_request.ssh_key;

        sessions_[record.session_id] = record;
        connection->add_session(record.session_id);
        auto timer = std::make_unique<boost::asio::steady_timer>(ioc_);
        auto arm_timeout = std::make_shared<std::function<void(const boost::system::error_code&)>>();
        *arm_timeout = [this, session_id = record.session_id, arm_timeout](
                           const boost::system::error_code& ec) {
            if (ec) {
                return;
            }

            const auto session_it = sessions_.find(session_id);
            if (session_it == sessions_.end()) {
                return;
            }
            const auto connection_it = connections_.find(session_it->second.connection_id);
            if (connection_it != connections_.end() &&
                !should_release_session_after_timeout(connection_it->second->live_tunnel_count())) {
                auto timer_it = session_timers_.find(session_id);
                if (timer_it != session_timers_.end()) {
                    timer_it->second->expires_after(std::chrono::minutes(10));
                    timer_it->second->async_wait(*arm_timeout);
                }
                return;
            }

            release_session(session_id);
        };
        timer->expires_after(std::chrono::minutes(10));
        timer->async_wait(*arm_timeout);
        session_timers_[record.session_id] = std::move(timer);

        auto ready = daemon_event("session", "session.create.ready");
        add_request_context(ready, create_request);
        ready["connection_id"] = record.connection_id;
        ready["session_id"] = record.session_id;
        ready["local_port"] = record.local_port;
        p2p::utils::emit_structured_log(spdlog::level::info, std::move(ready));

        reply(make_ok({
            {"session", make_session_view(record, connection->state_name())},
            {"connection", connection->view()},
        }));
    });

    if (needs_start) {
        connection->start();
    }
}

void PeerlinkService::handle_close_session(const Json& request, ReplyFn reply) {
    const auto session_id = request.value("session_id", "");
    if (session_id.empty()) {
        reply(make_error(
            bto::observability::code::kDaemonInvalidRequest,
            "session_id is required"));
        return;
    }
    if (!sessions_.contains(session_id)) {
        reply(make_error(
            bto::observability::code::kDaemonNotFound,
            "session not found"));
        return;
    }
    release_session(session_id);
    reply(make_ok({
        {"session_id", session_id},
    }));
}

void PeerlinkService::handle_close_target(const Json& request, ReplyFn reply) {
    const auto query = request.value("target", "");
    const auto local_did = request.value("local_did", "");
    const auto relay = request.value("relay", "");
    if (query.empty()) {
        reply(make_error(
            bto::observability::code::kDaemonInvalidRequest,
            "target is required"));
        return;
    }

    std::vector<std::shared_ptr<TargetConnection>> matches;
    std::string matched_connection_id;
    for (auto& [connection_id, connection] : connections_) {
        auto view = connection->view();
        if (!local_did.empty() && view.local_did != local_did) {
            continue;
        }
        if (!relay.empty() && view.relay != relay) {
            continue;
        }
        if (connection_id == query ||
            connection->target_name() == query ||
            connection->target_did() == query) {
            matches.push_back(connection);
            matched_connection_id = connection_id;
        }
    }

    if (matches.size() > 1) {
        reply(make_error(
            bto::observability::code::kDaemonAmbiguousTarget,
            "target matches multiple connections"));
        return;
    }
    if (matches.size() == 1) {
        matches.front()->stop();
        reply(make_ok({
            {"connection_id", matched_connection_id},
        }));
        return;
    }

    reply(make_error(
        bto::observability::code::kDaemonNotFound,
        "target not found"));
}

void PeerlinkService::handle_shutdown(ReplyFn reply) {
    reply(make_ok({
        {"shutdown", true},
    }));
    if (shutdown_handler_) {
        auto handler = shutdown_handler_;
        std::thread([handler]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            handler();
        }).detach();
    }
}

void PeerlinkService::on_connection_stopped(const std::string& connection_id) {
    auto it = connections_.find(connection_id);
    if (it == connections_.end()) {
        return;
    }

    auto event = daemon_event("connection", "connection.released");
    event["connection_id"] = connection_id;
    event["trace_id"] = it->second->view().trace_id;
    p2p::utils::emit_structured_log(spdlog::level::info, std::move(event));

    endpoint_manager_.release(connection_id);

    for (auto session_it = sessions_.begin(); session_it != sessions_.end();) {
        if (session_it->second.connection_id == connection_id) {
            auto timer_it = session_timers_.find(session_it->first);
            if (timer_it != session_timers_.end()) {
                timer_it->second->cancel();
                session_timers_.erase(timer_it);
            }
            session_it = sessions_.erase(session_it);
        } else {
            ++session_it;
        }
    }

    connections_.erase(it);
}

void PeerlinkService::release_session(const std::string& session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return;
    }

    auto connection_it = connections_.find(it->second.connection_id);
    if (connection_it != connections_.end()) {
        connection_it->second->release_session(session_id);
    }

    auto timer_it = session_timers_.find(session_id);
    if (timer_it != session_timers_.end()) {
        timer_it->second->cancel();
        session_timers_.erase(timer_it);
    }

    sessions_.erase(it);
}

auto PeerlinkService::next_session_id() -> std::string {
    return "sess-" + std::to_string(next_session_id_++);
}

auto PeerlinkService::connection_key(const CreateSessionRequest& request) const -> std::string {
    return request.local_did + "|" + request.target_did + "|" +
           request.relay_host + ":" + std::to_string(request.relay_port);
}

auto PeerlinkService::make_session_view(const SessionRecord& record, const std::string& state) const
    -> SessionView {
    SessionView view;
    view.trace_id = record.trace_id;
    view.session_id = record.session_id;
    view.connection_id = record.connection_id;
    view.target_name = record.target_name;
    view.target_did = record.target_did;
    view.local_port = record.local_port;
    view.ssh_user = record.ssh_user;
    view.ssh_key = record.ssh_key;
    view.state = state;
    return view;
}

PeerlinkDaemonServer::PeerlinkDaemonServer(boost::asio::io_context& ioc, std::string socket_path)
    : ioc_(ioc)
    , socket_path_(std::move(socket_path))
    , lock_path_(socket_path_ + ".lock")
    , acceptor_(ioc)
    , service_(ioc) {
    service_.set_shutdown_handler([this]() {
        stop();
    });
}

PeerlinkDaemonServer::~PeerlinkDaemonServer() {
    stop();
}

auto PeerlinkDaemonServer::start() -> bool {
    if (started_) {
        return true;
    }

    try {
        std::filesystem::create_directories(std::filesystem::path(socket_path_).parent_path());
        lock_fd_ = ::open(lock_path_.c_str(), O_CREAT | O_RDWR, 0600);
        if (lock_fd_ < 0) {
            return false;
        }
        if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
            ::close(lock_fd_);
            lock_fd_ = -1;
            return false;
        }

        std::filesystem::remove(socket_path_);

        boost::system::error_code ec;
        acceptor_.open(boost::asio::local::stream_protocol(), ec);
        if (ec) {
            return false;
        }
        acceptor_.bind(socket_path_, ec);
        if (ec) {
            return false;
        }
        acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec) {
            return false;
        }
        ::chmod(socket_path_.c_str(), 0600);

        started_ = true;
        do_accept();
        return true;
    } catch (...) {
        return false;
    }
}

void PeerlinkDaemonServer::stop() {
    if (!started_) {
        if (lock_fd_ >= 0) {
            ::flock(lock_fd_, LOCK_UN);
            ::close(lock_fd_);
            lock_fd_ = -1;
        }
        return;
    }

    started_ = false;
    boost::system::error_code ignored;
    acceptor_.close(ignored);
    std::filesystem::remove(socket_path_);
    if (lock_fd_ >= 0) {
        ::flock(lock_fd_, LOCK_UN);
        ::close(lock_fd_);
        lock_fd_ = -1;
    }
    ioc_.stop();
}

void PeerlinkDaemonServer::do_accept() {
    if (!started_) {
        return;
    }

    acceptor_.async_accept([this](const boost::system::error_code& ec,
                                  boost::asio::local::stream_protocol::socket socket) {
        if (!ec) {
            std::make_shared<ClientSession>(std::move(socket), service_)->start();
        }
        if (started_) {
            do_accept();
        }
    });
}

}  // namespace bto::daemon
