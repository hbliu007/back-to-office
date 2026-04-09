/**
 * @file config.cpp
 * @brief BTO 配置实现
 */

#include "config.hpp"
#include <fstream>
#include <algorithm>
#include <cstdlib>

namespace bto::config {

namespace {

auto trim(const std::string& s) -> std::string {
    auto start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

auto unquote(const std::string& s) -> std::string {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

}  // anonymous namespace

auto default_config_path() -> std::string {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.bto/config.toml";
}

auto default_runtime_dir() -> std::string {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.peerlink/run";
}

auto default_daemon_socket_path() -> std::string {
    return default_runtime_dir() + "/peerlinkd.sock";
}

auto Config::load(const std::string& path) -> std::optional<Config> {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    Config config;
    std::string current_peer;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        // Section: [peers.name] 或 [hosts.name] (v0 兼容)
        if (line[0] == '[') {
            auto end = line.find(']');
            if (end == std::string::npos) continue;
            auto section = trim(line.substr(1, end - 1));

            if (section.find("peers.") == 0) {
                current_peer = section.substr(6);
                config.peers[current_peer] = PeerConfig{};
                config.peers[current_peer].did = current_peer;
            } else if (section.find("hosts.") == 0) {
                current_peer = section.substr(6);
                config.peers[current_peer] = PeerConfig{};
                config.peers[current_peer].did = current_peer;
            } else {
                current_peer.clear();
            }
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto key = trim(line.substr(0, eq));
        auto value = unquote(trim(line.substr(eq + 1)));

        if (current_peer.empty()) {
            if (key == "did" || key == "identity") config.did = value;
            else if (key == "relay") config.relay = value;
        } else {
            if (key == "did") config.peers[current_peer].did = value;
            else if (key == "user") config.peers[current_peer].user = value;
            else if (key == "key") config.peers[current_peer].key = value;
            else if (key == "host") config.peers[current_peer].host = value;
            else if (key == "port") {
                try { config.peers[current_peer].port =
                          static_cast<uint16_t>(std::stoi(value)); }
                catch (...) {}
            }
        }
    }

    return config;
}

auto Config::save(const std::string& path) const -> bool {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "# BTO 配置文件\n";
    file << "did = \"" << did << "\"\n";
    file << "relay = \"" << relay << "\"\n";

    for (const auto& [name, peer] : peers) {
        file << "\n[peers." << name << "]\n";
        file << "  did = \"" << peer.did << "\"\n";
        if (!peer.user.empty()) file << "  user = \"" << peer.user << "\"\n";
        if (!peer.key.empty())  file << "  key = \"" << peer.key << "\"\n";
        if (!peer.host.empty()) file << "  host = \"" << peer.host << "\"\n";
        if (peer.port != 22)    file << "  port = " << peer.port << "\n";
    }

    return file.good();
}

auto Config::resolve_peer(const std::string& input) const
    -> std::optional<std::pair<std::string, PeerConfig>> {
    // 精确匹配
    auto it = peers.find(input);
    if (it != peers.end()) return *it;

    // 后缀/前缀模糊匹配
    std::vector<std::string> matches;
    for (const auto& [key, peer] : peers) {
        if (key.size() >= input.size()) {
            if (key.compare(key.size() - input.size(), input.size(), input) == 0 ||
                key.compare(0, input.size(), input) == 0) {
                matches.push_back(key);
            }
        }
    }
    if (matches.size() == 1)
        return std::make_pair(matches[0], peers.at(matches[0]));

    return std::nullopt;
}

auto Config::relay_host() const -> std::string {
    auto pos = relay.find_last_of(':');
    return (pos != std::string::npos) ? relay.substr(0, pos) : relay;
}

auto Config::relay_port() const -> uint16_t {
    auto pos = relay.find_last_of(':');
    if (pos != std::string::npos) {
        try { return static_cast<uint16_t>(std::stoi(relay.substr(pos + 1))); }
        catch (...) {}
    }
    return 9700;
}

}  // namespace bto::config
