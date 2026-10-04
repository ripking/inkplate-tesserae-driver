#include "status_screen.h"

#include "board.h"

void showStatusScreen(Inkplate &d, const char *title, const char *l1,
                      const char *l2, const char *l3) {
    // Same orientation as a landscape dashboard.
    d.setRotation(PAINT_ROTATION);
    // Built-in font is 6x8 px per size step; scale to the panel width
    // (size 3/2 on the 600 px 6COLOR, proportionally larger on bigger panels).
    int unit = d.width() / 300;
    if (unit < 1)
        unit = 1;
    int margin = 10 * unit;
    d.clearDisplay();
    d.setTextColor(INKPLATE_BLACK);
    d.setTextSize(unit + unit / 2);
    d.setCursor(margin, 20 * unit);
    d.print(title);
    d.setTextSize(unit);
    int y = 55 * unit;
    for (const char *line : {l1, l2, l3}) {
        if (!line)
            continue;
        d.setCursor(margin, y);
        d.print(line);
        y += 18 * unit;
    }
    d.display();
    d.setRotation(0);
}
