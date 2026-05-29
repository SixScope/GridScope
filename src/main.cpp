#include <Arduino.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <Adafruit_VEML7700.h>
#include <time.h>
#include <ESPmDNS.h>
#include "qrcode.h"
#include "DisplayManager.h"
#include "DataManager.h"
#include "WebConfig.h"

Adafruit_VEML7700 veml = Adafruit_VEML7700();
WiFiManager wifiManager;
bool hasVeml = false;
const int LDR_PIN = 3;
volatile bool displaysNeedRefresh = false;
volatile bool forceFetchRequest = false;
volatile bool forceDisplaysRefresh = false;
volatile int lastFilteredLdr = 0;

void drawQRCode(uint8_t screenIdx, const char* text) {
    QRCode qrcode; uint8_t qrcodeData[qrcode_getBufferSize(4)]; 
    qrcode_initText(&qrcode, qrcodeData, 4, 0, text);
    displayMgr.selectDisplay(screenIdx);
    displayMgr.tft.fillScreen(TFT_WHITE);
    int scale = 5; int offset = (240 - (qrcode.size * scale)) / 2;
    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                displayMgr.tft.fillRect(offset + x * scale, offset + y * scale, scale, scale, TFT_BLACK);
            }
        }
    }
    displayMgr.unselectAll();
}

void configModeCallback(WiFiManager *myWiFiManager) {
    String ssid = myWiFiManager->getConfigPortalSSID();
    displayMgr.drawLogoToDisplay(0, "/logo.jpg");
    drawQRCode(1, ("WIFI:T:WPA;S:" + ssid + ";P:;;").c_str());
    displayMgr.selectDisplay(2); displayMgr.tft.fillScreen(TFT_BLACK); displayMgr.tft.setTextColor(TFT_WHITE); displayMgr.tft.setTextDatum(MC_DATUM);
    displayMgr.tft.drawString("WIFI SETUP", 120, 70, 4); displayMgr.tft.setTextColor(TFT_YELLOW); displayMgr.tft.drawString(ssid, 120, 140, 4); displayMgr.unselectAll();
    displayMgr.selectDisplay(3); displayMgr.tft.fillScreen(TFT_BLACK); displayMgr.tft.setTextColor(TFT_WHITE); displayMgr.tft.setTextDatum(MC_DATUM);
    displayMgr.tft.drawString("WiFi connection", 120, 60, 4); displayMgr.tft.drawString("is required.", 120, 100, 4);
    displayMgr.tft.drawString("Connect phone", 120, 160, 4); displayMgr.tft.drawString("to set up.", 120, 200, 4); displayMgr.unselectAll();
    displayMgr.selectDisplay(4); displayMgr.tft.fillScreen(TFT_BLACK); displayMgr.tft.setTextColor(TFT_WHITE); displayMgr.tft.setTextDatum(MC_DATUM);
    displayMgr.tft.drawString("Open browser to:", 120, 80, 4); displayMgr.tft.setTextColor(TFT_YELLOW); displayMgr.tft.drawString("192.168.4.1", 120, 150, 4); displayMgr.unselectAll();
    displayMgr.drawLogoToDisplay(5, "/logo.jpg");
}

void updateDisplays(bool force = false) {
    static float lastVal[6] = {-1e9, -1e9, -1e9, -1e9, -1e9, -1e9};
    static int lastIdx[6] = {-1, -1, -1, -1, -1, -1};
    static uint16_t lastTheme = 99;

    Config cfg = webConfig.getConfig();
    bool themeChanged = (cfg.theme != lastTheme);
    lastTheme = cfg.theme;

    for(int i = 0; i < 6; i++) {
        int gaugeIdx = (int)cfg.screenData[i];
        GaugeConfig gcfg = dataMgr.getGaugeConfig(gaugeIdx);
        if (gcfg.id == "") continue;

        if (!force && !themeChanged && lastIdx[i] == gaugeIdx && lastVal[i] == gcfg.currentValue) {
            continue;
        }

        float val = gcfg.currentValue;
        float pct = gcfg.currentPct;
        float min_gauge = (gcfg.numRanges > 0) ? gcfg.ranges[0].min : 0;
        float max_gauge = (gcfg.numRanges > 0) ? gcfg.ranges[gcfg.numRanges-1].max : 100;

        uint16_t themeColor = (cfg.theme == 1) ? TFT_MAGENTA : TFT_GREENYELLOW;
        DisplayRange dranges[3];
        for(int r=0; r<gcfg.numRanges; r++) {
            dranges[r].min = gcfg.ranges[r].min; dranges[r].max = gcfg.ranges[r].max; dranges[r].redzone = gcfg.ranges[r].redzone;
        }

        displayMgr.drawGauge(i, gcfg.name.c_str(), val, min_gauge, max_gauge, gcfg.unit.c_str(), themeColor, pct, gcfg.numRanges, dranges, !gcfg.lastUpdateSuccess);
        
        if (i == 3) {
            displayMgr.selectDisplay(3);
            displayMgr.tft.setTextColor(TFT_GREENYELLOW); 
            displayMgr.tft.setTextDatum(BC_DATUM);
            int ldrVal = lastFilteredLdr;
            int ldrPct = (ldrVal * 100) / 4095;
            char buf[16];
            snprintf(buf, sizeof(buf), "LDR: %d%%", ldrPct);
            displayMgr.tft.drawString(buf, 120, 230, 2);
            displayMgr.unselectAll();
        }
        if (i == 4) {
            displayMgr.selectDisplay(4);
            displayMgr.tft.setTextColor(TFT_DARKGREY); 
            displayMgr.tft.setTextDatum(BC_DATUM);
            displayMgr.tft.drawString("gridscope.local", 120, 230, 2);
            displayMgr.unselectAll();
        }
        if (i == 5) { 
            displayMgr.selectDisplay(5);
            displayMgr.tft.setTextColor(TFT_GREENYELLOW); 
            displayMgr.tft.setTextDatum(BC_DATUM);
            displayMgr.tft.drawString(WiFi.localIP().toString(), 120, 230, 2);
            displayMgr.unselectAll();
        }

        lastVal[i] = val;
        lastIdx[i] = gaugeIdx;
    }
}

void dataFetchTask(void *parameter) {
    unsigned long lastFreqUpdate = 0;
    unsigned long lastDataUpdate = 0;
    const unsigned long FREQ_UPDATE_INTERVAL = 60000;
    const unsigned long UPDATE_INTERVAL = 300000;
    
    // Wait until connected to WiFi
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Perform initial fetches immediately in the background
    lastFreqUpdate = 0;
    lastDataUpdate = 0;
    
    while (true) {
        if (WiFi.status() == WL_CONNECTED) {
            bool needsRefresh = false;
            unsigned long now = millis();
            
            if (forceFetchRequest) {
                forceFetchRequest = false;
                logMsg("Forcing immediate background data fetch...");
                if (dataMgr.fetchElexonFrequency()) {
                    needsRefresh = true;
                }
                lastFreqUpdate = now;
            }
            
            // Background frequency fetch
            if (now - lastFreqUpdate >= FREQ_UPDATE_INTERVAL || lastFreqUpdate == 0) {
                if (dataMgr.fetchElexonFrequency()) {
                    needsRefresh = true;
                }
                lastFreqUpdate = now;
            }
            
            // Background general data fetch
            if (now - lastDataUpdate >= UPDATE_INTERVAL || lastDataUpdate == 0) {
                if (dataMgr.updateData()) {
                    needsRefresh = true;
                }
                lastDataUpdate = now;
            }
            
            if (needsRefresh) {
                displaysNeedRefresh = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("--- Booting GridScope ---");

    // Initialize Filesystem early for logos
    if(!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
    }

    displayMgr.begin();
    
    // 1. On boot show logos on all screens
    displayMgr.drawLogoFromFile("/logo.jpg");
    
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(TFT_BL, 5000, 8); ledcWrite(TFT_BL, 255);
#else
    ledcSetup(0, 5000, 8); ledcAttachPin(TFT_BL, 0); ledcWrite(0, 255);
#endif
    Serial.println("Display Manager initialized. Setting up I2C...");
    Wire.begin(21, 22);
    Serial.println("Initializing VEML7700...");
    if (veml.begin()) {
        hasVeml = true;
        Serial.println("VEML7700 found!");
    } else {
        Serial.println("VEML7700 not found, using LDR.");
    }
    WiFi.setHostname("gridscope");
    WiFi.persistent(true); WiFi.setAutoReconnect(true); WiFi.mode(WIFI_STA);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    WiFi.enableIPv6();
#else
    WiFi.enableIpV6();
#endif
    
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setConnectTimeout(10);
    wifiManager.setConfigPortalTimeout(0); 
    wifiManager.setCaptivePortalEnable(true); 
    wifiManager.setConfigPortalBlocking(true); 

    String savedSSID = WiFi.SSID();
    bool connected = false;

    if (savedSSID.length() > 0) {
        // 2. If there is a saved WiFi attempt to connect
        displayMgr.selectDisplay(0);
        displayMgr.tft.fillScreen(TFT_BLACK);
        displayMgr.tft.setTextColor(TFT_WHITE);
        displayMgr.tft.setTextDatum(MC_DATUM);
        displayMgr.tft.drawString("Trying to connect:", 120, 80, 4);
        displayMgr.tft.setTextColor(TFT_YELLOW);
        displayMgr.tft.drawString(savedSSID, 120, 140, 4);
        displayMgr.unselectAll();

        WiFi.begin();
        
        // 3. Wait up to 10 seconds.
        unsigned long startAttempt = millis();
        while (millis() - startAttempt < 10000) {
            if (WiFi.status() == WL_CONNECTED) { 
                connected = true; 
                break; 
            }
            delay(100);
        }

        if (connected) {
            displayMgr.selectDisplay(0);
            displayMgr.tft.fillScreen(TFT_BLACK);
            displayMgr.tft.setTextColor(TFT_CYAN);
            displayMgr.tft.setTextDatum(MC_DATUM);
            displayMgr.tft.drawString("Obtaining IP...", 120, 120, 4);
            displayMgr.unselectAll();
        }

        // 4. If after 10 seconds, clear displays
        if (!connected) {
            displayMgr.clearAll();
            
            // 5. Loop for another 10 seconds showing status and countdown
            unsigned long startCountdown = millis();
            while (millis() - startCountdown < 10000) {
                int remaining = 10 - (millis() - startCountdown) / 1000;
                
                // Screen 1: "Waiting for WiFi:", "[AP name]"
                displayMgr.selectDisplay(0);
                displayMgr.tft.fillScreen(TFT_BLACK);
                displayMgr.tft.setTextColor(TFT_WHITE);
                displayMgr.tft.setTextDatum(MC_DATUM);
                displayMgr.tft.drawString("Waiting for WiFi:", 120, 80, 4);
                displayMgr.tft.setTextColor(TFT_YELLOW);
                displayMgr.tft.drawString(savedSSID, 120, 140, 4);
                
                // Screen 2: Countdown timer
                displayMgr.selectDisplay(1);
                displayMgr.tft.fillScreen(TFT_BLACK);
                displayMgr.tft.setTextColor(TFT_WHITE);
                displayMgr.tft.setTextDatum(MC_DATUM);
                displayMgr.tft.drawString("Setup in:", 120, 80, 4);
                displayMgr.tft.setTextColor(TFT_GREENYELLOW);
                displayMgr.tft.drawString(String(remaining) + "s", 120, 140, 4);
                
                displayMgr.unselectAll();

                if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
                delay(500);
            }
        }
    }

    // 6. If countdown reaches 0 (or no saved WiFi), go to WiFi provisioning
    if (!connected) {
        if(!wifiManager.autoConnect("GridScope_AP")) { delay(1000); ESP.restart(); }
    }

    // Main App Initialization
    webConfig.begin();
    if (MDNS.begin("gridscope")) {
        MDNS.addService("http", "tcp", 80);
        logMsg("mDNS responder started (gridscope.local)");
    } else {
        logMsg("Failed to start mDNS responder");
    }
    dataMgr.begin(); // DataManager also calls LittleFS.begin(true)
    
    // 7. Try to get ntp time
    displayMgr.selectDisplay(0);
    displayMgr.tft.fillScreen(TFT_BLACK);
    displayMgr.tft.setTextColor(TFT_GREENYELLOW);
    displayMgr.tft.setTextDatum(MC_DATUM);
    displayMgr.tft.drawString("Getting NTP Time...", 120, 120, 4);
    displayMgr.unselectAll();

    delay(500);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    // Wait up to 5s for initial NTP sync
    unsigned long ntpStart = millis();
    while (millis() - ntpStart < 5000) {
        time_t now = time(nullptr);
        if (now > 1000000) break;
        delay(100);
    }
    
    logMsg("System ready. Fetching data...");
    
    displayMgr.selectDisplay(0);
    displayMgr.tft.fillScreen(TFT_BLACK);
    displayMgr.tft.setTextColor(TFT_YELLOW);
    displayMgr.tft.setTextDatum(MC_DATUM);
    displayMgr.tft.drawString("Querying API...", 120, 120, 4);
    displayMgr.unselectAll();

    // 8. Spawn FreeRTOS task on Core 1 for background network fetches
    Serial.printf("[Diagnostics] PSRAM Found: %s, Size: %u, Free: %u\n", 
                  psramFound() ? "YES" : "NO", ESP.getPsramSize(), ESP.getFreePsram());
    Serial.printf("[Diagnostics] Free Heap: %u, Max Block: %u\n", 
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    BaseType_t res = xTaskCreatePinnedToCore(
        dataFetchTask,
        "dataFetchTask",
        8192,
        NULL,
        1,
        NULL,
        1
    );
    Serial.printf("[Diagnostics] Task Creation Result: %s\n", 
                  (res == pdPASS) ? "SUCCESS" : "FAILED");
}

void updateBacklight() {
    static int lastLdrPct = -1;
    static int lastPwmVal = -1;
    static uint8_t lastMinPwm = 255;
    static uint8_t lastMinLdrPct = 255;
    static uint8_t lastMaxLdrPct = 255;

    if (hasVeml) {
        float lux = veml.readLux();
        int pwm = map(lux, 0, 1000, 50, 255);
        pwm = constrain(pwm, 10, 255);
        if (pwm != lastPwmVal) {
            lastPwmVal = pwm;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
            ledcWrite(TFT_BL, pwm);
#else
            ledcWrite(0, pwm);
#endif
        }
    } else {
        int rawLdr = analogRead(LDR_PIN);
        
        static float filteredLdr = -1.0f;
        if (filteredLdr < 0) {
            filteredLdr = rawLdr;
        } else {
            // Apply Adaptive Exponential Moving Average (EMA) filter.
            // Small changes (noise) use a small alpha (0.03) for maximum stability.
            // Large changes scale alpha up to 0.5 for fast response (approx. 500ms).
            float diff = abs(rawLdr - filteredLdr);
            float ratio = diff / 1000.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            float alpha = 0.03f + (0.12f * ratio); // Cap maximum alpha at 0.15 to keep noise filtered
            filteredLdr = (alpha * rawLdr) + ((1.0f - alpha) * filteredLdr);
        }
        int ldrVal = (int)(filteredLdr + 0.5f);
        lastFilteredLdr = ldrVal;
        
        Config cfg = webConfig.getConfig();
        
        // Map LDR starting from darkness threshold (cfg.minLdrPct) up to daylight threshold (cfg.maxLdrPct)
        int minLdrVal = (cfg.minLdrPct * 4095) / 100;
        int maxLdrVal = (cfg.maxLdrPct * 4095) / 100;
        if (maxLdrVal <= minLdrVal) maxLdrVal = minLdrVal + 1; // Ensure threshold is strictly above darkness limit
        
        int mappedLdr = ldrVal;
        if (mappedLdr < minLdrVal) mappedLdr = minLdrVal; // Clamp values below darkness floor
        if (mappedLdr > maxLdrVal) mappedLdr = maxLdrVal; // Clamp to threshold to prevent extrapolation in map()
        
        int pwm = map(mappedLdr, minLdrVal, maxLdrVal, cfg.minPwm, 255);
        pwm = constrain(pwm, 0, 255);
        
        // Slew the physical PWM slowly towards the target PWM to prevent feedback oscillations
        static float currentPwm = -1.0f;
        if (currentPwm < 0) {
            currentPwm = pwm;
        } else {
            // Limit change rate to max 8 PWM steps per 100ms (approx 3 seconds for full transition)
            float diff = pwm - currentPwm;
            float maxStep = 8.0f;
            if (diff > maxStep) diff = maxStep;
            if (diff < -maxStep) diff = -maxStep;
            currentPwm += diff;
        }
        int finalPwm = (int)(currentPwm + 0.5f);
        
        // Write to PWM if PWM or config changed
        if (finalPwm != lastPwmVal || cfg.minPwm != lastMinPwm || cfg.minLdrPct != lastMinLdrPct || cfg.maxLdrPct != lastMaxLdrPct) {
            lastPwmVal = finalPwm;
            lastMinPwm = cfg.minPwm;
            lastMinLdrPct = cfg.minLdrPct;
            lastMaxLdrPct = cfg.maxLdrPct;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
            ledcWrite(TFT_BL, finalPwm);
#else
            ledcWrite(0, finalPwm);
#endif
            Serial.printf("[Live Update] LDR Value: %d, TargetPWM: %d, FinalPWM: %d, MinPWM: %d, MinLDR: %d%%, MaxLDR: %d%%\n", 
                          ldrVal, pwm, finalPwm, cfg.minPwm, cfg.minLdrPct, cfg.maxLdrPct);
        }
        
        // Draw LDR percentage at the bottom of the 4th screen (index 3)
        int ldrPct = (ldrVal * 100) / 4095;
        if (ldrPct != lastLdrPct) {
            lastLdrPct = ldrPct;
            char buf[16];
            snprintf(buf, sizeof(buf), "LDR: %d%%", ldrPct);
            displayMgr.selectDisplay(3);
            displayMgr.tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK); 
            displayMgr.tft.setTextDatum(BC_DATUM);
            displayMgr.tft.drawString(buf, 120, 230, 2);
            displayMgr.unselectAll();
        }
        
        // Throttle general status logging
        static unsigned long lastLog = 0;
        if (millis() - lastLog > 2000) {
            Serial.printf("LDR Value: %d (%d%%), PWM: %d, MinPWM: %d, MinLDR: %d%%, MaxLDR: %d%%\n", 
                          ldrVal, ldrPct, finalPwm, cfg.minPwm, cfg.minLdrPct, cfg.maxLdrPct);
            lastLog = millis();
        }
    }
}

void loop() {
    static unsigned long lastLoopBacklight = 0;
    
    if (WiFi.status() == WL_CONNECTED) {
        static bool timeSynced = false;
        static unsigned long lastNtpTry = 0;
        
        if (!timeSynced) {
            time_t now = time(nullptr);
            if (now > 1000000) { // Time is set (> year 1970)
                struct tm timeinfo;
                if (getLocalTime(&timeinfo, 10)) {
                    if (timeinfo.tm_year > 100) { // year > 2000
                        timeSynced = true;
                        logMsg("NTP Time Synced!");
                        // Force immediate frequency update after sync in background
                        forceFetchRequest = true;
                    }
                }
            }
            
            if (!timeSynced && (millis() - lastNtpTry > 30000 || lastNtpTry == 0)) {
                logMsg("Waiting for NTP sync...");
                lastNtpTry = millis();
            }
        }

        // Check if background task fetched new data and redraw displays
        if (displaysNeedRefresh || forceDisplaysRefresh) {
            bool force = forceDisplaysRefresh;
            displaysNeedRefresh = false;
            forceDisplaysRefresh = false;
            static bool initialClearDone = false;
            if (!initialClearDone) {
                initialClearDone = true;
                displayMgr.clearAll();
                updateDisplays(true); // Force redraw on first success
            } else {
                updateDisplays(force);
            }
        }
    }
    
    // Non-blocking backlight check every 100ms
    if (millis() - lastLoopBacklight > 100 || lastLoopBacklight == 0) {
        updateBacklight();
        lastLoopBacklight = millis();
    }
    
    delay(10);
}
