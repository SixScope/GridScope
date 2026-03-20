#include <Arduino.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <Adafruit_VEML7700.h>
#include <time.h>
#include "qrcode.h"
#include "../include/DisplayManager.h"
#include "../include/DataManager.h"
#include "../include/WebConfig.h"

Adafruit_VEML7700 veml = Adafruit_VEML7700();
bool hasVeml = false;
unsigned long lastDataUpdate = 0;
const unsigned long UPDATE_INTERVAL = 300000; 

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
    displayMgr.selectDisplay(0); displayMgr.tft.pushImage(0, 0, 240, 240, logo_img); displayMgr.unselectAll();
    drawQRCode(1, ("WIFI:T:WPA;S:" + ssid + ";P:;;").c_str());
    displayMgr.selectDisplay(2); displayMgr.tft.fillScreen(TFT_BLACK); displayMgr.tft.setTextColor(TFT_WHITE); displayMgr.tft.setTextDatum(MC_DATUM);
    displayMgr.tft.drawString("WIFI SETUP", 120, 70, 4); displayMgr.tft.setTextColor(TFT_YELLOW); displayMgr.tft.drawString(ssid, 120, 140, 4); displayMgr.unselectAll();
    displayMgr.selectDisplay(3); displayMgr.tft.fillScreen(TFT_BLACK); displayMgr.tft.setTextColor(TFT_WHITE); displayMgr.tft.setTextDatum(MC_DATUM);
    displayMgr.tft.drawString("WiFi connection", 120, 60, 4); displayMgr.tft.drawString("is required.", 120, 100, 4);
    displayMgr.tft.drawString("Connect phone", 120, 160, 4); displayMgr.tft.drawString("to set up.", 120, 200, 4); displayMgr.unselectAll();
    displayMgr.selectDisplay(4); displayMgr.tft.fillScreen(TFT_BLACK); displayMgr.tft.setTextColor(TFT_WHITE); displayMgr.tft.setTextDatum(MC_DATUM);
    displayMgr.tft.drawString("Open browser to:", 120, 80, 4); displayMgr.tft.setTextColor(TFT_YELLOW); displayMgr.tft.drawString("192.168.4.1", 120, 150, 4); displayMgr.unselectAll();
    displayMgr.selectDisplay(5); displayMgr.tft.pushImage(0, 0, 240, 240, logo_img); displayMgr.unselectAll();
}

void updateDisplays() {
    Config cfg = webConfig.getConfig();
    for(int i = 0; i < 6; i++) {
        GaugeConfig gcfg = dataMgr.getGaugeConfig((int)cfg.screenData[i]);
        if (gcfg.id == "") continue;

        float val = gcfg.currentValue;
        float pct = gcfg.currentPct;

        float min_gauge = (gcfg.numRanges > 0) ? gcfg.ranges[0].min : 0;
        float max_gauge = (gcfg.numRanges > 0) ? gcfg.ranges[gcfg.numRanges-1].max : 100;

        uint16_t themeColor = (cfg.theme == 1) ? TFT_MAGENTA : TFT_GREENYELLOW;
        DisplayRange dranges[3];
        for(int r=0; r<gcfg.numRanges; r++) {
            dranges[r].min = gcfg.ranges[r].min; dranges[r].max = gcfg.ranges[r].max; dranges[r].redzone = gcfg.ranges[r].redzone;
        }

        displayMgr.drawGauge(i, gcfg.name.c_str(), val, min_gauge, max_gauge, gcfg.unit.c_str(), themeColor, pct, gcfg.numRanges, dranges);
        
        if (i == 4) displayMgr.drawFooter(i, "GridScope");
        if (i == 5) { 
            displayMgr.selectDisplay(5);
            displayMgr.tft.setTextColor(TFT_GREENYELLOW); displayMgr.tft.setTextSize(1); displayMgr.tft.setTextDatum(BC_DATUM);
            displayMgr.tft.drawString(WiFi.localIP().toString(), 120, 210);
            displayMgr.tft.setTextColor(TFT_DARKGREY); displayMgr.tft.drawString("gridscope.local", 120, 230);
            displayMgr.unselectAll();
        }
    }
}

void setup() {
    Serial.begin(115200);
    displayMgr.begin();
    displayMgr.drawLogoAll(); displayMgr.unselectAll();
    dataMgr.begin();
    ledcSetup(0, 5000, 8); ledcAttachPin(25, 0); ledcWrite(0, 255); 
    Wire.begin(21, 22); if (veml.begin()) hasVeml = true;
    WiFi.persistent(true); WiFi.setAutoReconnect(true); WiFi.mode(WIFI_STA); WiFi.enableIpV6();
    WiFiManager wifiManager;
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setConnectTimeout(10);
    wifiManager.setConfigPortalTimeout(0); 
    wifiManager.setCaptivePortalEnable(true); 
    String savedSSID = WiFi.SSID();
    bool connected = false;
    if (savedSSID.length() > 0) {
        WiFi.begin(); unsigned long startAttempt = millis();
        while (millis() - startAttempt < 20000) {
            int remaining = 20 - (millis() - startAttempt) / 1000;
            displayMgr.selectDisplay(5); displayMgr.tft.fillScreen(TFT_BLACK); displayMgr.tft.setTextColor(TFT_WHITE);
            displayMgr.tft.setTextDatum(MC_DATUM); displayMgr.tft.drawString("WiFi Connecting...", 120, 80, 4); 
            displayMgr.tft.setTextColor(TFT_YELLOW); displayMgr.tft.drawString(savedSSID, 120, 130, 4);
            displayMgr.tft.setTextColor(TFT_WHITE); displayMgr.tft.drawString(String(remaining) + "s", 120, 180, 4); displayMgr.unselectAll();
            if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
            delay(500);
        }
    }
    if (!connected) {
        if(!wifiManager.autoConnect("GridScope_AP")) { delay(1000); ESP.restart(); }
    }
    webConfig.begin();
    logMsg("WiFi Connected! Syncing time...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    time_t now = time(nullptr); int retry = 0;
    while (now < 8 * 3600 * 2 && retry < 10) { delay(500); now = time(nullptr); retry++; }
    logMsg("System ready. Fetching data...");
    displayMgr.clearAll();
    dataMgr.updateData();
    updateDisplays();
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (millis() - lastDataUpdate > UPDATE_INTERVAL || lastDataUpdate == 0) {
            if (dataMgr.updateData()) { updateDisplays(); lastDataUpdate = millis(); }
            else { updateDisplays(); } 
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
