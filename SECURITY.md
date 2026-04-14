# Security Policy

This repository is the public download surface for BTO. It should stay safe, boring, and easy to audit.

## Supported Releases

Only the latest GitHub Release is considered supported for general users.

## Safe Install Expectations

- Prefer GitHub Releases and the repository `install.sh`.
- Release assets should include both `bto` and `peerlinkd`.
- Releases should publish `checksums.sha256`.
- Public install instructions must not embed per-user tokens in shell commands.
- README and website copy should avoid unverifiable security absolutes.

## Pre-Publish Checklist

Before pushing or publishing a release, verify all of the following:

- No hard-coded admin credentials, bootstrap tokens, or private service URLs are present.
- No runtime databases, WAL files, logs, or local caches are included.
- No release instructions point to private IPs or broken domains.
- `install.sh` still downloads from GitHub Releases and installs the same artifacts the release assets contain.
- Security-sensitive claims in README still match the real transport path and deployment model.

## Reporting

If you find a security issue, please avoid opening a public exploit issue first.

- Open a private security advisory on GitHub if available.
- Or contact the maintainer directly before public disclosure.
