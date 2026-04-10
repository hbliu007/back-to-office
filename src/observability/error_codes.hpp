#pragma once

namespace bto::observability::code {

inline constexpr char kDaemonInvalidRequest[] = "BTO_DAEMON_INVALID_REQUEST";
inline constexpr char kDaemonInvalidJson[] = "BTO_DAEMON_INVALID_JSON";
inline constexpr char kDaemonUnknownAction[] = "BTO_DAEMON_UNKNOWN_ACTION";
inline constexpr char kDaemonConnectFailed[] = "BTO_DAEMON_CONNECT_FAILED";
inline constexpr char kDaemonPortConflict[] = "BTO_DAEMON_PORT_CONFLICT";
inline constexpr char kDaemonPortUnavailable[] = "BTO_DAEMON_PORT_UNAVAILABLE";
inline constexpr char kDaemonNotFound[] = "BTO_DAEMON_NOT_FOUND";
inline constexpr char kDaemonAmbiguousTarget[] = "BTO_DAEMON_AMBIGUOUS_TARGET";

inline constexpr char kBridgeStartFailed[] = "BTO_BRIDGE_START_FAILED";
inline constexpr char kBridgeInitFailed[] = "BTO_BRIDGE_INIT_FAILED";
inline constexpr char kBridgeConnectFailed[] = "BTO_BRIDGE_CONNECT_FAILED";
inline constexpr char kBridgeDisconnected[] = "BTO_BRIDGE_DISCONNECTED";
inline constexpr char kBridgeAcceptFailed[] = "BTO_BRIDGE_ACCEPT_FAILED";
inline constexpr char kBridgeTcpReadFailed[] = "BTO_BRIDGE_TCP_READ_FAILED";
inline constexpr char kBridgeTcpWriteFailed[] = "BTO_BRIDGE_TCP_WRITE_FAILED";
inline constexpr char kBridgeBufferOverflow[] = "BTO_BRIDGE_BUFFER_OVERFLOW";

}  // namespace bto::observability::code
