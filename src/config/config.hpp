/**
 * @file config.hpp
 * @brief BTO 配置 — 纯客户端，连接远端 P2P 设备
 */

#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <optional>
#include <utility>

namespace bto::config {

struct PeerConfig {
    std::string did;
    std::string user;           // SSH 用户名（可选）
    std::string key;            // SSH 私钥路径（可选）
    uint16_t port = 22;         // SSH 端口（可选，默认 22）
};

struct Config {
    std::string did;                            // 本机 DID
    std::string relay;                          // relay 地址 host:port
    std::map<std::string, PeerConfig> peers;    // 已知对端

    [[nodiscard]] static auto load(const std::string& path) -> std::optional<Config>;
    [[nodiscard]] auto save(const std::string& path) const -> bool;
    [[nodiscard]] auto resolve_peer(const std::string& input) const
        -> std::optional<std::pair<std::string, PeerConfig>>;

    [[nodiscard]] auto relay_host() const -> std::string;
    [[nodiscard]] auto relay_port() const -> uint16_t;
};

auto default_config_path() -> std::string;

}  // namespace bto::config
