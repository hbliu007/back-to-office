#pragma once

#include "p2p/core/p2p_client.hpp"

namespace bto::daemon {

inline auto should_use_channel_handshake(p2p::core::ConnectionPath path) -> bool {
    return path == p2p::core::ConnectionPath::RELAY;
}

class ChannelHandshake {
public:
    auto needs_open_probe() const -> bool { return !remote_ready_ && !open_probe_sent_; }

    auto should_buffer_tcp_payload() const -> bool { return !remote_ready_; }

    void mark_open_probe_sent() { open_probe_sent_ = true; }

    void mark_remote_ready() {
        remote_ready_ = true;
        open_probe_sent_ = true;
    }

    void on_reconnect() {
        // Always reset — remote end loses channel state on reconnect
        open_probe_sent_ = false;
        remote_ready_ = false;
    }

private:
    bool open_probe_sent_ = false;
    bool remote_ready_ = false;
};

}  // namespace bto::daemon
