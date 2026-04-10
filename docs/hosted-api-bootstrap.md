# Remote agent bootstrap payload (frozen)

When the remote host runs the device installer (claim-code path or SSH bootstrap), the control plane delivers a **one-time bootstrap document** that configures `p2p-tunnel-server` (the only supported remote runtime in V2).

This shape is frozen for Track A client/installer work; the hosted service MUST NOT invent a different runtime without a versioned contract bump.

## JSON body

```json
{
  "device_id": "dev_123",
  "display_name": "office-213",
  "target_did": "office-213",
  "bootstrap_token": "...",
  "artifact_channel": "stable",
  "service_mode": "systemd",
  "config_path": "/etc/bto-agent/config.json"
}
```

| Field | Description |
|-------|-------------|
| `device_id` | Hosted stable identifier (API id). |
| `display_name` | User-visible name (`bto connect <name>`). |
| `target_did` | Transport DID used with PeerLink / tunnel registration. |
| `bootstrap_token` | Short-lived secret for agent registration (not a long-lived user token). |
| `artifact_channel` | Release channel for agent binaries (`stable`, `beta`, …). |
| `service_mode` | `systemd` (Linux) or `launchd` (macOS). |
| `config_path` | Path written by installer for the agent. |

## Transport

Delivery mechanism (HTTPS download, inline in install script env, etc.) is implementation detail of Track B, but the **decoded JSON** MUST match this schema.
