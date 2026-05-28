#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct RangeConfig {
    float min = 0;
    float max = 0;
    int redzone = 0;
};

struct GaugeConfig {
    String id = "";
    String name = "Empty";
    String unit = "";
    String fuelTypes[10];
    int numFuelTypes = 0;
    RangeConfig ranges[10];
    int numRanges = 0;
    float currentValue = 0;
    float currentPct = -1;
    bool lastUpdateSuccess = true;
};

struct GridData {
    float solar = 0;
};

class DataManager {
public:
    DataManager();
    void begin();
    bool updateData();
    bool loadConfig();
    bool fetchElexonFrequency();
    GaugeConfig getGaugeConfig(int index);
    GaugeConfig getGaugeConfigById(String id);
    int getNumConfigs() { return numConfigs; }
    String getGaugeId(int index);
    String getGaugeName(int index);
    float getDemandValue();

private:
    GaugeConfig configs[20];
    int numConfigs;
    float demandValue = 0;
    float currentSolar = 0;
    float demandAdjust = 0;
    GridData currentData;
    SemaphoreHandle_t mutex;
    
    bool fetchElexonGeneration(float* nextValues, float& localDemandAdjust);
    bool fetchElexonDemand(float &apiDemand);
    bool fetchSheffieldSolar(float* nextValues, float& localSolar);
};

extern DataManager dataMgr;

#endif
