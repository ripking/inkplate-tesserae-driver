#include "tesserae_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "board.h"

static const uint32_t kHttpTimeoutMs = 20000;

bool TesseraeClient::registerDevice(const String &pairingCode,
                                    const String &name, const String &mac,
                                    String &tokenOut) {
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(_base + "/api/v1/device/register"))
        return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Pairing-Code", pairingCode);

    JsonDocument doc;
    doc["device_id"] = _id;
    doc["kind"] = "esp32_client";
    doc["panel_w"] = PANEL_W;
    doc["panel_h"] = PANEL_H;
    doc["gamut"] = PANEL_GAMUT;
    doc["name"] = name;
    doc["fw_version"] = FW_VERSION;
    doc["mac"] = mac;
    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    if (code != 200 && code != 201) {
        Serial.printf("register: HTTP %d %s\n", code, http.getString().c_str());
        http.end();
        return false;
    }
    JsonDocument reply;
    DeserializationError err = deserializeJson(reply, http.getStream());
    http.end();
    if (err || reply["device_token"].isNull()) {
        Serial.printf("register: bad reply (%s)\n", err.c_str());
        return false;
    }
    tokenOut = reply["device_token"].as<String>();
    return true;
}

FetchResult TesseraeClient::fetchEnvelope(const String &etag,
                                          FrameEnvelope &out) {
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(_devicePath("frame")))
        return FetchResult::Error;
    http.addHeader("Authorization", "Bearer " + _token);
    if (etag.length())
        http.addHeader("If-None-Match", etag);
    const char *collect[] = {"ETag"};
    http.collectHeaders(collect, 1);

    int code = http.GET();
    if (code == 304) {
        http.end();
        return FetchResult::NotModified;
    }
    if (code == 204) {
        http.end();
        return FetchResult::NoContent;
    }
    if (code == 401 || code == 403) {
        http.end();
        return FetchResult::AuthError;
    }
    if (code != 200) {
        Serial.printf("frame: HTTP %d\n", code);
        http.end();
        return FetchResult::Error;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    out.etag = http.header("ETag");
    http.end();
    if (err)
        return FetchResult::Error;
    out.url = doc["url"].as<String>();
    out.format = doc["format"] | "bin";
    out.panelW = doc["panel_w"] | PANEL_W;
    out.panelH = doc["panel_h"] | PANEL_H;
    // Newer servers echo the stride the .bin is packed at when the panel
    // block records one; 0 when absent.
    out.nativeW = doc["native_w"] | 0;
    out.nativeH = doc["native_h"] | 0;
    return out.url.length() ? FetchResult::NewFrame : FetchResult::Error;
}

bool TesseraeClient::downloadFrame(const String &url, uint8_t *buf,
                                   size_t expectedLen) {
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(url))
        return false;
    int code = http.GET();
    if (code != 200) {
        Serial.printf("download: HTTP %d\n", code);
        http.end();
        return false;
    }
    WiFiClient *stream = http.getStreamPtr();
    size_t got = 0;
    uint32_t deadline = millis() + 60000;
    while (got < expectedLen && millis() < deadline) {
        if (!http.connected() && !stream->available())
            break;
        int n = stream->readBytes(buf + got, expectedLen - got);
        if (n > 0)
            got += n;
    }
    // A body longer than expectedLen means a server/format mismatch. Give
    // any trailing bytes a short window to arrive before deciding.
    bool extra = false;
    uint32_t graceDeadline = millis() + 250;
    while (millis() < graceDeadline) {
        if (stream->available() > 0) {
            extra = true;
            break;
        }
        if (!http.connected())
            break;
        delay(10);
    }
    http.end();
    if (got != expectedLen || extra) {
        Serial.printf("download: got %u of %u bytes (extra=%d)\n",
                      (unsigned)got, (unsigned)expectedLen, extra);
        return false;
    }
    return true;
}

bool TesseraeClient::postStatus(int batteryMv, int batteryPct, int rssi,
                                const String &ip, uint32_t nextSleepS,
                                StatusReply &out) {
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(_devicePath("status")))
        return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + _token);

    JsonDocument doc;
    if (batteryMv > 0) {
        doc["battery_mv"] = batteryMv;
        doc["battery_pct"] = batteryPct;
    }
    doc["rssi"] = rssi;
    doc["ip"] = ip;
    doc["next_sleep_s"] = nextSleepS;
    doc["fw_version"] = FW_VERSION;
    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("status: HTTP %d\n", code);
        http.end();
        return false;
    }
    JsonDocument reply;
    DeserializationError err = deserializeJson(reply, http.getStream());
    http.end();
    if (err)
        return false;
    long interval = reply["next_poll_s"] | 0L;
    if (interval <= 0)
        interval = reply["config"]["sleep_interval_s"] | 0L;
    out.nextPollS = interval > 0 ? (uint32_t)interval : 0;
    return true;
}

void TesseraeClient::postLog(const char *level, const String &msg) {
    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(_devicePath("log")))
        return;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + _token);
    JsonDocument doc;
    doc["level"] = level;
    doc["msg"] = msg;
    String body;
    serializeJson(doc, body);
    http.POST(body); // best-effort
    http.end();
}
