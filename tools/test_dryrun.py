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


def test_decode_bin_odd_width_rows_are_byte_aligned():
    # 3x2 frame: rows [0,1,2] and [3,4,5]; trailing low nibbles (0xF)
    # are padding and each row starts on a byte boundary.
    data = bytes([0x01, 0x2F, 0x34, 0x5F])
    img = decode_bin(data, 3, 2)
    assert img.size == (3, 2)
    assert [img.getpixel((x, 0)) for x in range(3)] == [PALETTE[0], PALETTE[1], PALETTE[2]]
    assert [img.getpixel((x, 1)) for x in range(3)] == [PALETTE[3], PALETTE[4], PALETTE[5]]


def test_state_round_trip(tmp_path):
    from tesserae_dryrun import load_state, save_state
    p = tmp_path / "state.json"
    assert load_state(p) == {}
    save_state(p, {"token": "abc", "etag": '"d1"'})
    assert load_state(p) == {"token": "abc", "etag": '"d1"'}
