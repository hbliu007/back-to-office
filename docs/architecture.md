# 系统架构

## 当前结构

```
┌──────────────────────────────────────────────────────────────┐
│                         bto CLI                              │
│     connect / list / ps / close / daemon / add / remove     │
├───────────────────────┬──────────────────────────────────────┤
│ cli/parser            │ config/config                        │
│ 命令解析              │ ~/.bto/config.toml / ~/.peerlink/run │
└───────────────┬───────┴──────────────────────┬───────────────┘
                │                              │
                │ UDS JSON API                 │
        ┌───────▼──────────────────────────────▼────────┐
        │                 peerlinkd                     │
        │ LocalEndpointManager / TargetConnection       │
        │ SessionBroker / PeerlinkService               │
        └───────────────┬───────────────────────────────┘
                        │
                ┌───────▼────────┐
                │ ConnectBridge  │
                │ 单目标单本地端口 │
                │ 多 SSH 通道复用 │
                └───────┬────────┘
                        │
                ┌───────▼────────┐
                │ PeerLink P2P   │
                │ relay-only     │
                └────────────────┘
```

## 关键变化

### 从 `bto` 前台进程下沉到 `peerlinkd`

旧模型的问题不是 `ConnectBridge` 不能多通道，而是 `bto connect` 把以下三件事绑死在一个前台进程里：

- 固定占用一个本地端口，默认 `2222`
- 只管理一个 `ssh` 子进程生命周期
- 子进程退出时直接结束整个本地桥接

这会导致多个独立终端分别执行 `bto 213` / `bto 215` 时，在 CLI 级别互相抢端口、抢生命周期。

新模型改为：

- `peerlinkd` 常驻，统一管理本地端口、目标连接和会话引用
- `bto` 只负责解析配置、向 daemon 申请会话、拉起当前终端的 `ssh`
- 同一目标可复用一个本地监听端口和一个 `ConnectBridge`
- 不同目标自动分配不同本地端口，避免默认 `2222` 冲突

### 通用目标，不绑定 213/215

daemon 只认通用的连接键：

- `local_did`
- `target_did`
- `relay_host:relay_port`

因此它天然支持任意 DID，不依赖办公室机器命名，也不假设只有 `office-213` / `office-215`。

### DID 与用户分层

- `local_did` 表示当前本机身份，可以按不同用户/不同机器配置
- `target_did` 表示远端 PeerLink 设备身份
- `ssh_user` / `ssh_key` 只是 CLI 为当前终端提供的 SSH 前端参数，不参与 daemon 连接键

这意味着：

- 不同用户可以各自维护自己的 `~/.bto/config.toml`
- 不同 DID 的本机客户端不会错误复用同一条目标连接
- 同一个目标 DID 可以被多个本地终端并发访问

## 模块职责

### `bto.cpp`

- 默认优先走 `peerlinkd`
- `connect` 在 daemon 可用时变成“申请会话 + 前台 ssh”
- `--legacy-direct` 保留旧模式，作为回退路径
- `ps` / `close` / `daemon` 提供本地运维入口

### `daemon/peerlink_service.*`

- 对外暴露 versioned JSON API
- 管理连接复用、会话引用、空闲连接回收
- 通过 UDS 向本机 CLI 提供统一入口

### `daemon/local_endpoint_manager.*`

- 为不同目标分配本地监听端口
- 只在用户显式传入 `--listen` 时尝试固定端口
- 默认自动分配，避免多个目标撞在 `2222`

### `p2p_bridge_v2.*`

- `ConnectBridge` 负责单目标、多通道复用
- `TunnelSession` 负责单 SSH TCP 流
- 现已补充 `ready / failed / stopped` 回调和活跃会话计数，适合被 daemon 托管

## 运行时约束

- 当前 daemon socket: `~/.peerlink/run/peerlinkd.sock`
- 当前配置文件: `~/.bto/config.toml`
- 所有 PeerLink 通道仍使用 `RELAY_ONLY`
- 当一个目标没有引用会话且没有活动隧道时，daemon 会延迟回收对应连接
- 若客户端异常退出，未显式关闭的 session 会由 daemon 的租约超时兜底回收
