#include "../include/DisplayManager.h"

DisplayManager displayMgr;

#define SHARED_RST 26

DisplayManager::DisplayManager() {
    sprite = nullptr;
}

void DisplayManager::begin() {
    for(int i = 0; i < NUM_DISPLAYS; i++) {
        pinMode(csPins[i], OUTPUT);
        digitalWrite(csPins[i], HIGH);
    }
    pinMode(SHARED_RST, OUTPUT);
    digitalWrite(SHARED_RST, HIGH);
    delay(50);
    digitalWrite(SHARED_RST, LOW);
    delay(50);
    digitalWrite(SHARED_RST, HIGH);
    delay(150); 
    
    for(int i = 0; i < NUM_DISPLAYS; i++) {
        selectDisplay(i);
        tft.init(); 
        tft.setRotation(0);
        tft.fillScreen(TFT_BLACK);
        unselectAll();
        delay(50);
    }
    
    sprite = new TFT_eSprite(&tft);
    sprite->setColorDepth(8);
    sprite->createSprite(240, 240);
}

void DisplayManager::selectDisplay(uint8_t index) {
    for(int i = 0; i < NUM_DISPLAYS; i++) digitalWrite(csPins[i], HIGH);
    if(index < NUM_DISPLAYS) digitalWrite(csPins[index], LOW);
}

void DisplayManager::unselectAll() {
    for(int i = 0; i < NUM_DISPLAYS; i++) digitalWrite(csPins[i], HIGH);
}

void DisplayManager::clearAll() {
    for(int i = 0; i < NUM_DISPLAYS; i++) {
        selectDisplay(i);
        tft.fillScreen(TFT_BLACK);
    }
    unselectAll();
}

void DisplayManager::drawLogoAll() {
    for(int i = 0; i < NUM_DISPLAYS; i++) {
        selectDisplay(i);
        tft.pushImage(0, 0, 240, 240, logo_img);
    }
    unselectAll();
}

void DisplayManager::drawText(uint8_t index, const char* text, int x, int y, int size) {
    selectDisplay(index);
    tft.setTextSize(size);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, x, y);
    unselectAll();
}

void DisplayManager::drawFooter(uint8_t index, const char* text) {
    selectDisplay(index);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(BC_DATUM);
    tft.drawString(text, 120, 230);
    unselectAll();
}

void DisplayManager::drawWrappedArc(int x, int y, int r, int ir, int start, int end, uint16_t color) {
    int s = start % 360;
    int e = end % 360;
    if (e == 0 && end > start) e = 360;
    if (end - start >= 360) {
        sprite->drawSmoothArc(x, y, r, ir, 0, 360, color, TFT_BLACK);
        return;
    }
    if (e > s) {
        sprite->drawSmoothArc(x, y, r, ir, s, e, color, TFT_BLACK);
    } else {
        sprite->drawSmoothArc(x, y, r, ir, s, 360, color, TFT_BLACK);
        sprite->drawSmoothArc(x, y, r, ir, 0, e, color, TFT_BLACK);
    }
}

uint16_t getWarningColor(float p) {
    uint8_t r = 255;
    uint8_t g = 255 - (p * 255);
    uint8_t b = 0;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void DisplayManager::drawGauge(uint8_t index, const char* title, float value, float min_val, float max_val, const char* unit, uint16_t theme_color, float percentage, int numRanges, DisplayRange* ranges) {
    sprite->fillSprite(TFT_BLACK);
    
    int px = 120; int py = 120; int radius = 118; int arcWidth = 15; 
    float startAngle = 240.0f; // 8 o'clock
    float endAngle = 480.0f;   // 4 o'clock
    float sweep = endAngle - startAngle;
    float totalRange = max_val - min_val;
    if (totalRange <= 0) totalRange = 1.0f;

    // 1. STATIC BACKGROUND ZONES (Gradient)
    if (ranges && numRanges > 0) {
        for (int i = 0; i < numRanges; i++) {
            if (ranges[i].redzone > 0) {
                float r_min = max(ranges[i].min, min_val);
                float r_max = min(ranges[i].max, max_val);
                if (r_max > r_min) {
                    float a_start = startAngle + ((r_min - min_val) / totalRange) * sweep;
                    float a_end = startAngle + ((r_max - min_val) / totalRange) * sweep;
                    for (float a = a_start; a < a_end; a += 2.0f) {
                        float p = (a - a_start) / (a_end - a_start);
                        uint16_t col = (ranges[i].redzone == 1) ? getWarningColor(p * 0.5f) : getWarningColor(0.5f + p * 0.5f);
                        drawWrappedArc(px, py, radius, radius - arcWidth, (int)a, (int)min(a + 2.5f, a_end), col);
                    }
                }
            }
        }
    }

    // 2. Ticks and Scale Numbers
    sprite->setTextDatum(MC_DATUM);
    for (int i = 0; i <= 20; i++) {
        float angle = startAngle + (i / 20.0f) * sweep;
        float rad = (angle - 90.0f) * 0.0174533f;
        float tickVal = min_val + (i / 20.0f) * totalRange;
        bool major = (i % 4 == 0); 
        int tLen = major ? 12 : 6;
        uint16_t tCol = major ? TFT_WHITE : TFT_SILVER;
        int x1 = px + cos(rad) * (radius - arcWidth - 2);
        int y1 = py + sin(rad) * (radius - arcWidth - 2);
        int x2 = px + cos(rad) * (radius - arcWidth - 2 - tLen);
        int y2 = py + sin(rad) * (radius - arcWidth - 2 - tLen);
        sprite->drawLine(x1, y1, x2, y2, tCol);
        if (major) {
            int lx = px + cos(rad) * (radius - 45); 
            int ly = py + sin(rad) * (radius - 45);
            sprite->setTextColor(TFT_WHITE);
            sprite->drawString(String((int)tickVal), lx, ly, 1);
        }
    }

    // 3. Needle
    float val_clamped = constrain(value, min_val, max_val);
    float needleAngle = startAngle + ((val_clamped - min_val) / totalRange) * sweep;
    float nRad = (needleAngle - 90.0f) * 0.0174533f;
    int nx = px + cos(nRad) * (radius - 5);
    int ny = py + sin(nRad) * (radius - 5);
    sprite->drawWideLine(px, py, nx, ny, 3, theme_color, TFT_BLACK);
    sprite->fillCircle(px, py, 8, TFT_LIGHTGREY);
    sprite->fillCircle(px, py, 4, TFT_BLACK);

    // 4. Texts
    sprite->setTextDatum(TC_DATUM);
    sprite->setTextColor(TFT_WHITE);
    String valStr = (value >= 100 || strcmp(unit, "Hz") == 0) ? String(value, 1) : String(value, 2);
    valStr += " " + String(unit);
    sprite->drawString(valStr, 120, 55, 4); 
    
    if (percentage >= 0) {
        sprite->setTextColor(TFT_CYAN);
        String pctStr = String(percentage, 1) + "%";
        sprite->drawString(pctStr, 120, 145, 4);
    }

    sprite->setTextColor(TFT_WHITE);
    sprite->drawString(title, 120, 185, 4);
    
    selectDisplay(index);
    sprite->pushSprite(0, 0);
    unselectAll();
}
