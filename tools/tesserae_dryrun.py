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
                    px[xx, y] = PALETTE[nibble] if nibble < 7 else PALETTE[1]
    return img


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
