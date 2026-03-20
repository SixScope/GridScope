#include "../include/DataManager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "../include/WebConfig.h"

DataManager dataMgr;

DataManager::DataManager() {
    numConfigs = 0;
    demandValue = 0;
    currentSolar = 0;
    demandAdjust = 0;
}

void DataManager::begin() {
    if(!LittleFS.begin(true)) {
        logMsg("LittleFS Mount Failed");
        return;
    }
    loadConfig();
}

bool DataManager::loadConfig() {
    File file = LittleFS.open("/values.json", "r");
    if(!file) return false;
    JsonDocument doc;
    if (deserializeJson(doc, file)) return false;
    JsonArray arr = doc.as<JsonArray>();
    numConfigs = 0;
    for (JsonObject obj : arr) {
        if (numConfigs >= 20) break;
        configs[numConfigs].id = obj["id"] | obj["name"].as<String>();
        configs[numConfigs].id.toLowerCase();
        configs[numConfigs].id.replace(" ", "_");
        configs[numConfigs].name = obj["name"].as<String>();
        configs[numConfigs].unit = obj["unit"].as<String>();
        configs[numConfigs].fuelType = obj["fuelType"].as<String>();
        JsonArray ranges = obj["ranges"];
        configs[numConfigs].numRanges = ranges.size();
        for(int i=0; i<ranges.size() && i<3; i++) {
            configs[numConfigs].ranges[i].min = ranges[i]["min"].as<float>();
            configs[numConfigs].ranges[i].max = ranges[i]["max"].as<float>();
            configs[numConfigs].ranges[i].redzone = ranges[i]["redzone"].as<int>();
        }
        configs[numConfigs].currentValue = 0;
        configs[numConfigs].currentPct = -1;
        numConfigs++;
    }
    file.close();
    return true;
}

GaugeConfig DataManager::getGaugeConfig(int index) {
    if (index >= 0 && index < numConfigs) return configs[index];
    return GaugeConfig();
}

GaugeConfig DataManager::getGaugeConfigById(String id) {
    for(int i=0; i<numConfigs; i++) {
        if (configs[i].id == id) return configs[i];
    }
    return GaugeConfig();
}

String DataManager::getGaugeId(int index) {
    if (index >= 0 && index < numConfigs) return configs[index].id;
    return "";
}

String DataManager::getGaugeName(int index) {
    if (index >= 0 && index < numConfigs) return configs[index].name;
    return "";
}

String getISO8601(time_t t) {
    struct tm *nowtm = gmtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", nowtm);
    return String(buf);
}

bool DataManager::updateData() {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    demandAdjust = 0;
    
    // 1. Generation - This populates the main generation data AND adds to demandAdjust
    bool sGen = fetchElexonGeneration();
    
    // 2. Solar - This adds Sheffield solar to demandAdjust
    bool sSolar = fetchSheffieldSolar();
    demandAdjust += currentSolar;
    
    // 3. Demand - Fetch the base ITSDO value
    float apiDemand = 0;
    bool sDemand = fetchElexonDemand(apiDemand);
    
    // 4. Calculate Final Demand
    demandValue = apiDemand + demandAdjust;
    logMsg("Demand: Base=%.1f, Adj=%.1f, Total=%.1f GW", apiDemand, demandAdjust, demandValue);
    
    for(int i=0; i<numConfigs; i++) {
        if(configs[i].fuelType == "DEMAND") configs[i].currentValue = demandValue;
    }

    // 5. Frequency
    bool sFreq = fetchElexonFrequency();
    
    // 6. Update Percentages for all generation gauges relative to the final demand
    if (demandValue > 0) {
        for(int i=0; i<numConfigs; i++) {
            if (configs[i].fuelType != "DEMAND" && configs[i].fuelType != "FREQ") {
                configs[i].currentPct = (configs[i].currentValue / demandValue) * 100.0;
            }
        }
    }
    
    return sGen || sDemand || sFreq;
}

bool DataManager::fetchElexonDemand(float &apiDemand) {
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(client, "https://data.elexon.co.uk/bmrs/api/v1/datasets/ITSDO?format=json");
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    int code = http.GET();
    if(code == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            apiDemand = doc["data"][0]["demand"].as<float>() / 1000.0;
            return true;
        }
    }
    http.end(); return false;
}

bool DataManager::fetchElexonFrequency() {
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    time_t now = time(nullptr);
    String to = getISO8601(now);
    String from = getISO8601(now - 120);
    String url = "https://data.elexon.co.uk/bmrs/api/v1/system/frequency/stream?from=" + from + "&to=" + to;
    
    http.begin(client, url);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    int code = http.GET();
    if(code == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            JsonArray arr = doc.as<JsonArray>();
            if (arr.size() > 0) {
                float val = arr[arr.size() - 1]["frequency"].as<float>();
                for(int i=0; i<numConfigs; i++) {
                    if(configs[i].fuelType == "FREQ") configs[i].currentValue = val;
                }
                return true;
            }
        }
    }
    http.end(); return false;
}

bool DataManager::fetchElexonGeneration() {
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(client, "https://data.elexon.co.uk/bmrs/api/v1/generation/outturn/current?format=json");
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    int code = http.GET();
    if(code == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            JsonArray arr = doc.as<JsonArray>();
            for(JsonObject item : arr) {
                const char* ft = item["fuelType"];
                float val = item["currentUsage"].as<float>() / 1000.0;
                
                if (ft) {
                    // Logic: Add SOLAR and absolute values of Negative generation to demandAdjust
                    if (strcmp(ft, "SOLAR") == 0) demandAdjust += val;
                    if (val < 0) demandAdjust += fabs(val);

                    for(int i=0; i<numConfigs; i++) {
                        if (configs[i].fuelType == ft) configs[i].currentValue = val;
                    }
                }
            }
            return true;
        }
    }
    http.end(); return false;
}

bool DataManager::fetchSheffieldSolar() {
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.begin(client, "https://api.pvlive.uk/pvlive/api/v4/pes/0");
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    if(http.GET() == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            if (doc["data"].size() > 0) {
                float val = doc["data"][0][2].as<float>() / 1000.0;
                currentSolar = val;
                for(int i=0; i<numConfigs; i++) {
                    if(configs[i].fuelType == "SOLAR") configs[i].currentValue = val;
                }
                return true;
            }
        }
    }
    http.end(); return false;
}
