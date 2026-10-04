# Bring-up checklist

Work top to bottom; each stage gates the next.

## 1. Server (Unraid)

- [ ] Deploy per `server/README.md`; `docker ps` shows `tesserae` healthy.
- [ ] Admin UI reachable at `http://<unraid-ip>:8765`; onboarding done.
- [ ] A dashboard page exists with at least one widget.
- [ ] Pairing code generated (Settings → Devices → Pair new device).

## 2. Protocol dry-run (no hardware at risk)

    # One-time setup from the repo root:
    python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
    .venv/bin/python tools/tesserae_dryrun.py register \
        --server http://<unraid-ip>:8765 --code <6-digit-code>
    # In the UI: assign the dashboard page to the new "Dry-run Inkplate"
    .venv/bin/python tools/tesserae_dryrun.py cycle

- [ ] `register` returns a token (device visible in UI).
- [ ] `cycle` downloads exactly 134400 bytes and writes `tools/frame.png`.
- [ ] `frame.png` looks like the dashboard (correct colors, not garbled —
      this validates the 4-bpp layout and palette order end-to-end).
- [ ] Second `fetch` without changes prints `304 not modified`.
- [ ] Telemetry visible in the UI (battery 88%, RSSI −55 from the fake status).

## 3. Firmware on the Inkplate

- [ ] `cp firmware/include/config.example.h firmware/include/config.h`,
      fill in Wi-Fi, server URL, a fresh pairing code, unique DEVICE_ID.
      Start with `POWER_MODE POWER_USB` for bring-up (serial stays up).
- [ ] Connect Inkplate over USB-C; `cd firmware && pio run -e inkplate6color -t upload`
      (Inkplate 13SPECTRA: `-e inkplate13spectra`; optionally flash
      `-e inkplate13spectra_selftest` first to check colours and orientation
      without Wi-Fi).
- [ ] `pio device monitor`: watch register → fetch → `painting 600x448 frame`
      (13SPECTRA: `painting 1600x1200` or `1200x1600 frame (canvas 1600x1200)`).
- [ ] Panel shows the dashboard (~25–30 s ACeP refresh).
- [ ] Device appears in Tesserae UI with real battery/RSSI/IP.
- [ ] Next cycle logs `frame unchanged (304)` when the dashboard hasn't changed.
- [ ] Edit the dashboard in the UI → next poll repaints.

## 4. Palette sanity

- [ ] Make a test page with 7 color swatches (black/white/green/blue/red/
      yellow/orange widgets or a test image); verify each renders as itself
      on-panel. A mismatch means editing `kPalette` in
      `firmware/src/frame_painter.cpp` — nothing else.

## 5. Battery mode soak

- [ ] Set `POWER_MODE POWER_BATTERY`, set the device's sleep interval in the
      UI (e.g. 30 min), reflash, disconnect USB, run on LiPo.
- [ ] Over a few hours: frames update on schedule, battery_mv in the UI is
      plausible and slowly declining, no error screens.

## Troubleshooting

- `Pairing failed` on panel → stale/typo'd code; codes are one-shot — make a
  fresh one and reflash (or the device re-registers automatically if the
  server recognizes its MAC).
- Garbled colors → palette order; see stage 4.
- `frame is N bytes, expected 134400` in dry-run → the server instance was
  created with wrong panel dimensions; delete the device in the UI and
  re-register.
- Device paints but UI shows no telemetry → check `POST /status` in
  Settings → Events; token issues show as 401/403 in the device log there.
