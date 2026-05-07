<p align="center">
  <img src="assets/github-hero.svg" alt="BTO — Back To Office" width="100%">
</p>

<p align="center">
  <a href="https://bto.asia"><img src="https://img.shields.io/badge/🌐_官网-bto.asia-6366f1?style=for-the-badge" alt="bto.asia"></a>&nbsp;
  <a href="https://bto.asia/docs"><img src="https://img.shields.io/badge/📖_文档-查看-22c55e?style=for-the-badge" alt="Docs"></a>&nbsp;
  <a href="https://github.com/hbliu007/back-to-office/releases/latest"><img src="https://img.shields.io/github/v/release/hbliu007/back-to-office?style=for-the-badge&color=f59e0b&label=版本" alt="Release"></a>&nbsp;
  <img src="https://img.shields.io/badge/平台-macOS_|_Linux-0f172a?style=for-the-badge" alt="Platform">
</p>

<br>

<h2 align="center">3 条命令，SSH 回你的办公室</h2>

```bash
curl -fsSL https://bto.asia/install.sh | sh    # 安装
bto login                                       # 登录
bto connect office                              # 连接，搞定
```

<p align="center">
  <a href="https://bto.asia/docs"><strong>📖 查看完整文档 →</strong></a>
</p>

---

<br>

<table>
<tr>
<td width="50%">

### 😤 没有 BTO 的日子

- 找 IT 开 VPN 工单，等两天
- 研究 FRP / 内网穿透，配半天
- 买公网 IP，改路由器防火墙
- 回家发现 session 断了，debug 白做

</td>
<td width="50%">

### 😌 有了 BTO

- `bto connect office`
- 搞定。继续写代码。

</td>
</tr>
</table>

<br>

---

<br>

<h3 align="center">🔒 安全设计</h3>

<p align="center">
<strong>端到端加密</strong> — Relay 只转发密文，无法解密<br>
<strong>密码零存储</strong> — bcrypt 单向哈希，泄露也无法还原<br>
<strong>设备隔离</strong> — 别人无法发现或连接你的设备<br>
</p>

<p align="center"><a href="https://bto.asia/docs#security">了解更多 →</a></p>

<br>

---

<br>

<h3 align="center">对比</h3>

<div align="center">

| | **BTO** | FRP | Tailscale | VPN |
|:--|:--:|:--:|:--:|:--:|
| 一条命令安装 | ✅ | ❌ | ❌ | ❌ |
| 无需公网 IP | ✅ | ❌ | ✅ | ❌ |
| 专为 SSH 场景 | ✅ | ⚠️ | ❌ | ⚠️ |
| 端到端加密 | ✅ | ❌ | ✅ | ✅ |
| 开源可审计 | ✅ | ✅ | ❌ | — |

</div>

<br>

---

<br>

<h3 align="center">谁在用？</h3>

<p align="center">
🖥️ 下班后继续写代码的程序员<br>
🎮 远程跑 GPU 训练的研究员<br>
🔧 从家里排查实验室机器的运维<br>
🤖 保持 Claude Code / Cursor 远程会话不断的 AI 开发者
</p>

<br>

---

<p align="center">
  <a href="https://bto.asia/register"><img src="https://img.shields.io/badge/立即注册-免费使用-6366f1?style=for-the-badge" alt="Register"></a>
</p>

<p align="center">
  <sub>⭐ Star 这个项目，让更多人告别 VPN 折腾</sub>
</p>

<br>

<p align="center">
  <a href="https://bto.asia">官网</a> · <a href="https://bto.asia/docs">文档</a> · <a href="https://bto.asia/download">下载</a> · <a href="SECURITY.md">安全</a> · <a href="LICENSE">MIT License</a>
</p>
