#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace bto::daemon {

class LocalEndpointManager {
public:
    explicit LocalEndpointManager(uint16_t start_port = 2222)
        : start_port_(start_port) {}

    auto acquire(const std::string& key, std::optional<uint16_t> preferred_port)
        -> std::optional<uint16_t>;
    void release(const std::string& key);

private:
    auto is_port_available(uint16_t port) const -> bool;
    auto next_available_port() const -> std::optional<uint16_t>;

    uint16_t start_port_ = 2222;
    std::map<std::string, uint16_t> assigned_ports_;
    std::set<uint16_t> reserved_ports_;
};

}  // namespace bto::daemon
