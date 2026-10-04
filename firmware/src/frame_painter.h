#pragma once

#include <Inkplate.h>

// Unpack a Tesserae 4-bpp frame (board gamut) into the Inkplate framebuffer
// and refresh the panel (~25-30 s on ACeP, longer on Spectra 6). Blocking.
// rotation is the GFX rotation that shows this stride upright (board.h).
void paintFrame(Inkplate &d, const uint8_t *packed, int w, int h,
                uint8_t rotation);
