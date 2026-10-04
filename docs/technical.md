# Technical reference

Background for anyone modifying the firmware. If you just want to get a
display running, start with the [main README](../README.md).

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

## The wake cycle

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
   - `401`/`403`: clear the stored pairing and re-register on the next cycle.
4. **Report status.** Sends `POST /api/v1/device/<id>/status` with battery,
   RSSI, IP and firmware version. The reply's `next_poll_s` sets the sleep
   time, clamped to 30 s – 7 days.
5. **Sleep.** Deep sleep in `POWER_BATTERY` mode, a wait loop in `POWER_USB`
   mode.

## Error handling

- Wi-Fi and HTTP failures back off exponentially (60 s, 120 s, 240 s…),
  capped at `FALLBACK_POLL_S`.
- Transient failures leave the last dashboard on screen.
- After 5 consecutive failures, the panel shows a status screen explaining
  what's wrong. A brand-new device that fails to pair shows the pairing help
  screen immediately.
- A failed or wrong-length frame download keeps the old image and is reported
  to the server via `POST /api/v1/device/<id>/log`.

## Frame format

Tesserae's `esp32_bin` renderer with the `inky_7colour` gamut produces
row-major 4-bit indices: high nibble = even column, low nibble = odd column,
and each row padded to a whole byte. A 600×448 frame is 134,400 bytes.

| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| Color | black | white | yellow | red | blue | green | orange |

> **Note:** this order is **not** the same as the Inkplate library's
> `INKPLATE_*` color constants. Indices 2–5 differ. The order above was
> checked byte-for-byte against Tesserae's calibration swatches. The lookup
> table lives in `firmware/src/frame_painter.cpp` and is mirrored in
> `tools/tesserae_dryrun.py`.

## Supporting another Inkplate

Panel-specific code is limited to `firmware/include/config.h` (dimensions)
and `firmware/src/frame_painter.cpp` (palette), plus the board env in
`platformio.ini`.

## Tests

```sh
cd firmware && pio test -e native        # tesscore unit tests, run on your computer
.venv/bin/python -m pytest tools/        # dry-run decoder and state tests
```

## Dry-run tool

`tools/tesserae_dryrun.py` acts as the device over the same REST protocol.
Subcommands:

- `register --server URL --code CODE`: pair as a fake device.
- `fetch [--force]`: download the frame and write `tools/frame.png`.
  `--force` ignores the cached ETag.
- `status`: post fake telemetry.
- `cycle`: `fetch` followed by `status`.
