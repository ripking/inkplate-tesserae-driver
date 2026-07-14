#include <Inkplate.h>

#include "config.h"

Inkplate display;

void setup() {
    Serial.begin(115200);
    display.begin();
    Serial.printf("inkplate-tesserae %s scaffold; panel %dx%d\n", FW_VERSION,
                  PANEL_W, PANEL_H);
}

void loop() { delay(1000); }
