// Per-board panel profile, selected by the PlatformIO env's
// -DARDUINO_INKPLATE* flag. Everything panel-specific that the protocol
// and painter need lives here, so config.h only carries site settings.
#pragma once

#include "config.h"

// Older config.h copies defined the panel size; the board decides now.
#undef PANEL_W
#undef PANEL_H

// PANEL_W x PANEL_H is the landscape frame Tesserae packs for this panel:
// the dims we register, and the .bin's row stride. A panel set to portrait
// in the Tesserae UI still arrives at this stride; the server turns the
// composition onto it. PAINT_ROTATION is the Adafruit-GFX rotation that
// shows that frame upright on the panel.

#if defined(ARDUINO_INKPLATE13SPECTRA)
// Inkplate 13SPECTRA: 13.3" E Ink Spectra 6, ESP32-S3. Its framebuffer is
// portrait-native (1200x1600), but Tesserae's 13.3" preset packs
// landscape, so we paint through GFX rotation 1 (derived from the
// selftest's orientation; the transposed path below is the one exercised
// on hardware so far).
#define BOARD_NAME "Inkplate 13SPECTRA"
#define PANEL_W 1600
#define PANEL_H 1200
#define PAINT_ROTATION 1
// A device record holding the portrait framebuffer as its native stride
// (e.g. paired with a declared rotation) gets 1200x1600 frames, which are
// the library's raw rotation-0 layout (verified with the selftest env).
#define PAINT_ROTATION_TRANSPOSED 0
// Tesserae canonicalises spectra_6 to waveshare_e6, whose nibbles are the
// Spectra 6 controller codes (0 black, 1 white, 2 yellow, 3 red, 5 blue,
// 6 green).
#define PANEL_GAMUT "spectra_6"

#elif defined(ARDUINO_INKPLATECOLOR)
// Inkplate 6COLOR: 5.8" ACeP 7-colour, ESP32.
#define BOARD_NAME "Inkplate 6COLOR"
#define PANEL_W 600
#define PANEL_H 448
#define PAINT_ROTATION 0
#define PANEL_GAMUT "inky_7colour"

#else
#error "Unsupported board: build with an inkplate env from platformio.ini"
#endif
