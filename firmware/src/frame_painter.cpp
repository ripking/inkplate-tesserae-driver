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
