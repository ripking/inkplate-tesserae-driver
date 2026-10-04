#include "frame_painter.h"

#include "board.h"
#include "tesscore.h"

// Frame nibble -> Inkplate color constant, per board. This table is the
// single place to remap if a panel/gamut disagrees. Unlisted nibbles are
// never emitted by the server and paint white.
#if defined(ARDUINO_INKPLATE13SPECTRA)
// Tesserae waveshare_e6 (from our declared spectra_6) emits the Spectra 6
// controller codes 0 black, 1 white, 2 yellow, 3 red, 5 blue, 6 green;
// nibbles 4 and 7 are reserved. The library's constants are a dense 0..5
// index it maps back to those same controller codes.
static const uint8_t kPalette[16] = {
    INKPLATE_BLACK, INKPLATE_WHITE, INKPLATE_YELLOW, INKPLATE_RED,
    INKPLATE_WHITE, INKPLATE_BLUE,  INKPLATE_GREEN,  INKPLATE_WHITE,
    INKPLATE_WHITE, INKPLATE_WHITE, INKPLATE_WHITE,  INKPLATE_WHITE,
    INKPLATE_WHITE, INKPLATE_WHITE, INKPLATE_WHITE,  INKPLATE_WHITE,
};
#else
// Tesserae inky_7colour -> Inkplate 6COLOR. The server's gamut order is
// NOT identity with the Inkplate constants: verified byte-for-byte against
// the server's Calibration "Palette swatches" pattern (2026-07-14), the
// server emits
//   0=black 1=white 2=yellow 3=red 4=blue 5=green 6=orange
// so indices 2..5 must be remapped here (black/white/orange already align).
static const uint8_t kPalette[16] = {
    INKPLATE_BLACK, INKPLATE_WHITE, INKPLATE_YELLOW, INKPLATE_RED,
    INKPLATE_BLUE,  INKPLATE_GREEN, INKPLATE_ORANGE, INKPLATE_WHITE,
    INKPLATE_WHITE, INKPLATE_WHITE, INKPLATE_WHITE,  INKPLATE_WHITE,
    INKPLATE_WHITE, INKPLATE_WHITE, INKPLATE_WHITE,  INKPLATE_WHITE,
};
#endif

static void emitPixel(int x, int y, uint8_t idx, void *ctx) {
    Inkplate *d = static_cast<Inkplate *>(ctx);
    d->drawPixel(x, y, kPalette[idx & 0x0F]);
}

void paintFrame(Inkplate &d, const uint8_t *packed, int w, int h,
                uint8_t rotation) {
    d.setRotation(rotation);
    uint32_t t0 = millis();
    tesscore::unpack4bpp(packed, w, h, emitPixel, &d);
    Serial.printf("unpacked in %lums; refreshing panel\n",
                  (unsigned long)(millis() - t0));
    d.display();
}
