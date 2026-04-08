# 数据流与会话生命周期

## 默认连接流程

```
用户执行: bto connect office-213

bto
  ├─ parse_arguments()
  ├─ Config::load(~/.bto/config.toml)
  ├─ resolve_peer() / 组装 local_did + target_did + relay
  ├─ 通过 UDS 请求 peerlinkd.create_session
  │
  └─ daemon 返回:
       session_id = "sess-42"
       local_port = 2222 / 2223 / auto-assigned
```

随后：

```
bto
  ├─ fork/exec ssh user@127.0.0.1 -p <local_port>
  ├─ 等待当前终端的 ssh 退出
  └─ peerlinkd.close_session(session_id)
```

## daemon 内部连接复用

```
create_session(local_did, target_did, relay)
  │
  ├─ 命中已有 TargetConnection?
  │    ├─ 是: 复用既有 local_port 和 ConnectBridge
  │    └─ 否: 分配新的 local_port，创建 ConnectBridge
  │
  ├─ 等待 ConnectBridge ready
  └─ 生成 SessionRecord 返回给当前 bto 进程
```

连接键是：

```
local_did + "|" + target_did + "|" + relay_host:relay_port
```

这保证了：

- 同一用户同一 DID 访问同一目标时会复用连接
- 不同本机 DID 不会互相串用
- 不同目标会自动拿到不同本地端口

## `ConnectBridge` 内部流向

```
peerlinkd 持有一个 ConnectBridge
  │
  ├─ 本地 SSH #1 连入 → TunnelSession #1 → create_channel()
  ├─ 本地 SSH #2 连入 → TunnelSession #2 → create_channel()
  └─ 本地 SSH #N 连入 → TunnelSession #N → create_channel()
```

关键点：

- 一个目标只保留一个 `P2PClient`
- 每个本地 SSH TCP 流对应一个 `TunnelSession`
- 每个 `TunnelSession` 对应一个 P2P channel

## 为什么这能解决多终端问题

旧路径的问题在于“一个 `bto connect` 进程 = 一个本地端口 + 一个生命周期”。

新路径中：

- 终端 A 的 `bto 213` 只持有自己的 ssh 子进程
- 终端 B 的 `bto 213` 会向同一个 daemon 再申请一个 session
- 终端 C 的 `bto 215` 会拿到另一个目标连接和另一个本地端口

因此三个终端互相隔离，但底层连接由 daemon 统一协调。

## 关闭与回收

```
ssh 退出
  ├─ bto 调用 close_session(session_id)
  ├─ TargetConnection 引用计数 -1
  └─ 若无 session 引用且无活动 tunnel:
       30s 后自动 stop()

如果 `bto` 在拿到 session 后异常退出，没有机会主动发送 `close_session`，
daemon 还会依赖 session 租约超时做兜底清理，避免本地端口永久悬挂。
```

`ConnectBridge::stop()` 现在会：

- 先摘出 `sessions_`
- 在锁外逐个关闭会话
- 回调 daemon 做连接状态清理

这样避免了 stop 时直接在锁内递归触发 cleanup 的死锁问题。
