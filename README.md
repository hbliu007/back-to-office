<p align="center">
  <img src="assets/github-hero.svg" alt="BTO hero banner" width="100%">
</p>

<h1 align="center">BTO (Back To Office)</h1>

<p align="center">
  <strong>Remote SSH access to your office machine, without VPN tickets or inbound port forwarding.</strong>
</p>

<p align="center">
  <a href="https://github.com/hbliu007/back-to-office/releases/latest">
    <img src="https://img.shields.io/github/v/release/hbliu007/back-to-office?style=flat-square&logo=github&label=release" alt="Latest release">
  </a>
  <a href="https://github.com/hbliu007/back-to-office/releases/latest">
    <img src="https://img.shields.io/badge/downloads-GitHub%20Releases-1f6feb?style=flat-square&logo=github" alt="Downloads">
  </a>
  <img src="https://img.shields.io/badge/install-bto%20%2B%20peerlinkd-2ea043?style=flat-square" alt="Installs bto and peerlinkd">
  <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux-0a3069?style=flat-square" alt="macOS and Linux">
  <img src="https://img.shields.io/badge/license-MIT-f2cc60?style=flat-square" alt="MIT license">
</p>

<p align="center">
  <a href="#install-in-30-seconds">Install</a> ·
  <a href="https://github.com/hbliu007/back-to-office/releases/latest">Downloads</a> ·
  <a href="#your-first-connection">First Connection</a> ·
  <a href="#trust-and-safety">Trust &amp; Safety</a> ·
  <a href="SECURITY.md">Security</a>
</p>

> This GitHub repository is intentionally product-only. It is the public install surface for BTO, not the full development codebase.

## What BTO Is

- One small CLI for reaching an office or lab machine over SSH from anywhere.
- Built for people who need remote shell access, not a full mesh VPN or admin platform.
- Designed to be understood in one screen and installed in one command.

## Install in 30 Seconds

```console
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
```

What the installer does:

- Downloads the latest release from GitHub Releases
- Installs both `bto` and `peerlinkd`
- Verifies `checksums.sha256` when the release provides it
- Defaults to `~/.local/bin` unless `/usr/local/bin` is writable

Prefer a manual download? Use the release assets directly:

| Platform | Asset |
|:--|:--|
| macOS Apple Silicon | `bto-vX.Y.Z-darwin-arm64.tar.gz` |
| macOS Intel | `bto-vX.Y.Z-darwin-x86_64.tar.gz` |
| Linux x86_64 | `bto-vX.Y.Z-linux-x86_64.tar.gz` |
| Linux ARM64 | `bto-vX.Y.Z-linux-arm64.tar.gz` |

## Your First Connection

### 1. Install BTO

```console
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
```

### 2. Add your office machine once

```console
$ bto add office-213 --did office-213 --relay relay.example.com:9700
```

### 3. Connect like it is on your desk

```console
$ bto office-213
```

What happens next:

- BTO resolves the target name or DID
- `peerlinkd` starts if needed
- BTO reuses the local bridge and launches SSH for you

## Why People Choose BTO

| Decision point | BTO | FRP | Tailscale | Traditional VPN |
|:--|:--:|:--:|:--:|:--:|
| Optimized for plain SSH access | `Yes` | `Partial` | `No` | `Partial` |
| One small CLI instead of a platform | `Yes` | `Yes` | `No` | `No` |
| Requires inbound port forwarding | `No` | `Often` | `No` | `Often` |
| Works with your own relay | `Yes` | `Yes` | `Partial` | `Yes` |
| Installs in one command | `Yes` | `Partial` | `Yes` | `Rarely` |

BTO wins when you want the smallest thing that gets you back into your office machine fast.

## Trust and Safety

- The canonical install path is GitHub Releases plus `install.sh`, not a private IP or ad-hoc file share.
- The installer only fetches release assets and validates SHA256 checksums when available.
- Basic installation does not require embedding tokens in `curl | sh` commands.
- The relay is part of the transport path in relay mode, so this repo avoids absolute claims that are hard to verify for every deployment.
- Before production rollout, read [SECURITY.md](SECURITY.md) and review your own relay, logging, and credential policy.

## What This Repo Contains

- `README.md`: the product story and install path
- `install.sh`: the public installer
- `SECURITY.md`: publishing and trust guidance
- `Releases`: the binaries users actually download

## License

MIT. See [LICENSE](LICENSE).
