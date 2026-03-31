#include "DisplayManager.h"
#include <TJpg_Decoder.h>

DisplayManager displayMgr;

#define SHARED_RST 26

// Callback function for TJpg_Decoder to render blocks to the TFT
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= displayMgr.tft.height()) return false;
    displayMgr.tft.pushImage(x, y, w, h, bitmap);
    return true;
}

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
        tft.setSwapBytes(true);
        tft.fillScreen(TFT_BLACK);
        unselectAll();
        delay(50);
    }

    sprite = new TFT_eSprite(&tft);
    sprite->setColorDepth(8);
    sprite->createSprite(240, 240);

    // Initialize TJpg_Decoder
    TJpgDec.setCallback(tft_output);
    TJpgDec.setJpgScale(1);
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

void DisplayManager::drawLogoToDisplay(uint8_t index, const char* filename) {
    if (!LittleFS.exists(filename)) return;
    
    selectDisplay(index);
    TJpgDec.drawFsJpg(0, 0, filename, LittleFS);
    unselectAll();
}

void DisplayManager::drawLogoFromFile(const char* filename) {
    if (!LittleFS.exists(filename)) {
        Serial.printf("Logo file %s not found in LittleFS!\n", filename);
        return;
    }
    Serial.printf("Drawing logo %s to all screens...\n", filename);

    for(int i = 0; i < NUM_DISPLAYS; i++) {
        selectDisplay(i);
        TJpgDec.drawFsJpg(0, 0, filename, LittleFS);
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

// p=0 → yellow, p=1 → red
uint16_t getWarningColor(float p) {
    uint8_t r = 255;
    uint8_t g = (uint8_t)(255.0f * (1.0f - p));
    uint8_t b = 0;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Draw a single arc slice, handling the 0/360 wrap boundary.
// arcS and arcE are already normalised to 0-360.
static void drawArcSlice(TFT_eSprite* sprite, int px, int py, int radius, int arcWidth, int arcS, int arcE, uint16_t col, uint16_t bgCol = TFT_BLACK) {
    if (arcS == arcE) return;
    if (arcE > arcS) {
        sprite->drawSmoothArc(px, py, radius, radius - arcWidth, arcS, arcE, col, bgCol);
    } else {
        // Slice wraps through 0/360
        sprite->drawSmoothArc(px, py, radius, radius - arcWidth, arcS, 360, col, bgCol);
        sprite->drawSmoothArc(px, py, radius, radius - arcWidth, 0,    arcE, col, bgCol);
    }
}

void DisplayManager::drawGauge(uint8_t index, const char* title, float value, float min_val, float max_val, const char* unit, uint16_t theme_color, float percentage, int numRanges, DisplayRange* ranges, bool failed) {
    sprite->fillSprite(TFT_BLACK);

    const int   px       = 120;
    const int   py       = 120;
    const int   radius   = 118;
    const int   arcWidth = 15;
    const uint16_t bgTrackCol = 0x2104; // Very dark grey for the track

    const float startAngle = 240.0f;
    const float endAngle   = 480.0f;
    const float sweep      = endAngle - startAngle; // 240°

    float totalRange = max_val - min_val;
    if (totalRange <= 0) totalRange = 1.0f;

    auto toClockAngle = [](float logicalAngle) -> int {
        // Shift by -180 to match the (rad = (logAngle - 180) * PI/180) used by ticks/needle
        float a = fmod(logicalAngle - 180.0f, 360.0f);
        if (a < 0) a += 360.0f;
        return (int)a;
    };


    // -------------------------------------------------------------------------
    // 1. BACKGROUND TRACK & COLOUR ZONES
    // -------------------------------------------------------------------------
    // Draw the full background track first
    drawArcSlice(sprite, px, py, radius, arcWidth, toClockAngle(startAngle), toClockAngle(endAngle), bgTrackCol, TFT_BLACK);

    if (ranges && numRanges > 0) {
        for (int i = 0; i < numRanges; i++) {
            if (ranges[i].redzone <= 0) continue;

            float r_min = max(ranges[i].min, min_val);
            float r_max = min(ranges[i].max, max_val);
            if (r_max <= r_min) continue;

            float zoneAngleStart = startAngle + ((r_min - min_val) / totalRange) * sweep;
            float zoneAngleEnd   = startAngle + ((r_max - min_val) / totalRange) * sweep;

            // Step through the zone in larger slices for performance
            for (float a = zoneAngleStart; a < zoneAngleEnd; a += 6.0f) {
                float sliceEnd = min(a + 6.5f, zoneAngleEnd);
                float p = (a - zoneAngleStart) / (zoneAngleEnd - zoneAngleStart);
                
                // If the zone is on the left half (before 12 o'clock / 360°), 
                // invert p so the gradient runs towards Yellow at the center.
                if (zoneAngleEnd <= 360.1f) {
                    p = 1.0f - p;
                }

                float rampP = (ranges[i].redzone == 1) ? (p * 0.5f) : (0.5f + p * 0.5f);
                uint16_t col = getWarningColor(rampP);

                drawArcSlice(sprite, px, py, radius, arcWidth, toClockAngle(a), toClockAngle(sliceEnd), col, bgTrackCol);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 2. TICKS AND SCALE NUMBERS
    // -------------------------------------------------------------------------
    sprite->setTextDatum(MC_DATUM);
    for (int i = 0; i <= 20; i++) {
        float logAngle = startAngle + (i / 20.0f) * sweep;
        float rad      = (logAngle - 90.0f) * 0.0174533f;
        float tickVal  = min_val + (i / 20.0f) * totalRange;
        bool  major    = (i % 4 == 0);
        int   tLen     = major ? 12 : 6;
        uint16_t tCol  = major ? TFT_WHITE : TFT_SILVER;

        int x1 = px + (int)(cos(rad) * (radius - arcWidth - 2));
        int y1 = py + (int)(sin(rad) * (radius - arcWidth - 2));
        int x2 = px + (int)(cos(rad) * (radius - arcWidth - 2 - tLen));
        int y2 = py + (int)(sin(rad) * (radius - arcWidth - 2 - tLen));
        sprite->drawLine(x1, y1, x2, y2, tCol);

        if (major) {
            int lx = px + (int)(cos(rad) * (radius - 45));
            int ly = py + (int)(sin(rad) * (radius - 45));
            sprite->setTextColor(TFT_WHITE);
            String tickStr = (totalRange < 10) ? String(tickVal, 1) : String((int)tickVal);
            sprite->drawString(tickStr, lx, ly, 1);
        }
    }

    // -------------------------------------------------------------------------
    // 3. NEEDLE
    // -------------------------------------------------------------------------
    float val_clamped = constrain(value, min_val, max_val);
    float needleAngle = startAngle + ((val_clamped - min_val) / totalRange) * sweep;
    float nRad        = (needleAngle - 90.0f) * 0.0174533f;
    int   nx          = px + (int)(cos(nRad) * (radius - 5));
    int   ny          = py + (int)(sin(nRad) * (radius - 5));
    sprite->drawWideLine(px, py, nx, ny, 3, theme_color, TFT_BLACK);
    sprite->fillCircle(px, py, 8, TFT_LIGHTGREY);
    sprite->fillCircle(px, py, 4, TFT_BLACK);

    // -------------------------------------------------------------------------
    // 4. TEXT
    // -------------------------------------------------------------------------
    sprite->setTextDatum(TC_DATUM);
    sprite->setTextColor(TFT_WHITE);
    String valStr = String(value, (value >= 100.0f) ? 1 : 2) + " " + String(unit);
    sprite->drawString(valStr, 120, 70, 4);

    sprite->setTextColor(failed ? TFT_RED : TFT_WHITE);
    sprite->drawString(title, 120, 185, 4);

    selectDisplay(index);
    sprite->pushSprite(0, 0);
    unselectAll();
}
