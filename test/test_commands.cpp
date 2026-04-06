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
