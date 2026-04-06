<p align="center">
  <img src="docs/images/architecture.svg" alt="BTO — Back To Office" width="100%">
</p>

<h1 align="center">BTO (Back-To-Office)</h1>

<p align="center">
  <strong>One command to SSH into your office from anywhere — zero VPN, self-hosted relay, mobile ready.</strong>
</p>

<p align="center">
  <a href="https://github.com/hbliu007/back-to-office/releases/latest">
    <img src="https://img.shields.io/github/v/release/hbliu007/back-to-office?style=flat-square&logo=github&label=Release" alt="Release">
  </a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square&logo=c%2B%2B" alt="C++20">
  <img src="https://img.shields.io/badge/Coverage-85.5%25-brightgreen?style=flat-square" alt="Coverage">
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=flat-square" alt="License: MIT">
  <a href="https://github.com/hbliu007/back-to-office/stargazers">
    <img src="https://img.shields.io/github/stars/hbliu007/back-to-office?style=flat-square&logo=github" alt="Stars">
  </a>
  <a href="https://github.com/hbliu007/back-to-office/issues">
    <img src="https://img.shields.io/github/issues/hbliu007/back-to-office?style=flat-square" alt="Issues">
  </a>
</p>

<p align="center">
  <a href="#-quick-start">Quick Start</a> · <a href="#-how-it-works">How It Works</a> · <a href="#-comparison">Comparison</a> · <a href="docs/">Docs</a> · <a href="#-build-from-source">Build</a>
</p>

---

## Why BTO?

You're at a coffee shop. You need to SSH into your office dev machine behind a corporate firewall.
VPN is complex, FRP needs port forwarding, and Tailscale depends on third-party DERP servers.

**BTO gives you a simpler way**: one binary, one command, P2P tunnel — built on [PeerLink](https://github.com/hbliu007/peerlink).

> [!IMPORTANT]
> **一条命令，从任何地方 SSH 回办公室** — 无需 VPN、无需端口转发、无需防火墙规则。支持 Linux / macOS / Android (Termux)。

## 🚀 Quick Start

### 1. Install (10 seconds)

```console
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
  ✓ Detected: darwin arm64
  ✓ Downloaded: bto-v1.1.0-darwin-arm64.tar.gz
  ✓ SHA256 verified
  ✓ Installed to ~/.local/bin/bto
```

### 2. Office Machine (run once, keep alive)

```console
$ bto daemon --did office-213 --relay relay.bto.asia:9700
  ✓ Registered as office-213
  ✓ Listening for incoming connections
  ✓ Forwarding to localhost:22
```

### 3. Your Laptop (from anywhere)

```console
$ bto connect office-213
  ✓ Connected to office-213 via relay
  ✓ Listening on localhost:2222

$ ssh -p 2222 user@127.0.0.1
user@office-213:~$
```

That's it. No VPN, no port forwarding, no firewall rules.

> [!TIP]
> **Fuzzy matching**: `bto 213` or `bto office` automatically matches `office-213`.

## ⚡ Features

| | | |
|:---|:---|:---|
| **Zero Configuration** | **Cross Platform** | **Secure** |
| Single binary, no dependencies | Linux, macOS, Android (Termux) | DID identity + relay encryption |
| One-line install via `curl\|sh` | x86_64 and ARM64 | No credential exposure |
| Fuzzy peer name matching | Mobile SSH from phone | Self-hosted relay option |

### Platform Support

| Platform | Architecture | Status |
|:---------|:------------|:------:|
| Linux | x86_64, ARM64 | ✅ Stable |
| macOS | Apple Silicon (M1+), Intel | ✅ Stable |
| Android | Termux (ARM64) | ✅ Stable |
| Windows | WSL2 | 🔄 Planned |

## 🔍 How It Works

BTO builds on [PeerLink P2P](https://github.com/hbliu007/peerlink). Each device registers a DID (Decentralized Identifier) with a relay server. Devices find each other through the relay and establish P2P tunnels — even behind NAT.

<p align="center">
  <img src="docs/images/connection-flow.svg" alt="Connection Flow" width="100%">
</p>

### Connection Sequence

```
bto connect office-213
  → Register local DID with relay
  → Look up office-213 via relay
  → Establish P2P tunnel (relay-assisted)
  → Listen on local port 2222
  → Forward SSH traffic through the tunnel
```

<details>
<summary><strong>📖 Detailed Data Flow</strong></summary>

```
Your Laptop                    Relay Server               Office Machine
(bto connect)               (relay.bto.asia:9700)         (bto daemon)
     │                              │                          │
     │──── REGISTER home-001 ──────►│                          │
     │◄─── OK ─────────────────────│                          │
     │                              │                          │
     │──── CONNECT office-213 ────►│                          │
     │                              │──── INCOMING home-001 ──►│
     │                              │                          │
     │◄════════ P2P Tunnel Established ═══════════════════════►│
     │                              │                          │
     │──── SSH Handshake ──────────────────────────────────────►│
     │◄─── SSH Response ───────────────────────────────────────│
     │──── Encrypted Data ─────────────────────────────────────►│
     │◄─── Encrypted Data ─────────────────────────────────────│
```

</details>

## 🆚 Comparison

| Feature | BTO | SSH + VPN | FRP | Tailscale |
|:--------|:---:|:---------:|:---:|:---------:|
| One-Line Install | ✅ | ❌ | ⚠️ | ✅ |
| Zero Port Forwarding | ✅ | ❌ | ❌ | ✅ |
| Self-Hosted | ✅ Full | ✅ | ✅ | ⚠️ Headscale |
| No 3rd-Party Dependency | ✅ | ✅ | ✅ | ❌ DERP |
| Mobile Support | ✅ Termux | ⚠️ | ❌ | ✅ |
| Setup Complexity | **~30 sec** | Hours | ~10 min | ~5 min |
| Binary Size | **~1 MB** | N/A | ~15 MB | ~50 MB |
| Memory Usage | **< 10 MB** | N/A | ~30 MB | ~100 MB |

## 📋 Usage

```bash
bto <peer>                    # Connect (shortcut, fuzzy match)
bto connect <peer>            # Connect to a remote device
bto daemon --did <name>       # Run as daemon (accept incoming)
bto list                      # List configured devices
bto add <name> [--did <d>]    # Add a device
bto remove <name>             # Remove a device
bto status                    # Show connection status
bto config                    # Show configuration
bto ping                      # Test relay connectivity
```

### Multi-Session

Every connection creates an independent P2P tunnel:

```bash
# Terminal 1
ssh -p 2222 user@127.0.0.1

# Terminal 2 (concurrent, independent tunnel)
ssh -p 2222 user@127.0.0.1
```

## ⚙️ Configuration

Config file: `~/.bto/config.toml`

```toml
did = "home-mac"
relay = "relay.bto.asia:9700"

[peers.office-213]
  did = "office-213"

[peers.office-215]
  did = "office-215"
```

## 📱 Mobile (Termux)

BTO works on Android via Termux — SSH into your office from your phone:

```console
$ pkg install curl
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
$ bto 213
  ✓ Connected to office-213
  ✓ Listening on localhost:2222
```

## 🔧 Build from Source

Requirements: C++20 compiler, CMake 3.20+, Boost, spdlog, fmt, protobuf

```bash
# Build PeerLink P2P libraries first
cd p2p-cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
cmake --build build -j$(nproc)

# Build BTO
cd ../back-to-office
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install
sudo cp build/bto /usr/local/bin/
```

<details>
<summary><strong>🐳 Cross-compile with Docker</strong></summary>

```bash
# Build for Linux x86_64 on macOS
docker run --rm --platform linux/amd64 \
  -v "$PWD":/workspace -w /workspace \
  ubuntu:22.04 bash -c '
    apt-get update && apt-get install -y build-essential cmake \
      libssl-dev libboost-all-dev libprotobuf-dev protobuf-compiler
    cd p2p-cpp && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
    cd .. && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
  '
```

</details>

## 🏗️ Architecture

```
back-to-office/
├── src/
│   ├── bto.cpp              # Main entry, command dispatcher
│   ├── p2p_bridge.hpp/cpp   # PeerLink P2P client wrapper
│   ├── tcp_bridge.hpp/cpp   # TCP ↔ P2P bidirectional bridge
│   ├── cli/parser.hpp/cpp   # CLI argument parser
│   └── config/config.hpp/cpp # TOML config manager
├── test/                     # 116 tests, 85.5% coverage
├── docs/                     # CLI & config reference
├── scripts/                  # Build & release scripts
└── install.sh                # One-line installer
```

| Metric | Value |
|:-------|:-----:|
| Lines of Code | **~1,100** |
| Test Count | **116** |
| Test Coverage | **85.5%** |
| Binary Size | **~1 MB** |
| Dependencies | PeerLink P2P only |

## 🔗 Related Projects

| Project | Description |
|:--------|:------------|
| [PeerLink](https://github.com/hbliu007/peerlink) | P2P Secure Access Platform (BTO's foundation) |
| [PeerLink Relay](https://github.com/hbliu007/peerlink) | Relay server for NAT traversal |

## 📄 License

[MIT License](LICENSE) — Free for personal and commercial use.

---

<p align="center">
  <a href="https://github.com/hbliu007/back-to-office"><strong>GitHub</strong></a> · <a href="https://github.com/hbliu007/back-to-office/issues">Issues</a> · <a href="https://github.com/hbliu007/peerlink">PeerLink</a>
</p>
