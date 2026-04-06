# BTO (Back-To-Office)

> One command to SSH into your office from anywhere — zero VPN, self-hosted relay, mobile ready.

```
笔记本 (咖啡厅)             P2P Relay              办公室 (NAT 后)
┌──────────────┐         ┌──────────┐         ┌──────────────┐
│ bto 213      │◄──────►│  Relay   │◄──────►│ bto daemon   │
│              │  P2P    │  Server  │  P2P    │  sshd:22     │
└──────────────┘         └──────────┘         └──────────────┘
       ↑                                             ↑
  ssh -p 2222                                   office-213
  user@127.0.0.1                               (DID registered)
```

## Install

```bash
curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
```

Specific version:
```bash
curl -fsSL ... | bash -s -- --version v1.1.0
```

### Supported Platforms

| Platform | Architecture |
|----------|-------------|
| Linux    | x86_64, ARM64 |
| macOS    | Apple Silicon (M1+), Intel |
| Android  | Termux (ARM64) |

## Quick Start

```bash
# 1. Add your office machine
bto add office-213 --did office-213

# 2. Connect
bto connect office-213

# 3. SSH in (another terminal)
ssh -p 2222 user@127.0.0.1
```

That's it. No VPN, no port forwarding, no firewall rules.

## How It Works

BTO builds on the [PeerLink P2P platform](https://github.com/hbliu007/peerlink). Each device registers a DID (Decentralized Identifier) with a relay server. Devices find each other through the relay and establish direct P2P tunnels — even behind NAT.

```
bto connect office-213
  → Register local DID with relay
  → Look up office-213 via relay
  → Establish P2P tunnel
  → Listen on local port 2222
  → Forward SSH traffic through the tunnel
```

## Usage

```bash
bto <peer>                    # Connect (shortcut)
bto connect <peer>            # Connect to a remote device
bto daemon --did <name>       # Run as daemon (accept incoming)
bto list                      # List configured devices
bto add <name> [--did <d>]    # Add a device
bto remove <name>             # Remove a device
bto status                    # Show connection status
bto config                    # Show configuration
bto ping                      # Test relay connectivity
bto help [command]            # Show help
```

### Fuzzy Matching

```bash
bto 213          # matches office-213
bto office       # matches office-213 (if unique)
```

### Multi-Session

Every SSH connection creates an independent P2P tunnel:

```bash
# Terminal 1
ssh -p 2222 user@127.0.0.1

# Terminal 2 (concurrent, independent tunnel)
ssh -p 2222 user@127.0.0.1
```

## Configuration

Config file: `~/.bto/config.toml`

```toml
did = "home-mac"
relay = "relay.bto.asia:9700"

[peers.office-213]
  did = "office-213"

[peers.office-215]
  did = "office-215"
```

## Build from Source

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

## Mobile (Termux)

BTO works on Android via Termux:

```bash
pkg install curl
curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
bto 213    # SSH into office from your phone
```

## Architecture

- **~1100 lines** of C++20
- **116 tests**, 85.5% coverage
- **4 modules**: CLI parser, config manager, P2P bridge, main dispatcher
- Built on PeerLink P2P (relay, signaling, DID, STUN)

## License

MIT
