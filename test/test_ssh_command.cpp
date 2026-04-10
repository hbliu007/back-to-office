#include "util/ssh_command.hpp"

#include <algorithm>
#include <gtest/gtest.h>

namespace {

TEST(SshCommand, AddsExplicitKeepaliveOptions) {
    const auto argv = bto::build_ssh_argv("liuhongbo", "", 2222, "office-213");

    EXPECT_NE(std::find(argv.begin(), argv.end(), "-o"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "ServerAliveInterval=30"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "ServerAliveCountMax=3"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "TCPKeepAlive=yes"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "HostKeyAlias=office-213"), argv.end());
}

TEST(SshCommand, OmitsHostKeyAliasWhenNotProvided) {
    const auto argv = bto::build_ssh_argv("liuhongbo", "", 2222, "");

    EXPECT_EQ(std::find(argv.begin(), argv.end(), "HostKeyAlias="), argv.end());
}

TEST(SshCommand, IncludesIdentityFileWhenKeyProvided) {
    const auto argv = bto::build_ssh_argv("liuhongbo", "/tmp/id_ed25519", 2200, "");

    EXPECT_NE(std::find(argv.begin(), argv.end(), "-i"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "/tmp/id_ed25519"), argv.end());
    EXPECT_NE(std::find(argv.begin(), argv.end(), "liuhongbo@127.0.0.1"), argv.end());
}

}  // namespace
