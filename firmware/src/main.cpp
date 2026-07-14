// Inkplate 6COLOR Tesserae client.
//
// Each cycle: connect Wi-Fi -> ensure paired -> GET /frame (ETag) ->
// paint on 200 -> POST /status -> sleep for the server-chosen interval.
// POWER_BATTERY deep-sleeps between cycles; POWER_USB wait-loops.

#include <Inkplate.h>
#include <WiFi.h>

#include "config.h"
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
        showStatusScreen(display, "Pairing failed",
                         "Check PAIRING_CODE in config.h",
                         ("Server: " TESSERAE_BASE_URL),
                         "Generate a code: Settings > Devices");
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
        state.setFailures(++failures);
        Serial.printf("wifi failed (%u consecutive)\n", failures);
        if (failures == kFailuresBeforeScreen)
            showStatusScreen(display, "Wi-Fi unreachable",
                             ("SSID: " WIFI_SSID), "Still retrying...");
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    if (!ensurePaired()) {
        state.setFailures(++failures);
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    FrameEnvelope env;
    FetchResult res = client.fetchEnvelope(state.etag(), env);

    if (res == FetchResult::AuthError) {
        // Token revoked/instance deleted server-side: re-pair next cycle.
        Serial.println("auth error; clearing pairing");
        state.clear();
        state.setFailures(++failures);
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }
    if (res == FetchResult::Error) {
        state.setFailures(++failures);
        if (failures == kFailuresBeforeScreen)
            showStatusScreen(display, "Server unreachable",
                             ("URL: " TESSERAE_BASE_URL), "Still retrying...");
        return tesscore::backoffSeconds(failures, FALLBACK_POLL_S);
    }

    if (res == FetchResult::NewFrame) {
        size_t len = tesscore::packedSize4bpp(env.panelW, env.panelH);
        uint8_t *buf = (uint8_t *)ps_malloc(len);
        if (!buf)
            buf = (uint8_t *)malloc(len);
        if (buf && client.downloadFrame(env.url, buf, len)) {
            Serial.printf("painting %ux%u frame\n", env.panelW, env.panelH);
            paintFrame(display, buf, env.panelW, env.panelH);
            state.setEtag(env.etag);
        } else {
            // Keep the old image; tell the server.
            client.postLog("error", buf ? "frame download failed"
                                        : "frame buffer alloc failed");
        }
        free(buf);
    } else if (res == FetchResult::NoContent) {
        Serial.println("no page assigned to this device yet");
    } else {
        Serial.println("frame unchanged (304)");
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

void setup() {
    Serial.begin(115200);
    Serial.printf("\ninkplate-tesserae %s (%s mode)\n", FW_VERSION,
                  POWER_MODE == POWER_BATTERY ? "battery" : "usb");
    display.begin();
    state.begin();
    goToSleep(runCycle());
    // POWER_USB falls through to loop(); POWER_BATTERY never returns.
}

void loop() {
    goToSleep(runCycle());
}
