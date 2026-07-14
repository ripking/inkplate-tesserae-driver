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
    TEST_ASSERT_EQUAL_UINT32(7680, backoffSeconds(8, 604800));   // keeps doubling past 3840
    TEST_ASSERT_EQUAL_UINT32(604800, backoffSeconds(50, 604800)); // caps at large capS, no overflow
}

void test_packed_size() {
    TEST_ASSERT_EQUAL_UINT32(134400, packedSize4bpp(600, 448)); // unchanged for even width
    TEST_ASSERT_EQUAL_UINT32(4, packedSize4bpp(4, 2));
    TEST_ASSERT_EQUAL_UINT32(4, packedSize4bpp(3, 2));          // odd width: 2 bytes/row
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

static uint8_t oddGrid[2][3];
static void emitToOddGrid(int x, int y, uint8_t idx, void *) { oddGrid[y][x] = idx; }

void test_unpack_4bpp_odd_width_skips_trailing_low_nibble() {
    // 3x2: row bytes [0x01, 0x2F], [0x34, 0x5F] — trailing low nibbles
    // (0xF) must be skipped, and row 1 must start at byte 2.
    const uint8_t data[] = {0x01, 0x2F, 0x34, 0x5F};
    unpack4bpp(data, 3, 2, emitToOddGrid, nullptr);
    const uint8_t want0[] = {0, 1, 2}, want1[] = {3, 4, 5};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want0, oddGrid[0], 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want1, oddGrid[1], 3);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_clamp_interval);
    RUN_TEST(test_backoff);
    RUN_TEST(test_packed_size);
    RUN_TEST(test_unpack_4bpp_nibble_order);
    RUN_TEST(test_unpack_4bpp_odd_width_skips_trailing_low_nibble);
    return UNITY_END();
}
