#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bto {

/**
 * @brief Validate that a string does not contain shell-dangerous characters.
 * @return true if safe, false if dangerous characters are present.
 */
inline bool is_safe_shell_arg(const std::string& s) {
    static constexpr const char* kDangerousChars = "@;|&$`\"'\\!(){}[]<>?*~#\n\r";
    return s.find_first_of(kDangerousChars) == std::string::npos;
}

/**
 * @brief Validate that a path does not contain path-traversal sequences.
 * @return true if safe, false if ".." is found.
 */
inline bool is_safe_path(const std::string& path) {
    return path.find("..") == std::string::npos;
}

inline auto build_ssh_argv(const std::string& user,
                           const std::string& key,
                           uint16_t port,
                           const std::string& host_key_alias,
                           bool insecure_connect = false,
                           const std::string& known_hosts_path = {})
    -> std::optional<std::vector<std::string>> {
    // Validate user: must not contain shell-dangerous characters
    if (user.empty() || !is_safe_shell_arg(user)) {
        return std::nullopt;
    }

    // Validate key path: must not contain path traversal
    if (!key.empty() && (!is_safe_path(key) || !is_safe_shell_arg(key))) {
        return std::nullopt;
    }

    std::vector<std::string> argv;
    argv.emplace_back("ssh");

    if (insecure_connect) {
        argv.emplace_back("-o");
        argv.emplace_back("StrictHostKeyChecking=no");
        argv.emplace_back("-o");
        argv.emplace_back("UserKnownHostsFile=/dev/null");
    } else {
        argv.emplace_back("-o");
        argv.emplace_back("StrictHostKeyChecking=accept-new");
        if (!known_hosts_path.empty()) {
            argv.emplace_back("-o");
            argv.emplace_back("UserKnownHostsFile=" + known_hosts_path);
        }
        if (!host_key_alias.empty()) {
            argv.emplace_back("-o");
            argv.emplace_back("HostKeyAlias=" + host_key_alias);
        }
    }

    argv.emplace_back("-o");
    argv.emplace_back("LogLevel=ERROR");
    argv.emplace_back("-o");
    argv.emplace_back("ServerAliveInterval=30");
    argv.emplace_back("-o");
    argv.emplace_back("ServerAliveCountMax=3");
    argv.emplace_back("-o");
    argv.emplace_back("TCPKeepAlive=yes");
    argv.emplace_back("-p");
    argv.emplace_back(std::to_string(port));

    if (!key.empty()) {
        argv.emplace_back("-i");
        argv.emplace_back(key);
    }

    argv.emplace_back(user + "@127.0.0.1");
    return argv;
}

}  // namespace bto
