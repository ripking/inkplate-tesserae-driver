#!/usr/bin/env python3
"""Act as a Tesserae device without hardware.

Speaks the same REST protocol as the Inkplate firmware: register with a
pairing code, fetch the packed 4-bpp frame with ETag caching, decode it to
a PNG, and post telemetry. Validates the whole server pipeline before
anything is flashed, and doubles as a debugging tool afterwards.
"""

from PIL import Image

# Tesserae `inky_7colour` gamut nibble order, verified byte-for-byte against
# the server's Calibration "Palette swatches" pattern (2026-07-14). NOT the
# Inkplate constant order: indices 2..5 are yellow/red/blue/green, not
# green/blue/red/yellow. firmware/src/frame_painter.cpp mirrors this LUT.
PALETTE = [
    (0, 0, 0),        # 0 black
    (255, 255, 255),  # 1 white
    (255, 255, 0),    # 2 yellow
    (255, 0, 0),      # 3 red
    (0, 0, 255),      # 4 blue
    (0, 128, 0),      # 5 green
    (255, 140, 0),    # 6 orange
]

# Tesserae `waveshare_e6` (declared as spectra_6) nibbles are the Spectra 6
# controller codes; 4 and 7 are reserved. firmware/src/frame_painter.cpp
# maps the same codes for the Inkplate 13SPECTRA.
SPECTRA6_PALETTE = {
    0: (0, 0, 0),        # black
    1: (255, 255, 255),  # white
    2: (255, 255, 0),    # yellow
    3: (255, 0, 0),      # red
    5: (0, 0, 255),      # blue
    6: (0, 128, 0),      # green
}

# Mirrors firmware/include/board.h: panel_w/h is the landscape canvas the
# device registers, which Tesserae's panel presets also use as the .bin
# stride.
BOARDS = {
    "6color": dict(panel_w=600, panel_h=448, gamut="inky_7colour",
                   palette=dict(enumerate(PALETTE)), device_id="inkplate6c_dryrun"),
    "13spectra": dict(panel_w=1600, panel_h=1200, gamut="spectra_6",
                      palette=SPECTRA6_PALETTE, device_id="inkplate13s_dryrun"),
}


def decode_bin(data: bytes, w: int, h: int, palette=None) -> Image.Image:
    """Unpack a Tesserae 4-bpp .bin frame (high nibble = even column).

    ``palette`` maps nibble -> RGB (default: the 6COLOR list); unmapped
    nibbles decode as white."""
    lut = dict(enumerate(PALETTE)) if palette is None else palette
    white = (255, 255, 255)
    expected = h * ((w + 1) // 2)
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
                    px[xx, y] = lut.get(nibble, white)
    return img


import argparse
import json
import sys
import uuid
from pathlib import Path

import requests

STATE_PATH = Path(__file__).parent / ".tesserae_state.json"


def load_state(path=STATE_PATH) -> dict:
    p = Path(path)
    return json.loads(p.read_text()) if p.exists() else {}


def save_state(path, state: dict) -> None:
    Path(path).write_text(json.dumps(state, indent=2))


def _auth(state):
    return {"Authorization": f"Bearer {state['token']}"}


def cmd_register(args):
    state = load_state()
    board = BOARDS[args.board]
    manifest = {
        "device_id": args.device_id or board["device_id"],
        "kind": "esp32_client",
        "panel_w": board["panel_w"],
        "panel_h": board["panel_h"],
        "gamut": board["gamut"],
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
        device_id=manifest["device_id"],
        board=args.board,
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
    board = BOARDS[state.get("board", "6color")]
    # The .bin is packed at native_* when the server echoes it (a device
    # record holding a portrait framebuffer), else at the landscape canvas.
    w = env.get("native_w", board["panel_w"])
    h = env.get("native_h", board["panel_h"])
    img = decode_bin(frame.content, w, h, board["palette"])
    if (w < h) != (board["panel_w"] < board["panel_h"]):
        # The server turned the canvas 90 deg CW onto a transposed stride;
        # undo it so the PNG reads like the mounted panel.
        img = img.rotate(90, expand=True)
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
    p.add_argument("--board", choices=sorted(BOARDS), default="6color")
    p.add_argument("--device-id", default=None, help="default depends on --board")
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
