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
