#include "daemon/local_endpoint_manager.hpp"

#include <boost/asio.hpp>

namespace bto::daemon {

auto LocalEndpointManager::acquire(const std::string& key,
                                   std::optional<uint16_t> preferred_port)
    -> std::optional<uint16_t> {
    auto existing = assigned_ports_.find(key);
    if (existing != assigned_ports_.end()) {
        if (preferred_port && *preferred_port != existing->second) {
            return std::nullopt;
        }
        return existing->second;
    }

    std::optional<uint16_t> port = preferred_port;
    if (port && !is_port_available(*port)) {
        return std::nullopt;
    }
    if (!port) {
        port = next_available_port();
    }
    if (!port) {
        return std::nullopt;
    }

    assigned_ports_[key] = *port;
    reserved_ports_.insert(*port);
    return port;
}

void LocalEndpointManager::release(const std::string& key) {
    auto it = assigned_ports_.find(key);
    if (it == assigned_ports_.end()) {
        return;
    }
    reserved_ports_.erase(it->second);
    assigned_ports_.erase(it);
}

auto LocalEndpointManager::is_port_available(uint16_t port) const -> bool {
    if (reserved_ports_.contains(port)) {
        return false;
    }

    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acceptor(ioc);
    boost::system::error_code ec;

    acceptor.open(boost::asio::ip::tcp::v4(), ec);
    if (ec) {
        return false;
    }
    acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        return false;
    }
    acceptor.bind({boost::asio::ip::tcp::v4(), port}, ec);
    return !ec;
}

auto LocalEndpointManager::next_available_port() const -> std::optional<uint16_t> {
    for (uint32_t port = start_port_; port <= 65535; ++port) {
        auto candidate = static_cast<uint16_t>(port);
        if (is_port_available(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace bto::daemon
