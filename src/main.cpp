#include <Arduino.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <Adafruit_VEML7700.h>
#include <time.h>
#include "qrcode.h"
#include "DisplayManager.h"
#include "DataManager.h"
#include "WebConfig.h"

Adafruit_VEML7700 veml = Adafruit_VEML7700();
WiFiManager wifiManager;
bool hasVeml = false;
unsigned long lastDataUpdate = 0;
unsigned long lastFreqUpdate = 0;
const unsigned long UPDATE_INTERVAL = 300000; 
const unsigned long FREQ_UPDATE_INTERVAL = 60000;

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
        
        if (i == 4) {
            displayMgr.selectDisplay(4);
            displayMgr.tft.setTextColor(TFT_DARKGREY); 
            displayMgr.tft.setTextDatum(BC_DATUM);
            displayMgr.tft.drawString("gridscope.local", 120, 230);
            displayMgr.unselectAll();
        }
        if (i == 5) { 
            displayMgr.selectDisplay(5);
            displayMgr.tft.setTextColor(TFT_GREENYELLOW); displayMgr.tft.setTextSize(1); displayMgr.tft.setTextDatum(BC_DATUM);
            displayMgr.tft.drawString(WiFi.localIP().toString(), 120, 230);
            displayMgr.unselectAll();
        }

        lastVal[i] = val;
        lastIdx[i] = gaugeIdx;
    }
}

void setup() {
    Serial.begin(115200);

    // Initialize Filesystem early for logos
    if(!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
    }

    displayMgr.begin();
    
    // 1. On boot show logos on all screens
    displayMgr.drawLogoFromFile("/logo.jpg");
    
    ledcSetup(0, 5000, 8); ledcAttachPin(25, 0); ledcWrite(0, 255); 
    Wire.begin(21, 22); if (veml.begin()) hasVeml = true;
    WiFi.setHostname("gridscope");
    WiFi.persistent(true); WiFi.setAutoReconnect(true); WiFi.mode(WIFI_STA); WiFi.enableIpV6();
    
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setConnectTimeout(10);
    wifiManager.setConfigPortalTimeout(0); 
    wifiManager.setCaptivePortalEnable(true); 
    wifiManager.setConfigPortalBlocking(true); 

    String savedSSID = WiFi.SSID();
    bool connected = false;

    if (savedSSID.length() > 0) {
        // 2. If there is a saved WiFi attempt to connect
        WiFi.begin();
        
        // 3. Wait up to 10 seconds. Upon connection proceed to main app.
        unsigned long startAttempt = millis();
        while (millis() - startAttempt < 10000) {
            if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
            delay(100);
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
    dataMgr.begin(); // DataManager also calls LittleFS.begin(true)
    
    // 7. Try to get ntp time (will be handled in loop, but we trigger it here)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();
    
    logMsg("System ready. Fetching data...");
    
    // 8. Clear all screens and refresh gauges
    displayMgr.clearAll();
    dataMgr.updateData();
    updateDisplays(true);
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        // No longer processing WiFiManager in loop
    } else {
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
                        // Force immediate frequency update after sync
                        if (dataMgr.fetchElexonFrequency()) updateDisplays();
                        lastFreqUpdate = millis();
                    }
                }
            }
            
            if (!timeSynced && (millis() - lastNtpTry > 30000 || lastNtpTry == 0)) {
                logMsg("Attempting NTP sync...");
                configTime(0, 0, "pool.ntp.org", "time.nist.gov");
                setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
                tzset();
                lastNtpTry = millis();
            }
        }

        // Independent Frequency Update (every 1 minute)
        if (millis() - lastFreqUpdate > FREQ_UPDATE_INTERVAL || lastFreqUpdate == 0) {
            if (dataMgr.fetchElexonFrequency()) {
                updateDisplays();
            }
            lastFreqUpdate = millis();
        }

        // General Data Update (every 5 minutes)
        if (millis() - lastDataUpdate > UPDATE_INTERVAL || lastDataUpdate == 0) {
            if (dataMgr.updateData()) { 
                updateDisplays(); 
                lastDataUpdate = millis(); 
            } else {
                updateDisplays();
            } 
        }
    }
    if (hasVeml) {
        float lux = veml.readLux();
        int pwm = map(lux, 0, 1000, 50, 255);
        pwm = constrain(pwm, 10, 255);
        ledcWrite(0, pwm);
        delay(500);
    } else { delay(100); }
}
