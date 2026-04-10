# Claim code lifecycle

Claim codes pair a **hosted device record** with a **first-time install** on a remote Linux/macOS host.

## States

| State      | Meaning |
|------------|---------|
| `issued`   | Returned by `POST /v1/devices` or `POST /v1/devices/{id}/claim`; not yet used. |
| `consumed` | Successfully exchanged via `POST /v1/claim/consume`; binds the agent. |
| `expired`  | Past `expires_at` without consume. |
| `revoked`  | Invalidated by server policy or device removal. |

## Transitions

```text
issued -> consumed
issued -> expired
issued -> revoked
```

## Rules

- Codes MUST be single-use: after `consumed`, replay MUST return `409` or `410`.
- `expires_at` MUST be RFC 3339 UTC.
- The CLI prints the code once at creation; the server MUST NOT rely on the CLI to store it.

## Related API

See `openapi/hosted-api.yaml` paths `/v1/devices`, `/v1/devices/{device_id}/claim`, `/v1/claim/consume`.
