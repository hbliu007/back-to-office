#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bto::daemon {

using Json = nlohmann::json;

inline constexpr std::string_view kApiVersion = "v1";

struct CreateSessionRequest {
    std::string target_name;
    std::string target_did;
    std::string local_did;
    std::string relay_host;
    uint16_t relay_port = 9700;
    std::optional<uint16_t> requested_port;
    std::string ssh_user;
    std::string ssh_key;
};

struct SessionView {
    std::string session_id;
    std::string connection_id;
    std::string target_name;
    std::string target_did;
    uint16_t local_port = 0;
    std::string ssh_user;
    std::string ssh_key;
    std::string state;
};

struct ConnectionView {
    std::string connection_id;
    std::string target_name;
    std::string target_did;
    std::string local_did;
    std::string relay;
    uint16_t local_port = 0;
    std::size_t active_sessions = 0;
    std::size_t live_tunnels = 0;
    std::string state;
    std::string last_error;
};

inline auto make_ok(Json result = Json::object()) -> Json {
    return Json{
        {"ok", true},
        {"version", kApiVersion},
        {"result", std::move(result)},
    };
}

inline auto make_error(const std::string& code, const std::string& message) -> Json {
    return Json{
        {"ok", false},
        {"version", kApiVersion},
        {"error", {
            {"code", code},
            {"message", message},
        }},
    };
}

inline void to_json(Json& json, const SessionView& value) {
    json = Json{
        {"session_id", value.session_id},
        {"connection_id", value.connection_id},
        {"target_name", value.target_name},
        {"target_did", value.target_did},
        {"local_port", value.local_port},
        {"ssh_user", value.ssh_user},
        {"ssh_key", value.ssh_key},
        {"state", value.state},
    };
}

inline void to_json(Json& json, const ConnectionView& value) {
    json = Json{
        {"connection_id", value.connection_id},
        {"target_name", value.target_name},
        {"target_did", value.target_did},
        {"local_did", value.local_did},
        {"relay", value.relay},
        {"local_port", value.local_port},
        {"active_sessions", value.active_sessions},
        {"live_tunnels", value.live_tunnels},
        {"state", value.state},
        {"last_error", value.last_error},
    };
}

}  // namespace bto::daemon
