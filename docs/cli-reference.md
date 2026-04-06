# 命令行参考

## 概览

```
bto [命令] [参数] [选项]
```

无参数时默认执行 `list`。

## 命令

### connect — 连接远端设备

```bash
bto connect <peer> [--listen <port>] [--did <did>] [--relay <host:port>]
bto <peer>          # 快捷方式
```

- `<peer>` 支持模糊匹配（精确 → 后缀 → 前缀）
- 启动后监听本地端口（默认 2222），等待 SSH 连入
- 每个 SSH 连入创建独立 P2P 隧道（多会话并发）
- Ctrl+C 优雅退出

示例：
```bash
bto connect office-213              # 完整写法
bto 213                             # 快捷：后缀匹配
bto 213 --listen 3333               # 自定义端口
bto connect 213 --relay 10.0.0.1:9700  # 指定 relay
```

### list — 列出已配置设备

```bash
bto list
bto          # 等效（无参数默认）
```

输出格式：
```
已配置设备 (2 台):
  office-213  user=lhb
  office-215  user=lhb
```

### status — 显示配置状态

```bash
bto status
```

输出格式：
```
本机 DID:   home-mac
Relay:      relay.bto.asia:9700
已配置设备: 2 台
```

### config — 显示配置文件

```bash
bto config
```

显示配置文件路径和内容。若文件不存在，输出创建模板。

### add — 添加设备

```bash
bto add <name> [--did <did>] [--user <user>] [--key <path>]
```

- DID 默认与 name 相同
- 自动创建 `~/.bto/` 目录
- 保留已有配置

示例：
```bash
bto add office-213 --user lhb
bto add 213 --did office-213 --user lhb --key ~/.ssh/id_ed25519
```

### remove — 移除设备

```bash
bto remove <name>
```

精确匹配 name，不支持模糊。

### ping — 测试 Relay 可达性

```bash
bto ping [--relay <host:port>]
```

TCP 连接 relay → 发送 `PING\n` → 读取响应 → 报告延迟。

### help — 帮助

```bash
bto help [topic]
bto --help
bto -h
```

主题: `connect` / `add` / `remove` / `config` / `errors` / `error` / `exit-codes` / `list` / `status`

### version — 版本

```bash
bto --version
bto -v
bto version
```

## 全局选项

| 选项 | 说明 | 适用命令 |
|------|------|----------|
| `--did <did>` | 覆盖本机 DID | connect |
| `--relay <host:port>` | 覆盖 Relay 地址 | connect, ping |
| `--listen <port>` | 本地监听端口 (默认 2222) | connect |
| `--user <user>` | SSH 用户名 | add |
| `--key <path>` | SSH 私钥路径 | add |
| `--help, -h` | 显示帮助 | 全局 |
| `--version, -v` | 显示版本 | 全局 |

## 退出码

| 码 | 名称 | 含义 | 排查 |
|----|------|------|------|
| 0 | OK | 成功 | - |
| 1 | USAGE | 用法错误（缺参数） | `bto help` |
| 2 | CONFIG | 配置错误（无 relay） | `bto config` |
| 3 | NETWORK | 网络错误（relay 不可达） | `bto ping` |
| 4 | PEER_NOT_FOUND | 设备未找到 | `bto list` |
| 10 | INTERRUPTED | SIGINT/SIGTERM | - |

## 快捷方式

```bash
bto                    # → bto list
bto office-213         # → bto connect office-213
bto 213                # → bto connect 213 (模糊匹配)
bto 213 --listen 3333  # → bto connect 213 --listen 3333
```
