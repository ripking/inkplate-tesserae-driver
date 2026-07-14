#include "status_screen.h"

void showStatusScreen(Inkplate &d, const char *title, const char *l1,
                      const char *l2, const char *l3) {
    d.clearDisplay();
    d.setTextColor(INKPLATE_BLACK);
    d.setTextSize(3);
    d.setCursor(20, 40);
    d.print(title);
    d.setTextSize(2);
    int y = 110;
    for (const char *line : {l1, l2, l3}) {
        if (!line)
            continue;
        d.setCursor(20, y);
        d.print(line);
        y += 36;
    }
    d.display();
}
