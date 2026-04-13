# BTO (Back-To-Office) 项目架构

## 设计初衷与定位

**BTO 是应用层工具，不是底层协议实现。**

### 架构分层

```
┌─────────────────────────────────────────┐
│  应用层 (Application Layer)              │
│  - bto (客户端)                          │
│  - p2p-tunnel-server (服务端)            │
│  职责: SSH隧道、端口转发、用户配置        │
└─────────────────────────────────────────┘
              ↓ 依赖
┌─────────────────────────────────────────┐
│  协议层 (Protocol Layer)                 │
│  - PeerLink (p2p-cpp)                    │
│  - P2PClient / P2PConfig                 │
│  职责: P2P连接、NAT穿透、relay传输       │
└─────────────────────────────────────────┘
```

### 核心原则

1. **BTO 不实现底层协议**
   - 不自己处理 TCP relay 协议
   - 不自己实现 NAT 穿透
   - 不自己管理连接状态

2. **BTO 依赖 PeerLink**
   - 通过 `p2p::core::P2PClient` 建立连接
   - 使用 `P2PConfig` 配置 relay/STUN
   - 依赖 PeerLink 的 `MessageFramer` 协议

3. **协议兼容性**
   - bto 客户端 ↔ p2p-tunnel-server：使用 P2PClient 协议（带 framing）
   - relay-tunnel ↔ relay-tunnel：使用 raw TCP 协议（无 framing）
   - **两者不兼容**：bto 不能连接 relay-tunnel server

## 部署架构

### 正确部署

```
Mac (客户端)                    Linux (服务端)
┌──────────────┐              ┌──────────────────┐
│ bto connect  │              │ p2p-tunnel-server│
│ (P2PClient)  │              │ (P2PClient)      │
└──────┬───────┘              └────────┬─────────┘
       │                               │
       └───────→ Relay Server ←────────┘
              (relay.example.com:9700)
              
数据流: SSH ← bto ← [framed] ← relay ← [framed] ← p2p-tunnel-server ← SSH
```

### 错误部署（当前问题）

```
Mac (客户端)                    Linux (服务端)
┌──────────────┐              ┌──────────────────┐
│ bto connect  │              │ relay-tunnel     │
│ (P2PClient)  │              │ (raw protocol)   │
└──────┬───────┘              └────────┬─────────┘
       │                               │
       └───────→ Relay Server ←────────┘
              
数据流: SSH ← bto ← [framed] ← relay ← [raw] ← relay-tunnel ← SSH
                      ↑ 协议不匹配！
```

## 服务端选择

| 工具 | 协议 | 用途 | 兼容 bto |
|------|------|------|----------|
| **p2p-tunnel-server** | P2PClient (framed) | 生产环境 | ✅ 是 |
| **relay-tunnel** | Raw TCP | 测试/调试 | ❌ 否 |

## 部署命令

### 服务端 (213/215)

```bash
# 使用 p2p-tunnel-server (正确)
p2p-tunnel-server office-215 127.0.0.1 22 \
  --relay-server relay.example.com:9700 \
  --relay-mode relay-only

# 不要使用 relay-tunnel (错误)
# relay-tunnel server --did office-215 ...  # ❌ 协议不兼容
```

### 客户端 (Mac)

```bash
# bto 自动使用 P2PClient
bto connect office-215
# 或
bto 215
```

## 配置文件

`~/.bto/config.toml`:
```toml
did = "bto-client"
relay = "relay.example.com:9700"

[peers.office-213]
  did = "office-213"
  user = "lhb"
  key = "/Users/liuhongbo/.ssh/id_rsa_10.27.12.213"

[peers.office-215]
  did = "office-215"
  user = "lhb"
  key = "/Users/liuhongbo/.ssh/id_rsa_10.27.12.215"
```

## 长连接保活

用户需求：测试长时间登录、静止状态下连接保持正常。

### 检查点

1. **P2PClient 心跳机制**
   - 检查 `p2p-cpp/src/core/p2p_client.cpp` 是否有 keepalive
   - 检查 relay transport 是否有超时断开

2. **SSH 保活配置**
   - 客户端: `~/.ssh/config` 添加 `ServerAliveInterval 60`
   - 服务端: `/etc/ssh/sshd_config` 添加 `ClientAliveInterval 60`

3. **TCP Keepalive**
   - 检查 relay transport 是否启用 SO_KEEPALIVE

## 编译与分发规则（严格遵守）

### 编译环境
- **所有 Linux 二进制应在受控的发布构建机上编译**
- 禁止在目标服务器 (213/215) 上直接编译
- 阿里云源码路径: `/opt/peerlink/src/p2p-platform/p2p-cpp/`

### 分发流程（严格遵守，避免多进程冲突）
```
1. 同步修改的源码到阿里云
2. 在阿里云编译
3. 从阿里云下载二进制到本地
4. 从本地通过跳板机上传到目标服务器
5. 在目标服务器升级部署（按以下步骤严格执行）
```

### 升级部署步骤（严格遵守）
```bash
# 1. 先停止 systemd 服务
systemctl --user stop p2p-tunnel

# 2. 确认旧进程已完全退出，杀死残留进程
pkill -f p2p-tunnel-server-v2 2>/dev/null
sleep 1

# 3. 核对新程序已就位
ls -la ~/.local/bin/p2p-tunnel-server-v2
# 确认文件大小、时间戳与刚下载的一致

# 4. 复制新二进制并赋权
cp /tmp/p2p-tunnel-server-v2 ~/.local/bin/p2p-tunnel-server-v2
chmod +x ~/.local/bin/p2p-tunnel-server-v2

# 5. 以 systemd 服务方式启动（禁止手动 nohup 启动）
systemctl --user start p2p-tunnel

# 6. 验证：只有一个进程在运行
ps aux | grep p2p-tunnel-server-v2 | grep -v grep
systemctl --user status p2p-tunnel
```

**关键原则：一定要避免多个进程同时存在！**
- 必须先停服务、杀残留进程，再启动新版本
- 禁止在旧进程还在运行时直接启动新进程
- 升级后必须用 `ps aux` 确认只有一个进程

### 原因
- 目标服务器源码可能不完整
- 统一编译环境避免依赖差异
- 阿里云有完整的构建工具链

## 故障排查

### bto 连接失败

1. 检查服务端运行的是 `p2p-tunnel-server` 还是 `relay-tunnel`
2. 检查 DID 是否有多个进程抢占（`ps aux | grep <did>`）
3. 检查 relay server 是否可达（`nc -zv relay.example.com 9700`）
4. 查看日志：`journalctl --user -u p2p-tunnel.service -f`

### relay-tunnel 用途

relay-tunnel 仅用于：
- 快速测试 relay server 连通性
- 调试 relay 协议本身
- 不依赖 PeerLink 的简单场景

**生产环境必须使用 p2p-tunnel-server。**
