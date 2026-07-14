#pragma once

#include <Inkplate.h>

// Unpack a Tesserae inky_7colour 4-bpp frame into the Inkplate framebuffer
// and refresh the panel (~25-30 s on ACeP). Blocking.
void paintFrame(Inkplate &d, const uint8_t *packed, int w, int h);
