#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

enum DisplayDataType {
    DATA_DEMAND = 0,
    DATA_FREQUENCY,
    DATA_CCGT,
    DATA_NUCLEAR,
    DATA_WIND,
    DATA_SOLAR
};

struct Config {
    DisplayDataType screenData[6];
    uint8_t theme; 
    uint8_t minPwm;
    uint8_t maxLdrPct;
};

class WebConfig {
public:
    WebConfig();
    void begin();
    void log(const char* msg);
    Config getConfig();
    bool active = false;
    
private:
    AsyncWebServer server;
    AsyncEventSource events;
    Preferences preferences;
    Config currentConfig;
    
    void loadConfig();
    void saveConfig();
    String buildHtml();
    String getLogoBase64();
};

extern WebConfig webConfig;
void logMsg(const char* format, ...);

#endif
