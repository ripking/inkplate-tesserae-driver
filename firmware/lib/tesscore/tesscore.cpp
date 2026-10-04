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
    if (shift > 24)
        shift = 24;
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

FrameStride frameStride(int canvasW, int canvasH, int nativeW, int nativeH,
                        int panelW, int panelH) {
    if (nativeW > 0 && nativeH > 0) {
        if (nativeW == panelW && nativeH == panelH)
            return FrameStride::Landscape;
        if (nativeW == panelH && nativeH == panelW)
            return FrameStride::Transposed;
        return FrameStride::Mismatch;
    }
    if ((canvasW == panelW && canvasH == panelH) ||
        (canvasW == panelH && canvasH == panelW))
        return FrameStride::Landscape;
    return FrameStride::Mismatch;
}

} // namespace tesscore
