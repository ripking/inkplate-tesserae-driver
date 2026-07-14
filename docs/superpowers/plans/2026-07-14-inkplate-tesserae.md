# Inkplate 6 Color × Tesserae Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Family dashboards on an Inkplate 6 Color, rendered by a self-hosted Tesserae server on Unraid, via custom PlatformIO firmware speaking Tesserae's device REST protocol.

**Architecture:** Tesserae (Docker, Unraid) renders dashboards server-side and serves panel-native 4-bpp packed frames over REST. The firmware (Arduino framework + Soldered InkplateLibrary) registers with a pairing code, fetches frames with ETag caching, paints via a palette LUT, posts telemetry, and sleeps on a server-controlled interval — deep sleep on battery, wait-loop on USB. A Python dry-run tool exercises the full protocol without hardware.

**Tech Stack:** Docker Compose (Tesserae `ghcr.io/dmellok/tesserae`), PlatformIO + espressif32/Arduino, `e-radionicacom/InkplateLibrary@^11.1.2`, `bblanchon/ArduinoJson@^7`, PlatformIO Unity tests (native env), Python 3 + requests + Pillow + pytest.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-14-inkplate-tesserae-design.md` — read it before starting.
- Panel: 600×448, 7-color ACeP. Packed frame = 600×448/2 = **134400 bytes**, no header.
- Tesserae gamut: `inky_7colour`; palette index order 0=black, 1=white, 2=green, 3=blue, 4=red, 5=yellow, 6=orange — identical to `INKPLATE_BLACK..INKPLATE_ORANGE` (0..6).
- Register manifest `kind` MUST be `"esp32_client"` (server plugin providing the `esp32_bin` renderer).
- Sleep interval clamp: 30 ≤ s ≤ 604800 (client-side too).
- `firmware/include/config.h` is gitignored; `config.example.h` is committed. Never commit real Wi-Fi credentials.
- Build flag `-DARDUINO_INKPLATECOLOR` selects the 6COLOR driver in InkplateLibrary.
- Auth: `Authorization: Bearer <token>` on `/api/v1/device/<id>/*`; frame downloads (`/renders/...`) need no auth.
- Commit after every task; commit messages end with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

## Protocol reference (from `docs/dev/client-protocol.md` upstream)

- `POST /api/v1/device/register`, header `X-Pairing-Code: <6 digits>`, JSON body `{device_id, kind, panel_w, panel_h, gamut, name, fw_version, mac}` → `201` `{device_token, config:{sleep_interval_s}, reused_existing}`. `200` + `reused_existing: true` if device_id exists.
- `GET /api/v1/device/<id>/frame` with `If-None-Match: "<etag>"` → `200` JSON envelope `{url, format:"bin", panel_w, panel_h, render_id, renderer_id}` + `ETag` header; `304` no new frame; `204` no dashboard assigned yet.
- `GET <url>` (e.g. `/renders/<sha256>.bin`, no auth) → raw packed bytes.
- 4-bpp layout: row 0 first, left→right; high nibble = even column, low nibble = odd.
- `POST /api/v1/device/<id>/status` `{battery_mv, battery_pct, rssi, ip, next_sleep_s}` → `{config:{sleep_interval_s}, next_poll_s, server_time, ...}` (server derives battery_pct from mV if omitted: 3300 mV = 0%, 4200 mV = 100%).
- `POST /api/v1/device/<id>/log` `{level, msg}` → Events tab.

---

### Task 1: Repo scaffolding + Unraid server deployment

**Files:**
- Create: `.gitignore`
- Create: `README.md`
- Create: `server/docker-compose.yml`
- Create: `server/README.md`

**Interfaces:**
- Produces: a deployable Tesserae compose file; later tasks assume the server base URL looks like `http://<unraid-ip>:8765`.

- [ ] **Step 1: Create `.gitignore`**

```gitignore
# Python
__pycache__/
*.pyc
.venv/
tools/.tesserae_state.json
tools/frame.png

# PlatformIO
firmware/.pio/
firmware/include/config.h

# OS
.DS_Store
```

- [ ] **Step 2: Create top-level `README.md`**

```markdown
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
```

- [ ] **Step 3: Create `server/docker-compose.yml`**

Adapted from the official example (host networking is correct on Unraid — it makes render URLs use the host's LAN IP and lets mDNS work):

```yaml
# Tesserae on Unraid.
#
#   cd /mnt/user/appdata/tesserae && docker compose up -d
#
# Admin UI: http://<unraid-ip>:8765 — first request walks through
# password setup and onboarding.
#
# After upgrades work, pin `latest` to a specific tag so
# `docker compose pull && docker compose up -d` is deliberate.

services:
  tesserae:
    image: ghcr.io/dmellok/tesserae:latest
    container_name: tesserae
    restart: unless-stopped

    # Host networking (Linux/Unraid): Tesserae binds 8765 on the host
    # directly, frame URLs point at the real LAN IP, and the built-in
    # MQTT broker (1883) is reachable without a ports: block.
    network_mode: host

    volumes:
      # Everything Tesserae persists: settings, pages, schedules,
      # render cache, backups. Lives on the array; back it up.
      - /mnt/user/appdata/tesserae/data:/app/data

    environment:
      TESSERAE_IN_DOCKER: "1"
```

- [ ] **Step 4: Create `server/README.md`**

```markdown
# Tesserae on Unraid

## Install

Option A — **Compose Manager plugin** (recommended):
1. Apps → install "Compose.Manager" if not present.
2. Add a new stack named `tesserae`, paste `docker-compose.yml` from this
   directory, set the stack directory to `/mnt/user/appdata/tesserae`.
3. Compose Up.

Option B — SSH:
```sh
mkdir -p /mnt/user/appdata/tesserae
cp docker-compose.yml /mnt/user/appdata/tesserae/
cd /mnt/user/appdata/tesserae && docker compose up -d
```

Then open `http://<unraid-ip>:8765`, set the admin password, and run the
onboarding wizard. Keep it LAN-only; do not reverse-proxy it to the internet.

Privacy note: Settings → System → "Online features" controls the only
outbound calls (update checks + anonymous widget-install counts to
api.tesserae.ink). Off is fine; upgrades then happen via
`docker compose pull`.

## Compose a dashboard

Dashboards are composed in the browser (Pages). Add widgets, arrange tiles.
The preview at `http://<unraid-ip>:8765/preview/<device_id>.png` shows the
last rendered composition once a device exists and a page is assigned.

## Pair the Inkplate

1. Settings → Devices → **Pair new device** → note the 6-digit pairing code.
2. Put the code in `firmware/include/config.h` (`PAIRING_CODE`) — or use
   `tools/tesserae_dryrun.py register` first to validate server-side setup.
3. The device self-describes at registration (`kind: esp32_client`,
   600×448, gamut `inky_7colour`); Tesserae creates the instance and
   returns a bearer token. Assign a page to the new device and set the
   sleep interval under the device's config (presets from 1 min to 1 day).
```

- [ ] **Step 5: Validate compose YAML parses**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('server/docker-compose.yml')); print('OK')"`
(If PyYAML is missing: `python3 -m pip install --user pyyaml`, or validate with `docker compose -f server/docker-compose.yml config -q` if Docker is installed locally.)
Expected: `OK`

- [ ] **Step 6: Commit**

```bash
git add .gitignore README.md server/
git commit -m "feat: repo scaffolding + Tesserae Unraid deployment"
```

---

### Task 2: Dry-run tool — 4-bpp decoder (TDD)

**Files:**
- Create: `tools/requirements.txt`
- Create: `tools/tesserae_dryrun.py` (decoder + palette only in this task)
- Test: `tools/test_dryrun.py`

**Interfaces:**
- Produces: `PALETTE: list[tuple[int,int,int]]` (7 RGB entries, index order = Tesserae `inky_7colour` = Inkplate constants), `decode_bin(data: bytes, w: int, h: int) -> "PIL.Image.Image"` raising `ValueError` on wrong length. Task 3 builds the CLI around these.

- [ ] **Step 1: Create `tools/requirements.txt`**

```
requests>=2.31
Pillow>=10.0
pytest>=8.0
```

- [ ] **Step 2: Create venv and install**

```bash
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
```

- [ ] **Step 3: Write the failing test — `tools/test_dryrun.py`**

```python
import pytest
from tesserae_dryrun import PALETTE, decode_bin


def test_palette_has_seven_colors_in_tesserae_order():
    assert len(PALETTE) == 7
    assert PALETTE[0] == (0, 0, 0)        # black
    assert PALETTE[1] == (255, 255, 255)  # white
    assert PALETTE[4] == (255, 0, 0)      # red


def test_decode_bin_unpacks_high_nibble_even_column():
    # 4x2 frame: rows [0,1,2,3] and [4,5,6,1] packed two pixels/byte,
    # high nibble = even column.
    data = bytes([0x01, 0x23, 0x45, 0x61])
    img = decode_bin(data, 4, 2)
    assert img.size == (4, 2)
    assert img.getpixel((0, 0)) == PALETTE[0]
    assert img.getpixel((1, 0)) == PALETTE[1]
    assert img.getpixel((2, 0)) == PALETTE[2]
    assert img.getpixel((3, 0)) == PALETTE[3]
    assert img.getpixel((0, 1)) == PALETTE[4]
    assert img.getpixel((3, 1)) == PALETTE[1]


def test_decode_bin_rejects_wrong_length():
    with pytest.raises(ValueError):
        decode_bin(b"\x00\x00", 4, 2)  # needs 4 bytes


def test_decode_bin_out_of_palette_index_maps_white():
    img = decode_bin(bytes([0xF0]), 2, 1)  # nibble 0xF is undefined
    assert img.getpixel((0, 0)) == PALETTE[1]
```

- [ ] **Step 4: Run test to verify it fails**

Run: `cd tools && ../.venv/bin/pytest test_dryrun.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'tesserae_dryrun'`

- [ ] **Step 5: Write minimal implementation — `tools/tesserae_dryrun.py`**

```python
#!/usr/bin/env python3
"""Act as a Tesserae device without hardware.

Speaks the same REST protocol as the Inkplate firmware: register with a
pairing code, fetch the packed 4-bpp frame with ETag caching, decode it to
a PNG, and post telemetry. Validates the whole server pipeline before
anything is flashed, and doubles as a debugging tool afterwards.
"""

from PIL import Image

# Tesserae `inky_7colour` gamut order == Inkplate 6COLOR constants 0..6.
PALETTE = [
    (0, 0, 0),        # 0 black
    (255, 255, 255),  # 1 white
    (0, 128, 0),      # 2 green
    (0, 0, 255),      # 3 blue
    (255, 0, 0),      # 4 red
    (255, 255, 0),    # 5 yellow
    (255, 140, 0),    # 6 orange
]


def decode_bin(data: bytes, w: int, h: int) -> Image.Image:
    """Unpack a Tesserae 4-bpp .bin frame (high nibble = even column)."""
    expected = w * h // 2
    if len(data) != expected:
        raise ValueError(f"frame is {len(data)} bytes, expected {expected}")
    img = Image.new("RGB", (w, h))
    px = img.load()
    i = 0
    for y in range(h):
        for x in range(0, w, 2):
            byte = data[i]
            i += 1
            for xx, nibble in ((x, byte >> 4), (x + 1, byte & 0x0F)):
                if xx < w:
                    px[xx, y] = PALETTE[nibble] if nibble < 7 else PALETTE[1]
    return img
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd tools && ../.venv/bin/pytest test_dryrun.py -v`
Expected: 4 PASSED

- [ ] **Step 7: Commit**

```bash
git add tools/
git commit -m "feat: dry-run tool 4-bpp frame decoder with tests"
```

---

### Task 3: Dry-run tool — protocol CLI

**Files:**
- Modify: `tools/tesserae_dryrun.py` (append CLI below decoder)
- Test: `tools/test_dryrun.py` (append state round-trip test)

**Interfaces:**
- Consumes: `decode_bin`, `PALETTE` from Task 2.
- Produces: CLI commands `register`, `fetch`, `status`, `cycle`; state file `tools/.tesserae_state.json` holding `{token, etag, device_id, base_url}`. `load_state(path) -> dict` / `save_state(path, state)`.

- [ ] **Step 1: Write the failing test (append to `tools/test_dryrun.py`)**

```python
def test_state_round_trip(tmp_path):
    from tesserae_dryrun import load_state, save_state
    p = tmp_path / "state.json"
    assert load_state(p) == {}
    save_state(p, {"token": "abc", "etag": '"d1"'})
    assert load_state(p) == {"token": "abc", "etag": '"d1"'}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools && ../.venv/bin/pytest test_dryrun.py::test_state_round_trip -v`
Expected: FAIL — `ImportError: cannot import name 'load_state'`

- [ ] **Step 3: Append CLI implementation to `tools/tesserae_dryrun.py`**

```python
import argparse
import json
import sys
import uuid
from pathlib import Path

import requests

STATE_PATH = Path(__file__).parent / ".tesserae_state.json"
PANEL_W, PANEL_H = 600, 448


def load_state(path=STATE_PATH) -> dict:
    p = Path(path)
    return json.loads(p.read_text()) if p.exists() else {}


def save_state(path, state: dict) -> None:
    Path(path).write_text(json.dumps(state, indent=2))


def _auth(state):
    return {"Authorization": f"Bearer {state['token']}"}


def cmd_register(args):
    state = load_state()
    manifest = {
        "device_id": args.device_id,
        "kind": "esp32_client",
        "panel_w": PANEL_W,
        "panel_h": PANEL_H,
        "gamut": "inky_7colour",
        "name": args.name,
        "fw_version": "dryrun-0.1.0",
        "mac": args.mac or "02:00:%02X:%02X:%02X:%02X" % tuple(uuid.uuid4().bytes[:4]),
    }
    r = requests.post(
        f"{args.server}/api/v1/device/register",
        headers={"X-Pairing-Code": args.code},
        json=manifest,
        timeout=15,
    )
    print(r.status_code, r.text[:500])
    r.raise_for_status()
    body = r.json()
    state.update(
        base_url=args.server,
        device_id=args.device_id,
        token=body["device_token"],
        etag=None,
    )
    save_state(STATE_PATH, state)
    print(f"registered (reused_existing={body.get('reused_existing')}); token saved")


def cmd_fetch(args):
    state = load_state()
    headers = _auth(state)
    if state.get("etag") and not args.force:
        headers["If-None-Match"] = state["etag"]
    url = f"{state['base_url']}/api/v1/device/{state['device_id']}/frame"
    r = requests.get(url, headers=headers, timeout=30)
    print("GET /frame ->", r.status_code)
    if r.status_code == 304:
        print("not modified; use --force to repaint")
        return
    if r.status_code == 204:
        print("no dashboard assigned to this device yet (assign a page in the UI)")
        return
    r.raise_for_status()
    env = r.json()
    print("envelope:", json.dumps(env, indent=2))
    frame = requests.get(env["url"], timeout=30)
    frame.raise_for_status()
    img = decode_bin(frame.content, env.get("panel_w", PANEL_W), env.get("panel_h", PANEL_H))
    out = Path(__file__).parent / "frame.png"
    img.save(out)
    state["etag"] = r.headers.get("ETag")
    save_state(STATE_PATH, state)
    print(f"decoded {len(frame.content)} bytes -> {out} (etag={state['etag']})")


def cmd_status(args):
    state = load_state()
    r = requests.post(
        f"{state['base_url']}/api/v1/device/{state['device_id']}/status",
        headers=_auth(state),
        json={"battery_mv": 4100, "battery_pct": 88, "rssi": -55, "ip": "dryrun"},
        timeout=15,
    )
    r.raise_for_status()
    print(json.dumps(r.json(), indent=2))


def cmd_cycle(args):
    cmd_fetch(args)
    cmd_status(args)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("register", help="pair with the server (creates the device)")
    p.add_argument("--server", required=True, help="e.g. http://192.168.1.50:8765")
    p.add_argument("--code", required=True, help="6-digit pairing code from the UI")
    p.add_argument("--device-id", default="inkplate6c_dryrun")
    p.add_argument("--name", default="Dry-run Inkplate")
    p.add_argument("--mac", default=None)
    p.set_defaults(fn=cmd_register)

    p = sub.add_parser("fetch", help="GET frame, decode to frame.png")
    p.add_argument("--force", action="store_true", help="ignore stored ETag")
    p.set_defaults(fn=cmd_fetch)

    p = sub.add_parser("status", help="POST a fake heartbeat")
    p.set_defaults(fn=cmd_status)

    p = sub.add_parser("cycle", help="fetch + status, like one device wake")
    p.add_argument("--force", action="store_true")
    p.set_defaults(fn=cmd_cycle)

    args = ap.parse_args()
    try:
        args.fn(args)
    except requests.HTTPError as e:
        print(f"HTTP error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run all tests**

Run: `cd tools && ../.venv/bin/pytest test_dryrun.py -v`
Expected: 5 PASSED

- [ ] **Step 5: Smoke-check CLI parses**

Run: `.venv/bin/python tools/tesserae_dryrun.py --help && .venv/bin/python tools/tesserae_dryrun.py register --help`
Expected: usage text for both, exit 0.

- [ ] **Step 6: Commit**

```bash
git add tools/
git commit -m "feat: dry-run CLI — register/fetch/status against real server"
```

---

### Task 4: Firmware scaffold (compiles)

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/include/config.example.h`
- Create: `firmware/src/main.cpp` (placeholder; replaced in Task 9)

**Interfaces:**
- Produces: `config.h` macro names every later firmware task uses verbatim: `WIFI_SSID`, `WIFI_PASSWORD`, `TESSERAE_BASE_URL`, `DEVICE_ID`, `DEVICE_NAME`, `PAIRING_CODE`, `POWER_MODE` (`POWER_BATTERY`/`POWER_USB`), `FALLBACK_POLL_S`, `PANEL_W`, `PANEL_H`, `FW_VERSION`.

- [ ] **Step 1: Ensure PlatformIO CLI is installed**

Run: `pio --version || brew install platformio`
Expected: `PlatformIO Core, version 6.x`

- [ ] **Step 2: Create `firmware/platformio.ini`**

```ini
[env:inkplate6color]
platform = espressif32@6.5.0
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
build_flags =
    -DARDUINO_INKPLATECOLOR
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
lib_deps =
    e-radionicacom/InkplateLibrary@^11.1.2
    bblanchon/ArduinoJson@^7.0.4

; Pure-logic unit tests run on the host, no hardware needed:
;   pio test -e native
[env:native]
platform = native
test_framework = unity
build_flags = -std=c++17
```

- [ ] **Step 3: Create `firmware/include/config.example.h`**

```cpp
// Copy to config.h and fill in. config.h is gitignored.
#pragma once

// ---- Wi-Fi ----
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// ---- Tesserae server ----
#define TESSERAE_BASE_URL "http://192.168.1.50:8765" // no trailing slash
#define DEVICE_ID     "inkplate6c_livingroom"        // unique per panel
#define DEVICE_NAME   "Living Room Inkplate"
#define PAIRING_CODE  "000000" // Settings -> Devices -> Pair new device

// ---- Power ----
// POWER_BATTERY: deep sleep between polls (LiPo, weeks of runtime).
// POWER_USB:     stay awake and wait-loop (short refresh intervals OK).
#define POWER_BATTERY 0
#define POWER_USB     1
#define POWER_MODE    POWER_BATTERY

// Poll interval (seconds) used until the server supplies one, and as the
// ceiling for error backoff.
#define FALLBACK_POLL_S 900

// ---- Panel (Inkplate 6COLOR) ----
#define PANEL_W 600
#define PANEL_H 448

#define FW_VERSION "0.1.0"
```

- [ ] **Step 4: Copy example to real config for local builds**

```bash
cp firmware/include/config.example.h firmware/include/config.h
```

- [ ] **Step 5: Create placeholder `firmware/src/main.cpp`**

```cpp
#include <Inkplate.h>

#include "config.h"

Inkplate display;

void setup() {
    Serial.begin(115200);
    display.begin();
    Serial.printf("inkplate-tesserae %s scaffold; panel %dx%d\n", FW_VERSION,
                  PANEL_W, PANEL_H);
}

void loop() { delay(1000); }
```

- [ ] **Step 6: Verify it compiles**

Run: `cd firmware && pio run -e inkplate6color`
Expected: `SUCCESS` (first run downloads toolchains; takes a few minutes)

- [ ] **Step 7: Commit**

```bash
git add firmware/platformio.ini firmware/include/config.example.h firmware/src/main.cpp
git commit -m "feat: firmware scaffold — Inkplate 6COLOR PlatformIO project compiles"
```

---

### Task 5: Core pure logic — unpack/clamp/backoff (TDD, native tests)

**Files:**
- Create: `firmware/lib/tesscore/tesscore.h`
- Create: `firmware/lib/tesscore/tesscore.cpp`
- Test: `firmware/test/test_core/test_core.cpp`

**Interfaces:**
- Produces (namespace `tesscore`):
  - `constexpr uint32_t kMinIntervalS = 30; kMaxIntervalS = 604800;`
  - `uint32_t clampInterval(long v, uint32_t fallbackS)` — ≤0 → fallback, else clamped to bounds.
  - `uint32_t backoffSeconds(uint8_t consecutiveFailures, uint32_t capS)` — 0→0, 1→60, 2→120, doubling, capped.
  - `constexpr size_t packedSize4bpp(int w, int h)` — `w*h/2`.
  - `typedef void (*PixelEmit)(int x, int y, uint8_t idx, void *ctx);`
  - `void unpack4bpp(const uint8_t *src, int w, int h, PixelEmit emit, void *ctx)`.
- No Arduino dependencies — must build on `env:native`.

- [ ] **Step 1: Write the failing test — `firmware/test/test_core/test_core.cpp`**

```cpp
#include <unity.h>

#include <cstring>

#include "tesscore.h"

using namespace tesscore;

void test_clamp_interval() {
    TEST_ASSERT_EQUAL_UINT32(900, clampInterval(0, 900));
    TEST_ASSERT_EQUAL_UINT32(900, clampInterval(-5, 900));
    TEST_ASSERT_EQUAL_UINT32(30, clampInterval(10, 900));
    TEST_ASSERT_EQUAL_UINT32(300, clampInterval(300, 900));
    TEST_ASSERT_EQUAL_UINT32(604800, clampInterval(9999999, 900));
}

void test_backoff() {
    TEST_ASSERT_EQUAL_UINT32(0, backoffSeconds(0, 900));
    TEST_ASSERT_EQUAL_UINT32(60, backoffSeconds(1, 900));
    TEST_ASSERT_EQUAL_UINT32(120, backoffSeconds(2, 900));
    TEST_ASSERT_EQUAL_UINT32(240, backoffSeconds(3, 900));
    TEST_ASSERT_EQUAL_UINT32(900, backoffSeconds(5, 900));   // 960 capped
    TEST_ASSERT_EQUAL_UINT32(900, backoffSeconds(200, 900)); // shift capped
}

void test_packed_size() {
    TEST_ASSERT_EQUAL_UINT32(134400, packedSize4bpp(600, 448));
    TEST_ASSERT_EQUAL_UINT32(4, packedSize4bpp(4, 2));
}

static uint8_t grid[2][4];
static void emitToGrid(int x, int y, uint8_t idx, void *) { grid[y][x] = idx; }

void test_unpack_4bpp_nibble_order() {
    // Same fixture as the Python decoder test: rows [0,1,2,3], [4,5,6,1].
    const uint8_t data[] = {0x01, 0x23, 0x45, 0x61};
    std::memset(grid, 0xAA, sizeof(grid));
    unpack4bpp(data, 4, 2, emitToGrid, nullptr);
    const uint8_t want0[] = {0, 1, 2, 3}, want1[] = {4, 5, 6, 1};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want0, grid[0], 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want1, grid[1], 4);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_clamp_interval);
    RUN_TEST(test_backoff);
    RUN_TEST(test_packed_size);
    RUN_TEST(test_unpack_4bpp_nibble_order);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native`
Expected: build error — `tesscore.h: No such file or directory`

- [ ] **Step 3: Write `firmware/lib/tesscore/tesscore.h`**

```cpp
// Pure protocol/format logic shared by firmware and native unit tests.
// Must stay free of Arduino/ESP-IDF includes.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tesscore {

// Tesserae's documented bounds for sleep_interval_s.
constexpr uint32_t kMinIntervalS = 30;
constexpr uint32_t kMaxIntervalS = 604800;

uint32_t clampInterval(long v, uint32_t fallbackS);

// 60s, 120s, 240s, ... per consecutive failure, capped at capS.
uint32_t backoffSeconds(uint8_t consecutiveFailures, uint32_t capS);

constexpr size_t packedSize4bpp(int w, int h) {
    return static_cast<size_t>(w) * static_cast<size_t>(h) / 2;
}

// Tesserae 4-bpp .bin: row-major, high nibble = even column.
typedef void (*PixelEmit)(int x, int y, uint8_t idx, void *ctx);
void unpack4bpp(const uint8_t *src, int w, int h, PixelEmit emit, void *ctx);

} // namespace tesscore
```

- [ ] **Step 4: Write `firmware/lib/tesscore/tesscore.cpp`**

```cpp
#include "tesscore.h"

namespace tesscore {

uint32_t clampInterval(long v, uint32_t fallbackS) {
    if (v <= 0)
        return fallbackS;
    if (static_cast<uint32_t>(v) < kMinIntervalS)
        return kMinIntervalS;
    if (static_cast<uint32_t>(v) > kMaxIntervalS)
        return kMaxIntervalS;
    return static_cast<uint32_t>(v);
}

uint32_t backoffSeconds(uint8_t consecutiveFailures, uint32_t capS) {
    if (consecutiveFailures == 0)
        return 0;
    uint8_t shift = consecutiveFailures - 1;
    if (shift > 6)
        shift = 6;
    uint32_t s = 60u << shift;
    return s > capS ? capS : s;
}

void unpack4bpp(const uint8_t *src, int w, int h, PixelEmit emit, void *ctx) {
    size_t i = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            uint8_t b = src[i++];
            emit(x, y, b >> 4, ctx);
            if (x + 1 < w)
                emit(x + 1, y, b & 0x0F, ctx);
        }
    }
}

} // namespace tesscore
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware && pio test -e native`
Expected: `4 Tests 0 Failures 0 Ignored` — PASSED

- [ ] **Step 6: Verify the embedded env still compiles**

Run: `cd firmware && pio run -e inkplate6color`
Expected: `SUCCESS`

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/tesscore/ firmware/test/
git commit -m "feat: tesscore pure logic (unpack/clamp/backoff) with native tests"
```

---

### Task 6: State store (NVS)

**Files:**
- Create: `firmware/src/state_store.h`
- Create: `firmware/src/state_store.cpp`

**Interfaces:**
- Produces: `class StateStore` with `void begin()`, `String token()`, `void setToken(const String&)`, `String etag()`, `void setEtag(const String&)`, `uint8_t failures()`, `void setFailures(uint8_t)`, `void clear()`. Backed by `Preferences` namespace `"tesserae"`. Used by Task 9.
- Hardware-bound (NVS); verified by compilation here and on-device in Task 10.

- [ ] **Step 1: Write `firmware/src/state_store.h`**

```cpp
// Persistent device state in ESP32 NVS: bearer token, last frame ETag,
// consecutive-failure counter. Survives deep sleep and reflash.
#pragma once

#include <Arduino.h>

class StateStore {
  public:
    void begin();

    String token();
    void setToken(const String &t);

    String etag();
    void setEtag(const String &e);

    uint8_t failures();
    void setFailures(uint8_t n);

    void clear(); // wipe pairing (token + etag)
};
```

- [ ] **Step 2: Write `firmware/src/state_store.cpp`**

```cpp
#include "state_store.h"

#include <Preferences.h>

// One shared handle; NVS namespace names are <=15 chars.
static Preferences prefs;

void StateStore::begin() { prefs.begin("tesserae", false); }

String StateStore::token() { return prefs.getString("token", ""); }
void StateStore::setToken(const String &t) { prefs.putString("token", t); }

String StateStore::etag() { return prefs.getString("etag", ""); }
void StateStore::setEtag(const String &e) { prefs.putString("etag", e); }

uint8_t StateStore::failures() { return prefs.getUChar("failures", 0); }
void StateStore::setFailures(uint8_t n) { prefs.putUChar("failures", n); }

void StateStore::clear() {
    prefs.remove("token");
    prefs.remove("etag");
    prefs.remove("failures");
}
```

- [ ] **Step 3: Verify it compiles**

Run: `cd firmware && pio run -e inkplate6color`
Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add firmware/src/state_store.*
git commit -m "feat: NVS-backed state store (token, etag, failure count)"
```

---

### Task 7: Tesserae REST client

**Files:**
- Create: `firmware/src/tesserae_client.h`
- Create: `firmware/src/tesserae_client.cpp`

**Interfaces:**
- Consumes: `config.h` macros (Task 4).
- Produces (used verbatim by Task 9):

```cpp
struct FrameEnvelope { String url; String format; String etag; int panelW; int panelH; };
struct StatusReply   { uint32_t nextPollS; };  // 0 = server didn't say
enum class FetchResult { NewFrame, NotModified, NoContent, AuthError, Error };

class TesseraeClient {
  public:
    TesseraeClient(const String &baseUrl, const String &deviceId);
    void setToken(const String &token);
    bool registerDevice(const String &pairingCode, const String &name,
                        const String &mac, String &tokenOut);
    FetchResult fetchEnvelope(const String &etag, FrameEnvelope &out);
    bool downloadFrame(const String &url, uint8_t *buf, size_t expectedLen);
    bool postStatus(int batteryMv, int batteryPct, int rssi, const String &ip,
                    uint32_t nextSleepS, StatusReply &out);
    void postLog(const char *level, const String &msg);
};
```

- Network-bound; verified by compilation here, by the dry-run tool's identical flow server-side, and on-device in Task 10.

- [ ] **Step 1: Write `firmware/src/tesserae_client.h`**

```cpp
// Tesserae device REST protocol (docs/dev/client-protocol.md upstream).
// Knows nothing about the panel.
#pragma once

#include <Arduino.h>

struct FrameEnvelope {
    String url;    // absolute URL of the packed .bin
    String format; // "bin"
    String etag;   // ETag header of the /frame response
    int panelW = 0;
    int panelH = 0;
};

struct StatusReply {
    uint32_t nextPollS = 0; // 0 = server didn't say
};

enum class FetchResult { NewFrame, NotModified, NoContent, AuthError, Error };

class TesseraeClient {
  public:
    TesseraeClient(const String &baseUrl, const String &deviceId)
        : _base(baseUrl), _id(deviceId) {}

    void setToken(const String &token) { _token = token; }

    // POST /api/v1/device/register with X-Pairing-Code. True on 200/201;
    // fills tokenOut.
    bool registerDevice(const String &pairingCode, const String &name,
                        const String &mac, String &tokenOut);

    // GET /api/v1/device/<id>/frame with optional If-None-Match.
    FetchResult fetchEnvelope(const String &etag, FrameEnvelope &out);

    // GET url (no auth needed for /renders/). True iff exactly
    // expectedLen bytes were read into buf.
    bool downloadFrame(const String &url, uint8_t *buf, size_t expectedLen);

    // POST /api/v1/device/<id>/status. True on 200; fills out.
    bool postStatus(int batteryMv, int batteryPct, int rssi, const String &ip,
                    uint32_t nextSleepS, StatusReply &out);

    // POST /api/v1/device/<id>/log. Best-effort, ignores failures.
    void postLog(const char *level, const String &msg);

  private:
    String _devicePath(const char *leaf) const {
        return _base + "/api/v1/device/" + _id + "/" + leaf;
    }
    String _base, _id, _token;
};
```

- [ ] **Step 2: Write `firmware/src/tesserae_client.cpp`**

```cpp
#include "tesserae_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"

static const uint32_t kHttpTimeoutMs = 20000;

bool TesseraeClient::registerDevice(const String &pairingCode,
                                    const String &name, const String &mac,
                                    String &tokenOut) {
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(_base + "/api/v1/device/register"))
        return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Pairing-Code", pairingCode);

    JsonDocument doc;
    doc["device_id"] = _id;
    doc["kind"] = "esp32_client";
    doc["panel_w"] = PANEL_W;
    doc["panel_h"] = PANEL_H;
    doc["gamut"] = "inky_7colour";
    doc["name"] = name;
    doc["fw_version"] = FW_VERSION;
    doc["mac"] = mac;
    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    if (code != 200 && code != 201) {
        Serial.printf("register: HTTP %d %s\n", code, http.getString().c_str());
        http.end();
        return false;
    }
    JsonDocument reply;
    DeserializationError err = deserializeJson(reply, http.getStream());
    http.end();
    if (err || reply["device_token"].isNull()) {
        Serial.printf("register: bad reply (%s)\n", err.c_str());
        return false;
    }
    tokenOut = reply["device_token"].as<String>();
    return true;
}

FetchResult TesseraeClient::fetchEnvelope(const String &etag,
                                          FrameEnvelope &out) {
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(_devicePath("frame")))
        return FetchResult::Error;
    http.addHeader("Authorization", "Bearer " + _token);
    if (etag.length())
        http.addHeader("If-None-Match", etag);
    const char *collect[] = {"ETag"};
    http.collectHeaders(collect, 1);

    int code = http.GET();
    if (code == 304) {
        http.end();
        return FetchResult::NotModified;
    }
    if (code == 204) {
        http.end();
        return FetchResult::NoContent;
    }
    if (code == 401 || code == 403) {
        http.end();
        return FetchResult::AuthError;
    }
    if (code != 200) {
        Serial.printf("frame: HTTP %d\n", code);
        http.end();
        return FetchResult::Error;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    out.etag = http.header("ETag");
    http.end();
    if (err)
        return FetchResult::Error;
    out.url = doc["url"].as<String>();
    out.format = doc["format"] | "bin";
    out.panelW = doc["panel_w"] | PANEL_W;
    out.panelH = doc["panel_h"] | PANEL_H;
    return out.url.length() ? FetchResult::NewFrame : FetchResult::Error;
}

bool TesseraeClient::downloadFrame(const String &url, uint8_t *buf,
                                   size_t expectedLen) {
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(url))
        return false;
    int code = http.GET();
    if (code != 200) {
        Serial.printf("download: HTTP %d\n", code);
        http.end();
        return false;
    }
    WiFiClient *stream = http.getStreamPtr();
    size_t got = 0;
    uint32_t deadline = millis() + 60000;
    while (got < expectedLen && millis() < deadline) {
        if (!http.connected() && !stream->available())
            break;
        int n = stream->readBytes(buf + got, expectedLen - got);
        if (n > 0)
            got += n;
    }
    // Anything still buffered means the body was longer than expected.
    bool extra = stream->available() > 0;
    http.end();
    if (got != expectedLen || extra) {
        Serial.printf("download: got %u of %u bytes (extra=%d)\n",
                      (unsigned)got, (unsigned)expectedLen, extra);
        return false;
    }
    return true;
}

bool TesseraeClient::postStatus(int batteryMv, int batteryPct, int rssi,
                                const String &ip, uint32_t nextSleepS,
                                StatusReply &out) {
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(_devicePath("status")))
        return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + _token);

    JsonDocument doc;
    if (batteryMv > 0) {
        doc["battery_mv"] = batteryMv;
        doc["battery_pct"] = batteryPct;
    }
    doc["rssi"] = rssi;
    doc["ip"] = ip;
    doc["next_sleep_s"] = nextSleepS;
    doc["fw_version"] = FW_VERSION;
    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("status: HTTP %d\n", code);
        http.end();
        return false;
    }
    JsonDocument reply;
    DeserializationError err = deserializeJson(reply, http.getStream());
    http.end();
    if (err)
        return false;
    long interval = reply["next_poll_s"] | 0L;
    if (interval <= 0)
        interval = reply["config"]["sleep_interval_s"] | 0L;
    out.nextPollS = interval > 0 ? (uint32_t)interval : 0;
    return true;
}

void TesseraeClient::postLog(const char *level, const String &msg) {
    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(_devicePath("log")))
        return;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + _token);
    JsonDocument doc;
    doc["level"] = level;
    doc["msg"] = msg;
    String body;
    serializeJson(doc, body);
    http.POST(body); // best-effort
    http.end();
}
```

- [ ] **Step 3: Verify it compiles**

Run: `cd firmware && pio run -e inkplate6color`
Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add firmware/src/tesserae_client.*
git commit -m "feat: Tesserae REST client (register, frame fetch, status, log)"
```

---

### Task 8: Frame painter + status screen

**Files:**
- Create: `firmware/src/frame_painter.h`
- Create: `firmware/src/frame_painter.cpp`
- Create: `firmware/src/status_screen.h`
- Create: `firmware/src/status_screen.cpp`

**Interfaces:**
- Consumes: `tesscore::unpack4bpp` (Task 5), `Inkplate` from InkplateLibrary.
- Produces: `void paintFrame(Inkplate &d, const uint8_t *packed, int w, int h)` (unpack → drawPixel → `display()`); `void showStatusScreen(Inkplate &d, const char *title, const char *l1, const char *l2, const char *l3)` (full-screen message, used only for pairing/persistent-failure states).

- [ ] **Step 1: Write `firmware/src/frame_painter.h`**

```cpp
#pragma once

#include <Inkplate.h>

// Unpack a Tesserae inky_7colour 4-bpp frame into the Inkplate framebuffer
// and refresh the panel (~25-30 s on ACeP). Blocking.
void paintFrame(Inkplate &d, const uint8_t *packed, int w, int h);
```

- [ ] **Step 2: Write `firmware/src/frame_painter.cpp`**

```cpp
#include "frame_painter.h"

#include "tesscore.h"

// Tesserae inky_7colour nibble -> Inkplate 6COLOR color constant.
// Same order today (identity), but this table is the single place to
// remap if a future panel/gamut ever disagrees.
static const uint8_t kPalette[7] = {
    INKPLATE_BLACK, INKPLATE_WHITE, INKPLATE_GREEN, INKPLATE_BLUE,
    INKPLATE_RED,   INKPLATE_YELLOW, INKPLATE_ORANGE,
};

static void emitPixel(int x, int y, uint8_t idx, void *ctx) {
    Inkplate *d = static_cast<Inkplate *>(ctx);
    d->drawPixel(x, y, idx < 7 ? kPalette[idx] : INKPLATE_WHITE);
}

void paintFrame(Inkplate &d, const uint8_t *packed, int w, int h) {
    tesscore::unpack4bpp(packed, w, h, emitPixel, &d);
    d.display();
}
```

- [ ] **Step 3: Write `firmware/src/status_screen.h`**

```cpp
#pragma once

#include <Inkplate.h>

// Full-screen text notice. Costs a full ACeP refresh, so callers use it
// only for first-boot/pairing help and persistent failures - never for
// transient errors (those keep the last dashboard on screen).
void showStatusScreen(Inkplate &d, const char *title, const char *l1,
                      const char *l2 = nullptr, const char *l3 = nullptr);
```

- [ ] **Step 4: Write `firmware/src/status_screen.cpp`**

```cpp
#include "status_screen.h"

void showStatusScreen(Inkplate &d, const char *title, const char *l1,
                      const char *l2, const char *l3) {
    d.clearDisplay();
    d.setTextColor(INKPLATE_BLACK);
    d.setTextSize(3);
    d.setCursor(20, 40);
    d.print(title);
    d.setTextSize(2);
    int y = 110;
    for (const char *line : {l1, l2, l3}) {
        if (!line)
            continue;
        d.setCursor(20, y);
        d.print(line);
        y += 36;
    }
    d.display();
}
```

- [ ] **Step 5: Verify it compiles**

Run: `cd firmware && pio run -e inkplate6color`
Expected: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git add firmware/src/frame_painter.* firmware/src/status_screen.*
git commit -m "feat: frame painter (4-bpp -> panel) and status screen"
```

---

### Task 9: Main orchestration + power modes

**Files:**
- Modify: `firmware/src/main.cpp` (replace placeholder entirely)

**Interfaces:**
- Consumes: everything from Tasks 4–8 exactly as declared there.

- [ ] **Step 1: Replace `firmware/src/main.cpp`**

```cpp
// Inkplate 6COLOR Tesserae client.
//
// Each cycle: connect Wi-Fi -> ensure paired -> GET /frame (ETag) ->
// paint on 200 -> POST /status -> sleep for the server-chosen interval.
// POWER_BATTERY deep-sleeps between cycles; POWER_USB wait-loops.

#include <Inkplate.h>
#include <WiFi.h>

#include "config.h"
#include "frame_painter.h"
#include "state_store.h"
#include "status_screen.h"
#include "tesscore.h"
#include "tesserae_client.h"

static Inkplate display;
static StateStore state;
static TesseraeClient client(TESSERAE_BASE_URL, DEVICE_ID);

// Consecutive failures before we sacrifice the dashboard for an error
// screen. Transient errors keep the last frame visible.
static const uint8_t kFailuresBeforeScreen = 5;

static void goToSleep(uint32_t seconds) {
    Serial.printf("sleeping %us\n", seconds);
    Serial.flush();
#if POWER_MODE == POWER_BATTERY
    WiFi.disconnect(true);
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
#else
    uint32_t end = millis() + seconds * 1000UL;
    while ((int32_t)(end - millis()) > 0)
        delay(250);
#endif
}

static bool connectWifi() {
    if (WiFi.status() == WL_CONNECTED)
        return true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t deadline = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline)
        delay(250);
    return WiFi.status() == WL_CONNECTED;
}

static bool ensurePaired() {
    String token = state.token();
    if (token.length()) {
        client.setToken(token);
        return true;
    }
    Serial.println("no token; registering with pairing code");
    if (!client.registerDevice(PAIRING_CODE, DEVICE_NAME, WiFi.macAddress(),
                               token)) {
        showStatusScreen(display, "Pairing failed",
                         "Check PAIRING_CODE in config.h",
                         ("Server: " TESSERAE_BASE_URL),
                         "Generate a code: Settings > Devices");
        return false;
    }
    state.setToken(token);
    state.setEtag("");
    client.setToken(token);
    Serial.println("paired OK");
    return true;
}

static int batteryMv() {
    double v = display.readBattery();
    return v > 0.5 ? (int)(v * 1000.0) : 0;
}

static int batteryPct(int mv) {
    if (mv <= 0)
        return 0;
    int pct = (mv - 3300) * 100 / (4200 - 3300); // server uses same curve
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

// One wake cycle. Returns seconds to sleep before the next one.
static uint32_t runCycle() {
    uint8_t failures = state.failures();

    if (!connectWifi()) {
        state.setFailures(++failures);
        Serial.printf("wifi failed (%u consecutive)\n", failures);
        if (failures == kFailuresBeforeScreen)
            showStatusScreen(display, "Wi-Fi unreachable",
                             ("SSID: " WIFI_SSID), "Still retrying...");
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    if (!ensurePaired()) {
        state.setFailures(++failures);
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    FrameEnvelope env;
    FetchResult res = client.fetchEnvelope(state.etag(), env);

    if (res == FetchResult::AuthError) {
        // Token revoked/instance deleted server-side: re-pair next cycle.
        Serial.println("auth error; clearing pairing");
        state.clear();
        state.setFailures(++failures);
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }
    if (res == FetchResult::Error) {
        state.setFailures(++failures);
        if (failures == kFailuresBeforeScreen)
            showStatusScreen(display, "Server unreachable",
                             ("URL: " TESSERAE_BASE_URL), "Still retrying...");
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    if (res == FetchResult::NewFrame) {
        size_t len = tesscore::packedSize4bpp(env.panelW, env.panelH);
        uint8_t *buf = (uint8_t *)ps_malloc(len);
        if (!buf)
            buf = (uint8_t *)malloc(len);
        if (buf && client.downloadFrame(env.url, buf, len)) {
            Serial.printf("painting %ux%u frame\n", env.panelW, env.panelH);
            paintFrame(display, buf, env.panelW, env.panelH);
            state.setEtag(env.etag);
        } else {
            // Keep the old image; tell the server.
            client.postLog("error", buf ? "frame download failed"
                                        : "frame buffer alloc failed");
        }
        free(buf);
    } else if (res == FetchResult::NoContent) {
        Serial.println("no page assigned to this device yet");
    } else {
        Serial.println("frame unchanged (304)");
    }

    state.setFailures(0);

    int mv = batteryMv();
    StatusReply reply;
    uint32_t plannedSleep = FALLBACK_POLL_S;
    if (client.postStatus(mv, batteryPct(mv), WiFi.RSSI(),
                          WiFi.localIP().toString(), plannedSleep, reply) &&
        reply.nextPollS > 0) {
        plannedSleep = reply.nextPollS;
    }
    return tesscore::clampInterval((long)plannedSleep, FALLBACK_POLL_S);
}

void setup() {
    Serial.begin(115200);
    Serial.printf("\ninkplate-tesserae %s (%s mode)\n", FW_VERSION,
                  POWER_MODE == POWER_BATTERY ? "battery" : "usb");
    display.begin();
    state.begin();
    goToSleep(runCycle());
    // POWER_USB falls through to loop(); POWER_BATTERY never returns.
}

void loop() {
    goToSleep(runCycle());
}
```

- [ ] **Step 2: Verify both power modes compile**

Run: `cd firmware && pio run -e inkplate6color`
Then edit `firmware/include/config.h`: set `#define POWER_MODE POWER_USB`, run `pio run -e inkplate6color` again, then set it back to `POWER_BATTERY`.
Expected: `SUCCESS` both times.

- [ ] **Step 3: Run native tests once more (regression)**

Run: `cd firmware && pio test -e native`
Expected: all PASS

- [ ] **Step 4: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat: main cycle — wifi, pairing, fetch/paint, telemetry, dual power modes"
```

---

### Task 10: Bring-up checklist + end-to-end verification (human-in-the-loop)

**Files:**
- Create: `docs/bringup.md`

**Interfaces:**
- Consumes: everything. This task is executed jointly with the user — server deploy and flashing need their Unraid box and the physical Inkplate.

- [ ] **Step 1: Create `docs/bringup.md`**

```markdown
# Bring-up checklist

Work top to bottom; each stage gates the next.

## 1. Server (Unraid)

- [ ] Deploy per `server/README.md`; `docker ps` shows `tesserae` healthy.
- [ ] Admin UI reachable at `http://<unraid-ip>:8765`; onboarding done.
- [ ] A dashboard page exists with at least one widget.
- [ ] Pairing code generated (Settings → Devices → Pair new device).

## 2. Protocol dry-run (no hardware at risk)

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
- [ ] Connect Inkplate over USB-C; `cd firmware && pio run -e inkplate6color -t upload`.
- [ ] `pio device monitor`: watch register → fetch → `painting 600x448 frame`.
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
```

- [ ] **Step 2: Walk stages 1–2 with the user** (needs Unraid access; the dry-run can run from this Mac)

- [ ] **Step 3: Walk stages 3–5 with the user** (needs the physical Inkplate on USB)

- [ ] **Step 4: Commit**

```bash
git add docs/bringup.md
git commit -m "docs: bring-up checklist for server, dry-run, and device"
```

---

## Self-review notes

- **Spec coverage:** server deployment (Task 1), dry-run tool (Tasks 2–3), firmware scaffold/config incl. dual power modes (Tasks 4, 9), pure-logic modules with tests (Task 5), state store (6), REST client incl. register/etag/status/log (7), painter + status screen with "never overwrite dashboard on transient errors" policy (8, 9), verification plan (10). Out-of-scope items from spec remain out.
- **Interface consistency:** `tesscore::` names, `StateStore`, `TesseraeClient`, `FrameEnvelope`, `FetchResult`, `paintFrame`, `showStatusScreen` are declared once and consumed with identical signatures in Task 9.
- **Known risk carried from spec:** if the server rejects `kind: "esp32_client"` for REST-registered devices, the dry-run (Task 10 stage 2) surfaces it cheaply; fallback is registering with `kind: "circuitpython_generic"` + `format: "bmp"` and adding a BMP paint path — decision point for the user, not silently done.
