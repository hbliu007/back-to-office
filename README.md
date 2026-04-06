<p align="center">
  <img src="docs/images/architecture.svg" alt="BTO — Back To Office" width="100%">
</p>

<h1 align="center">BTO (Back-To-Office)</h1>

<p align="center">
  <strong>SSH into your office machine from anywhere. One command. No VPN.</strong>
</p>

<p align="center">
  <a href="https://github.com/hbliu007/back-to-office/releases/latest">
    <img src="https://img.shields.io/github/v/release/hbliu007/back-to-office?style=flat-square&logo=github&label=Release" alt="Release">
  </a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square&logo=c%2B%2B" alt="C++20">
  <img src="https://img.shields.io/badge/Binary-~1MB-green?style=flat-square" alt="~1MB">
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=flat-square" alt="License: MIT">
  <a href="https://github.com/hbliu007/back-to-office/stargazers">
    <img src="https://img.shields.io/github/stars/hbliu007/back-to-office?style=flat-square&logo=github" alt="Stars">
  </a>
</p>

<p align="center">
  <a href="#-get-started-in-30-seconds">Get Started</a> · <a href="#-the-problem">Why BTO</a> · <a href="#-how-it-compares">Comparison</a> · <a href="#-how-it-works">How It Works</a> · <a href="docs/">Docs</a>
</p>

---

## The Problem

You're at a coffee shop. You need to SSH into your office dev machine — the one with your GPU, your data, your environment. But it's behind a corporate firewall.

**Your options today:**

- **VPN** — Ask IT, wait days, deal with split tunneling headaches
- **FRP / Ngrok** — Set up a public server, configure port forwarding, manage tokens
- **Tailscale** — Install on every device, depend on third-party DERP relay servers

You just want to type one command and get a shell. That's what BTO does.

## The Solution

```
bto connect office-213
```

That's it. BTO creates a P2P tunnel through NAT and firewalls, gives you a local port, and you SSH through it. No VPN, no port forwarding, no firewall rules, no third-party dependencies.

**~1,100 lines of C++. ~1 MB binary. One job, done well.**

> [!IMPORTANT]
> Similar to [bore](https://github.com/ekzhang/bore) and [frp](https://github.com/fatedier/frp), except BTO is built for one specific use case — **remote SSH access to office machines** — and does it with P2P tunnels instead of requiring a public relay server. That's all it does: no more, and no less.

## Get Started in 30 Seconds

### Install

```console
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
```

### Office Machine <sub>(run once, keep alive)</sub>

```console
$ bto daemon --did office-213 --relay relay.bto.asia:9700
  ✓ Registered as office-213
  ✓ Listening for incoming connections
```

### Your Laptop <sub>(from anywhere)</sub>

```console
$ bto connect office-213
  ✓ Connected via P2P tunnel
  ✓ Listening on localhost:2222

$ ssh -p 2222 user@127.0.0.1
user@office-213:~$   # You're in.
```

> [!TIP]
> **Fuzzy matching** — `bto 213` or `bto office` works too.

## How It Compares

| | BTO | FRP | Tailscale | SSH + VPN |
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

**When to choose BTO over alternatives:**

- You want **SSH access to office machines**, not a full mesh VPN
- You want **zero infrastructure** — no public server, no cloud account, no IT approval
- You want **minimal footprint** — a single ~1 MB binary with no dependencies
- You want **full control** — self-host the relay, own your data

## How It Works

BTO is built on [PeerLink](https://github.com/hbliu007/peerlink), a P2P networking library. Each device registers a DID (Decentralized Identifier) with a lightweight relay. The relay helps devices discover each other and establish direct P2P tunnels — even behind NAT.

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

The relay only brokers the initial handshake. Once the P2P tunnel is established, **all traffic flows directly between your devices** — the relay never sees your data.

<p align="center">
  <img src="docs/images/connection-flow.svg" alt="Connection Flow" width="100%">
</p>

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
  ✓ Listening on localhost:2222
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
