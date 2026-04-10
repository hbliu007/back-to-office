# Session: connect grant & upgrade grant

Hosted APIs authorize **connect** and **upgrade** without exposing relay hostnames in the default CLI UX. The client uses these responses to populate the existing `peerlinkd` / P2P path.

## Connect grant (`POST /v1/sessions/connect`)

Response (success):

```json
{
  "session_id": "sess_123",
  "expires_at": "2026-04-09T12:00:00Z",
  "broker_token": "...",
  "device_id": "dev_123",
  "target_did": "office-213",
  "relay_host": "relay.example.com",
  "relay_port": 9700,
  "ssh_user": "lhb",
  "ssh_key_ref": "key_default"
}
```

| Field | Description |
|-------|-------------|
| `session_id` | Correlates logs; may be passed to close/cleanup if needed. |
| `expires_at` | Grant expiry (RFC 3339 UTC). |
| `broker_token` | Opaque token for session broker / signaling (if required by transport). |
| `device_id` | Hosted id. |
| `target_did` | DID for P2P/tunnel. |
| `relay_host` / `relay_port` | Infrastructure endpoints (user does not type these in default flow). |
| `ssh_user` | Default SSH user for local `ssh -p <local_port>`. |
| `ssh_key_ref` | Hint for local key selection (e.g. profile id); exact mapping is client-side. |

## Upgrade grant (`POST /v1/sessions/upgrade`)

Same transport fields as connect MAY be included if the upgrade path needs a fresh broker context. Minimum: authorization for pushing the agent artifact to `target_did` within `expires_at`.

Errors MUST use stable `code` / `message` (see OpenAPI) so the CLI can show actionable text.
