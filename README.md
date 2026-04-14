<p align="center">
  <img src="assets/github-hero.svg" alt="BTO hero banner" width="100%">
</p>

<h1 align="center">BTO (Back To Office)</h1>

<p align="center">
  <strong>Know exactly what this is. Install it fast. Get back to your office machine and keep working.</strong>
</p>

<p align="center">
  <a href="https://github.com/hbliu007/back-to-office/releases/latest">
    <img src="https://img.shields.io/github/v/release/hbliu007/back-to-office?style=flat-square&logo=github&label=release" alt="Latest release">
  </a>
  <a href="https://github.com/hbliu007/back-to-office/releases/latest">
    <img src="https://img.shields.io/badge/downloads-GitHub%20Releases-1f6feb?style=flat-square&logo=github" alt="Downloads">
  </a>
  <img src="https://img.shields.io/badge/install-release%20binaries-2ea043?style=flat-square" alt="Installs release binaries">
  <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux-0a3069?style=flat-square" alt="macOS and Linux">
  <img src="https://img.shields.io/badge/license-MIT-f2cc60?style=flat-square" alt="MIT license">
</p>

<p align="center"><a href="#install-in-30-seconds"><strong>Install Now</strong></a> · <a href="https://bto.asia/register"><strong>Create Account</strong></a> · <a href="https://bto.asia/dashboard">Dashboard</a> · <a href="SECURITY.md">Security</a></p>

> This GitHub repository is intentionally product-only. It is the public install surface for BTO, not the full development codebase.

## What This Is

BTO is a CLI for one very specific job:

**remote SSH access to your office computer through the official BTO service, so you can continue the same work session from somewhere else.**

If your code, logs, GPU, dataset, or AI coding workflow live on the office machine, BTO is the tool that gets you back onto that machine quickly.

## What You Get

- You leave the office but keep working on the same box
- You reopen the same repository and keep the same debugging momentum
- You keep Claude Code, Codex, or plain terminal work moving
- You avoid turning this into a whole VPN project
- You use the official BTO account and device flow instead of manual network setup

## Install in 30 Seconds

```console
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
```

This installer is designed to match the latest GitHub Release assets.

- It downloads the latest release from GitHub Releases
- It installs `bto` and any companion binary bundled in that release
- It verifies SHA256 when a checksum file is published
- It defaults to `~/.local/bin` unless `/usr/local/bin` is writable

As of **April 14, 2026**, I verified that the latest public release page exposes macOS ARM64 and Linux AMD64 assets, and updated the installer to match the current asset naming and checksum naming used there.

## Official Service Flow

1. Install BTO from this repository
2. Create your account at [bto.asia/register](https://bto.asia/register)
3. Open your console at [bto.asia/dashboard](https://bto.asia/dashboard)
4. Get your token / device setup from the official BTO service
5. Connect back to your office machine and continue working

## Before You Install

- GitHub distributes the binary, but **GitHub does not issue user tokens**
- User accounts, tokens, and device onboarding belong to the official BTO service:
  [bto.asia/register](https://bto.asia/register) and [bto.asia/dashboard](https://bto.asia/dashboard)
- Basic installation should never require putting your personal token into a public GitHub command

## Why This Hits Home

<p align="center">
  <img src="assets/storyboard.svg" alt="BTO story from office to remote coding" width="100%">
</p>

You leave the office.

Your code is still on the office machine. Your GPU is there. Your data is there. Your terminal session is there. Sometimes even your AI coding workflow is already open there. Maybe your real goal is just this:

- reopen that machine from home
- continue the same debugging session
- keep Claude Code, Codex, or plain SSH work moving on the same box
- avoid asking IT for VPN access or touching router / firewall settings

That is the emotional job BTO is built for.

Not "networking infrastructure". Not "a full platform". Just getting you back onto your office computer so work continues.

## What BTO Is

- One small CLI for reaching an office or lab machine over SSH from anywhere.
- Built for people who need remote shell access, not a full mesh VPN or admin platform.
- Designed to be understood in one screen and installed in one command.

| Platform | Asset |
|:--|:--|
| macOS Apple Silicon | `bto-vX.Y.Z-darwin-arm64.tar.gz` |
| macOS Intel | `bto-vX.Y.Z-darwin-amd64.tar.gz` or `darwin-x86_64` |
| Linux x86_64 | `bto-vX.Y.Z-linux-amd64.tar.gz` |
| Linux ARM64 | `bto-vX.Y.Z-linux-arm64.tar.gz` |

## Your First Connection

### 1. Install BTO

```console
$ curl -fsSL https://raw.githubusercontent.com/hbliu007/back-to-office/main/install.sh | bash
```

### 2. Create your account and open your dashboard

```console
$ open https://bto.asia/register
$ open https://bto.asia/dashboard
```

### 3. Follow the official onboarding flow

- sign in
- get your token
- register or open your office device
- finish the setup shown in the dashboard

### 4. Connect like it is on your desk

```console
$ bto office-213
```

What happens next:

- BTO uses your configured official BTO account / device context
- `peerlinkd` starts if needed
- BTO reuses the local bridge and launches SSH for you

If your mental model is "I want to get back into the office box and keep coding", you're using BTO the right way.

## The One-Line Pitch

BTO is for the moment when you close your office door, open your laptop somewhere else, and want the same office machine back under your fingertips in one command.

## Why People Choose BTO

| Decision point | BTO | FRP | Tailscale | Traditional VPN |
|:--|:--:|:--:|:--:|:--:|
| Optimized for plain SSH access | `Yes` | `Partial` | `No` | `Partial` |
| One small CLI instead of a platform | `Yes` | `Yes` | `No` | `No` |
| Requires inbound port forwarding | `No` | `Often` | `No` | `Often` |
| Works with your own relay | `Yes` | `Yes` | `Partial` | `Yes` |
| Installs in one command | `Yes` | `Partial` | `Yes` | `Rarely` |

BTO wins when you want the smallest thing that gets you back into your office machine fast.

## Why The Story Matters More Than Specs

The best developer tools on GitHub tend to do three things above the fold:

- show a real screenshot or visual scene
- explain the job in one sentence
- give you one install path immediately

That is why this repository now leads with a visual story and a single install command instead of engineering structure. People do not fall in love with `src/` trees. They fall in love with the feeling of "yes, this solves my exact problem".

## Trust and Safety

- The canonical install path is GitHub Releases plus `install.sh`, not a private IP or ad-hoc file share.
- The installer only fetches release assets and validates SHA256 checksums when available.
- Basic installation does not require embedding tokens in `curl | sh` commands.
- Tokens should come from the official BTO service, not from GitHub.
- The relay is part of the transport path in relay mode, so this repo avoids absolute claims that are hard to verify for every deployment.
- Before production rollout, read [SECURITY.md](SECURITY.md) and review your own relay, logging, and credential policy.

## What This Repo Contains

- `README.md`: the product story and install path
- `install.sh`: the public installer
- `SECURITY.md`: publishing and trust guidance
- `Releases`: the binaries users actually download

## License

MIT. See [LICENSE](LICENSE).
