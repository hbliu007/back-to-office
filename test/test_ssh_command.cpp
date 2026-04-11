#include "util/ssh_command.hpp"

#include <algorithm>
#include <gtest/gtest.h>

namespace {

TEST(SshCommand, AddsExplicitKeepaliveOptions) {
    const auto result = bto::build_ssh_argv("liuhongbo", "", 2222, "office-213");
    ASSERT_TRUE(result.has_value());
    const auto& argv = *result;

    EXPECT_NE(std::find(argv.begin(), argv.end(), "-o"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "ServerAliveInterval=30"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "ServerAliveCountMax=3"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "TCPKeepAlive=yes"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "HostKeyAlias=office-213"), argv.end());
}

TEST(SshCommand, OmitsHostKeyAliasWhenNotProvided) {
    const auto result = bto::build_ssh_argv("liuhongbo", "", 2222, "");
    ASSERT_TRUE(result.has_value());
    const auto& argv = *result;

    EXPECT_EQ(std::find(argv.begin(), argv.end(), "HostKeyAlias="), argv.end());
}

TEST(SshCommand, IncludesIdentityFileWhenKeyProvided) {
    const auto result = bto::build_ssh_argv("liuhongbo", "/tmp/id_ed25519", 2200, "");
    ASSERT_TRUE(result.has_value());
    const auto& argv = *result;

    EXPECT_NE(std::find(argv.begin(), argv.end(), "-i"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "/tmp/id_ed25519"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "liuhongbo@127.0.0.1"), argv.end());
}

TEST(SshCommand, RejectsUserWithDangerousChars) {
    EXPECT_FALSE(bto::build_ssh_argv("user;rm -rf", "", 22, "").has_value());
    EXPECT_FALSE(bto::build_ssh_argv("user@host", "", 22, "").has_value());
    EXPECT_FALSE(bto::build_ssh_argv("user|cmd", "", 22, "").has_value());
    EXPECT_FALSE(bto::build_ssh_argv("user&bg", "", 22, "").has_value());
    EXPECT_FALSE(bto::build_ssh_argv("user$var", "", 22, "").has_value());
    EXPECT_FALSE(bto::build_ssh_argv("user`cmd`", "", 22, "").has_value());
    EXPECT_FALSE(bto::build_ssh_argv("", "", 22, "").has_value());
}

TEST(SshCommand, RejectsKeyWithPathTraversal) {
    EXPECT_FALSE(bto::build_ssh_argv("user", "/tmp/../etc/shadow", 22, "").has_value());
    EXPECT_FALSE(bto::build_ssh_argv("user", "../../secret", 22, "").has_value());
}

}  // namespace
