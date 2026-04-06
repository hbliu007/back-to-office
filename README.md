<div align="center">

```
╔══════════════════════════════════════════════════════════════════╗
║                                                                  ║
║   ☕  Coffee Shop MacBook                            ═══════    ║
║                                                          ╔══╗   ║
║   $ bto connect office-213                          [1]  ║▓▓║   ║
║     ✓ P2P tunnel established                            ╚══╝   ║
║     ✓ Forwarding localhost:2222 → office-213:22              ║
║                                                               ║
║   $ ssh -p 2222 dev@localhost                                ║
║   dev@office-213:~$ claude                                    ║
║                                                               ║
║   ╭──────────────────────────────────────────────────╮        ║
║   │                                                    │        ║
║   │   Welcome to Claude Code!                          │        ║
║   │                                                    │        ║
║   │   Model:    claude-sonnet-4-6                      │        ║
║   │   cwd:     /home/dev/projects/llm-inference        │        ║
║   │   tools:   Read, Edit, Bash, Grep, Glob, ...       │        ║
║   │                                                    │        ║
║   │   Running on remote GPU machine 🚀                 │        ║
║   │                                                    │        ║
║   ╰──────────────────────────────────────────────────╯        ║
║                                                                  ║
║   ─── via relay.bto.asia ───────────── Office (behind NAT) ─── ║
║                                          Corporate Firewall 🔒 ║
║                                          $ bto daemon            ║
║                                            ✓ Registered          ║
║                                            ✓ Awaiting peers      ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
```

# BTO — Back To Office

**SSH into your office machine from anywhere. One command. No VPN. No public IP.**

Connect to your office GPU rig from a coffee shop and run Claude Code, VS Code Remote, or any SSH workflow — through NAT, firewalls, and corporate networks.

[![Release](https://img.shields.io/github/v/release/hbliu007/back-to-office?style=flat-square&logo=github)](https://github.com/hbliu007/back-to-office/releases/latest)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square&logo=c%2B%2B)]()
[![Binary ~1MB](https://img.shields.io/badge/Binary-~1MB-green?style=flat-square)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)]()
[![Stars](https://img.shields.io/github/stars/hbliu007/back-to-office?style=flat-square&logo=github)]()

[Get Started](#get-started-in-30-seconds) · [How It Works](#how-it-works) · [Comparison](#how-it-compares) · [Mobile](#mobile--ssh-from-your-phone)

</div>

---

<p align="center">
  <img src="docs/images/architecture.svg" alt="BTO Architecture — P2P SSH Tunneling via PeerLink" width="100%">
</p>

## Why BTO

You're at a coffee shop. Your beefy GPU machine — the one running your models, your data, your entire dev environment — sits behind a corporate firewall at the office.

**Your options today:**

- **VPN** — File a ticket, wait days, deal with split tunneling and dropped connections
- **FRP / Ngrok** — Rent a public server, configure port forwarding, manage access tokens
- **Tailscale** — Install on every device, depend on third-party DERP relay servers, pay for teams

Or you could just type **one command**.

```console
$ bto connect office-213
  ✓ Connected via P2P tunnel
  ✓ Forwarding localhost:2222 → office-213:22
```

No VPN. No public server. No firewall rules. No third-party account.

**~1,100 lines of C++. ~1 MB binary. One job, done well.**

> [!TIP]
> **Perfect for AI coding agents** — Use Claude Code, Cursor, or Copilot on your laptop while the heavy lifting runs on your office GPU. BTO gives you the SSH tunnel; the AI does the rest.

## Get Started in 30 Seconds

### 1. Install

```console
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
```

### 2. Office Machine <sub>(run once, keep alive)</sub>

```console
$ bto daemon --did office-213 --relay relay.bto.asia:9700
  ✓ Registered as office-213
  ✓ Listening for incoming connections
```

### 3. Your Laptop <sub>(from anywhere)</sub>

```console
$ bto connect office-213
  ✓ Connected via P2P tunnel
  ✓ Forwarding localhost:2222 → office-213:22

$ ssh -p 2222 dev@127.0.0.1
dev@office-213:~$   # You're in.
```

That's it. Three steps. No config files, no firewall rules, no IT approval.

> [!NOTE]
> **Fuzzy matching** — `bto 213` or `bto office` works too. BTO finds the closest match.

## How It Works

BTO is built on [PeerLink](https://github.com/hbliu007/peerlink), a lightweight P2P networking library. Each device registers with a self-hosted relay using a DID (Decentralized Identifier). The relay brokers the handshake, then devices establish a **direct P2P tunnel** — even through NAT and firewalls.

```
Your Laptop                  Relay Server               Office Machine
(bto connect)             (relay.bto.asia:9700)          (bto daemon)
     │                            │                           │
     │── REGISTER home-001 ──────►│                           │
     │◄── OK ────────────────────│                           │
     │                            │                           │
     │── CONNECT office-213 ────►│                           │
     │                            │── INCOMING home-001 ────►│
     │                            │                           │
     │◄═══════════ P2P Tunnel Established ══════════════════►│
     │                            │                           │
     │── SSH traffic (encrypted) ─────────────────────────────►│
     │◄── SSH traffic (encrypted) ─────────────────────────────│
```

**The relay only brokers the handshake.** Once the P2P tunnel is established, all traffic flows directly between your devices. The relay never sees your data.

<p align="center">
  <img src="docs/images/connection-flow.svg" alt="Connection Flow Diagram" width="100%">
</p>

## How It Compares

| | **BTO** | FRP | Tailscale | SSH + VPN |
|:--|:--:|:--:|:--:|:--:|
| **Setup time** | **30 sec** | ~10 min | ~5 min | Hours |
| **Public server required** | No | Yes | No | Yes |
| **Port forwarding** | No | Yes | No | Yes |
| **Self-hosted** | Fully | Fully | Partially | Fully |
| **3rd-party dependency** | None | None | DERP servers | VPN provider |
| **Binary size** | **~1 MB** | ~15 MB | ~50 MB | N/A |
| **Memory usage** | **< 10 MB** | ~30 MB | ~100 MB | N/A |
| **Mobile support** | Termux | No | App | App |
| **One-line install** | Yes | Partial | Yes | No |

**Choose BTO when you want:**

- **SSH access to office machines** — not a full mesh VPN
- **Zero infrastructure** — no public server, no cloud account, no IT ticket
- **Minimal footprint** — a single ~1 MB binary, no runtime dependencies
- **Full control** — self-host the relay, own your data, audit the code

## Features

| | | |
|:--|:--|:--|
| **Zero Configuration** | **Cross-Platform** | **Secure by Design** |
| Single binary, no dependencies | Linux (x86_64, ARM64) | DID-based identity |
| One-line install via `curl \| sh` | macOS (Apple Silicon, Intel) | Relay-assisted encryption |
| Fuzzy peer name matching | Android (Termux) | No credentials stored on relay |
| TOML config, human-readable | Windows (WSL2, planned) | Self-hosted relay option |

## Usage

```bash
# Connect (the main thing you'll do)
bto office-213              # Fuzzy match — "bto 213" works too
bto connect office-213      # Explicit connect

# Daemon (run on the office machine)
bto daemon --did office-213 --relay relay.bto.asia:9700

# Device management
bto list                    # List configured peers
bto add office-215          # Add a peer
bto remove office-215       # Remove a peer
bto status                  # Connection status
bto ping                    # Test relay connectivity
```

## Mobile — SSH from Your Phone

BTO runs on Android via [Termux](https://termux.dev). SSH into your office from your phone:

```console
$ pkg install curl
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
$ bto 213
  ✓ Connected to office-213
  ✓ Forwarding localhost:2222
```

## Configuration

`~/.bto/config.toml` — simple, human-readable:

```toml
did = "home-mac"
relay = "relay.bto.asia:9700"

[peers.office-213]
  did = "office-213"

[peers.office-215]
  did = "office-215"
```

## Build from Source

<details>
<summary><strong>Requirements: C++20, CMake 3.20+, Boost, spdlog, fmt, protobuf</strong></summary>

```bash
# Build PeerLink first
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

**Docker cross-compile (Linux x86_64 on macOS):**

```bash
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

## Project Stats

| Metric | Value |
|:--|:--|
| Lines of Code | ~1,100 |
| Test Count | 116 |
| Test Coverage | 85.5% |
| Binary Size | ~1 MB |
| Dependencies | [PeerLink](https://github.com/hbliu007/peerlink) only |

## Related

- [PeerLink](https://github.com/hbliu007/peerlink) — The P2P networking library that powers BTO
- [awesome-tunneling](https://github.com/anderspitman/awesome-tunneling) — A curated list of tunneling solutions

## License

[MIT](LICENSE) — free for personal and commercial use.

---

<p align="center">
  <sub>Built with frustration from coffee-shop SSH sessions that never worked.</sub>
</p>
