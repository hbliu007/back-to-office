#include "daemon/session_lifecycle.hpp"

#include <gtest/gtest.h>

namespace {

TEST(SessionLifecycle, ActiveTunnelsPreventSessionExpiry) {
    EXPECT_FALSE(bto::daemon::should_release_session_after_timeout(1));
    EXPECT_FALSE(bto::daemon::should_release_session_after_timeout(3));
}

TEST(SessionLifecycle, IdleSessionExpiresWithoutLiveTunnels) {
    EXPECT_TRUE(bto::daemon::should_release_session_after_timeout(0));
}

TEST(SessionLifecycle, UnexpectedDisconnectPrefersReconnect) {
    EXPECT_EQ(bto::daemon::disconnect_action_for_bridge(
                  /*stopping=*/false,
                  /*active_tunnels=*/1),
              bto::daemon::DisconnectAction::reconnect);
    EXPECT_EQ(bto::daemon::disconnect_action_for_bridge(
                  /*stopping=*/false,
                  /*active_tunnels=*/0),
              bto::daemon::DisconnectAction::reconnect);
}

TEST(SessionLifecycle, ExplicitStopSkipsReconnect) {
    EXPECT_EQ(bto::daemon::disconnect_action_for_bridge(
                  /*stopping=*/true,
                  /*active_tunnels=*/2),
              bto::daemon::DisconnectAction::stop);
}

}  // namespace
