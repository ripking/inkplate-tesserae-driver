#include "frame_painter.h"

#include "tesscore.h"

// Tesserae inky_7colour nibble -> Inkplate 6COLOR color constant.
// The server's gamut order is NOT identity with the Inkplate constants:
// verified byte-for-byte against the server's Calibration "Palette
// swatches" pattern (2026-07-14), the server emits
//   0=black 1=white 2=yellow 3=red 4=blue 5=green 6=orange
// so indices 2..5 must be remapped here (black/white/orange already align).
// This table is the single place to remap if a panel/gamut disagrees.
static const uint8_t kPalette[7] = {
    INKPLATE_BLACK, INKPLATE_WHITE, INKPLATE_YELLOW, INKPLATE_RED,
    INKPLATE_BLUE,  INKPLATE_GREEN,  INKPLATE_ORANGE,
};

static void emitPixel(int x, int y, uint8_t idx, void *ctx) {
    Inkplate *d = static_cast<Inkplate *>(ctx);
    d->drawPixel(x, y, idx < 7 ? kPalette[idx] : INKPLATE_WHITE);
}

void paintFrame(Inkplate &d, const uint8_t *packed, int w, int h) {
    tesscore::unpack4bpp(packed, w, h, emitPixel, &d);
    d.display();
}
