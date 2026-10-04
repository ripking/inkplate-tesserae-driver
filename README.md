# Inkplate × Tesserae

Firmware that turns a [Soldered Inkplate](https://soldered.com/inkplate/)
e-ink display into a [Tesserae](https://tesserae.ink) dashboard client.

Tesserae is a self-hosted server that lets you lay out tile dashboards
(calendar, weather, photos and so on) in a browser. It renders them
headlessly, dithers them for your panel's palette, and serves finished
frames to devices. Its official firmware supports Seeed, Waveshare and
TRMNL boards, but not Inkplate. This project fills that gap: it speaks
Tesserae's device REST protocol natively, so **no server changes or
plugins are needed**.

```
┌───────────────────────┐   register / GET frame (ETag) / POST status   ┌─────────────────────┐
│  Tesserae server      │ ◄──────────────────────────────────────────── │  Inkplate 6COLOR    │
│  (Docker, LAN)        │ ────────────────────────────────────────────► │  (this firmware)    │
│  compose · render ·   │      4-bpp packed frame, 600×448, 134,400 B   │  unpack → paint →   │
│  dither · pack        │                                               │  deep sleep         │
└───────────────────────┘                                               └─────────────────────┘
```

## Features

- **Native Tesserae client.** Pairs with a 6-digit code, then fetches
  frames with a bearer token.
- **Repaints only when needed.** Frames are fetched with `If-None-Match`,
  so an unchanged dashboard costs one small request (`304`) and no panel
  refresh.
- **Two power modes, chosen at build time.**
  - `POWER_BATTERY`: ESP32 deep sleep between polls, so a LiPo lasts weeks.
  - `POWER_USB`: stays awake with the serial console up, for short
    intervals and bring-up.
- **The server sets the schedule.** The poll interval comes from the
  device's settings in the Tesserae UI (1 minute to 1 day) and is clamped
  to 30 s – 7 days.
- **Telemetry.** Battery voltage and percent, Wi-Fi RSSI, IP address and
  firmware version are reported on every cycle and shown in the Tesserae UI.
- **Handles failures without blanking the screen.**
  - Transient Wi-Fi or HTTP errors keep the last dashboard on screen and
    back off exponentially (1 → 2 → 4 min…, capped).
  - After 5 consecutive failures, the panel shows a short status screen
    explaining what's wrong.
  - If the server revokes the token, the device re-pairs automatically.
- **Hardware-free testing.**
  - `tools/tesserae_dryrun.py` acts as the device and decodes frames to PNG.
  - The pure logic (unpacking, backoff, clamping) has host-side unit tests.

## Hardware

| | |
|---|---|
| Board | Soldered **Inkplate 6COLOR** (ESP32 + PSRAM) |
| Panel | 5.8" ACeP 7-color e-paper, 600×448 |
| Refresh | ~25–30 s full refresh (normal for ACeP) |
| Power | USB-C, or a LiPo on the onboard JST connector |

Panel-specific code lives only in `firmware/include/config.h` (dimensions)
and `firmware/src/frame_painter.cpp` (palette). Supporting a larger
Inkplate means changing those two files, not rewriting the firmware.

## Repository layout

```
firmware/                   PlatformIO project (Arduino framework)
  platformio.ini            envs: inkplate6color (device), native (unit tests)
  include/config.example.h  copy to config.h (gitignored) and fill in
  src/main.cpp              wake cycle: Wi-Fi → pair → fetch → paint → status → sleep
  src/tesserae_client.*     REST protocol; knows nothing about the panel
  src/frame_painter.*       4-bpp frame → Inkplate framebuffer; holds the palette LUT
  src/state_store.*         NVS persistence: token, ETag, failure counter
  src/status_screen.*       on-panel messages for pairing and persistent errors
  lib/tesscore/             pure logic shared with native tests (no Arduino deps)
  test/test_core/           Unity tests for tesscore
server/                     Docker Compose deployment for Tesserae (Unraid example)
tools/                      tesserae_dryrun.py: device emulator + frame decoder
docs/                       bring-up checklist, design spec, implementation plan
```

## Quick start

Before you start you'll need:

- A machine that can run Docker (for the server).
- [PlatformIO](https://platformio.org/install) (CLI or VS Code extension).
- Python 3.10+ (only for the dry-run tool).

### 1. Run the Tesserae server

```sh
mkdir -p tesserae/data && cp server/docker-compose.yml tesserae/
cd tesserae && docker compose up -d
```

1. Open `http://<server-ip>:<port>` and finish the onboarding wizard.
2. Create a dashboard page with at least one widget.
3. Generate a pairing code under **Settings → Devices → Pair new device**.

The included compose file is set up for a specific Unraid host. Before
using it, edit the volume path, the port (it uses 8766 because 8765 was
taken on that host) and `TESSERAE_HOST_IP` (your server's LAN IP).
[`server/README.md`](server/README.md) explains each setting. Keep the
server on your LAN; don't expose it to the internet.

### 2. (Optional) Test the protocol without hardware

```sh
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/tesserae_dryrun.py register --server http://<server-ip>:<port> --code <6-digit-code>
# In the Tesserae UI, assign your page to the new "Dry-run Inkplate" device, then:
.venv/bin/python tools/tesserae_dryrun.py cycle
```

`cycle` downloads the frame, checks that it is exactly 134,400 bytes, and
writes `tools/frame.png`. If that PNG looks like your dashboard with the
right colors, the server side is working. Subcommands: `register`, `fetch`
(`--force` ignores the cached ETag), `status`, `cycle`.

### 3. Configure and flash the firmware

```sh
cp firmware/include/config.example.h firmware/include/config.h
```

Edit `config.h`:

| Setting | Meaning |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | 2.4 GHz network credentials |
| `TESSERAE_BASE_URL` | e.g. `http://192.168.1.50:8765` (no trailing slash) |
| `DEVICE_ID` / `DEVICE_NAME` | unique per panel, e.g. `inkplate6c_kitchen` |
| `PAIRING_CODE` | fresh 6-digit code from the Tesserae UI (codes are single-use) |
| `POWER_MODE` | start with `POWER_USB` for bring-up, switch to `POWER_BATTERY` later |
| `FALLBACK_POLL_S` | poll interval until the server supplies one; also the backoff ceiling |

Connect the Inkplate over USB-C, then:

```sh
cd firmware
pio run -e inkplate6color -t upload
pio device monitor              # watch: register → fetch → "painting 600x448 frame"
```

On first boot the device registers and appears in the Tesserae UI.
Assign a page to it; the next poll paints the dashboard.

For the full staged checklist (server, dry-run, device, palette check,
battery soak), see [`docs/bringup.md`](docs/bringup.md).

## How it works

Every wake runs one cycle (`firmware/src/main.cpp`):

1. **Connect to Wi-Fi**, with a 20 s timeout.
2. **Pair, if there's no saved token.** Sends `POST /api/v1/device/register`
   with the `X-Pairing-Code` header and stores the returned bearer token in
   NVS.
3. **Fetch the frame.** Sends `GET /api/v1/device/<id>/frame` with
   `If-None-Match`:
   - `304`: nothing changed; skip painting.
   - `204`: no page is assigned yet.
   - `200`: download the `.bin` from the envelope URL into PSRAM, check its
     length, paint it and save the new ETag.
   - `401`/`403`: clear the stored pairing and re-register on the next
     cycle.
4. **Report status.** Sends `POST /api/v1/device/<id>/status` with battery,
   RSSI, IP and firmware version. The reply's `next_poll_s` sets the sleep
   time.
5. **Sleep.** Deep sleep in battery mode, a wait loop in USB mode.

### Frame format

Tesserae's `esp32_bin` renderer with the `inky_7colour` gamut produces
row-major 4-bit indices: high nibble = even column, low nibble = odd column,
and each row padded to a whole byte. The palette index order is:

| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| Color | black | white | yellow | red | blue | green | orange |

> **Note:** this order is **not** the same as the Inkplate library's
> `INKPLATE_*` color constants. Indices 2–5 differ, and the design spec
> originally got this wrong. The order above was checked byte-for-byte
> against Tesserae's calibration swatches. The lookup table lives in
> `firmware/src/frame_painter.cpp` and is mirrored in
> `tools/tesserae_dryrun.py`.

## Testing

```sh
cd firmware && pio test -e native                   # tesscore unit tests (host)
.venv/bin/python -m pytest tools/                   # dry-run decoder/state tests
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| Panel shows **Pairing failed** | Pairing codes are single-use. Generate a fresh one, update `PAIRING_CODE` and reflash. |
| **Wi-Fi unreachable** screen | Check the SSID and password; the ESP32 only supports 2.4 GHz. |
| **Server unreachable** screen | Check `TESSERAE_BASE_URL` and the port, and that the device and server are on the same LAN. |
| Colors are wrong or swapped | Palette order; render a 7-swatch test page and adjust `kPalette` in `frame_painter.cpp`. |
| `frame is N bytes, expected 134400` | The device was registered with the wrong dimensions. Delete it in the UI and re-register. |
| Frame URLs point at `172.x.x.x` | Set `TESSERAE_HOST_IP` in the compose file (needed with Docker bridge networking). |
| Dashboard paints but the UI shows no telemetry | Look for 401/403 responses to `POST /status` under **Settings → Events**. |
| Upload fails or stalls | `upload_speed` is already lowered to 460800; try a different USB-C cable or port. |

## Status

Working on an Inkplate 6COLOR against a self-hosted Tesserae instance.
Firmware version `0.1.0`. Support for a larger Inkplate is planned.

## Acknowledgements

- [Tesserae](https://tesserae.ink) by [dmellok](https://github.com/dmellok/tesserae):
  the server that does the actual dashboard work.
- [Soldered InkplateLibrary](https://github.com/SolderedElectronics/Inkplate-Arduino-library):
  panel drivers.
- [ArduinoJson](https://arduinojson.org/).
