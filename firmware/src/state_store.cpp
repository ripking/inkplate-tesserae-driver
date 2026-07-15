#include "state_store.h"

#include <Preferences.h>

// One shared handle; NVS namespace names are <=15 chars.
static Preferences prefs;

void StateStore::begin() { prefs.begin("tesserae", false); }

String StateStore::token() { return prefs.getString("token", ""); }
void StateStore::setToken(const String &t) { prefs.putString("token", t); }

String StateStore::etag() { return prefs.getString("etag", ""); }
void StateStore::setEtag(const String &e) { prefs.putString("etag", e); }

uint8_t StateStore::failures() { return prefs.getUChar("failures", 0); }
void StateStore::setFailures(uint8_t n) { prefs.putUChar("failures", n); }

void StateStore::clear() {
    prefs.remove("token");
    prefs.remove("etag");
    prefs.remove("failures");
}
