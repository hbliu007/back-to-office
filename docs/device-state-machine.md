# Hosted device state machine

Devices registered in the hosted control plane follow this lifecycle. The CLI maps `status` strings to this model.

## States

| State     | Meaning |
|-----------|---------|
| `pending` | Created on the server; not yet claimed by a remote agent. |
| `claimed` | Claim code consumed or bootstrap completed; agent identity bound. |
| `online`  | Agent heartbeat / session recently seen (definition is server policy). |
| `offline` | No recent agent signal; not revoked. |
| `revoked` | Administratively disabled; API must reject new sessions and upgrades. |

## Transitions

```text
pending -> claimed -> online <-> offline
   |          |
   +----------+--> revoked (from any non-terminal state)
```

## CLI expectations

- `bto device list` shows a `status` field aligned with the table above.
- `connect` / `upgrade` must fail with a clear error when the device is `pending`, `offline` (if policy forbids), or `revoked`.
