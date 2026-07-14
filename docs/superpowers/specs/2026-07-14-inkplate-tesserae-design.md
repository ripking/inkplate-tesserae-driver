# Inkplate 6 Color × Tesserae Integration — Design

**Date:** 2026-07-14
**Status:** Approved
**Goal:** Family dashboards on an Inkplate 6 Color e-ink display, rendered and managed by a self-hosted [Tesserae](https://tesserae.ink) server running on Unraid.

## Background

- **Hardware:** Soldered Inkplate 6COLOR — ESP32 with PSRAM, 5.8" ACeP 7-color e-paper, 600×448, LiPo battery support, ~25–30 s full refresh. A larger Inkplate is on order; this design should extend to it with a new board/panel config.
- **Server:** Tesserae (AGPL, `dmellok/tesserae`) composes tile dashboards in a browser, renders them headless (Chromium), dithers against measured panel palettes server-side, packs frames panel-native, and serves them to devices over REST (battery clients) or MQTT.
- **Gap:** The official device firmware (`dmellok/tesserae-device-firmware`) supports Seeed/Waveshare/TRMNL boards only — no Inkplate. Everything else needed already exists:
  - Tesserae's `inky_7colour` gamut is the same ACeP 7-color palette as the Inkplate 6COLOR, in the same index order (0 black, 1 white, 2 green, 3 blue, 4 red, 5 yellow, 6 orange — matching `INKPLATE_*` color constants).
  - The `esp32_bin` renderer emits 4-bpp packed binary: `(w×h)/2` bytes, high nibble = even column, low nibble = odd. For 600×448 that is 134,400 bytes.
  - **No server-side changes are required.** The deliverable is device firmware plus deployment glue.

## Decisions (from brainstorming)

| Question | Decision |
|---|---|
| Server status | Not yet deployed — Unraid deployment is in scope |
| Power | Both: battery deep-sleep **and** USB always-on, selectable per build |
| Upstreaming | Not a goal; standalone local firmware |
| Provisioning | `config.h` edited before flashing |
| Approach | Native Tesserae REST client firmware (PlatformIO + Soldered InkplateLibrary) |

## Architecture

Repository layout (`/Users/openclaw/eink/inkplate`):

```
server/                 Tesserae deployment for Unraid
  docker-compose.yml
  README.md             Unraid setup: appdata path, first run, device + pairing code
firmware/               PlatformIO project (Arduino framework, board: Inkplate 6COLOR)
  platformio.ini
  include/config.h      (gitignored; config.example.h committed)
  src/                  main + modules below
tools/
  tesserae_dryrun.py    Acts as the device over REST; decodes .bin → PNG locally
docs/superpowers/specs/ this document
```

### Server deployment (`server/`)

- `docker-compose.yml` running the official Tesserae image, config/state persisted under `/mnt/user/appdata/tesserae`, UI on port 8765 (LAN only; no external exposure).
- README covers: Unraid Compose Manager (or equivalent) usage, first-run wizard, composing a dashboard, creating the device entry (custom panel, 600×448, gamut `inky_7colour`, renderer `esp32_bin`), and generating the 6-digit pairing code.
- Online features toggle (update checks / anonymous widget counts to `api.tesserae.ink`) left to the user; documented.

### Firmware (`firmware/`)

**Stack:** PlatformIO, Arduino framework, Soldered `InkplateLibrary` (handles ACeP panel init/waveforms/`display()`), `WiFi.h` + `HTTPClient` for REST, `Preferences` (NVS) for persistent state.

**Modules (single responsibility, testable boundaries):**

- `config.h` — Wi-Fi SSID/password, server base URL, device ID, pairing code, `POWER_MODE` (`POWER_BATTERY` / `POWER_USB`), fallback poll interval, panel dimensions.
- `tesserae_client` — the protocol: register/discover, frame GET with `If-None-Match`, frame download, status POST, log POST. Knows nothing about the panel.
- `frame_painter` — unpacks 4-bpp packed buffer → `drawPixel()` with a 7-entry palette lookup table (single place to fix any index mismatch) → `display()`. Knows nothing about HTTP.
- `state_store` — NVS wrapper: bearer token, last ETag, last frame URL, backoff counter. ETag also mirrored in RTC memory for cheap deep-sleep wakes.
- `power` — deep sleep (battery) vs. delay loop (USB); battery voltage/percent readout via Inkplate API.
- `status_screen` — on-panel messages for first-boot/pairing and persistent-failure states only; never overwrites a working dashboard during transient errors.

**Main loop (identical in both power modes; only the sleep primitive differs):**

1. Wake / iterate → connect Wi-Fi (bounded timeout).
2. If no token in NVS: `POST /api/v1/device/register` with `X-Pairing-Code`, MAC, `kind`, `gamut=inky_7colour`, dimensions; store bearer token. On failure show pairing-help screen, retry with backoff.
3. `GET /api/v1/device/<id>/frame` with `Authorization: Bearer <token>` and `If-None-Match: "<etag>"`.
   - `304` → no paint; proceed to status.
   - `204` → no dashboard assigned; sleep fallback interval.
   - `200` → parse JSON envelope, download `.bin` from `url` into PSRAM buffer, validate length == w×h/2, paint, store new ETag.
4. `POST /api/v1/device/<id>/status` with `battery_mv`, `battery_pct`, `rssi`, `ip`, firmware version.
5. Apply `next_poll_s` / `config.sleep_interval_s` from response, clamped to [30 s, 604800 s].
6. Sleep: `POWER_BATTERY` → ESP32 deep sleep (timer wake); `POWER_USB` → millis-based wait, then loop.

**Error handling:**

- Wi-Fi or HTTP failure → exponential backoff (e.g. 1, 2, 4 min… capped at fallback interval), then sleep; never busy-loop on battery.
- Short/invalid frame download → discard buffer, keep the current on-panel image, report via `POST /api/v1/device/<id>/log`.
- Persistent failure (N consecutive cycles) → render a small status notice; keep retrying at fallback interval.
- HTTP `Date` header is the time source (matches reference firmware); no SNTP.

### Dry-run tool (`tools/tesserae_dryrun.py`)

Python script that performs the exact device flow against the real server — register with pairing code, fetch frame, download `.bin`, decode 4-bpp nibbles through the same 7-color palette, write `frame.png`. Proves server config, auth, and byte format end-to-end before any hardware is flashed; doubles as a debugging tool later.

## Verification plan

1. **Server:** container healthy on Unraid; dashboard composes; `/preview/<id>.png` renders.
2. **Protocol dry-run:** `tesserae_dryrun.py` produces a correct PNG from the packed frame — validates registration, token auth, ETag/304 behavior, and palette order with zero hardware risk.
3. **On-device bring-up:** `pio run -t upload`, serial monitor through register → fetch → paint; device appears in Tesserae UI with battery/RSSI telemetry; verify `304` fast-path and measured deep-sleep behavior.
4. **Palette sanity:** dashboard with all 7 colors; confirm on-panel mapping (fix = one lookup table in `frame_painter`).
5. **Both power modes:** battery build survives a multi-hour soak (frames update on schedule, telemetry sane); USB build refreshes at a short interval without leaks/hangs (monitor free heap in logs).

## Out of scope (explicit)

- Upstreaming to `tesserae-device-firmware`.
- Captive-portal provisioning (possible later; `config.h` now).
- MQTT transport (REST covers both power modes; revisit only if push latency ever matters).
- Larger Inkplate support (design keeps panel specifics isolated in `frame_painter`/`config.h` so it's a follow-up, not a rewrite).

## Risks

- **Palette index mismatch** between Tesserae's `inky_7colour` packing and Inkplate's color constants → contained in one lookup table; caught by verification step 4.
- **Server-side device kind/renderer selection**: docs say custom panels pick `custom` + dimensions; if the UI couples `inky_7colour`/`esp32_bin` to specific device kinds, we may need to register with a `kind` the server accepts (e.g. the generic/custom kind). The dry-run tool surfaces this before flashing.
- **134 KB frame buffer** — comfortably fits Inkplate's PSRAM; streaming decode is a fallback if allocation ever fails.
