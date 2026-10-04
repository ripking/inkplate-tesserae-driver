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

// 60s, 120s, 240s, ... doubling per consecutive failure, capped at capS
// (shift is itself clamped to 24 so 60u<<shift can't overflow uint32,
// which comfortably exceeds any legal cap).
uint32_t backoffSeconds(uint8_t consecutiveFailures, uint32_t capS);

// Bytes consumed by unpack4bpp: rows are byte-aligned, so an odd width
// still occupies ceil(w/2) bytes per row.
constexpr size_t packedSize4bpp(int w, int h) {
    return static_cast<size_t>(h) * ((static_cast<size_t>(w) + 1) / 2);
}

// Which stride a /frame envelope's .bin is packed at, relative to our
// landscape panelW x panelH. A server-echoed native stride decides it and
// may be either orientation (devices paired with a declared rotation record
// a portrait framebuffer). Without one, Tesserae packs at the panel
// preset's landscape stride whichever way the canvas is turned, so the
// canvas dims may be ours in either orientation.
enum class FrameStride { Mismatch, Landscape, Transposed };
FrameStride frameStride(int canvasW, int canvasH, int nativeW, int nativeH,
                        int panelW, int panelH);

// Tesserae 4-bpp .bin: row-major, high nibble = even column.
typedef void (*PixelEmit)(int x, int y, uint8_t idx, void *ctx);
void unpack4bpp(const uint8_t *src, int w, int h, PixelEmit emit, void *ctx);

} // namespace tesscore
