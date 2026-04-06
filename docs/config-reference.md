# 配置文件参考

## 文件位置

```
~/.bto/config.toml
```

由 `$HOME/.bto/config.toml` 决定。若 `HOME` 未设置，回退到 `./.bto/config.toml`。

## 完整格式

```toml
# 全局配置
did = "home-mac"                    # 本机 DID（P2P 身份标识）
relay = "relay.bto.asia:9700"         # Relay 服务器地址

# 设备配置（可多个）
[peers.office-213]
  did = "office-213"                # 远端 DID（默认与 name 相同）
  user = "user"                      # SSH 用户名（可选）
  key = "/home/user/.ssh/id_ed25519" # SSH 私钥路径（可选）
  port = 22                         # SSH 端口（可选，默认 22）

[peers.office-215]
  did = "office-215"
  user = "user"
```

## 字段说明

### 全局字段

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `did` | string | 推荐 | 本机 P2P 身份标识，未设置时回退到 `bto-client` |
| `identity` | string | - | `did` 的别名（v0 兼容） |
| `relay` | string | 是* | Relay 服务器 `host:port`，connect/ping 必需 |

### Peer 字段

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `did` | string | peer name | 远端 P2P 身份标识 |
| `user` | string | (空) | SSH 用户名，用于连接提示 |
| `key` | string | (空) | SSH 私钥路径，用于连接提示 |
| `port` | int | 22 | SSH 端口（save 时省略默认值） |

## 兼容性

### v0 hosts 语法

旧版 `[hosts.xxx]` 语法自动转换为 `[peers.xxx]`：

```toml
# v0 格式（仍然支持）
[hosts.legacy-server]
  did = "legacy-did"
  user = "admin"

# 等效于
[peers.legacy-server]
  did = "legacy-did"
  user = "admin"
```

### 容错处理

- 空文件 → 返回空 Config（不报错）
- 注释行 (`#`) → 跳过
- 畸形行（无 `=`）→ 跳过
- 未闭合的 section (`[xxx`) → 跳过
- 未知 section → 跳过
- 无效 port → 使用默认值 22
- 带引号和不带引号的值都支持
- 值两侧的空格被 trim，引号内空格保留

## 模糊匹配规则

`resolve_peer()` 的匹配优先级：

1. **精确匹配** — 输入与 peer name 完全相同
2. **后缀匹配** — 输入是某个 peer name 的后缀（如 `213` 匹配 `office-213`）
3. **前缀匹配** — 输入是某个 peer name 的前缀（如 `home` 匹配 `home-macbook`）

**歧义处理**: 若 2/3 步匹配到多个结果，返回 nullopt。例如 `office` 同时匹配 `office-213` 和 `office-215`，返回失败。

## 命令行覆盖

以下命令行参数会覆盖配置文件值：

```bash
bto connect peer --did my-did     # 覆盖 config.did
bto connect peer --relay h:p      # 覆盖 config.relay
```

覆盖在 `main()` 中 `Config::load()` 之后应用，不修改配置文件。
