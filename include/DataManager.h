#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

struct RangeConfig {
    float min = 0;
    float max = 0;
    int redzone = 0;
};

struct GaugeConfig {
    String id = "";
    String name = "Empty";
    String unit = "";
    String fuelType = "";
    RangeConfig ranges[3];
    int numRanges = 0;
    float currentValue = 0;
    float currentPct = -1;
};

struct GridData {
    float solar = 0;
};

class DataManager {
public:
    DataManager();
    void begin();
    bool updateData();
    GaugeConfig getGaugeConfig(int index);
    GaugeConfig getGaugeConfigById(String id);
    int getNumConfigs() { return numConfigs; }
    String getGaugeId(int index);
    String getGaugeName(int index);
    float getDemandValue() { return demandValue; }

private:
    GaugeConfig configs[20];
    int numConfigs;
    float demandValue = 0;
    float currentSolar = 0;
    float demandAdjust = 0;
    GridData currentData;
    
    bool loadConfig();
    bool fetchElexonGeneration();
    bool fetchElexonDemand(float &apiDemand);
    bool fetchElexonFrequency();
    bool fetchSheffieldSolar();
};

extern DataManager dataMgr;

#endif
