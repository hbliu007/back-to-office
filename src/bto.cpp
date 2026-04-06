/**
 * @file bto.cpp
 * @brief BTO 主入口 — Back-To-Office P2P SSH 隧道客户端
 *
 * 纯客户端：连接远端已注册的 P2P 设备，本地端口转发到 SSH。
 * 被连方由 P2P 小组的 p2p-tunnel-server 负责注册和监听。
 */

#include "cli/parser.hpp"
#include "config/config.hpp"
#include "p2p_bridge.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <csignal>
#include <atomic>

namespace {

std::atomic<boost::asio::io_context*> g_ioc{nullptr};

void signal_handler(int /*sig*/) {
    auto* ioc = g_ioc.load(std::memory_order_acquire);
    if (ioc) ioc->stop();
}

auto build_p2p_config(const std::string& relay_host, uint16_t relay_port)
    -> p2p::core::P2PConfig
{
    p2p::core::P2PConfig cfg;
    cfg.tcp_relay_server = relay_host;
    cfg.tcp_relay_port = relay_port;
    cfg.relay_mode = p2p::core::RelayMode::RELAY_ONLY;
    return cfg;
}

auto parse_relay(const std::string& relay)
    -> std::pair<std::string, uint16_t>
{
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

namespace EC = bto::cli::ExitCode;

int cmd_connect(const bto::cli::Command& cmd, bto::config::Config& config) {

    if (cmd.target.empty()) {
        std::cerr << "错误: 请指定目标设备\n"
                  << "用法: bto connect <peer>\n"
                  << "提示: 运行 bto list 查看已配置设备\n";
        return EC::USAGE;
    }

    // 解析 peer DID
    std::string peer_did = cmd.target;
    auto resolved = config.resolve_peer(cmd.target);
    if (resolved) {
        peer_did = resolved->second.did;
        std::cout << "解析 '" << cmd.target << "' → " << peer_did << std::endl;
    }

    auto did = cmd.did.empty() ? config.did : cmd.did;
    if (did.empty()) did = "bto-client";

    auto relay = cmd.relay.empty() ? config.relay : cmd.relay;
    if (relay.empty()) {
        std::cerr << "错误: 未指定 Relay 服务器\n"
                  << "提示: 使用 --relay <host:port> 或在配置文件中设置\n"
                  << "      运行 bto config 查看配置\n";
        return EC::CONFIG;
    }

    auto [rhost, rport] = parse_relay(relay);
    auto p2p_cfg = build_p2p_config(rhost, rport);

    boost::asio::io_context ioc;
    g_ioc.store(&ioc, std::memory_order_release);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "bto connect — 目标=" << peer_did
              << " Relay=" << rhost << ":" << rport
              << " 本地端口=" << cmd.listen_port << std::endl;

    auto bridge = std::make_shared<bto::ConnectBridge>(
        ioc, did, peer_did, p2p_cfg, cmd.listen_port);
    if (resolved) {
        bridge->set_ssh_hint(resolved->second.user, resolved->second.key);
    }
    if (!bridge->start()) {
        return EC::NETWORK;
    }
    ioc.run();

    g_ioc.store(nullptr, std::memory_order_release);
    return EC::OK;
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

    if (config.did.empty() || config.relay.empty()) {
        std::cout << "\n提示: 运行 bto config 查看配置文件位置\n";
    }
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
                  << "  relay = \"relay.bto.asia:9700\"\n"
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
                  << "示例: bto add office-213 --user lhb\n";
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

}  // anonymous namespace

int main(int argc, char* argv[]) {
    auto cmd = bto::cli::parse_arguments(argc, argv);

    if (cmd.help)    { bto::cli::show_help(cmd.help_topic); return 0; }
    if (cmd.version) { bto::cli::show_version(); return 0; }

    // 加载配置
    auto config_path = bto::config::default_config_path();
    auto config = bto::config::Config::load(config_path).value_or(bto::config::Config{});

    // 命令行覆盖
    if (!cmd.did.empty()) config.did = cmd.did;
    if (!cmd.relay.empty()) config.relay = cmd.relay;

    if (cmd.name == "connect")      return cmd_connect(cmd, config);
    if (cmd.name == "list")         return cmd_list(config);
    if (cmd.name == "status")       return cmd_status(config);
    if (cmd.name == "config")       return cmd_config();
    if (cmd.name == "add")          return cmd_add(cmd, config);
    if (cmd.name == "remove")       return cmd_remove(cmd, config);
    if (cmd.name == "ping")         return cmd_ping(cmd, config);

    std::cerr << "未知命令: " << cmd.name << "\n"
              << "运行 bto help 查看所有命令\n";
    return EC::USAGE;
}
