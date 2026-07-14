// Persistent device state in ESP32 NVS: bearer token, last frame ETag,
// consecutive-failure counter. Survives deep sleep and reflash.
#pragma once

#include <Arduino.h>

class StateStore {
  public:
    void begin();

    String token();
    void setToken(const String &t);

    String etag();
    void setEtag(const String &e);

    uint8_t failures();
    void setFailures(uint8_t n);

    void clear(); // wipe pairing (token + etag)
};
