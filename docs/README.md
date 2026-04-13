# BTO Maintainer Docs

This folder is for maintainers, contributors, and release engineers.

If you only want to install BTO, go back to the product-facing [README](../README.md).

## Start Here

| Document | Purpose |
|------|------|
| [build-guide.md](build-guide.md) | Build, test, and coverage workflow |
| [cli-reference.md](cli-reference.md) | Command reference and current UX |
| [config-reference.md](config-reference.md) | Config file fields and examples |
| [architecture.md](architecture.md) | Runtime architecture and daemon model |
| [data-flow.md](data-flow.md) | Session lifecycle and data path |

## Working Rules For This Folder

- Keep user-facing install steps in [../README.md](../README.md), not here.
- Keep absolute deployment details and internal IPs out of tracked docs.
- Treat diagrams and architecture notes as maintainer material, not homepage material.
- Before publishing, re-read [../SECURITY.md](../SECURITY.md).
