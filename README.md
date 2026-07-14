# Inkplate × Tesserae

Family dashboards on Inkplate e-ink displays, rendered by a self-hosted
[Tesserae](https://tesserae.ink) server.

| Directory | Purpose |
|---|---|
| `server/` | Tesserae deployment for Unraid (Docker Compose) |
| `firmware/` | PlatformIO firmware for Inkplate 6COLOR — native Tesserae REST client |
| `tools/` | `tesserae_dryrun.py`: exercises the device protocol without hardware |
| `docs/` | Design spec, implementation plan, bring-up checklist |

Current target: **Inkplate 6COLOR** (600×448 ACeP 7-color). The panel-specific
parts are isolated in `firmware/include/config.h` and `frame_painter`, so a
larger Inkplate is a config + palette change, not a rewrite.

Start with `server/README.md`, then `docs/bringup.md`.
