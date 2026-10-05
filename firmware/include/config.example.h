// Copy to config.h and fill in. config.h is gitignored.
#pragma once

// ---- Wi-Fi (2.4 GHz only) ----
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// ---- Tesserae server ----
#define TESSERAE_BASE_URL "http://192.168.1.50:8766" // no trailing slash
#define DEVICE_ID     "inkplate_kitchen"             // unique per panel, no spaces
#define DEVICE_NAME   "Kitchen Display"
#define PAIRING_CODE  "000000" // Settings -> Devices -> Pair new device

// ---- Power ----
// POWER_BATTERY: deep sleep between polls (LiPo, weeks of runtime).
// POWER_USB:     stay awake and wait-loop (short refresh intervals OK).
#define POWER_BATTERY 0
#define POWER_USB     1
#define POWER_MODE    POWER_USB

// Poll interval (seconds) used until the server supplies one, and as the
// ceiling for error backoff.
#define FALLBACK_POLL_S 900

// Panel size, gamut and Tesserae kind come from board.h, picked by the
// PlatformIO env.

#define FW_VERSION "0.1.0"
