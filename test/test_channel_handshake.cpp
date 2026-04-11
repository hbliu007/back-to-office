#include "daemon/channel_handshake.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ChannelHandshake, EnabledOnlyForRelayPath) {
    EXPECT_TRUE(bto::daemon::should_use_channel_handshake(
        p2p::core::ConnectionPath::RELAY));
    EXPECT_FALSE(bto::daemon::should_use_channel_handshake(
        p2p::core::ConnectionPath::DIRECT_P2P));
    EXPECT_FALSE(bto::daemon::should_use_channel_handshake(
        p2p::core::ConnectionPath::NONE));
}

TEST(ChannelHandshake, NewChannelNeedsOpenProbeAndBuffersPayload) {
    bto::daemon::ChannelHandshake handshake;

    EXPECT_TRUE(handshake.needs_open_probe());
    EXPECT_TRUE(handshake.should_buffer_tcp_payload());
}

TEST(ChannelHandshake, OpenProbeStaysPendingUntilRemoteReady) {
    bto::daemon::ChannelHandshake handshake;

    handshake.mark_open_probe_sent();

    EXPECT_FALSE(handshake.needs_open_probe());
    EXPECT_TRUE(handshake.should_buffer_tcp_payload());
}

TEST(ChannelHandshake, ReconnectRearmsProbeForUnconfirmedChannel) {
    bto::daemon::ChannelHandshake handshake;

    handshake.mark_open_probe_sent();
    handshake.on_reconnect();

    EXPECT_TRUE(handshake.needs_open_probe());
    EXPECT_TRUE(handshake.should_buffer_tcp_payload());
}

TEST(ChannelHandshake, RemoteReadyClearsHandshakeGate) {
    bto::daemon::ChannelHandshake handshake;

    handshake.mark_open_probe_sent();
    handshake.mark_remote_ready();

    // After remote ready, no probe needed and TCP payload passes through
    EXPECT_FALSE(handshake.needs_open_probe());
    EXPECT_FALSE(handshake.should_buffer_tcp_payload());

    // But reconnect always resets — remote loses channel state
    handshake.on_reconnect();
    EXPECT_TRUE(handshake.needs_open_probe());
    EXPECT_TRUE(handshake.should_buffer_tcp_payload());
}

}  // namespace
