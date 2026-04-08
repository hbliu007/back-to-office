/**
 * @file bto.cpp
 * @brief BTO 主入口 — Back-To-Office P2P SSH 隧道客户端
 *
 * 纯客户端：连接远端已注册的 P2P 设备，本地端口转发到 SSH。
 * 被连方由 P2P 小组的 p2p-tunnel-server 负责注册和监听。
 */

#include "cli/parser.hpp"
#include "config/config.hpp"
#include "daemon/common.hpp"
#include "daemon/daemon_client.hpp"
#include "p2p_bridge_v2.hpp"
#include "p2p/core/artifact_transfer.hpp"
#include "upgrade/manifest_client.hpp"
#include "upgrade/upgrade_client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <optional>
#include <thread>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

namespace EC = bto::cli::ExitCode;

std::atomic<boost::asio::io_context*> g_ioc{nullptr};

struct ResolvedTarget {
    std::string name;
    std::string did;
    std::string user;
    std::string key;
    bool from_config = false;
};

void signal_handler(int /*sig*/) {
    auto* ioc = g_ioc.load(std::memory_order_acquire);
    if (ioc) {
        ioc->stop();
    }
}

auto current_user() -> std::string {
    const char* user = std::getenv("USER");
    return user ? user : "root";
}

auto default_ssh_user() -> std::string {
    const char* user = std::getenv("BTO_DEFAULT_SSH_USER");
    return user ? std::string(user) : "lhb";
}

auto current_executable_path() -> std::optional<std::filesystem::path> {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return std::nullopt;
    }
    return std::filesystem::canonical(buffer.c_str());
#elif defined(__linux__)
    std::vector<char> buffer(PATH_MAX);
    const auto size = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size <= 0) {
        return std::nullopt;
    }
    buffer[static_cast<std::size_t>(size)] = '\0';
    return std::filesystem::canonical(buffer.data());
#else
    return std::nullopt;
#endif
}

auto peerlinkd_exec_path() -> std::string {
    auto self = current_executable_path();
    if (self) {
        auto sibling = self->parent_path() / "peerlinkd";
        if (std::filesystem::exists(sibling)) {
            return sibling.string();
        }
    }
    return "peerlinkd";
}

auto build_p2p_config(const std::string& relay_host, uint16_t relay_port)
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
    return cfg;
}

auto parse_relay(const std::string& relay) -> std::pair<std::string, uint16_t> {
    auto pos = relay.find_last_of(':');
    if (pos != std::string::npos) {
        try {
            return {relay.substr(0, pos),
                    static_cast<uint16_t>(std::stoi(relay.substr(pos + 1)))};
        } catch (...) {
            std::cerr << "警告: relay 端口格式错误，使用默认 9700\n";
        }
    }
    return {relay, 9700};
}

auto resolve_target(const bto::config::Config& config, const std::string& input) -> ResolvedTarget {
    ResolvedTarget target;
    target.name = input;
    target.did = input;
    target.user = default_ssh_user();

    auto resolved = config.resolve_peer(input);
    if (!resolved) {
        return target;
    }

    target.name = resolved->first;
    target.did = resolved->second.did;
    target.user = resolved->second.user.empty() ? default_ssh_user() : resolved->second.user;
    target.key = resolved->second.key;
    target.from_config = true;
    return target;
}

struct UpgradeDefaults {
    std::string artifact_name;
    std::string live_binary;
    std::string activate_command;
    std::string rollback_command;
    std::string health_command;
};

auto default_upgrade_defaults(const bto::cli::Command& cmd) -> UpgradeDefaults {
    UpgradeDefaults defaults;
    defaults.artifact_name = cmd.artifact_name.empty() ? "p2p-tunnel-server" : cmd.artifact_name;
    if (defaults.artifact_name == "p2p-tunnel-server") {
        defaults.live_binary = cmd.live_binary.empty()
            ? "/usr/local/bin/p2p-tunnel-server"
            : cmd.live_binary;
        defaults.activate_command = cmd.activate_command.empty()
            ? "systemctl restart peerlink-tunnel"
            : cmd.activate_command;
        defaults.rollback_command = cmd.rollback_command.empty()
            ? "systemctl restart peerlink-tunnel"
            : cmd.rollback_command;
        defaults.health_command = cmd.health_command.empty()
            ? "systemctl is-active --quiet peerlink-tunnel"
            : cmd.health_command;
        return defaults;
    }

    defaults.live_binary = cmd.live_binary;
    defaults.activate_command = cmd.activate_command;
    defaults.rollback_command = cmd.rollback_command;
    defaults.health_command = cmd.health_command;
    return defaults;
}

auto spawn_ssh_process(const std::string& user, const std::string& key, uint16_t port)
    -> std::optional<pid_t> {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<std::string> storage;
        std::vector<char*> argv;

        storage.emplace_back("ssh");
        storage.emplace_back("-o");
        storage.emplace_back("StrictHostKeyChecking=no");
        storage.emplace_back("-o");
        storage.emplace_back("UserKnownHostsFile=/dev/null");
        storage.emplace_back("-o");
        storage.emplace_back("LogLevel=ERROR");
        storage.emplace_back("-p");
        storage.emplace_back(std::to_string(port));
        if (!key.empty()) {
            storage.emplace_back("-i");
            storage.emplace_back(key);
        }
        storage.emplace_back(user + "@127.0.0.1");

        for (auto& item : storage) {
            argv.push_back(item.data());
        }
        argv.push_back(nullptr);

        execvp("ssh", argv.data());
        _exit(127);
    }

    if (pid < 0) {
        return std::nullopt;
    }
    return pid;
}

auto wait_for_child(pid_t pid) -> int {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return EC::NETWORK;
        }
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return EC::NETWORK;
}

auto run_ssh(const std::string& user, const std::string& key, uint16_t port) -> int {
    auto pid = spawn_ssh_process(user, key, port);
    if (!pid) {
        return EC::NETWORK;
    }
    return wait_for_child(*pid);
}

auto spawn_peerlinkd(const std::string& socket_path) -> bool {
    const auto executable = peerlinkd_exec_path();
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }
        execl(executable.c_str(), "peerlinkd", "--socket", socket_path.c_str(), nullptr);
        execlp("peerlinkd", "peerlinkd", "--socket", socket_path.c_str(), nullptr);
        _exit(127);
    }
    return pid > 0;
}

auto ensure_daemon_available(const std::string& socket_path) -> bool {
    bto::daemon::DaemonClient client(socket_path);
    if (client.is_available()) {
        return true;
    }
    if (!spawn_peerlinkd(socket_path)) {
        return false;
    }
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (client.is_available()) {
            return true;
        }
    }
    return false;
}

auto try_connect_daemon(const bto::cli::Command& cmd, const bto::config::Config& config)
    -> std::optional<int> {
    if (cmd.target.empty()) {
        std::cerr << "错误: 请指定目标设备\n"
                  << "用法: bto connect <peer>\n";
        return EC::USAGE;
    }

    auto target = resolve_target(config, cmd.target);
    if (target.from_config) {
        std::cout << "解析 '" << cmd.target << "' → " << target.did << std::endl;
    }

    auto local_did = cmd.did.empty() ? config.did : cmd.did;
    if (local_did.empty()) {
        local_did = "bto-client";
    }

    auto relay = cmd.relay.empty() ? config.relay : cmd.relay;
    if (relay.empty()) {
        std::cerr << "错误: 未指定 Relay 服务器\n";
        return EC::CONFIG;
    }

    auto [relay_host, relay_port] = parse_relay(relay);
    const auto socket_path = bto::config::default_daemon_socket_path();
    if (!ensure_daemon_available(socket_path)) {
        return std::nullopt;
    }

    bto::daemon::DaemonClient client(socket_path);
    bto::daemon::Json request{
        {"action", "create_session"},
        {"target_name", target.name},
        {"target_did", target.did},
        {"local_did", local_did},
        {"relay_host", relay_host},
        {"relay_port", relay_port},
        {"ssh_user", target.user},
        {"ssh_key", target.key},
    };
    if (cmd.listen_port_explicit) {
        request["requested_port"] = cmd.listen_port;
    }

    bto::daemon::Json response;
    try {
        response = client.request(request);
    } catch (...) {
        return std::nullopt;
    }

    if (!response.value("ok", false)) {
        std::cerr << "[peerlinkd] "
                  << response["error"].value("message", "unknown error")
                  << "\n";
        return EC::NETWORK;
    }

    const auto& session = response["result"]["session"];
    const auto local_port = static_cast<uint16_t>(session.value("local_port", 0));
    const auto session_id = session.value("session_id", "");
    const auto ssh_user = session.value("ssh_user", target.user);
    const auto ssh_key = session.value("ssh_key", target.key);

    std::cout << "peerlinkd connect — 目标=" << target.did
              << " Relay=" << relay_host << ":" << relay_port
              << " 本地端口=" << local_port << std::endl;

    const int ssh_exit = run_ssh(ssh_user.empty() ? default_ssh_user() : ssh_user, ssh_key, local_port);

    if (!session_id.empty()) {
        try {
            client.request(bto::daemon::Json{
                {"action", "close_session"},
                {"session_id", session_id},
            });
        } catch (...) {
        }
    }

    return ssh_exit;
}

int cmd_connect_legacy(const bto::cli::Command& cmd, bto::config::Config& config) {
    if (cmd.target.empty()) {
        std::cerr << "错误: 请指定目标设备\n"
                  << "用法: bto connect <peer>\n";
        return EC::USAGE;
    }

    auto target = resolve_target(config, cmd.target);
    if (target.from_config) {
        std::cout << "解析 '" << cmd.target << "' → " << target.did << std::endl;
    }

    auto local_did = cmd.did.empty() ? config.did : cmd.did;
    if (local_did.empty()) {
        local_did = "bto-client";
    }

    auto relay = cmd.relay.empty() ? config.relay : cmd.relay;
    if (relay.empty()) {
        std::cerr << "错误: 未指定 Relay 服务器\n";
        return EC::CONFIG;
    }

    auto [relay_host, relay_port] = parse_relay(relay);
    auto p2p_cfg = build_p2p_config(relay_host, relay_port);

    boost::asio::io_context ioc;
    g_ioc.store(&ioc, std::memory_order_release);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "legacy connect — 目标=" << target.did
              << " Relay=" << relay_host << ":" << relay_port
              << " 本地端口=" << cmd.listen_port << std::endl;

    auto bridge = std::make_shared<bto::ConnectBridge>(
        ioc, local_did, target.did, p2p_cfg, cmd.listen_port);
    bridge->set_ssh_hint(target.user, target.key);
    if (!bridge->start()) {
        g_ioc.store(nullptr, std::memory_order_release);
        return EC::NETWORK;
    }

    int exit_code = EC::OK;
    bool ssh_started = false;
    bridge->on_ready([&]() {
        if (ssh_started) {
            return;
        }
        ssh_started = true;
        auto pid = spawn_ssh_process(target.user, target.key, cmd.listen_port);
        if (!pid) {
            exit_code = EC::NETWORK;
            ioc.stop();
            return;
        }
        std::thread([&ioc, &exit_code, pid]() {
            exit_code = wait_for_child(*pid);
            ioc.stop();
        }).detach();
    });

    bridge->on_failed([&](const std::string& message) {
        std::cerr << "[legacy] " << message << "\n";
        exit_code = EC::NETWORK;
        ioc.stop();
    });

    ioc.run();
    g_ioc.store(nullptr, std::memory_order_release);
    return exit_code;
}

int cmd_connect(const bto::cli::Command& cmd, bto::config::Config& config) {
    if (cmd.legacy_direct) {
        return cmd_connect_legacy(cmd, config);
    }

    auto daemon_result = try_connect_daemon(cmd, config);
    if (daemon_result.has_value()) {
        return *daemon_result;
    }

    std::cerr << "peerlinkd 不可用，回退到 legacy 直连模式\n";
    return cmd_connect_legacy(cmd, config);
}

int cmd_list(const bto::config::Config& config) {
    if (config.peers.empty()) {
        std::cout << "暂无已配置设备\n"
                  << "提示: 运行 bto add <name> --did <did> 添加设备\n";
        return EC::OK;
    }
    std::cout << "已配置设备 (" << config.peers.size() << " 台):\n";
    for (const auto& [name, peer] : config.peers) {
        std::cout << "  " << name;
        if (peer.did != name) std::cout << "  DID=" << peer.did;
        if (!peer.user.empty()) std::cout << "  user=" << peer.user;
        std::cout << "\n";
    }
    return EC::OK;
}

int cmd_status(const bto::config::Config& config) {
    std::cout << "本机 DID:   " << (config.did.empty() ? "(未设置)" : config.did) << "\n";
    std::cout << "Relay:      " << (config.relay.empty() ? "(未设置)" : config.relay) << "\n";
    std::cout << "已配置设备: " << config.peers.size() << " 台\n";
    std::cout << "Daemon:     " << bto::config::default_daemon_socket_path() << "\n";

    if (config.did.empty() || config.relay.empty()) {
        std::cout << "\n提示: 运行 bto config 查看配置文件位置\n";
    }
    return EC::OK;
}

int cmd_ps() {
    bto::daemon::DaemonClient client(bto::config::default_daemon_socket_path());
    if (!client.is_available()) {
        std::cout << "peerlinkd 未运行\n";
        return EC::OK;
    }

    try {
        auto response = client.request(bto::daemon::Json{
            {"action", "list_connections"},
        });
        if (!response.value("ok", false)) {
            std::cerr << response["error"].value("message", "daemon error") << "\n";
            return EC::NETWORK;
        }

        const auto& connections = response["result"]["connections"];
        if (connections.empty()) {
            std::cout << "当前无活动连接\n";
            return EC::OK;
        }

        for (const auto& item : connections) {
            std::cout << item.value("target_name", item.value("target_did", "unknown"))
                      << "  did=" << item.value("target_did", "")
                      << "  port=" << item.value("local_port", 0)
                      << "  state=" << item.value("state", "unknown")
                      << "  refs=" << item.value("active_sessions", 0)
                      << "  live=" << item.value("live_tunnels", 0)
                      << "\n";
        }
        return EC::OK;
    } catch (const std::exception& ex) {
        std::cerr << "peerlinkd 请求失败: " << ex.what() << "\n";
        return EC::NETWORK;
    }
}

int cmd_close(const bto::cli::Command& cmd, const bto::config::Config& config) {
    if (cmd.target.empty()) {
        std::cerr << "错误: 请指定要关闭的目标\n"
                  << "用法: bto close <peer>\n";
        return EC::USAGE;
    }

    auto target = resolve_target(config, cmd.target);
    auto local_did = cmd.did.empty() ? config.did : cmd.did;
    if (local_did.empty()) {
        local_did = "bto-client";
    }
    auto relay = cmd.relay.empty() ? config.relay : cmd.relay;
    std::string relay_pair;
    if (!relay.empty()) {
        auto [relay_host, relay_port] = parse_relay(relay);
        relay_pair = relay_host + ":" + std::to_string(relay_port);
    }

    bto::daemon::DaemonClient client(bto::config::default_daemon_socket_path());
    if (!client.is_available()) {
        std::cerr << "peerlinkd 未运行\n";
        return EC::NETWORK;
    }

    try {
        auto response = client.request(bto::daemon::Json{
            {"action", "close_target"},
            {"target", target.did},
            {"local_did", local_did},
            {"relay", relay_pair},
        });
        if (!response.value("ok", false)) {
            std::cerr << response["error"].value("message", "daemon error") << "\n";
            return EC::NETWORK;
        }
        std::cout << "已关闭目标桥接: " << target.did << "\n";
        return EC::OK;
    } catch (const std::exception& ex) {
        std::cerr << "peerlinkd 请求失败: " << ex.what() << "\n";
        return EC::NETWORK;
    }
}

int cmd_daemon(const bto::cli::Command& cmd) {
    const auto socket_path = bto::config::default_daemon_socket_path();
    bto::daemon::DaemonClient client(socket_path);
    auto action = cmd.daemon_action.empty() ? std::string("status") : cmd.daemon_action;

    if (action == "start") {
        if (!ensure_daemon_available(socket_path)) {
            std::cerr << "启动 peerlinkd 失败\n";
            return EC::NETWORK;
        }
        std::cout << "peerlinkd 已启动: " << socket_path << "\n";
        return EC::OK;
    }

    if (action == "stop") {
        if (!client.is_available()) {
            std::cout << "peerlinkd 未运行\n";
            return EC::OK;
        }
        try {
            client.request(bto::daemon::Json{{"action", "shutdown"}});
            std::cout << "peerlinkd 已停止\n";
            return EC::OK;
        } catch (const std::exception& ex) {
            std::cerr << "停止 peerlinkd 失败: " << ex.what() << "\n";
            return EC::NETWORK;
        }
    }

    if (client.is_available()) {
        try {
            auto response = client.request(bto::daemon::Json{{"action", "status"}});
            std::cout << "peerlinkd 运行中"
                      << "  connections=" << response["result"].value("connections", 0)
                      << "  sessions=" << response["result"].value("sessions", 0)
                      << "\n";
            return EC::OK;
        } catch (const std::exception& ex) {
            std::cerr << "peerlinkd 请求失败: " << ex.what() << "\n";
            return EC::NETWORK;
        }
    }

    std::cout << "peerlinkd 未运行\n";
    return EC::OK;
}

int cmd_config() {
    auto path = bto::config::default_config_path();
    std::cout << "配置文件: " << path << "\n\n";

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "(文件不存在)\n\n"
                  << "创建配置:\n"
                  << "  mkdir -p ~/.bto\n"
                  << "  cat > ~/.bto/config.toml << 'EOF'\n"
                  << "  did = \"my-device\"\n"
                  << "  relay = \"47.99.216.25:9700\"\n"
                  << "\n"
                  << "  [peers.office-213]\n"
                  << "    did = \"office-213\"\n"
                  << "    user = \"user\"\n"
                  << "  EOF\n";
        return EC::OK;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::cout << "  " << line << "\n";
    }
    return EC::OK;
}

int cmd_add(const bto::cli::Command& cmd, bto::config::Config& config) {

    if (cmd.target.empty()) {
        std::cerr << "错误: 请指定设备名称\n"
                  << "用法: bto add <name> [--did <did>] [--user <user>] [--key <path>]\n"
                  << "示例: bto add office-213 --user user\n";
        return EC::USAGE;
    }
    auto did = cmd.did.empty() ? cmd.target : cmd.did;
    bto::config::PeerConfig peer_cfg;
    peer_cfg.did = did;
    peer_cfg.user = cmd.user;
    peer_cfg.key = cmd.key;
    config.peers[cmd.target] = peer_cfg;

    auto path = bto::config::default_config_path();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    if (config.save(path)) {
        std::cout << "已添加: " << cmd.target << " (DID: " << did;
        if (!cmd.user.empty()) std::cout << ", user: " << cmd.user;
        if (!cmd.key.empty())  std::cout << ", key: " << cmd.key;
        std::cout << ")\n";
        return EC::OK;
    }
    std::cerr << "保存配置失败: " << path << "\n";
    return EC::CONFIG;
}

int cmd_remove(const bto::cli::Command& cmd, bto::config::Config& config) {

    if (cmd.target.empty()) {
        std::cerr << "错误: 请指定设备名称\n"
                  << "用法: bto remove <name>\n"
                  << "提示: 运行 bto list 查看已配置设备\n";
        return EC::USAGE;
    }
    if (config.peers.erase(cmd.target) == 0) {
        std::cerr << "未找到设备: " << cmd.target << "\n"
                  << "提示: 运行 bto list 查看已配置设备\n";
        return EC::PEER_NOT_FOUND;
    }
    auto path = bto::config::default_config_path();
    if (config.save(path)) {
        std::cout << "已移除: " << cmd.target << "\n";
        return EC::OK;
    }
    std::cerr << "保存配置失败: " << path << "\n";
    return EC::CONFIG;
}

int cmd_ping(const bto::cli::Command& cmd, const bto::config::Config& config) {
    auto relay = cmd.relay.empty() ? config.relay : cmd.relay;
    if (relay.empty()) {
        std::cerr << "错误: 未指定 Relay 服务器\n";
        return EC::CONFIG;
    }

    auto [rhost, rport] = parse_relay(relay);
    std::cout << "测试 Relay " << rhost << ":" << rport << " ..." << std::endl;

    try {
        boost::asio::io_context ioc;
        boost::asio::ip::tcp::resolver resolver(ioc);
        auto endpoints = resolver.resolve(rhost, std::to_string(rport));

        boost::asio::ip::tcp::socket socket(ioc);
        auto start = std::chrono::steady_clock::now();
        boost::asio::connect(socket, endpoints);
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // 发送 PING
        std::string ping_msg = "PING\n";
        boost::asio::write(socket, boost::asio::buffer(ping_msg));

        // 读响应
        boost::asio::streambuf buf;
        boost::asio::read_until(socket, buf, '\n');
        std::string response(boost::asio::buffers_begin(buf.data()),
                             boost::asio::buffers_end(buf.data()));

        // 去掉换行
        while (!response.empty() && (response.back() == '\n' || response.back() == '\r'))
            response.pop_back();

        socket.close();
        std::cout << "Relay 响应: " << response << " (" << ms << "ms)" << std::endl;
        return EC::OK;
    } catch (const std::exception& e) {
        std::cerr << "Relay 不可达: " << e.what() << "\n"
                  << "提示: 检查网络连接和 relay 地址\n";
        return EC::NETWORK;
    }
}

int cmd_upgrade(const bto::cli::Command& cmd, const bto::config::Config& config) {
    if (cmd.target.empty()) {
        std::cerr << "错误: 请指定目标设备\n"
                  << "用法: bto upgrade <peer>\n";
        return EC::USAGE;
    }

    auto relay = cmd.relay.empty() ? config.relay : cmd.relay;
    if (relay.empty()) {
        std::cerr << "错误: 未指定 Relay 服务器\n";
        return EC::CONFIG;
    }

    auto target = resolve_target(config, cmd.target);
    auto local_did = cmd.did.empty() ? config.did : cmd.did;
    if (local_did.empty()) {
        local_did = "bto-client";
    }

    const auto defaults = default_upgrade_defaults(cmd);
    if (defaults.artifact_name.empty()) {
        std::cerr << "错误: 未指定升级制品名\n";
        return EC::USAGE;
    }
    if (defaults.live_binary.empty()) {
        std::cerr << "错误: 请通过 --live-binary 指定远端目标路径\n";
        return EC::USAGE;
    }

    bto::upgrade::ManifestClient manifest_client(
        bto::upgrade::default_manifest_url(),
        bto::upgrade::default_binaries_base_url());
    std::string error;
    auto manifest = manifest_client.fetch_manifest(&error);
    if (!manifest.has_value()) {
        std::cerr << "拉取 manifest 失败: " << error << "\n";
        return EC::NETWORK;
    }

    auto it = std::find_if(
        manifest->artifacts.begin(), manifest->artifacts.end(),
        [&](const auto& item) { return item.name == defaults.artifact_name; });
    if (it == manifest->artifacts.end()) {
        std::cerr << "manifest 中未找到制品: " << defaults.artifact_name << "\n";
        return EC::CONFIG;
    }

    auto download_root = std::filesystem::temp_directory_path() / "peerlink-upgrades";
    auto download_path = download_root / it->name;
    if (!manifest_client.download_artifact(*it, download_path, &error)) {
        std::cerr << "下载制品失败: " << error << "\n";
        return EC::NETWORK;
    }

    const auto digest = p2p::core::compute_artifact_sha256(download_path);
    if (!digest.has_value() || *digest != it->sha256) {
        std::cerr << "制品校验失败: " << it->name << "\n";
        return EC::NETWORK;
    }

    auto [relay_host, relay_port] = parse_relay(relay);
    std::cout << "upgrade — 目标=" << target.did
              << " artifact=" << it->name
              << " version=" << manifest->version << "\n";

    bto::upgrade::UpgradeRequest request;
    request.local_did = local_did;
    request.target_did = target.did;
    request.relay_host = relay_host;
    request.relay_port = relay_port;
    request.artifact = *it;
    request.artifact_path = download_path;
    request.live_binary = defaults.live_binary;
    request.activate_command = defaults.activate_command;
    request.rollback_command = defaults.rollback_command;
    request.health_command = defaults.health_command;
    request.timeout_seconds = cmd.timeout_seconds;

    const auto result = bto::upgrade::run_remote_upgrade(request);
    if (!result.success) {
        std::cerr << "远端升级失败: " << result.error << "\n";
        return EC::NETWORK;
    }

    std::cout << "远端升级成功: " << target.did
              << "  replaced=" << (result.replaced ? "true" : "false")
              << "  rolled_back=" << (result.rolled_back ? "true" : "false")
              << "\n";
    return EC::OK;
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    auto cmd = bto::cli::parse_arguments(argc, argv);

    if (cmd.help)    { bto::cli::show_help(cmd.help_topic); return 0; }
    if (cmd.version) { bto::cli::show_version(); return 0; }

    // 加载配置
    auto config_path = bto::config::default_config_path();
    auto config = bto::config::Config::load(config_path).value_or(bto::config::Config{});
    auto runtime_config = config;

    if (!cmd.did.empty()) runtime_config.did = cmd.did;
    if (!cmd.relay.empty()) runtime_config.relay = cmd.relay;

    if (cmd.name == "connect")      return cmd_connect(cmd, runtime_config);
    if (cmd.name == "upgrade")      return cmd_upgrade(cmd, runtime_config);
    if (cmd.name == "list")         return cmd_list(config);
    if (cmd.name == "ps")           return cmd_ps();
    if (cmd.name == "close")        return cmd_close(cmd, runtime_config);
    if (cmd.name == "status")       return cmd_status(runtime_config);
    if (cmd.name == "daemon")       return cmd_daemon(cmd);
    if (cmd.name == "config")       return cmd_config();
    if (cmd.name == "add")          return cmd_add(cmd, config);
    if (cmd.name == "remove")       return cmd_remove(cmd, config);
    if (cmd.name == "ping")         return cmd_ping(cmd, runtime_config);

    std::cerr << "未知命令: " << cmd.name << "\n"
              << "运行 bto help 查看所有命令\n";
    return EC::USAGE;
}
