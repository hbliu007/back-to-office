/**
 * @file test_parser.cpp
 * @brief CLI 解析器全覆盖测试
 *
 * 覆盖目标:
 *   - parse_arguments(): 所有命令、参数组合、边界情况、快捷方式
 *   - show_help(): 各主题分支 + 未知主题
 *   - show_version(): 版本输出
 */

#include <gtest/gtest.h>
#include "cli/parser.hpp"
#include <sstream>
#include <vector>
#include <string>

namespace {

/// 构造 argc/argv 的辅助类，管理 C 字符串生命周期
class Args {
public:
    explicit Args(std::initializer_list<const char*> list) {
        for (auto s : list) storage_.emplace_back(s);
        for (auto& s : storage_) ptrs_.push_back(s.data());
    }

    int argc() const { return static_cast<int>(ptrs_.size()); }
    char** argv() { return ptrs_.data(); }

private:
    std::vector<std::string> storage_;
    std::vector<char*> ptrs_;
};

/// 捕获 stdout 的 RAII 辅助
class CaptureStdout {
public:
    CaptureStdout() { testing::internal::CaptureStdout(); }
    ~CaptureStdout() {
        if (!captured_) testing::internal::GetCapturedStdout();
    }
    std::string get() {
        captured_ = true;
        return testing::internal::GetCapturedStdout();
    }
private:
    bool captured_ = false;
};

class CaptureStderr {
public:
    CaptureStderr() { testing::internal::CaptureStderr(); }
    ~CaptureStderr() {
        if (!captured_) testing::internal::GetCapturedStderr();
    }
    std::string get() {
        captured_ = true;
        return testing::internal::GetCapturedStderr();
    }
private:
    bool captured_ = false;
};

}  // namespace

// ═══════════════════════════════════════════════════════════════
//  parse_arguments — 无参数
// ═══════════════════════════════════════════════════════════════

TEST(Parser, NoArgs_DefaultsToList) {
    Args a{"bto"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "list");
    EXPECT_TRUE(cmd.target.empty());
    EXPECT_FALSE(cmd.help);
    EXPECT_FALSE(cmd.version);
    EXPECT_EQ(cmd.listen_port, 2222);
}

// ═══════════════════════════════════════════════════════════════
//  parse_arguments — help / version 标志
// ═══════════════════════════════════════════════════════════════

TEST(Parser, HelpLongFlag) {
    Args a{"bto", "--help"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_TRUE(cmd.help);
}

TEST(Parser, HelpShortFlag) {
    Args a{"bto", "-h"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_TRUE(cmd.help);
}

TEST(Parser, HelpCommand) {
    Args a{"bto", "help"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_TRUE(cmd.help);
    EXPECT_TRUE(cmd.help_topic.empty());
}

TEST(Parser, HelpCommandWithTopic) {
    Args a{"bto", "help", "connect"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_TRUE(cmd.help);
    EXPECT_EQ(cmd.help_topic, "connect");
}

TEST(Parser, VersionLongFlag) {
    Args a{"bto", "--version"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_TRUE(cmd.version);
}

TEST(Parser, VersionShortFlag) {
    Args a{"bto", "-v"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_TRUE(cmd.version);
}

TEST(Parser, VersionCommand) {
    Args a{"bto", "version"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_TRUE(cmd.version);
}

// ═══════════════════════════════════════════════════════════════
//  parse_arguments — connect 命令
// ═══════════════════════════════════════════════════════════════

TEST(Parser, Connect_ExplicitCommand) {
    Args a{"bto", "connect", "office-213"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "office-213");
}

TEST(Parser, Connect_Shortcut) {
    Args a{"bto", "office-213"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "office-213");
}

TEST(Parser, Connect_WithListenPort) {
    Args a{"bto", "connect", "office-213", "--listen", "3333"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "office-213");
    EXPECT_EQ(cmd.listen_port, 3333);
    EXPECT_TRUE(cmd.listen_port_explicit);
}

TEST(Parser, Connect_WithInvalidListenPortFallsBack) {
    Args a{"bto", "connect", "office-213", "--listen", "70000"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.listen_port, 2222);
    EXPECT_FALSE(cmd.listen_port_explicit);
}

TEST(Parser, Connect_WithDid) {
    Args a{"bto", "connect", "myhost", "--did", "custom-did"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "myhost");
    EXPECT_EQ(cmd.did, "custom-did");
}

TEST(Parser, Connect_WithRelay) {
    Args a{"bto", "connect", "myhost", "--relay", "10.0.0.1:9700"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.relay, "10.0.0.1:9700");
}

TEST(Parser, Connect_WithoutTarget) {
    Args a{"bto", "connect"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_TRUE(cmd.target.empty());
}

TEST(Parser, Connect_TargetSkipsFlags) {
    // --listen 紧跟 connect，不应被当作 target
    Args a{"bto", "connect", "--listen", "4444"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_TRUE(cmd.target.empty());
    EXPECT_EQ(cmd.listen_port, 4444);
}

TEST(Parser, Connect_AllOptions) {
    Args a{"bto", "connect", "peer1",
           "--did", "my-did",
           "--relay", "1.2.3.4:9700",
           "--listen", "5555"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "peer1");
    EXPECT_EQ(cmd.did, "my-did");
    EXPECT_EQ(cmd.relay, "1.2.3.4:9700");
    EXPECT_EQ(cmd.listen_port, 5555);
    EXPECT_TRUE(cmd.listen_port_explicit);
}

TEST(Parser, Connect_LegacyDirect) {
    Args a{"bto", "connect", "peer1", "--legacy-direct"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "peer1");
    EXPECT_TRUE(cmd.legacy_direct);
}

TEST(Parser, UpgradeCommand) {
    Args a{"bto", "upgrade", "office-213"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "upgrade");
    EXPECT_EQ(cmd.target, "office-213");
}

TEST(Parser, UpgradeAllOptions) {
    Args a{"bto", "upgrade", "office-213",
           "--artifact", "p2p-tunnel-server",
           "--live-binary", "/usr/local/bin/p2p-tunnel-server",
           "--activate-command", "systemctl restart peerlink-tunnel",
           "--rollback-command", "systemctl restart peerlink-tunnel",
           "--health-command", "systemctl is-active peerlink-tunnel",
           "--timeout-seconds", "45"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "upgrade");
    EXPECT_EQ(cmd.artifact_name, "p2p-tunnel-server");
    EXPECT_EQ(cmd.live_binary, "/usr/local/bin/p2p-tunnel-server");
    EXPECT_EQ(cmd.activate_command, "systemctl restart peerlink-tunnel");
    EXPECT_EQ(cmd.rollback_command, "systemctl restart peerlink-tunnel");
    EXPECT_EQ(cmd.health_command, "systemctl is-active peerlink-tunnel");
    EXPECT_EQ(cmd.timeout_seconds, 45u);
}

TEST(Parser, UpgradeInvalidTimeoutFallsBack) {
    Args a{"bto", "upgrade", "office-213", "--timeout-seconds", "0"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.timeout_seconds, 30u);
}

// ═══════════════════════════════════════════════════════════════
//  parse_arguments — list / status / config
// ═══════════════════════════════════════════════════════════════

TEST(Parser, ListCommand) {
    Args a{"bto", "list"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "list");
}

TEST(Parser, StatusCommand) {
    Args a{"bto", "status"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "status");
}

TEST(Parser, ConfigCommand) {
    Args a{"bto", "config"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "config");
}

// ═══════════════════════════════════════════════════════════════
//  parse_arguments — add 命令
// ═══════════════════════════════════════════════════════════════

TEST(Parser, Add_BasicName) {
    Args a{"bto", "add", "office-215"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "add");
    EXPECT_EQ(cmd.target, "office-215");
}

TEST(Parser, Add_WithDid) {
    Args a{"bto", "add", "215", "--did", "office-215"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "add");
    EXPECT_EQ(cmd.target, "215");
    EXPECT_EQ(cmd.did, "office-215");
}

TEST(Parser, Add_WithUser) {
    Args a{"bto", "add", "server", "--user", "root"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "add");
    EXPECT_EQ(cmd.target, "server");
    EXPECT_EQ(cmd.user, "root");
}

TEST(Parser, Add_WithKey) {
    Args a{"bto", "add", "server", "--key", "/home/me/.ssh/id_rsa"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "add");
    EXPECT_EQ(cmd.key, "/home/me/.ssh/id_rsa");
}

TEST(Parser, Add_AllOptions) {
    Args a{"bto", "add", "mybox",
           "--did", "my-did-123",
           "--user", "admin",
           "--key", "/tmp/key"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "add");
    EXPECT_EQ(cmd.target, "mybox");
    EXPECT_EQ(cmd.did, "my-did-123");
    EXPECT_EQ(cmd.user, "admin");
    EXPECT_EQ(cmd.key, "/tmp/key");
}

// ═══════════════════════════════════════════════════════════════
//  parse_arguments — remove 命令
// ═══════════════════════════════════════════════════════════════

TEST(Parser, Remove_WithTarget) {
    Args a{"bto", "remove", "old-server"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "remove");
    EXPECT_EQ(cmd.target, "old-server");
}

TEST(Parser, Remove_NoTarget) {
    Args a{"bto", "remove"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "remove");
    // remove 总是吃下一个参数（即使为空），看实际行为
}

// ═══════════════════════════════════════════════════════════════
//  parse_arguments — ping 命令
// ═══════════════════════════════════════════════════════════════

TEST(Parser, Ping_NoTarget) {
    Args a{"bto", "ping"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "ping");
    EXPECT_TRUE(cmd.target.empty());
}

TEST(Parser, Ping_WithTarget) {
    Args a{"bto", "ping", "office-213"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "ping");
    EXPECT_EQ(cmd.target, "office-213");
}

TEST(Parser, Ping_TargetSkipsFlags) {
    Args a{"bto", "ping", "--relay", "1.2.3.4:9700"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "ping");
    EXPECT_TRUE(cmd.target.empty());
    EXPECT_EQ(cmd.relay, "1.2.3.4:9700");
}

TEST(Parser, PsCommand) {
    Args a{"bto", "ps"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "ps");
}

TEST(Parser, CloseCommand) {
    Args a{"bto", "close", "office-213"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "close");
    EXPECT_EQ(cmd.target, "office-213");
}

TEST(Parser, DaemonCommandWithAction) {
    Args a{"bto", "daemon", "start"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "daemon");
    EXPECT_EQ(cmd.daemon_action, "start");
}

// ═══════════════════════════════════════════════════════════════
//  parse_arguments — 快捷方式与边界情况
// ═══════════════════════════════════════════════════════════════

TEST(Parser, Shortcut_BareHost) {
    Args a{"bto", "my-server"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "my-server");
}

TEST(Parser, Shortcut_NumericTarget) {
    Args a{"bto", "213"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "213");
}

TEST(Parser, Shortcut_WithOptions) {
    Args a{"bto", "213", "--listen", "4000"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.name, "connect");
    EXPECT_EQ(cmd.target, "213");
    EXPECT_EQ(cmd.listen_port, 4000);
}

TEST(Parser, DefaultListenPort) {
    Args a{"bto", "connect", "peer"};
    auto cmd = bto::cli::parse_arguments(a.argc(), a.argv());

    EXPECT_EQ(cmd.listen_port, 2222);
    EXPECT_FALSE(cmd.listen_port_explicit);
}

TEST(Parser, CommandDefaultFields) {
    bto::cli::Command cmd;
    EXPECT_TRUE(cmd.name.empty());
    EXPECT_TRUE(cmd.target.empty());
    EXPECT_TRUE(cmd.did.empty());
    EXPECT_TRUE(cmd.relay.empty());
    EXPECT_TRUE(cmd.help_topic.empty());
    EXPECT_TRUE(cmd.user.empty());
    EXPECT_TRUE(cmd.key.empty());
    EXPECT_TRUE(cmd.daemon_action.empty());
    EXPECT_EQ(cmd.listen_port, 2222);
    EXPECT_FALSE(cmd.version);
    EXPECT_FALSE(cmd.help);
    EXPECT_FALSE(cmd.legacy_direct);
    EXPECT_FALSE(cmd.listen_port_explicit);
}

// ═══════════════════════════════════════════════════════════════
//  show_help — 各主题分支
// ═══════════════════════════════════════════════════════════════

TEST(ShowHelp, Overview) {
    CaptureStdout cap;
    bto::cli::show_help();
    auto out = cap.get();

    EXPECT_NE(out.find("bto"), std::string::npos);
    EXPECT_NE(out.find("connect"), std::string::npos);
    EXPECT_NE(out.find("list"), std::string::npos);
    EXPECT_NE(out.find("add"), std::string::npos);
    EXPECT_NE(out.find("remove"), std::string::npos);
    EXPECT_NE(out.find("ping"), std::string::npos);
    EXPECT_NE(out.find("--did"), std::string::npos);
    EXPECT_NE(out.find("--relay"), std::string::npos);
}

TEST(ShowHelp, Connect) {
    CaptureStdout cap;
    bto::cli::show_help("connect");
    auto out = cap.get();

    EXPECT_NE(out.find("connect"), std::string::npos);
    EXPECT_NE(out.find("<peer>"), std::string::npos);
    EXPECT_NE(out.find("--listen"), std::string::npos);
    EXPECT_NE(out.find("ssh"), std::string::npos);
}

TEST(ShowHelp, Add) {
    CaptureStdout cap;
    bto::cli::show_help("add");
    auto out = cap.get();

    EXPECT_NE(out.find("add"), std::string::npos);
    EXPECT_NE(out.find("--did"), std::string::npos);
}

TEST(ShowHelp, Remove) {
    CaptureStdout cap;
    bto::cli::show_help("remove");
    auto out = cap.get();

    EXPECT_NE(out.find("remove"), std::string::npos);
}

TEST(ShowHelp, Config) {
    CaptureStdout cap;
    bto::cli::show_help("config");
    auto out = cap.get();

    EXPECT_NE(out.find("config"), std::string::npos);
    EXPECT_NE(out.find("config.toml"), std::string::npos);
}

TEST(ShowHelp, Errors) {
    CaptureStdout cap;
    bto::cli::show_help("errors");
    auto out = cap.get();

    EXPECT_NE(out.find("0"), std::string::npos);
    EXPECT_NE(out.find("1"), std::string::npos);
    EXPECT_NE(out.find("2"), std::string::npos);
    EXPECT_NE(out.find("3"), std::string::npos);
    EXPECT_NE(out.find("SIGINT"), std::string::npos);
}

TEST(ShowHelp, ErrorAlias) {
    CaptureStdout cap;
    bto::cli::show_help("error");
    auto out = cap.get();

    EXPECT_NE(out.find("退出码"), std::string::npos);
}

TEST(ShowHelp, ExitCodesAlias) {
    CaptureStdout cap;
    bto::cli::show_help("exit-codes");
    auto out = cap.get();

    EXPECT_NE(out.find("退出码"), std::string::npos);
}

TEST(ShowHelp, ListTopic) {
    CaptureStdout cap;
    bto::cli::show_help("list");
    auto out = cap.get();

    EXPECT_NE(out.find("list"), std::string::npos);
}

TEST(ShowHelp, StatusTopic) {
    CaptureStdout cap;
    bto::cli::show_help("status");
    auto out = cap.get();

    EXPECT_NE(out.find("status"), std::string::npos);
}

TEST(ShowHelp, UnknownTopic) {
    CaptureStderr cerr_cap;
    CaptureStdout cout_cap;
    bto::cli::show_help("nonexistent");
    auto err = cerr_cap.get();
    auto out = cout_cap.get();

    EXPECT_NE(err.find("nonexistent"), std::string::npos);
    // 应回退到 overview
    EXPECT_NE(out.find("connect"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  show_version
// ═══════════════════════════════════════════════════════════════

TEST(ShowVersion, ContainsVersionAndBrand) {
    CaptureStdout cap;
    bto::cli::show_version();
    auto out = cap.get();

    EXPECT_NE(out.find("bto"), std::string::npos);
    EXPECT_NE(out.find("1.1.0"), std::string::npos);
    EXPECT_NE(out.find("PeerLink"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
//  ExitCode 常量
// ═══════════════════════════════════════════════════════════════

TEST(ExitCode, Values) {
    EXPECT_EQ(bto::cli::ExitCode::OK, 0);
    EXPECT_EQ(bto::cli::ExitCode::USAGE, 1);
    EXPECT_EQ(bto::cli::ExitCode::CONFIG, 2);
    EXPECT_EQ(bto::cli::ExitCode::NETWORK, 3);
    EXPECT_EQ(bto::cli::ExitCode::PEER_NOT_FOUND, 4);
    EXPECT_EQ(bto::cli::ExitCode::INTERRUPTED, 10);
}
