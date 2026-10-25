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
// Tesserae inky_7colour -> Inkplate 6COLOR. Since Tesserae 0.415.0 the
// server emits
//   0=black 1=white 2=green 3=blue 4=red 5=yellow 6=orange
// which is identity with the Inkplate constants, so this is a pass-through.
// Servers before 0.415.0 sent 2..5 as yellow/red/blue/green (tesserae#298);
// an earlier version of this table compensated for that.
static const uint8_t kPalette[16] = {
    INKPLATE_BLACK, INKPLATE_WHITE, INKPLATE_GREEN,  INKPLATE_BLUE,
    INKPLATE_RED,   INKPLATE_YELLOW, INKPLATE_ORANGE, INKPLATE_WHITE,
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
