#pragma once

#include "daemon/common.hpp"

#include <string>

namespace bto::daemon {

class DaemonClient {
public:
    explicit DaemonClient(std::string socket_path)
        : socket_path_(std::move(socket_path)) {}

    auto request(const Json& payload) const -> Json;
    auto is_available() const -> bool;

private:
    std::string socket_path_;
};

}  // namespace bto::daemon
