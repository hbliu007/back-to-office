#pragma once

#include <cstddef>

namespace bto::daemon {

enum class DisconnectAction {
    stop,
    reconnect,
};

inline auto should_release_session_after_timeout(std::size_t live_tunnels) -> bool {
    return live_tunnels == 0;
}

inline auto disconnect_action_for_bridge(bool stopping,
                                         std::size_t /*active_tunnels*/) -> DisconnectAction {
    return stopping ? DisconnectAction::stop : DisconnectAction::reconnect;
}

}  // namespace bto::daemon
