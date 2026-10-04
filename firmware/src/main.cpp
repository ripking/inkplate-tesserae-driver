// Inkplate Tesserae client (6COLOR, 13SPECTRA; see include/board.h).
//
// Each cycle: connect Wi-Fi -> ensure paired -> GET /frame (ETag) ->
// paint on 200 -> POST /status -> sleep for the server-chosen interval.
// POWER_BATTERY deep-sleeps between cycles; POWER_USB wait-loops.

#include <Inkplate.h>
#include <WiFi.h>

#include "board.h"
#include "frame_painter.h"
#include "state_store.h"
#include "status_screen.h"
#include "tesscore.h"
#include "tesserae_client.h"

static Inkplate display;
static StateStore state;
static TesseraeClient client(TESSERAE_BASE_URL, DEVICE_ID);

// Consecutive failures before we sacrifice the dashboard for an error
// screen. Transient errors keep the last frame visible.
static const uint8_t kFailuresBeforeScreen = 5;

// Saturating increment: never wraps a uint8_t failure counter past 255.
static uint8_t bumpFailures(uint8_t n) { return n < 255 ? n + 1 : n; }

static void goToSleep(uint32_t seconds) {
    Serial.printf("sleeping %us\n", seconds);
    Serial.flush();
#if POWER_MODE == POWER_BATTERY
    WiFi.disconnect(true);
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
#else
    uint32_t end = millis() + seconds * 1000UL;
    while ((int32_t)(end - millis()) > 0)
        delay(250);
#endif
}

static bool connectWifi() {
    if (WiFi.status() == WL_CONNECTED)
        return true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t deadline = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline)
        delay(250);
    return WiFi.status() == WL_CONNECTED;
}

static bool ensurePaired() {
    String token = state.token();
    if (token.length()) {
        client.setToken(token);
        return true;
    }
    Serial.println("no token; registering with pairing code");
    if (!client.registerDevice(PAIRING_CODE, DEVICE_NAME, WiFi.macAddress(),
                               token)) {
        Serial.println("pairing failed");
        return false;
    }
    state.setToken(token);
    state.setEtag("");
    client.setToken(token);
    Serial.println("paired OK");
    return true;
}

static int batteryMv() {
    double v = display.readBattery();
    return v > 0.5 ? (int)(v * 1000.0) : 0;
}

static int batteryPct(int mv) {
    if (mv <= 0)
        return 0;
    int pct = (mv - 3300) * 100 / (4200 - 3300); // server uses same curve
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

// One wake cycle. Returns seconds to sleep before the next one.
static uint32_t runCycle() {
    uint8_t failures = state.failures();

    if (!connectWifi()) {
        failures = bumpFailures(failures);
        state.setFailures(failures);
        Serial.printf("wifi failed (%u consecutive)\n", failures);
        if (failures == kFailuresBeforeScreen)
            showStatusScreen(display, "Wi-Fi unreachable",
                             ("SSID: " WIFI_SSID), "Still retrying...");
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    if (!ensurePaired()) {
        // Show help immediately only on a fresh device that has never
        // painted a dashboard; otherwise escalate like any other failure.
        bool freshDevice = failures == 0 && state.etag().length() == 0;
        failures = bumpFailures(failures);
        state.setFailures(failures);
        if (freshDevice || failures == kFailuresBeforeScreen)
            showStatusScreen(display, "Pairing failed",
                             "Check PAIRING_CODE in config.h",
                             ("Server: " TESSERAE_BASE_URL),
                             "Generate a code: Settings > Devices");
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    FrameEnvelope env;
    FetchResult res = client.fetchEnvelope(state.etag(), env);

    if (res == FetchResult::AuthError) {
        // Token revoked/instance deleted server-side: re-pair next cycle.
        Serial.println("auth error; clearing pairing");
        state.clear();
        // Failure counter is deliberately re-persisted after the wipe so
        // escalation continues across a re-pair rather than resetting.
        failures = bumpFailures(failures);
        state.setFailures(failures);
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }
    if (res == FetchResult::Error) {
        failures = bumpFailures(failures);
        state.setFailures(failures);
        if (failures == kFailuresBeforeScreen)
            showStatusScreen(display, "Server unreachable",
                             ("URL: " TESSERAE_BASE_URL), "Still retrying...");
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    bool cycleOk = true;
    if (res == FetchResult::NewFrame) {
        tesscore::FrameStride stride = tesscore::frameStride(
            env.panelW, env.panelH, env.nativeW, env.nativeH, PANEL_W, PANEL_H);
#ifndef PAINT_ROTATION_TRANSPOSED
        if (stride == tesscore::FrameStride::Transposed)
            stride = tesscore::FrameStride::Mismatch; // unverified on this board
#endif
        if (stride == tesscore::FrameStride::Mismatch) {
            Serial.printf("panel mismatch: server says %dx%d (native %dx%d), panel is %dx%d\n",
                          env.panelW, env.panelH, env.nativeW, env.nativeH,
                          PANEL_W, PANEL_H);
            client.postLog("error", "panel dimension mismatch; fix the device instance in the UI");
            cycleOk = false;
        } else {
            bool transposed = stride == tesscore::FrameStride::Transposed;
            int fw = transposed ? PANEL_H : PANEL_W;
            int fh = transposed ? PANEL_W : PANEL_H;
            size_t len = tesscore::packedSize4bpp(fw, fh);
            uint8_t *buf = (uint8_t *)ps_malloc(len);
            if (!buf)
                buf = (uint8_t *)malloc(len);
            if (buf && client.downloadFrame(env.url, buf, len)) {
                Serial.printf("painting %ux%u frame (canvas %ux%u)\n", fw, fh,
                              env.panelW, env.panelH);
#ifdef PAINT_ROTATION_TRANSPOSED
                paintFrame(display, buf, fw, fh,
                           transposed ? PAINT_ROTATION_TRANSPOSED : PAINT_ROTATION);
#else
                paintFrame(display, buf, fw, fh, PAINT_ROTATION);
#endif
                state.setEtag(env.etag);
            } else {
                // Keep the old image; tell the server.
                client.postLog("error", buf ? "frame download failed"
                                            : "frame buffer alloc failed");
                cycleOk = false;
            }
            free(buf);
        }
    } else if (res == FetchResult::NoContent) {
        Serial.println("no page assigned to this device yet");
    } else {
        Serial.println("frame unchanged (304)");
    }

    if (!cycleOk) {
        failures = bumpFailures(failures);
        state.setFailures(failures);
        if (failures == kFailuresBeforeScreen)
            showStatusScreen(display, "Frame updates failing",
                             "Dashboard may be stale", "Still retrying...");
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }
    state.setFailures(0);

    int mv = batteryMv();
    StatusReply reply;
    uint32_t plannedSleep = FALLBACK_POLL_S;
    if (client.postStatus(mv, batteryPct(mv), WiFi.RSSI(),
                          WiFi.localIP().toString(), plannedSleep, reply) &&
        reply.nextPollS > 0) {
        plannedSleep = reply.nextPollS;
    }
    return tesscore::clampInterval((long)plannedSleep, FALLBACK_POLL_S);
}

#ifdef PANEL_SELFTEST
// Bring-up check without Wi-Fi or a server: paints a synthetic frame in
// the board's wire format through the real painter. Native rows are split
// into bands of each frame colour in nibble order, and a black square
// marks frame (0,0), which should land top-left on a landscape mount.
static void paintSelfTest() {
#if defined(ARDUINO_INKPLATE13SPECTRA)
    static const uint8_t kNibbles[] = {0, 1, 2, 3, 5, 6};
#else
    static const uint8_t kNibbles[] = {0, 1, 2, 3, 4, 5, 6};
#endif
    const int n = sizeof(kNibbles);
    const int rowBytes = (PANEL_W + 1) / 2;
    size_t len = tesscore::packedSize4bpp(PANEL_W, PANEL_H);
    uint8_t *buf = (uint8_t *)ps_malloc(len);
    if (!buf) {
        Serial.println("selftest: alloc failed");
        return;
    }
    for (int y = 0; y < PANEL_H; y++) {
        uint8_t v = kNibbles[y * n / PANEL_H];
        memset(buf + (size_t)y * rowBytes, (v << 4) | v, rowBytes);
    }
    int sq = PANEL_W / 8; // black origin marker, inset on a white border
    for (int y = 0; y < sq + 8; y++)
        memset(buf + (size_t)y * rowBytes, 0x11, (sq + 8) / 2);
    for (int y = 4; y < sq + 4; y++)
        memset(buf + (size_t)y * rowBytes + 2, 0x00, sq / 2);
    Serial.printf("selftest: %d bands, origin marker %dpx\n", n, sq);
    paintFrame(display, buf, PANEL_W, PANEL_H, PAINT_ROTATION);
    free(buf);
}
#endif

void setup() {
    Serial.begin(115200);
    Serial.printf("\ninkplate-tesserae %s on %s (%s mode)\n", FW_VERSION,
                  BOARD_NAME, POWER_MODE == POWER_BATTERY ? "battery" : "usb");
    display.begin();
#ifdef PANEL_SELFTEST
    paintSelfTest();
    Serial.println("selftest done");
    for (;;)
        delay(1000);
#endif
    state.begin();
    goToSleep(runCycle());
    // POWER_USB falls through to loop(); POWER_BATTERY never returns.
}

void loop() {
    goToSleep(runCycle());
}
