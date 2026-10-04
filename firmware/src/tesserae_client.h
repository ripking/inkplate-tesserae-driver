// Tesserae device REST protocol (docs/dev/client-protocol.md upstream).
// Knows nothing about the panel.
#pragma once

#include <Arduino.h>

struct FrameEnvelope {
    String url;    // absolute URL of the packed .bin
    String format; // "bin"
    String etag;   // ETag header of the /frame response
    int panelW = 0;
    int panelH = 0;
    int nativeW = 0; // .bin row stride, if the server echoed it (else 0)
    int nativeH = 0;
};

struct StatusReply {
    uint32_t nextPollS = 0; // 0 = server didn't say
};

enum class FetchResult { NewFrame, NotModified, NoContent, AuthError, Error };

class TesseraeClient {
  public:
    TesseraeClient(const String &baseUrl, const String &deviceId)
        : _base(baseUrl), _id(deviceId) {}

    void setToken(const String &token) { _token = token; }

    // POST /api/v1/device/register with X-Pairing-Code. True on 200/201;
    // fills tokenOut.
    bool registerDevice(const String &pairingCode, const String &name,
                        const String &mac, String &tokenOut);

    // GET /api/v1/device/<id>/frame with optional If-None-Match.
    FetchResult fetchEnvelope(const String &etag, FrameEnvelope &out);

    // GET url (no auth needed for /renders/). True iff exactly
    // expectedLen bytes were read into buf.
    bool downloadFrame(const String &url, uint8_t *buf, size_t expectedLen);

    // POST /api/v1/device/<id>/status. True on 200; fills out.
    bool postStatus(int batteryMv, int batteryPct, int rssi, const String &ip,
                    uint32_t nextSleepS, StatusReply &out);

    // POST /api/v1/device/<id>/log. Best-effort, ignores failures.
    void postLog(const char *level, const String &msg);

  private:
    String _devicePath(const char *leaf) const {
        return _base + "/api/v1/device/" + _id + "/" + leaf;
    }
    String _base, _id, _token;
};
