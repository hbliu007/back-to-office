/**
 * @file parser.cpp
 * @brief BTO 命令行解析实现
 */

#include "parser.hpp"
#include <iostream>
#include <vector>

namespace bto::cli {

auto parse_arguments(int argc, char* argv[]) -> Command {
    Command cmd;
    if (argc < 2) { cmd.name = "list"; return cmd; }

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    size_t i = 0;
    while (i < args.size()) {
        const auto& arg = args[i];

        if (arg == "--help" || arg == "-h") {
            cmd.help = true; return cmd;
        }
        if (arg == "--version" || arg == "-v") {
            cmd.version = true; return cmd;
        }

        // 子命令
        if (arg == "help") {
            cmd.help = true;
            if (i + 1 < args.size()) cmd.help_topic = args[++i];
            return cmd;
        }
        if (arg == "version") {
            cmd.version = true; return cmd;
        }
        if (arg == "connect") {
            cmd.name = "connect";
            if (i + 1 < args.size() && !args[i+1].empty() && args[i+1][0] != '-')
                cmd.target = args[++i];
        }
        else if (arg == "list")   { cmd.name = "list"; }
        else if (arg == "status") { cmd.name = "status"; }
        else if (arg == "config") { cmd.name = "config"; }
        else if (arg == "ping") {
            cmd.name = "ping";
            if (i + 1 < args.size() && !args[i+1].empty() && args[i+1][0] != '-')
                cmd.target = args[++i];
        }
        else if (arg == "add") {
            cmd.name = "add";
            if (i + 1 < args.size()) cmd.target = args[++i];
        }
        else if (arg == "remove") {
            cmd.name = "remove";
            if (i + 1 < args.size()) cmd.target = args[++i];
        }
        // 选项
        else if (arg == "--did" && i + 1 < args.size()) {
            cmd.did = args[++i];
        }
        else if (arg == "--relay" && i + 1 < args.size()) {
            cmd.relay = args[++i];
        }
        else if (arg == "--listen" && i + 1 < args.size()) {
            try {
                cmd.listen_port = static_cast<uint16_t>(std::stoi(args[++i]));
            } catch (...) {
                std::cerr << "警告: --listen 端口格式错误，使用默认 2222\n";
            }
        }
        else if (arg == "--user" && i + 1 < args.size()) {
            cmd.user = args[++i];
        }
        else if (arg == "--key" && i + 1 < args.size()) {
            cmd.key = args[++i];
        }
        // 快捷方式: bto <host> = bto connect <host>
        else if (cmd.name.empty() && !arg.empty() && arg[0] != '-') {
            cmd.name = "connect";
            cmd.target = arg;
        }
        ++i;
    }

    if (cmd.name.empty()) cmd.name = "list";
    return cmd;
}

// ─── Help ──────────────────────────────────────────────────────

namespace {

void help_overview() {
    std::cout <<
        "bto (Back-To-Office) — P2P SSH 隧道客户端\n"
        "\n"
        "用法:\n"
        "  bto connect <peer>           连接远端设备（支持多终端并发）\n"
        "  bto <peer>                   同上（快捷方式）\n"
        "  bto list                     列出已配置设备\n"
        "  bto add <name> [--did <d>]   添加设备\n"
        "  bto remove <name>            移除设备\n"
        "  bto status                   显示配置状态\n"
        "  bto config                   显示配置文件路径和内容\n"
        "  bto ping                     测试 Relay 是否可达\n"
        "  bto help [命令|errors]       查看帮助\n"
        "\n"
        "全局选项:\n"
        "  --did <did>          本机 DID（覆盖配置文件）\n"
        "  --relay <host:port>  Relay 服务器地址（覆盖配置文件）\n"
        "  --listen <port>      本地监听端口（默认 2222）\n"
        "  --user <user>        SSH 用户名（配合 add 使用）\n"
        "  --key <path>         SSH 私钥路径（配合 add 使用）\n"
        "  --version, -v        显示版本\n"
        "  --help, -h           显示帮助\n"
        "\n"
        "快速开始:\n"
        "  1. bto add office-213 --did office-213\n"
        "  2. bto connect office-213\n"
        "  3. ssh -p 2222 user@127.0.0.1\n"
        "\n"
        "更多帮助: bto help connect | bto help errors\n";
}

void help_connect() {
    std::cout <<
        "bto connect — 连接远端设备\n"
        "\n"
        "用法:\n"
        "  bto connect <peer> [选项]\n"
        "  bto <peer>                   快捷方式\n"
        "\n"
        "参数:\n"
        "  <peer>    设备名称或 DID。支持模糊匹配：\n"
        "            bto 213     → 匹配 office-213\n"
        "            bto office  → 匹配 office-213（仅当唯一匹配时）\n"
        "\n"
        "选项:\n"
        "  --listen <port>   本地监听端口（默认 2222）\n"
        "  --did <did>       本机 DID（覆盖配置）\n"
        "  --relay <h:p>     Relay 服务器（覆盖配置）\n"
        "\n"
        "多终端支持:\n"
        "  每个 SSH 连入创建独立 P2P 隧道，互不影响。\n"
        "  可同时开多个终端连接同一个远端设备。\n"
        "\n"
        "示例:\n"
        "  bto connect office-213\n"
        "  bto 213 --listen 3333\n"
        "  bto connect office-215 --relay relay.example.com:9700\n"
        "\n"
        "连接后:\n"
        "  ssh -p 2222 user@127.0.0.1           # 终端 1\n"
        "  ssh -p 2222 user@127.0.0.1           # 终端 2（并发）\n"
        "\n"
        "长时间工作建议:\n"
        "  • 远端使用 tmux，SSH 断了可恢复: tmux attach\n"
        "  • SSH 配置 ServerAliveInterval 30 防空闲断开\n"
        "  • bto 进程保持运行，SSH 断了重连即可\n";
}

void help_add() {
    std::cout <<
        "bto add — 添加设备\n"
        "\n"
        "用法:\n"
        "  bto add <name> [--did <did>]\n"
        "\n"
        "参数:\n"
        "  <name>     设备名称（用于快捷引用）\n"
        "  --did      设备 DID（默认与 name 相同）\n"
        "\n"
        "示例:\n"
        "  bto add office-213                    # DID = office-213\n"
        "  bto add 213 --did office-213          # 名称 213, DID office-213\n"
        "  bto add home --did home-macbook       # 自定义名称和 DID\n";
}

void help_remove() {
    std::cout <<
        "bto remove — 移除设备\n"
        "\n"
        "用法:\n"
        "  bto remove <name>\n"
        "\n"
        "示例:\n"
        "  bto remove office-213\n";
}

void help_config() {
    std::cout <<
        "bto config — 显示配置\n"
        "\n"
        "显示配置文件路径和当前配置内容。\n"
        "配置文件位于 ~/.bto/config.toml\n"
        "\n"
        "配置文件格式:\n"
        "  did = \"home-mac\"              # 本机 DID\n"
        "  relay = \"47.99.216.25:9700\"   # Relay 服务器\n"
        "\n"
        "  [peers.office-213]             # 设备配置\n"
        "    did = \"office-213\"\n"
        "\n"
        "  [peers.office-215]\n"
        "    did = \"office-215\"\n";
}

void help_errors() {
    std::cout <<
        "bto 退出码参考\n"
        "\n"
        "  0   成功\n"
        "  1   用法错误 — 缺少参数或未知命令\n"
        "      → 检查命令拼写，运行 bto help\n"
        "\n"
        "  2   配置错误 — 配置文件缺失或格式错误\n"
        "      → 检查 ~/.bto/config.toml\n"
        "      → 运行 bto config 查看当前配置\n"
        "\n"
        "  3   网络错误 — Relay 不可达或 P2P 连接失败\n"
        "      → 检查 relay 地址: bto status\n"
        "      → 测试 relay: nc -zv <host> <port>\n"
        "      → 确认远端 tunnel-server 已运行\n"
        "\n"
        "  4   设备未找到 — 指定的设备名不在配置中\n"
        "      → 运行 bto list 查看已配置设备\n"
        "      → 运行 bto add <name> 添加设备\n"
        "\n"
        "  10  信号中断 — 收到 SIGINT 或 SIGTERM\n"
        "\n"
        "脚本中使用:\n"
        "  bto connect office-213 || echo \"退出码: $?\"\n";
}

}  // anonymous namespace

void show_help(const std::string& topic) {
    if (topic.empty())       help_overview();
    else if (topic == "connect")  help_connect();
    else if (topic == "add")      help_add();
    else if (topic == "remove")   help_remove();
    else if (topic == "config")   help_config();
    else if (topic == "errors" || topic == "error" || topic == "exit-codes")
        help_errors();
    else if (topic == "list" || topic == "status") {
        std::cout << "bto " << topic << " — 无需额外参数，直接运行即可。\n"
                  << "运行 bto help 查看所有命令。\n";
    }
    else {
        std::cerr << "未知帮助主题: " << topic << "\n\n";
        help_overview();
    }
}

void show_version() {
    std::cout << "bto 1.1.0 (PeerLink P2P)\n"
              << "多会话隧道 · 自动重连 · 模糊匹配\n"
              << "配置: ~/.bto/config.toml\n";
}

} // namespace bto::cli
