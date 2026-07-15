#pragma once

#include <Inkplate.h>

// Full-screen text notice. Costs a full ACeP refresh, so callers use it
// only for first-boot/pairing help and persistent failures - never for
// transient errors (those keep the last dashboard on screen).
void showStatusScreen(Inkplate &d, const char *title, const char *l1,
                      const char *l2 = nullptr, const char *l3 = nullptr);
