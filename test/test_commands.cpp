/**
 * @file test_commands.cpp
 * @brief BTO 命令集成测试
 *
 * 通过运行 bto 二进制验证各命令的退出码和输出。
 * 使用隔离的 HOME 目录避免污染真实配置。
 *
 * 覆盖目标:
 *   - bto list / status / config / add / remove 的完整流程
 *   - 退出码一致性
 *   - 配置文件的创建和修改
 *   - 错误场景（缺少参数、未知命令、设备未找到）
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <array>
#include <string>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "daemon/common.hpp"

namespace fs = std::filesystem;

namespace {

/// 执行命令并捕获 stdout + 退出码
struct ExecResult {
    std::string output;
    int exit_code;
};

ExecResult exec_bto(const std::string& args, const std::string& home) {
    // 设置隔离的 HOME 环境
    std::string cmd = "HOME=" + home + " " + BTO_BINARY + " " + args + " 2>&1";

    ExecResult result;
    std::array<char, 4096> buffer;
    std::string output;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        return result;
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

    int status = pclose(pipe);
    result.output = output;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

/// RAII 临时 HOME 目录
class IsolatedHome {
public:
    IsolatedHome() {
        path_ = fs::temp_directory_path() / "bto_cmd_test" /
                ("h" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~IsolatedHome() { fs::remove_all(path_); }

    std::string home() const { return path_.string(); }
    std::string config_path() const {
        return (path_ / ".bto" / "config.toml").string();
    }

    std::string daemon_socket_path() const {
        return (path_ / ".peerlink" / "run" / "peerlinkd.sock").string();
    }

    void write_config(const std::string& content) {
        auto dir = path_ / ".bto";
        fs::create_directories(dir);
        std::ofstream f(dir / "config.toml");
        f << content;
    }

    std::string read_config() {
        std::ifstream f(config_path());
        return {std::istreambuf_iterator<char>(f),
                std::istreambuf_iterator<char>()};
    }

private:
    fs::path path_;
    static inline int counter_ = 0;
};

class FakeDaemonServer {
public:
    FakeDaemonServer(std::string socket_path, std::vector<bto::daemon::Json> responses)
        : socket_path_(std::move(socket_path))
        , responses_(std::move(responses)) {}

    void start() {
        fs::create_directories(fs::path(socket_path_).parent_path());
        ::unlink(socket_path_.c_str());

        server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_GE(server_fd_, 0);

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());
        ASSERT_EQ(::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        ASSERT_EQ(::listen(server_fd_, 8), 0);

        thread_ = std::thread([this]() { serve(); });
    }

    ~FakeDaemonServer() {
        if (server_fd_ >= 0) {
            ::shutdown(server_fd_, SHUT_RDWR);
            ::close(server_fd_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        ::unlink(socket_path_.c_str());
    }

    auto requests() const -> const std::vector<bto::daemon::Json>& {
        return requests_;
    }

private:
    void serve() {
        for (const auto& response : responses_) {
            int client_fd = ::accept(server_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                return;
            }

            std::string request;
            char ch = '\0';
            while (::read(client_fd, &ch, 1) == 1) {
                if (ch == '\n') {
                    break;
                }
                request.push_back(ch);
            }
            if (!request.empty()) {
                requests_.push_back(bto::daemon::Json::parse(request));
            }

            const auto payload = response.dump() + "\n";
            (void)::write(client_fd, payload.data(), payload.size());
            ::close(client_fd);
        }
    }

    std::string socket_path_;
    std::vector<bto::daemon::Json> responses_;
    std::vector<bto::daemon::Json> requests_;
    int server_fd_ = -1;
    std::thread thread_;
};

}  // namespace

// ═══════════════════════════════════════════════════════════════
//  help / version
// ═══════════════════════════════════════════════════════════════

TEST(Commands, Help) {
    IsolatedHome env;
    auto r = exec_bto("help", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("connect"), std::string::npos);
    EXPECT_NE(r.output.find("list"), std::string::npos);
}

TEST(Commands, HelpFlag) {
    IsolatedHome env;
    auto r = exec_bto("--help", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("bto"), std::string::npos);
}

TEST(Commands, Version) {
    IsolatedHome env;
    auto r = exec_bto("--version", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("1.1.0"), std::string::npos);
}

TEST(Commands, VersionCommand) {
    IsolatedHome env;
    auto r = exec_bto("version", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("bto"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  list
// ═══════════════════════════════════════════════════════════════

TEST(Commands, ListEmpty) {
    IsolatedHome env;
    auto r = exec_bto("list", env.home());

    EXPECT_EQ(r.exit_code, 0);
    // 无配置文件时应正常运行
}

TEST(Commands, ListWithPeers) {
    IsolatedHome env;
    env.write_config(R"(
did = "test"
relay = "host:9700"

[peers.server-a]
  did = "server-a"
  user = "admin"

[peers.server-b]
  did = "server-b"
)");

    auto r = exec_bto("list", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("server-a"), std::string::npos);
    EXPECT_NE(r.output.find("server-b"), std::string::npos);
    EXPECT_NE(r.output.find("admin"), std::string::npos);
    EXPECT_NE(r.output.find("2"), std::string::npos);  // "2 台"
}

TEST(Commands, ListShowsActiveUnconfiguredDaemonTargets) {
    IsolatedHome env;
    env.write_config(R"(
did = "test"
relay = "host:9700"

[peers.office-213]
  did = "office-213"
  user = "lhb"
)");

    FakeDaemonServer daemon(
        env.daemon_socket_path(),
        {
            bto::daemon::make_ok({{"connections", 2}, {"sessions", 0}}),
            bto::daemon::make_ok({{"connections", bto::daemon::Json::array({
                {
                    {"target_name", "office-213"},
                    {"target_did", "office-213"},
                    {"local_port", 2231},
                    {"state", "ready"},
                    {"active_sessions", 1},
                    {"live_tunnels", 1},
                },
                {
                    {"target_name", "124"},
                    {"target_did", "124"},
                    {"local_port", 2234},
                    {"state", "ready"},
                    {"active_sessions", 1},
                    {"live_tunnels", 1},
                },
            })}})
        });
    daemon.start();

    auto r = exec_bto("list", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("已配置设备 (1 台)"), std::string::npos);
    EXPECT_NE(r.output.find("office-213"), std::string::npos);
    EXPECT_NE(r.output.find("活动但未配置设备 (1 台)"), std::string::npos);
    EXPECT_NE(r.output.find("124"), std::string::npos);
}

TEST(Commands, ListDoesNotHideDifferentUnconfiguredSuffixTarget) {
    IsolatedHome env;
    env.write_config(R"(
did = "test"
relay = "host:9700"

[peers.office-124]
  did = "office-124"
  user = "lhb"
)");

    FakeDaemonServer daemon(
        env.daemon_socket_path(),
        {
            bto::daemon::make_ok({{"connections", 1}, {"sessions", 0}}),
            bto::daemon::make_ok({{"connections", bto::daemon::Json::array({
                {
                    {"target_name", "124"},
                    {"target_did", "124"},
                    {"local_port", 2234},
                    {"state", "ready"},
                    {"active_sessions", 1},
                    {"live_tunnels", 1},
                },
            })}})
        });
    daemon.start();

    auto r = exec_bto("list", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("office-124"), std::string::npos);
    EXPECT_NE(r.output.find("活动但未配置设备 (1 台)"), std::string::npos);
    EXPECT_NE(r.output.find("\n  124\n"), std::string::npos);
}

TEST(Commands, ListDefaultNoArgs) {
    // bto 无参数 → 默认 list
    IsolatedHome env;
    auto r = exec_bto("", env.home());

    EXPECT_EQ(r.exit_code, 0);
}

// ═══════════════════════════════════════════════════════════════
//  status
// ═══════════════════════════════════════════════════════════════

TEST(Commands, StatusEmpty) {
    IsolatedHome env;
    auto r = exec_bto("status", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("DID"), std::string::npos);
    EXPECT_NE(r.output.find("Relay"), std::string::npos);
}

TEST(Commands, StatusWithConfig) {
    IsolatedHome env;
    env.write_config(R"(
did = "my-laptop"
relay = "relay.bto.asia:9700"

[peers.s1]
  did = "s1"
)");

    auto r = exec_bto("status", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("my-laptop"), std::string::npos);
    EXPECT_NE(r.output.find("relay.bto.asia:9700"), std::string::npos);
    EXPECT_NE(r.output.find("1"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  config
// ═══════════════════════════════════════════════════════════════

TEST(Commands, ConfigNoFile) {
    IsolatedHome env;
    auto r = exec_bto("config", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("config.toml"), std::string::npos);
}

TEST(Commands, ConfigWithFile) {
    IsolatedHome env;
    env.write_config(R"(
did = "show-me"
relay = "relay:9700"
)");

    auto r = exec_bto("config", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("show-me"), std::string::npos);
    EXPECT_NE(r.output.find("relay:9700"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  add
// ═══════════════════════════════════════════════════════════════

TEST(Commands, AddBasic) {
    IsolatedHome env;
    auto r = exec_bto("add new-server", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("new-server"), std::string::npos);

    // 验证配置文件被创建
    auto content = env.read_config();
    EXPECT_NE(content.find("new-server"), std::string::npos);
}

TEST(Commands, AddWithAllOptions) {
    IsolatedHome env;
    env.write_config(R"(
did = "home-mac"
relay = "relay:9700"
)");
    auto r = exec_bto("add mybox --did custom-did --user root --key /tmp/key",
                       env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("mybox"), std::string::npos);
    EXPECT_NE(r.output.find("custom-did"), std::string::npos);
    EXPECT_NE(r.output.find("root"), std::string::npos);

    auto content = env.read_config();
    EXPECT_NE(content.find("[peers.mybox]"), std::string::npos);
    EXPECT_NE(content.find("custom-did"), std::string::npos);
    EXPECT_NE(content.find("root"), std::string::npos);
    EXPECT_NE(content.find("/tmp/key"), std::string::npos);
    EXPECT_NE(content.find("did = \"home-mac\""), std::string::npos);
}

TEST(Commands, AddNoName) {
    IsolatedHome env;
    auto r = exec_bto("add", env.home());

    EXPECT_EQ(r.exit_code, 1);  // USAGE error
}

TEST(Commands, AddPreservesExisting) {
    IsolatedHome env;
    env.write_config(R"(
did = "laptop"
relay = "host:9700"

[peers.existing]
  did = "existing-did"
)");

    exec_bto("add new-one", env.home());

    auto content = env.read_config();
    EXPECT_NE(content.find("existing"), std::string::npos);
    EXPECT_NE(content.find("new-one"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  remove
// ═══════════════════════════════════════════════════════════════

TEST(Commands, RemoveExisting) {
    IsolatedHome env;
    env.write_config(R"(
did = "test"
relay = "host:9700"

[peers.to-remove]
  did = "to-remove"
  user = "nobody"

[peers.to-keep]
  did = "to-keep"
)");

    auto r = exec_bto("remove to-remove", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("to-remove"), std::string::npos);

    auto content = env.read_config();
    EXPECT_EQ(content.find("to-remove"), std::string::npos);
    EXPECT_NE(content.find("to-keep"), std::string::npos);
}

TEST(Commands, RemoveNonExistent) {
    IsolatedHome env;
    env.write_config(R"(
did = "test"
[peers.real]
  did = "real"
)");

    auto r = exec_bto("remove ghost", env.home());

    EXPECT_EQ(r.exit_code, 4);  // PEER_NOT_FOUND
}

TEST(Commands, RemoveNoName) {
    IsolatedHome env;
    auto r = exec_bto("remove", env.home());

    EXPECT_EQ(r.exit_code, 1);  // USAGE error
}

// ═══════════════════════════════════════════════════════════════
//  connect — 只测早期退出路径（不触发 P2P 初始化）
// ═══════════════════════════════════════════════════════════════

TEST(Commands, ConnectNoTarget) {
    IsolatedHome env;
    env.write_config("did = \"test\"\nrelay = \"host:9700\"");

    auto r = exec_bto("connect", env.home());

    EXPECT_EQ(r.exit_code, 1);  // USAGE
}

TEST(Commands, ConnectNoRelay) {
    IsolatedHome env;
    // 无 relay 配置
    env.write_config("did = \"test\"");

    auto r = exec_bto("connect some-peer", env.home());

    EXPECT_EQ(r.exit_code, 2);  // CONFIG
}

TEST(Commands, ConnectPrefersExactActiveTargetOverConfigSuffixMatch) {
    IsolatedHome env;
    env.write_config(R"(
did = "test"
relay = "host:9700"

[peers.office-124]
  did = "office-124"
  user = "lhb"
)");

    FakeDaemonServer daemon(
        env.daemon_socket_path(),
        {
            bto::daemon::make_ok({{"connections", 1}, {"sessions", 0}}),
            bto::daemon::make_ok({{"connections", bto::daemon::Json::array({
                {
                    {"target_name", "124"},
                    {"target_did", "124"},
                    {"local_port", 2234},
                    {"state", "ready"},
                    {"active_sessions", 1},
                    {"live_tunnels", 1},
                },
            })}}),
            bto::daemon::make_error("BTO_DAEMON_CONNECT_FAILED", "expected failure")
        });
    daemon.start();

    auto r = exec_bto("connect 124", env.home());

    EXPECT_EQ(r.exit_code, 3);
    ASSERT_GE(daemon.requests().size(), 3u);
    EXPECT_EQ(daemon.requests()[2].value("action", ""), "create_session");
    EXPECT_EQ(daemon.requests()[2].value("target_name", ""), "124");
    EXPECT_EQ(daemon.requests()[2].value("target_did", ""), "124");
}

TEST(Commands, UpgradeNoTarget) {
    IsolatedHome env;
    env.write_config("did = \"test\"\nrelay = \"host:9700\"");

    auto r = exec_bto("upgrade", env.home());

    EXPECT_EQ(r.exit_code, 1);
}

TEST(Commands, UpgradeNoRelay) {
    IsolatedHome env;
    env.write_config("did = \"test\"");

    auto r = exec_bto("upgrade office-213", env.home());

    EXPECT_EQ(r.exit_code, 2);
}

TEST(Commands, PsNoDaemon) {
    IsolatedHome env;
    auto r = exec_bto("ps", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("peerlinkd"), std::string::npos);
}

TEST(Commands, DaemonStatusNoDaemon) {
    IsolatedHome env;
    auto r = exec_bto("daemon status", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("peerlinkd"), std::string::npos);
}

TEST(Commands, CloseNoDaemon) {
    IsolatedHome env;
    auto r = exec_bto("close office-213", env.home());

    EXPECT_EQ(r.exit_code, 3);
    EXPECT_NE(r.output.find("peerlinkd"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  未知命令
// ═══════════════════════════════════════════════════════════════

TEST(Commands, UnknownCommand) {
    IsolatedHome env;
    auto r = exec_bto("nonexistent-command", env.home());

    // 快捷方式会将它当作 connect target
    // 但如果无 relay 配置，会返回 CONFIG 错误
    EXPECT_NE(r.exit_code, 0);
}

// ═══════════════════════════════════════════════════════════════
//  help 子主题
// ═══════════════════════════════════════════════════════════════

TEST(Commands, HelpConnect) {
    IsolatedHome env;
    auto r = exec_bto("help connect", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("connect"), std::string::npos);
    EXPECT_NE(r.output.find("--listen"), std::string::npos);
}

TEST(Commands, HelpUpgrade) {
    IsolatedHome env;
    auto r = exec_bto("help upgrade", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("upgrade"), std::string::npos);
    EXPECT_NE(r.output.find("--artifact"), std::string::npos);
}

TEST(Commands, HelpErrors) {
    IsolatedHome env;
    auto r = exec_bto("help errors", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("退出码"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  ping — 不可达 relay
// ═══════════════════════════════════════════════════════════════

TEST(Commands, PingNoRelay) {
    IsolatedHome env;
    // 无 relay 配置
    auto r = exec_bto("ping", env.home());

    EXPECT_EQ(r.exit_code, 2);  // CONFIG
}

TEST(Commands, PingUnreachable) {
    IsolatedHome env;
    // 用 127.0.0.1 的未监听端口（连接会立即被拒绝）
    env.write_config("did = \"test\"\nrelay = \"127.0.0.1:19999\"");

    auto r = exec_bto("ping", env.home());

    EXPECT_EQ(r.exit_code, 3);  // NETWORK
}

TEST(Commands, PingWithRelayFlag) {
    IsolatedHome env;
    auto r = exec_bto("ping --relay 127.0.0.1:19998", env.home());

    EXPECT_EQ(r.exit_code, 3);  // NETWORK
}

// NOTE: connect 实际连接路径（peer resolution, relay override, did override）
// 需要 P2P 基础设施，P2PClient::initialize() 会阻塞。
// 这些逻辑通过 test_parser + test_config 的单元测试间接覆盖。

// ═══════════════════════════════════════════════════════════════
//  status — 验证提示信息
// ═══════════════════════════════════════════════════════════════

TEST(Commands, StatusShowsHintWhenEmpty) {
    IsolatedHome env;
    auto r = exec_bto("status", env.home());

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("DID"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  add — DID 默认与 name 相同
// ═══════════════════════════════════════════════════════════════

TEST(Commands, AddDefaultDid) {
    IsolatedHome env;
    auto r = exec_bto("add myhost", env.home());

    EXPECT_EQ(r.exit_code, 0);
    // DID 应与 name 相同
    auto content = env.read_config();
    EXPECT_NE(content.find("[peers.myhost]"), std::string::npos);
    EXPECT_NE(content.find("\"myhost\""), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  add + remove 全流程
// ═══════════════════════════════════════════════════════════════

TEST(Commands, AddThenListThenRemove) {
    IsolatedHome env;

    // Step 1: Add
    auto r1 = exec_bto("add test-peer --did test-did --user tester", env.home());
    EXPECT_EQ(r1.exit_code, 0);

    // Step 2: List — 应显示 test-peer
    auto r2 = exec_bto("list", env.home());
    EXPECT_EQ(r2.exit_code, 0);
    EXPECT_NE(r2.output.find("test-peer"), std::string::npos);
    EXPECT_NE(r2.output.find("tester"), std::string::npos);

    // Step 3: Remove
    auto r3 = exec_bto("remove test-peer", env.home());
    EXPECT_EQ(r3.exit_code, 0);

    // Step 4: List — 应不再显示
    auto r4 = exec_bto("list", env.home());
    EXPECT_EQ(r4.exit_code, 0);
    EXPECT_EQ(r4.output.find("test-peer"), std::string::npos);
}
