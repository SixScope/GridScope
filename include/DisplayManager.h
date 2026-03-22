#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <TFT_eSPI.h>
#include "logo.h"

#define NUM_DISPLAYS 6

struct DisplayRange {
    float min;
    float max;
    int redzone;
};

// CS pins for each display
const uint8_t csPins[NUM_DISPLAYS] = {5, 17, 16, 2, 12, 13};

class DisplayManager {
public:
    DisplayManager();
    void begin();
    void selectDisplay(uint8_t index);
    void unselectAll();
    void clearAll();
    void drawLogoAll();
    void drawText(uint8_t index, const char* text, int x, int y, int size);
    void drawGauge(uint8_t index, const char* title, float value, float min_val, float max_val, const char* unit, uint16_t theme_color, float percentage, int numRanges, DisplayRange* ranges, bool failed = false);
    void drawFooter(uint8_t index, const char* text);
    
    TFT_eSPI tft = TFT_eSPI(); 
    TFT_eSprite* sprite;

private:
    void drawWrappedArc(int x, int y, int r, int ir, int start, int end, uint16_t color);
};

extern DisplayManager displayMgr;

#endif
