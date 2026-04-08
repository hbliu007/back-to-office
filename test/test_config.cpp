/**
 * @file test_config.cpp
 * @brief Config 模块全覆盖测试
 *
 * 覆盖目标:
 *   - Config::load(): TOML 解析、section 处理、v0 兼容、异常输入
 *   - Config::save(): 序列化、可选字段、往返一致性
 *   - Config::resolve_peer(): 精确匹配、前缀/后缀模糊、歧义、无匹配
 *   - relay_host() / relay_port(): 解析、默认值、异常格式
 *   - default_config_path(): HOME 环境变量处理
 */

#include <gtest/gtest.h>
#include "config/config.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;

namespace {

/// RAII 临时目录，析构时自动删除
class TmpDir {
public:
    TmpDir() {
        auto base = fs::temp_directory_path() / "bto_test";
        fs::create_directories(base);
        path_ = base / ("t" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TmpDir() { fs::remove_all(path_); }

    fs::path path() const { return path_; }
    std::string file(const std::string& name) const {
        return (path_ / name).string();
    }

    /// 写入文件并返回路径
    std::string write(const std::string& name, const std::string& content) {
        auto p = path_ / name;
        std::ofstream f(p);
        f << content;
        return p.string();
    }

private:
    fs::path path_;
    static inline int counter_ = 0;
};

/// RAII 环境变量覆盖
class EnvOverride {
public:
    EnvOverride(const char* name, const char* value)
        : name_(name) {
        auto old = std::getenv(name);
        if (old) old_value_ = old;
        setenv(name, value, 1);
    }
    ~EnvOverride() {
        if (old_value_) setenv(name_, old_value_->c_str(), 1);
        else unsetenv(name_);
    }
private:
    const char* name_;
    std::optional<std::string> old_value_;
};

}  // namespace

// ═══════════════════════════════════════════════════════════════
//  Config::load — 基本加载
// ═══════════════════════════════════════════════════════════════

TEST(ConfigLoad, BasicConfig) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
did = "home-mac"
relay = "relay.bto.asia:9700"

[peers.office-213]
  did = "office-213"
  user = "user"
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());

    EXPECT_EQ(cfg->did, "home-mac");
    EXPECT_EQ(cfg->relay, "relay.bto.asia:9700");
    ASSERT_EQ(cfg->peers.size(), 1);
    EXPECT_EQ(cfg->peers.at("office-213").did, "office-213");
    EXPECT_EQ(cfg->peers.at("office-213").user, "user");
}

TEST(ConfigLoad, MultiplePeers) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
did = "laptop"
relay = "10.0.0.1:9700"

[peers.server-a]
  did = "server-a-did"
  user = "root"
  key = "/root/.ssh/id_ed25519"
  port = 2222

[peers.server-b]
  did = "server-b-did"
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    ASSERT_EQ(cfg->peers.size(), 2);

    auto& a = cfg->peers.at("server-a");
    EXPECT_EQ(a.did, "server-a-did");
    EXPECT_EQ(a.user, "root");
    EXPECT_EQ(a.key, "/root/.ssh/id_ed25519");
    EXPECT_EQ(a.port, 2222);

    auto& b = cfg->peers.at("server-b");
    EXPECT_EQ(b.did, "server-b-did");
    EXPECT_TRUE(b.user.empty());
    EXPECT_TRUE(b.key.empty());
    EXPECT_EQ(b.port, 22);  // 默认
}

TEST(ConfigLoad, V0HostsCompatibility) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
did = "old-client"
relay = "1.2.3.4:9700"

[hosts.legacy-server]
  did = "legacy-did"
  user = "admin"
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    ASSERT_EQ(cfg->peers.size(), 1);
    EXPECT_EQ(cfg->peers.at("legacy-server").did, "legacy-did");
    EXPECT_EQ(cfg->peers.at("legacy-server").user, "admin");
}

TEST(ConfigLoad, IdentityAlias) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
identity = "my-identity"
relay = "1.2.3.4:9700"
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->did, "my-identity");
}

TEST(ConfigLoad, EmptyFile) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", "");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_TRUE(cfg->did.empty());
    EXPECT_TRUE(cfg->relay.empty());
    EXPECT_TRUE(cfg->peers.empty());
}

TEST(ConfigLoad, CommentsAndBlankLines) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
# 这是注释
did = "test"

# 另一个注释
relay = "host:9700"

)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->did, "test");
    EXPECT_EQ(cfg->relay, "host:9700");
}

TEST(ConfigLoad, MalformedLines) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
did = "ok"
this-is-not-valid
relay = "host:9700"
no-equals-sign
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->did, "ok");
    EXPECT_EQ(cfg->relay, "host:9700");
}

TEST(ConfigLoad, MalformedSection) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
did = "test"
[incomplete-section
relay = "host:9700"
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->did, "test");
    // relay 在 malformed section 之后，应作为 peer 字段被忽略或作为全局字段
}

TEST(ConfigLoad, InvalidPort) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
did = "test"

[peers.bad-port]
  did = "bad-port"
  port = not-a-number
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->peers.at("bad-port").port, 22);  // 默认值
}

TEST(ConfigLoad, NonExistentFile) {
    auto cfg = bto::config::Config::load("/nonexistent/path/config.toml");
    EXPECT_FALSE(cfg.has_value());
}

TEST(ConfigLoad, UnknownSection) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
did = "test"

[unknown.section]
  foo = "bar"

[peers.real]
  did = "real-did"
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->peers.size(), 1);
    EXPECT_EQ(cfg->peers.at("real").did, "real-did");
}

TEST(ConfigLoad, WhitespaceAroundValues) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
  did   =   "  spaced  "
  relay  =  "  host:9700  "
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->did, "  spaced  ");  // 引号内空格保留
    EXPECT_EQ(cfg->relay, "  host:9700  ");
}

TEST(ConfigLoad, UnquotedValues) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
did = bare-value
relay = host:9700
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->did, "bare-value");
    EXPECT_EQ(cfg->relay, "host:9700");
}

TEST(ConfigLoad, PeerDefaultDid) {
    TmpDir tmp;
    auto path = tmp.write("config.toml", R"(
[peers.myhost]
  user = "me"
)");

    auto cfg = bto::config::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    // DID 默认与 peer name 相同
    EXPECT_EQ(cfg->peers.at("myhost").did, "myhost");
    EXPECT_EQ(cfg->peers.at("myhost").user, "me");
}

// ═══════════════════════════════════════════════════════════════
//  Config::save — 序列化
// ═══════════════════════════════════════════════════════════════

TEST(ConfigSave, BasicSave) {
    TmpDir tmp;
    auto path = tmp.file("output.toml");

    bto::config::Config cfg;
    cfg.did = "my-laptop";
    cfg.relay = "1.2.3.4:9700";

    ASSERT_TRUE(cfg.save(path));

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("my-laptop"), std::string::npos);
    EXPECT_NE(content.find("1.2.3.4:9700"), std::string::npos);
}

TEST(ConfigSave, WithPeers) {
    TmpDir tmp;
    auto path = tmp.file("output.toml");

    bto::config::Config cfg;
    cfg.did = "laptop";
    cfg.relay = "relay:9700";

    bto::config::PeerConfig peer;
    peer.did = "server-1";
    peer.user = "admin";
    peer.key = "/tmp/key";
    peer.port = 2222;
    cfg.peers["my-server"] = peer;

    ASSERT_TRUE(cfg.save(path));

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("[peers.my-server]"), std::string::npos);
    EXPECT_NE(content.find("server-1"), std::string::npos);
    EXPECT_NE(content.find("admin"), std::string::npos);
    EXPECT_NE(content.find("/tmp/key"), std::string::npos);
    EXPECT_NE(content.find("2222"), std::string::npos);
}

TEST(ConfigSave, OmitsDefaultPort) {
    TmpDir tmp;
    auto path = tmp.file("output.toml");

    bto::config::Config cfg;
    cfg.did = "test";
    bto::config::PeerConfig peer;
    peer.did = "p1";
    peer.port = 22;  // 默认值不应写入
    cfg.peers["p1"] = peer;

    ASSERT_TRUE(cfg.save(path));

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    EXPECT_EQ(content.find("port"), std::string::npos);
}

TEST(ConfigSave, OmitsEmptyOptionalFields) {
    TmpDir tmp;
    auto path = tmp.file("output.toml");

    bto::config::Config cfg;
    cfg.did = "test";
    bto::config::PeerConfig peer;
    peer.did = "p1";
    // user, key 为空
    cfg.peers["p1"] = peer;

    ASSERT_TRUE(cfg.save(path));

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    EXPECT_EQ(content.find("user"), std::string::npos);
    EXPECT_EQ(content.find("key"), std::string::npos);
}

TEST(ConfigSave, FailsOnInvalidPath) {
    bto::config::Config cfg;
    cfg.did = "test";
    EXPECT_FALSE(cfg.save("/nonexistent/deep/path/file.toml"));
}

// ═══════════════════════════════════════════════════════════════
//  Config::load + save 往返一致性
// ═══════════════════════════════════════════════════════════════

TEST(ConfigRoundTrip, SaveThenLoad) {
    TmpDir tmp;
    auto path = tmp.file("roundtrip.toml");

    bto::config::Config original;
    original.did = "roundtrip-did";
    original.relay = "10.0.0.1:9700";

    bto::config::PeerConfig p1;
    p1.did = "peer-1-did";
    p1.user = "user1";
    p1.key = "/home/user1/.ssh/id_rsa";
    p1.port = 3333;
    original.peers["peer-1"] = p1;

    bto::config::PeerConfig p2;
    p2.did = "peer-2-did";
    // user, key 为空, port 默认
    original.peers["peer-2"] = p2;

    ASSERT_TRUE(original.save(path));

    auto loaded = bto::config::Config::load(path);
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->did, original.did);
    EXPECT_EQ(loaded->relay, original.relay);
    ASSERT_EQ(loaded->peers.size(), original.peers.size());

    EXPECT_EQ(loaded->peers.at("peer-1").did, "peer-1-did");
    EXPECT_EQ(loaded->peers.at("peer-1").user, "user1");
    EXPECT_EQ(loaded->peers.at("peer-1").key, "/home/user1/.ssh/id_rsa");
    EXPECT_EQ(loaded->peers.at("peer-1").port, 3333);

    EXPECT_EQ(loaded->peers.at("peer-2").did, "peer-2-did");
    EXPECT_TRUE(loaded->peers.at("peer-2").user.empty());
    EXPECT_EQ(loaded->peers.at("peer-2").port, 22);
}

// ═══════════════════════════════════════════════════════════════
//  Config::resolve_peer — 模糊匹配
// ═══════════════════════════════════════════════════════════════

class ResolvePeerTest : public ::testing::Test {
protected:
    void SetUp() override {
        bto::config::PeerConfig p;

        p.did = "office-213"; p.user = "user";
        cfg_.peers["office-213"] = p;

        p.did = "office-215"; p.user = "user";
        cfg_.peers["office-215"] = p;

        p.did = "home-macbook"; p.user = "me";
        cfg_.peers["home-macbook"] = p;
    }

    bto::config::Config cfg_;
};

TEST_F(ResolvePeerTest, ExactMatch) {
    auto result = cfg_.resolve_peer("office-213");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "office-213");
    EXPECT_EQ(result->second.did, "office-213");
}

TEST_F(ResolvePeerTest, SuffixMatch_Unique) {
    auto result = cfg_.resolve_peer("macbook");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "home-macbook");
}

TEST_F(ResolvePeerTest, PrefixMatch_Unique) {
    auto result = cfg_.resolve_peer("home");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "home-macbook");
}

TEST_F(ResolvePeerTest, SuffixMatch_213) {
    auto result = cfg_.resolve_peer("213");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "office-213");
}

TEST_F(ResolvePeerTest, SuffixMatch_215) {
    auto result = cfg_.resolve_peer("215");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "office-215");
}

TEST_F(ResolvePeerTest, AmbiguousMatch_ReturnsNullopt) {
    // "office" 前缀匹配 office-213 和 office-215
    auto result = cfg_.resolve_peer("office");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ResolvePeerTest, NoMatch) {
    auto result = cfg_.resolve_peer("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ResolvePeerTest, EmptyInput) {
    auto result = cfg_.resolve_peer("");
    // 空字符串不应匹配任何 peer
    // 行为取决于实现：所有 key 都以 "" 开头，所以会匹配全部 → 歧义 → nullopt
    EXPECT_FALSE(result.has_value());
}

TEST_F(ResolvePeerTest, InputLongerThanKeys) {
    auto result = cfg_.resolve_peer("office-213-extra-long-suffix");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ResolvePeerTest, SinglePeerConfig) {
    bto::config::Config single;
    bto::config::PeerConfig p;
    p.did = "only-one";
    single.peers["only-one"] = p;

    auto result = single.resolve_peer("one");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, "only-one");
}

TEST_F(ResolvePeerTest, EmptyPeers) {
    bto::config::Config empty;
    auto result = empty.resolve_peer("anything");
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════
//  relay_host / relay_port
// ═══════════════════════════════════════════════════════════════

TEST(RelayParsing, HostAndPort) {
    bto::config::Config cfg;
    cfg.relay = "relay.bto.asia:9700";

    EXPECT_EQ(cfg.relay_host(), "relay.bto.asia");
    EXPECT_EQ(cfg.relay_port(), 9700);
}

TEST(RelayParsing, HostOnly) {
    bto::config::Config cfg;
    cfg.relay = "myhost";

    EXPECT_EQ(cfg.relay_host(), "myhost");
    EXPECT_EQ(cfg.relay_port(), 9700);  // 默认
}

TEST(RelayParsing, HostWithInvalidPort) {
    bto::config::Config cfg;
    cfg.relay = "myhost:abc";

    EXPECT_EQ(cfg.relay_host(), "myhost");
    EXPECT_EQ(cfg.relay_port(), 9700);  // 回退到默认
}

TEST(RelayParsing, EmptyRelay) {
    bto::config::Config cfg;
    // relay 为空
    EXPECT_TRUE(cfg.relay_host().empty());
    EXPECT_EQ(cfg.relay_port(), 9700);
}

TEST(RelayParsing, IPv6WithPort) {
    bto::config::Config cfg;
    cfg.relay = "::1:9700";

    // find_last_of(':') 会找到最后一个 ':'
    EXPECT_EQ(cfg.relay_host(), "::1");
    EXPECT_EQ(cfg.relay_port(), 9700);
}

TEST(RelayParsing, CustomPort) {
    bto::config::Config cfg;
    cfg.relay = "10.0.0.1:8080";

    EXPECT_EQ(cfg.relay_host(), "10.0.0.1");
    EXPECT_EQ(cfg.relay_port(), 8080);
}

// ═══════════════════════════════════════════════════════════════
//  default_config_path
// ═══════════════════════════════════════════════════════════════

TEST(DefaultConfigPath, UsesHome) {
    EnvOverride home("HOME", "/tmp/fake-home");
    auto path = bto::config::default_config_path();
    EXPECT_EQ(path, "/tmp/fake-home/.bto/config.toml");
}

TEST(DefaultConfigPath, FallbackWhenNoHome) {
    // 这个测试比较特殊：需要 unset HOME
    // 保存后恢复
    auto old = std::getenv("HOME");
    unsetenv("HOME");

    auto path = bto::config::default_config_path();
    EXPECT_EQ(path, "./.bto/config.toml");

    if (old) setenv("HOME", old, 1);
}

TEST(DefaultRuntimeDir, UsesHome) {
    EnvOverride home("HOME", "/tmp/fake-home");
    auto path = bto::config::default_runtime_dir();
    EXPECT_EQ(path, "/tmp/fake-home/.peerlink/run");
}

TEST(DefaultDaemonSocketPath, UsesHome) {
    EnvOverride home("HOME", "/tmp/fake-home");
    auto path = bto::config::default_daemon_socket_path();
    EXPECT_EQ(path, "/tmp/fake-home/.peerlink/run/peerlinkd.sock");
}

// ═══════════════════════════════════════════════════════════════
//  PeerConfig 默认值
// ═══════════════════════════════════════════════════════════════

TEST(PeerConfig, Defaults) {
    bto::config::PeerConfig pc;
    EXPECT_TRUE(pc.did.empty());
    EXPECT_TRUE(pc.user.empty());
    EXPECT_TRUE(pc.key.empty());
    EXPECT_EQ(pc.port, 22);
}
