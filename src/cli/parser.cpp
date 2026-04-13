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
        if (arg == "login") {
            cmd.name = "login";
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                cmd.target = args[++i];
            }
        }
        else if (arg == "logout") {
            cmd.name = "logout";
        }
        else if (arg == "whoami") {
            cmd.name = "whoami";
        }
        else if (arg == "device") {
            cmd.name = "device";
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                cmd.device_action = args[++i];
                if ((cmd.device_action == "create" || cmd.device_action == "install" ||
                     cmd.device_action == "remove") &&
                    i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                    cmd.target = args[++i];
                }
            }
        }
        else if (arg == "connect") {
            cmd.name = "connect";
            if (i + 1 < args.size() && !args[i+1].empty() && args[i+1][0] != '-')
                cmd.target = args[++i];
        }
        else if (arg == "upgrade") {
            cmd.name = "upgrade";
            if (i + 1 < args.size() && !args[i+1].empty() && args[i+1][0] != '-')
                cmd.target = args[++i];
        }
        else if (arg == "push") {
            cmd.name = "push";
            // bto push <peer> <local-file>
            if (i + 1 < args.size() && !args[i+1].empty() && args[i+1][0] != '-')
                cmd.target = args[++i];
            if (i + 1 < args.size() && !args[i+1].empty() && args[i+1][0] != '-')
                cmd.local_file = args[++i];
        }
        else if (arg == "list")   { cmd.name = "list"; }
        else if (arg == "ps")     { cmd.name = "ps"; }
        else if (arg == "close") {
            cmd.name = "close";
            if (i + 1 < args.size() && !args[i+1].empty() && args[i+1][0] != '-')
                cmd.target = args[++i];
        }
        else if (arg == "status") { cmd.name = "status"; }
        else if (arg == "config") { cmd.name = "config"; }
        else if (arg == "daemon") {
            cmd.name = "daemon";
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                cmd.daemon_action = args[++i];
            }
        }
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
                const auto parsed = std::stoi(args[++i]);
                if (parsed > 0 && parsed <= 65535) {
                    cmd.listen_port = static_cast<uint16_t>(parsed);
                    cmd.listen_port_explicit = true;
                } else {
                    std::cerr << "警告: --listen 端口超出范围，使用默认 2222\n";
                }
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
        else if (arg == "--artifact" && i + 1 < args.size()) {
            cmd.artifact_name = args[++i];
        }
        else if (arg == "--live-binary" && i + 1 < args.size()) {
            cmd.live_binary = args[++i];
        }
        else if (arg == "--activate-command" && i + 1 < args.size()) {
            cmd.activate_command = args[++i];
        }
        else if (arg == "--rollback-command" && i + 1 < args.size()) {
            cmd.rollback_command = args[++i];
        }
        else if (arg == "--health-command" && i + 1 < args.size()) {
            cmd.health_command = args[++i];
        }
        else if (arg == "--timeout-seconds" && i + 1 < args.size()) {
            try {
                const auto parsed = std::stoul(args[++i]);
                if (parsed > 0 && parsed <= 3600) {
                    cmd.timeout_seconds = static_cast<uint32_t>(parsed);
                } else {
                    std::cerr << "警告: --timeout-seconds 超出范围，使用默认 30\n";
                }
            } catch (...) {
                std::cerr << "警告: --timeout-seconds 格式错误，使用默认 30\n";
            }
        }
        else if (arg == "--remote-ssh-host" && i + 1 < args.size()) {
            cmd.remote_ssh_host = args[++i];
        }
        else if (arg == "--ssh" && i + 1 < args.size()) {
            cmd.ssh_spec = args[++i];
        }
        else if (arg == "--force-download") {
            cmd.force_download = true;
        }
        else if (arg == "--skip-remote-check") {
            cmd.skip_remote_version_check = true;
        }
        else if (arg == "--legacy-direct") {
            cmd.legacy_direct = true;
        }
        // 快捷方式: bto <host> = bto connect <host>（不与已识别的顶层命令冲突）
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
        "bto (Back-To-Office) — 托管优先的 P2P SSH 隧道客户端\n"
        "\n"
        "推荐路径（托管账号 + 设备）:\n"
        "  bto login                    登录托管账号（需设置 BTO_API_BASE）\n"
        "  bto whoami                   查看当前登录态\n"
        "  bto logout                   清除本地登录令牌\n"
        "  bto device list              列出托管设备\n"
        "  bto device create <name>    创建设备并获取 claim 码\n"
        "  bto device install <n> --ssh user@host   SSH 安装远端 agent（将接入）\n"
        "  bto device remove <name>     移除托管设备记录\n"
        "  bto connect <name>           连接远端（当前默认仍用 ~/.bto/config.toml；托管授权接入后同命令）\n"
        "  bto upgrade [<name>]         升级（当前行为见 bto help upgrade；托管授权将后续接入）\n"
        "  bto push <name> <file>       推送任意本地文件到远端并执行命令（见 bto help push）\n"
        "\n"
        "本会话管理:\n"
        "  bto list                     列出本地 config 中的设备（兼容）\n"
        "  bto ps / bto status          查看 peerlinkd / 配置摘要\n"
        "  bto close <peer>             关闭本地桥接\n"
        "  bto <peer>                   等同 connect（快捷方式）\n"
        "\n"
        "帮助:\n"
        "  bto help advanced            旧版自建 relay、add/daemon 等高级用法\n"
        "  bto help connect | device | errors\n"
        "\n"
        "环境:\n"
        "  BTO_API_BASE                 托管 API 根 URL，如 https://api.example.com\n"
        "  --version, -v / --help, -h\n";
}

void help_advanced() {
    std::cout <<
        "bto — 高级 / 自建 Relay（非默认新手路径）\n"
        "\n"
        "用法:\n"
        "  bto connect <peer> [ --did ... --relay ... --listen ... --legacy-direct ]\n"
        "  bto upgrade <peer> [制品与 SSH 预检等选项，见 bto help upgrade ]\n"
        "  bto add <name> [--did ...] [--user ...] [--key ...]\n"
        "  bto remove <name>\n"
        "  bto daemon <status|start|stop>\n"
        "  bto config\n"
        "  bto ping [peer]\n"
        "\n"
        "全局选项（节选）:\n"
        "  --did <did>          本机 DID（覆盖 ~/.bto/config.toml）\n"
        "  --relay <host:port>  Relay（覆盖配置）\n"
        "  --listen <port>      本地监听端口（默认 2222）\n"
        "  --legacy-direct      旧版单进程直连\n"
        "\n"
        "配置: ~/.bto/config.toml · 登录令牌: ~/.bto/auth.json\n";
}

void help_login() {
    std::cout <<
        "bto login [email] — 托管账号登录\n"
        "\n"
        "环境变量:\n"
        "  BTO_API_BASE           托管 API 根 URL（https://...，无末尾斜杠）\n"
        "  BTO_LOGIN_PASSWORD     密码（勿写入 shell 历史时可仅 export 当前会话）\n"
        "  BTO_LOGIN_EMAIL        若命令行未写 email，则用此邮箱\n"
        "\n"
        "示例:\n"
        "  export BTO_API_BASE=https://api.example.com\n"
        "  export BTO_LOGIN_PASSWORD='...'\n"
        "  bto login user@example.com\n"
        "\n"
        "本地开发若使用 http://，需同时设置 BTO_ALLOW_INSECURE_HTTP_API=1。\n"
        "契约: back-to-office/docs/openapi/hosted-api.yaml\n";
}

void help_device() {
    std::cout <<
        "bto device — 托管设备\n"
        "\n"
        "  bto device list\n"
        "  bto device create <display_name>\n"
        "  bto device install <display_name> --ssh user@host\n"
        "  bto device remove <display_name>\n"
        "\n"
        "需已登录（~/.bto/auth.json）。API 契约见 docs/openapi/hosted-api.yaml。\n";
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
        "  --listen <port>   优先请求 daemon 使用该本地端口\n"
        "  --did <did>       本机 DID（覆盖配置）\n"
        "  --relay <h:p>     Relay 服务器（覆盖配置）\n"
        "  --legacy-direct   显式使用旧版单进程模式\n"
        "\n"
        "多终端支持:\n"
        "  默认通过本机 peerlinkd 复用目标连接并分配本地监听端口。\n"
        "  可同时开多个终端连接同一个或不同远端设备。\n"
        "\n"
        "示例:\n"
        "  bto connect office-213\n"
        "  bto 213\n"
        "  bto connect office-215 --relay relay.example.com:9700\n"
        "  BTO_ALLOW_DAEMON_FALLBACK=1 bto connect office-213\n"
        "  bto connect office-213 --legacy-direct\n"
        "\n"
        "连接后:\n"
        "  bto 会自动拉起本地 SSH，无需再手工 ssh -p <port>\n"
        "\n"
        "长时间工作建议:\n"
        "  • 远端使用 tmux，SSH 断了可恢复: tmux attach\n"
        "  • SSH 配置 ServerAliveInterval 30 防空闲断开\n";
}

void help_upgrade() {
    std::cout <<
        "bto upgrade — 通过 PeerLink 推送二进制并触发远端升级\n"
        "\n"
        "用法:\n"
        "  bto upgrade <peer> [选项]\n"
        "\n"
        "版本与下载:\n"
        "  • 拉取阿里云 manifest（version / git_commit / 各制品 sha256）\n"
        "  • 若配置 peers.<name>.host 或 --remote-ssh-host：SSH 预检远端 live-binary\n"
        "    的 sha256；与 manifest 一致时打印命中提示，并继续统一的升级/健康验证路径\n"
        "  • 否则若本地缓存文件（临时目录 peerlink-upgrades/<制品>）与 manifest\n"
        "    一致则跳过从阿里云下载，仍会通过 P2P 推送\n"
        "  • --force-download 强制重新从阿里云下载（忽略本地缓存命中）\n"
        "  • --skip-remote-check 跳过 SSH 远端预检\n"
        "\n"
        "默认行为:\n"
        "  • 默认制品 p2p-tunnel-server，远端路径 /usr/local/bin/p2p-tunnel-server\n"
        "  • 传输完成后在远端执行 apply\n"
        "\n"
        "选项:\n"
        "  --artifact <name>          制品名（默认 p2p-tunnel-server）\n"
        "  --live-binary <path>       远端目标路径\n"
        "  --remote-ssh-host <host>   覆盖配置文件中的 host，用于 SSH 预检\n"
        "  --force-download           忽略本地缓存，强制从阿里云下载\n"
        "  --skip-remote-check        不对远端做 sha256sum 预检\n"
        "  --activate-command <cmd>   替换后二次激活命令\n"
        "  --rollback-command <cmd>   回滚后二次激活命令\n"
        "  --health-command <cmd>     健康检查命令\n"
        "  --timeout-seconds <n>      超时秒数（默认 30）\n"
        "  --did <did>                本机 DID 覆盖\n"
        "  --relay <host:port>        Relay 覆盖\n";
}

void help_push() {
    std::cout <<
        "bto push — 通过 P2P 推送任意本地文件到远端并执行命令\n"
        "\n"
        "用法:\n"
        "  bto push <peer> <local-file> [选项]\n"
        "\n"
        "说明:\n"
        "  跳过 manifest 流程，直接将本地文件通过 PeerLink P2P 传输到远端，\n"
        "  然后执行指定的激活命令。适用于推送任意制品（npm 包、脚本等）。\n"
        "\n"
        "选项:\n"
        "  --activate-command <cmd>   传输完成后在远端执行的安装命令\n"
        "  --health-command <cmd>     健康检查命令\n"
        "  --rollback-command <cmd>   回滚命令\n"
        "  --timeout-seconds <n>      命令超时秒数（默认 60）\n"
        "  --did <did>                本机 DID 覆盖\n"
        "  --relay <host:port>        Relay 覆盖\n"
        "\n"
        "示例:\n"
        "  # 推送 Claude Code npm 包到远端并安装\n"
        "  bto push office-213 ./claude-code-2.1.104.tgz \\\n"
        "    --activate-command 'npm install -g /var/lib/peerlink-upgrades/*/claude-code-2.1.104.tgz' \\\n"
        "    --health-command 'claude --version'\n"
        "\n"
        "  # 推送任意脚本\n"
        "  bto push office-215 ./deploy.sh \\\n"
        "    --activate-command 'chmod +x /var/lib/peerlink-upgrades/*/deploy.sh && /var/lib/peerlink-upgrades/*/deploy.sh'\n";
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
        "  relay = \"relay.example.com:9700\"   # Relay 服务器\n"
        "\n"
        "  [peers.office-213]             # 设备配置\n"
        "    did = \"office-213\"\n"
        "\n"
        "  [peers.office-215]\n"
        "    did = \"office-215\"\n";
}

void help_close() {
    std::cout <<
        "bto close — 关闭指定目标的本地桥接\n"
        "\n"
        "用法:\n"
        "  bto close <peer>\n"
        "\n"
        "说明:\n"
        "  关闭 peerlinkd 中对应目标的本地监听端口与桥接连接。\n";
}

void help_daemon() {
    std::cout <<
        "bto daemon — 管理本机 peerlinkd\n"
        "\n"
        "用法:\n"
        "  bto daemon status\n"
        "  bto daemon start\n"
        "  bto daemon stop\n";
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
    else if (topic == "advanced") help_advanced();
    else if (topic == "login")    help_login();
    else if (topic == "device")   help_device();
    else if (topic == "connect")  help_connect();
    else if (topic == "add")      help_add();
    else if (topic == "upgrade")  help_upgrade();
    else if (topic == "push")     help_push();
    else if (topic == "remove")   help_remove();
    else if (topic == "config")   help_config();
    else if (topic == "close")    help_close();
    else if (topic == "daemon")   help_daemon();
    else if (topic == "errors" || topic == "error" || topic == "exit-codes")
        help_errors();
    else if (topic == "list" || topic == "status" || topic == "ps") {
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
