#!/usr/bin/env python3
"""Minimal test runner for verifying tesserae_dryrun without pytest."""

import sys
import traceback

# This will fail initially since the module doesn't exist
try:
    from tesserae_dryrun import PALETTE, decode_bin
except ModuleNotFoundError as e:
    print(f"FAIL: {e}")
    print("ModuleNotFoundError: No module named 'tesserae_dryrun'")
    sys.exit(1)

# Test 1: test_palette_has_seven_colors_in_tesserae_order
def test_palette_has_seven_colors_in_tesserae_order():
    assert len(PALETTE) == 7
    assert PALETTE[0] == (0, 0, 0)        # black
    assert PALETTE[1] == (255, 255, 255)  # white
    assert PALETTE[4] == (255, 0, 0)      # red

# Test 2: test_decode_bin_unpacks_high_nibble_even_column
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

# Test 3: test_decode_bin_rejects_wrong_length
def test_decode_bin_rejects_wrong_length():
    try:
        decode_bin(b"\x00\x00", 4, 2)  # needs 4 bytes
        raise AssertionError("Expected ValueError")
    except ValueError:
        pass

# Test 4: test_decode_bin_out_of_palette_index_maps_white
def test_decode_bin_out_of_palette_index_maps_white():
    img = decode_bin(bytes([0xF0]), 2, 1)  # nibble 0xF is undefined
    assert img.getpixel((0, 0)) == PALETTE[1]

tests = [
    test_palette_has_seven_colors_in_tesserae_order,
    test_decode_bin_unpacks_high_nibble_even_column,
    test_decode_bin_rejects_wrong_length,
    test_decode_bin_out_of_palette_index_maps_white,
]

passed = 0
failed = 0

for test in tests:
    try:
        test()
        print(f"✓ {test.__name__}")
        passed += 1
    except Exception as e:
        print(f"✗ {test.__name__}: {e}")
        traceback.print_exc()
        failed += 1

print(f"\n{passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
