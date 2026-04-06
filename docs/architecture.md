# 系统架构

## 模块依赖

```
┌─────────────────────────────────────────────────┐
│                    bto.cpp                       │
│              主入口 · 命令分发                    │
│    cmd_connect / list / status / config          │
│    cmd_add / remove / ping                       │
├──────────┬──────────────┬───────────────────────┤
│  cli/    │   config/    │    p2p_bridge          │
│ parser   │   config     │  TunnelSession         │
│          │              │  ConnectBridge          │
└──────────┴──────┬───────┴──────────┬────────────┘
                  │                  │
                  │        ┌────────▼────────┐
                  │        │  PeerLink P2P   │
                  │        │  p2p_core       │
                  │        │  p2p_transport   │
                  │        │  p2p_nat         │
                  │        └─────────────────┘
                  │
           ┌──────▼──────┐
           │ ~/.bto/     │
           │ config.toml │
           └─────────────┘
```

## 模块职责

### bto.cpp — 主入口

- `main()`: 解析命令行 → 加载配置 → 分发命令
- 7 个命令处理器，各自独立，无交叉依赖
- `signal_handler`: SIGINT/SIGTERM 优雅退出
- `parse_relay()`: 解析 `host:port` 格式

### cli/parser — 命令行解析

- `parse_arguments()`: argv → `Command` 结构体，手写解析（无第三方库）
- `show_help(topic)`: 分主题帮助系统，10 个主题分支
- `show_version()`: 版本信息
- `ExitCode`: 结构化退出码常量

**设计决策**: 未使用 getopt/CLI11 等库，因为命令结构简单且需要支持快捷方式语法 `bto <peer>`。

### config/config — 配置管理

- `Config::load()`: 手写 TOML 子集解析器
  - 支持 `[peers.name]` 和 `[hosts.name]`（v0 兼容）
  - 支持 `identity` 别名（→ `did`）
  - 忽略注释、空行、畸形行
- `Config::save()`: 序列化回 TOML，省略默认值
- `Config::resolve_peer()`: 模糊匹配算法
  - 精确匹配 → 后缀匹配 → 前缀匹配
  - 唯一匹配返回结果，歧义返回 nullopt
- `relay_host()` / `relay_port()`: 解析 relay 地址

### p2p_bridge — P2P 桥接层

**核心设计: 多会话架构**

```
ConnectBridge (TCP Acceptor, port 2222)
  │
  ├─ accept SSH #1 → TunnelSession #1
  │    DID: "my-laptop-session-1"
  │    P2PClient → Relay → peer sshd
  │
  ├─ accept SSH #2 → TunnelSession #2
  │    DID: "my-laptop-session-2"
  │    P2PClient → Relay → peer sshd
  │
  └─ accept SSH #N → TunnelSession #N
       DID: "my-laptop-session-N"
```

每个 SSH 连入创建独立的：
- `TunnelSession` 实例
- `P2PClient`（独立 DID、独立 relay 连接）
- 双向数据桥接

**会话隔离**: 单个会话断开不影响其他会话。ConnectBridge 通过 `sessions_` map 管理所有活跃会话。

## 关键设计模式

### shared_from_this

`TunnelSession` 继承 `enable_shared_from_this`，所有异步回调持有 `self = shared_from_this()`，保证回调执行时对象仍然存活。

### 背压控制

```
read_tcp() → send_data(P2P) → callback → read_tcp()
```

TCP 读取和 P2P 发送串行执行：`read_tcp()` 只在 `send_data()` 完成回调后才发起下一次读取，防止数据堆积。

### 延迟清理

```cpp
void TunnelSession::close() {
    // ...
    boost::asio::post(ioc_, [cleanup, id]() { cleanup(id); });
}
```

`close()` 可能在 P2P 回调中被调用。如果直接执行 cleanup（从 sessions_ map 移除自身），会导致在回调栈中销毁自身。通过 `boost::asio::post()` 延迟到当前回调返回后执行。

### RELAY_ONLY 模式

```cpp
cfg.relay_mode = p2p::core::RelayMode::RELAY_ONLY;
```

BTO 不使用 STUN/hole-punching，所有流量经 Relay 中转。这简化了网络配置，确保在严格 NAT 环境下也能工作。

## 命令分发流程

```
main()
  ├─ parse_arguments(argc, argv) → Command
  ├─ help? → show_help(topic) → exit 0
  ├─ version? → show_version() → exit 0
  ├─ Config::load(~/.bto/config.toml) → config
  ├─ 命令行覆盖 (--did, --relay)
  └─ 分发:
       connect  → cmd_connect() → ConnectBridge → ioc.run()
       list     → cmd_list()    → 打印 peers 表
       status   → cmd_status()  → 打印 DID/Relay/Peers 摘要
       config   → cmd_config()  → 打印配置文件内容
       add      → cmd_add()     → 修改 config → save()
       remove   → cmd_remove()  → 修改 config → save()
       ping     → cmd_ping()    → TCP 连接 relay → 发 PING → 读响应
```

## 外部依赖

| 依赖 | 用途 | 来源 |
|------|------|------|
| PeerLink (p2p_core 等) | P2P 通信 | `../p2p-cpp/` |
| Boost.Asio | 异步 I/O | 系统安装 |
| spdlog / fmt | 日志（PeerLink 依赖） | 系统安装 |
| Protobuf | P2P 协议序列化 | 系统安装 |
| GoogleTest | 单元测试 | 系统安装 |
