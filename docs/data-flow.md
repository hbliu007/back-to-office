# 数据流与会话生命周期

## 连接建立流程

```
用户执行: bto connect office-213

main()
  │
  ├─ parse_arguments() → Command{name="connect", target="office-213"}
  │
  ├─ Config::load("~/.bto/config.toml")
  │
  ├─ resolve_peer("office-213") → did="office-213", user="lhb"
  │
  ├─ build_p2p_config(relay_host, relay_port)
  │    └─ RelayMode::RELAY_ONLY
  │
  ├─ ConnectBridge(ioc, "home-mac", "office-213", p2p_cfg, 2222)
  │    ├─ set_ssh_hint("user", "")
  │    └─ start()
  │         ├─ acceptor_.bind(0.0.0.0:2222)
  │         ├─ acceptor_.listen()
  │         ├─ 打印: "ssh -p 2222 user@127.0.0.1"
  │         └─ do_accept()  ← 循环等待 SSH 连入
  │
  └─ ioc.run()  ← 事件循环启动
```

## SSH 连入 → 会话创建

```
SSH 客户端连入 port 2222
  │
  ▼
do_accept()
  │
  ├─ session_id = ++next_session_id_     (例: 1)
  ├─ session_did = "home-mac-session-1"
  │
  ├─ TunnelSession(id=1, did="home-mac-session-1", ioc, config, socket)
  │    │
  │    ├─ P2PClient(ioc, "home-mac-session-1", config)  ← 独立实例
  │    └─ tcp_socket_ = SSH 连接的 socket
  │
  ├─ sessions_[1] = session
  │
  ├─ session.start("office-213")
  │    │
  │    ├─ 注册回调: on_connected / on_disconnected / on_data / on_error
  │    │
  │    ├─ client_->initialize()         ← 注册 DID 到 Relay
  │    │    └─ callback:
  │    │         └─ client_->connect("office-213")  ← 连接远端
  │    │              └─ callback:
  │    │                   └─ on_connected 触发
  │    │                        ├─ create_channel() → channel_id_
  │    │                        └─ read_tcp()  ← 开始数据桥接
  │    │
  │    └─ (异步等待 P2P 连接建立)
  │
  └─ do_accept()  ← 继续接受下一个 SSH 连入
```

## 双向数据桥接

```
SSH 客户端                 BTO                      远端 sshd
    │                       │                          │
    │── TCP data ──────────►│                          │
    │                       │── send_data(P2P) ──────►│
    │                       │   (channel_id_)          │
    │                       │                          │
    │                       │◄── on_data(P2P) ────────│
    │◄── async_write(TCP) ──│                          │
    │                       │                          │

详细流程:

read_tcp()
  │
  ├─ tcp_socket_->async_read_some(buf, 8192)
  │    │
  │    └─ callback(ec, n):
  │         ├─ 错误? → close()
  │         ├─ buf.resize(n)
  │         └─ client_->send_data(channel_id_, buf)
  │              │
  │              └─ callback(ec):       ← 背压控制点
  │                   ├─ 错误? → close()
  │                   └─ read_tcp()     ← 发送完成后才读下一块
  │
on_data(channel_id, data)  ← P2P 接收回调
  │
  ├─ closed_? → return
  ├─ channel_id 匹配? → return (不匹配)
  ├─ socket open? → return (已关闭)
  └─ async_write(tcp_socket_, data)
       └─ callback(ec):
            └─ 错误? → close()
```

## 会话关闭流程

```
触发条件:
  • SSH 客户端断开 (read_tcp 收到 EOF)
  • P2P 连接断开 (on_disconnected)
  • P2P 错误 (connect 失败、send_data 失败)
  • TCP 写入错误

close()
  │
  ├─ closed_ = true           ← 幂等保护
  ├─ tcp_socket_->close()     ← 关闭 TCP 连接
  ├─ client_->close()         ← 关闭 P2P 连接
  └─ boost::asio::post(ioc_)  ← 延迟执行 cleanup
       │
       └─ on_cleanup_(id_)
            │
            └─ ConnectBridge::remove_session(id)
                 ├─ sessions_.erase(id)
                 └─ 打印: "#1 已清理 (活跃会话: 0)"

为什么延迟清理?
  close() 可能在 P2P 回调栈中被调用:
    on_data → async_write 错误 → close() → remove_session → erase(self)
  如果直接 erase，会在回调栈返回时访问已销毁对象。
  post() 保证 cleanup 在当前回调完全返回后才执行。
```

## 信号处理

```
SIGINT / SIGTERM
  │
  └─ signal_handler()
       └─ g_ioc->stop()
            │
            └─ ioc.run() 返回
                 └─ main() 返回 EC::OK

注意: ConnectBridge::stop() 不会被显式调用。
io_context 停止后，所有异步操作被取消，
shared_ptr 引用计数归零时 session 自动析构。
```

## 并发会话示意

```
终端 1: ssh -p 2222 user@127.0.0.1
终端 2: ssh -p 2222 user@127.0.0.1
终端 3: ssh -p 2222 user@127.0.0.1

ConnectBridge
├── Session #1: home-mac-session-1 ──P2P──► office-213  (独立 relay 连接)
├── Session #2: home-mac-session-2 ──P2P──► office-213  (独立 relay 连接)
└── Session #3: home-mac-session-3 ──P2P──► office-213  (独立 relay 连接)

每个 session 有独立的:
  • P2PClient 实例
  • DID 标识（base_did + "-session-" + N）
  • Relay 连接
  • 数据通道
  • TCP socket

session 之间完全隔离，某个断开不影响其他。
```
