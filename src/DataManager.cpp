#include "DataManager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "WebConfig.h"

DataManager dataMgr;

DataManager::DataManager() {
    numConfigs = 0;
    demandValue = 0;
    currentSolar = 0;
    demandAdjust = 0;
    mutex = xSemaphoreCreateMutex();
}

void DataManager::begin() {
    if(!LittleFS.begin(true)) {
        logMsg("LittleFS Mount Failed");
        return;
    }
    loadConfig();
}

bool DataManager::loadConfig() {
    logMsg("Loading config from /values.json...");
    File file = LittleFS.open("/values.json", "r");
    if(!file) {
        logMsg("Failed to open /values.json!");
        return false;
    }
    logMsg("File size: %d bytes", file.size());
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        logMsg("deserializeJson() failed: %s", error.c_str());
        file.close();
        return false;
    }
    JsonArray arr = doc.as<JsonArray>();
    logMsg("Parsed JSON array with %d entries", arr.size());
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        numConfigs = 0;
        for (JsonObject obj : arr) {
            if (numConfigs >= 20) break;
            configs[numConfigs].id = obj["id"] | obj["name"].as<String>();
            configs[numConfigs].id.toLowerCase();
            configs[numConfigs].id.replace(" ", "_");
            configs[numConfigs].name = obj["name"].as<String>();
            configs[numConfigs].unit = obj["unit"].as<String>();
            JsonArray fuelTypes = obj["fuelType"];
            if (fuelTypes.isNull()) {
                configs[numConfigs].fuelTypes[0] = obj["fuelType"].as<String>();
                configs[numConfigs].numFuelTypes = 1;
            } else {
                configs[numConfigs].numFuelTypes = 0;
                for(JsonVariant ft : fuelTypes) {
                    if (configs[numConfigs].numFuelTypes < 10) {
                        configs[numConfigs].fuelTypes[configs[numConfigs].numFuelTypes++] = ft.as<String>();
                    }
                }
            }
            JsonArray ranges = obj["ranges"];
            configs[numConfigs].numRanges = ranges.size();
            for(int i=0; i<ranges.size() && i<10; i++) {
                configs[numConfigs].ranges[i].min = ranges[i]["min"].as<float>();
                configs[numConfigs].ranges[i].max = ranges[i]["max"].as<float>();
                configs[numConfigs].ranges[i].redzone = ranges[i]["redzone"].as<int>();
            }
            configs[numConfigs].currentValue = 0;
            configs[numConfigs].currentPct = -1;
            configs[numConfigs].lastUpdateSuccess = true;
            numConfigs++;
        }
        xSemaphoreGive(mutex);
    }
    file.close();
    return true;
}

GaugeConfig DataManager::getGaugeConfig(int index) {
    GaugeConfig cfg;
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        if (index >= 0 && index < numConfigs) cfg = configs[index];
        xSemaphoreGive(mutex);
    }
    return cfg;
}

GaugeConfig DataManager::getGaugeConfigById(String id) {
    GaugeConfig cfg;
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        for(int i=0; i<numConfigs; i++) {
            if (configs[i].id == id) {
                cfg = configs[i];
                break;
            }
        }
        xSemaphoreGive(mutex);
    }
    return cfg;
}

String DataManager::getGaugeId(int index) {
    String id = "";
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        if (index >= 0 && index < numConfigs) id = configs[index].id;
        xSemaphoreGive(mutex);
    }
    return id;
}

String DataManager::getGaugeName(int index) {
    String name = "";
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        if (index >= 0 && index < numConfigs) name = configs[index].name;
        xSemaphoreGive(mutex);
    }
    return name;
}

float DataManager::getDemandValue() {
    float val = 0;
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        val = demandValue;
        xSemaphoreGive(mutex);
    }
    return val;
}

String getISO8601(time_t t) {
    struct tm *nowtm = gmtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", nowtm);
    return String(buf);
}

bool DataManager::updateData() {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    float nextValues[20];
    bool genUpdateSuccess[20];
    for(int i=0; i<20; i++) {
        nextValues[i] = 0;
        genUpdateSuccess[i] = true;
    }

    float localDemandAdjust = 0;
    
    bool sGen = fetchElexonGeneration(nextValues, localDemandAdjust);
    if (!sGen) logMsg("Error: Elexon Generation API failed");
    
    // 2. Solar
    float localSolar = 0;
    bool sSolar = fetchSheffieldSolar(nextValues, localSolar);
    if (!sSolar) logMsg("Error: Sheffield Solar API failed");
    localDemandAdjust += localSolar;

    // 3. Demand
    float apiDemand = 0;
    bool sDemand = fetchElexonDemand(apiDemand);
    if (!sDemand) logMsg("Error: Elexon Demand API failed");
    
    // 4. Calculate Final Demand
    float localDemandValue = apiDemand + localDemandAdjust;
    logMsg("Demand: Base=%.1f, Adj=%.1f, Total=%.1f GW | sGen:%d sSolar:%d sDemand:%d", apiDemand, localDemandAdjust, localDemandValue, sGen, sSolar, sDemand);
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        demandValue = localDemandValue;
        for(int i=0; i<numConfigs; i++) {
            bool usesDemand = false;
            bool usesFreq = false;
            bool usesGen = false;
            bool usesSolar = false;

            for(int j=0; j<configs[i].numFuelTypes; j++) {
                if (configs[i].fuelTypes[j] == "DEMAND") usesDemand = true;
                else if (configs[i].fuelTypes[j] == "FREQ") usesFreq = true;
                else if (configs[i].fuelTypes[j] == "SOLAR") usesSolar = true;
                else usesGen = true;
            }

            bool success = true;
            if (usesGen && !sGen) success = false;
            if (usesSolar && !sSolar) success = false;
            if (usesDemand && (!sDemand || !sGen || !sSolar)) success = false;

            if (configs[i].lastUpdateSuccess != success) {
                logMsg("Gauge '%s' status changed to: %s", configs[i].name.c_str(), success ? "OK" : "FAILED");
            }
            configs[i].lastUpdateSuccess = success;

            if (success) {
                if (usesDemand) configs[i].currentValue = localDemandValue;
                else if (!usesFreq) configs[i].currentValue = nextValues[i];
                
                // Update percentage
                if (localDemandValue > 0 && !usesDemand && !usesFreq) {
                    configs[i].currentPct = (configs[i].currentValue / localDemandValue) * 100.0;
                }
            }
        }
        xSemaphoreGive(mutex);
    }

    // 5. Frequency
    bool sFreq = fetchElexonFrequency();

    return sGen || sDemand || sSolar || sFreq;
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
            http.end();
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
    String from = getISO8601(now - 300); // 5 minute window
    String url = "https://data.elexon.co.uk/bmrs/api/v1/system/frequency/stream?from=" + from + "&to=" + to;
    
    http.begin(client, url);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    http.setTimeout(5000); // 5 second timeout for SSL/Network
    int code = http.GET();
    bool success = false;

    if(code == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            logMsg("Freq JSON error: %s", error.c_str());
        } else {
            JsonArray arr = doc.as<JsonArray>();
            int dataPoints = arr.size();
            if (dataPoints > 0) {
                float val = 0;
                for(int i = dataPoints - 1; i >= 0; i--) {
                    float f = arr[i]["frequency"].as<float>();
                    if (f > 0) {
                        val = f;
                        break;
                    }
                }

                if (val > 0) {
                    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
                        for(int i=0; i<numConfigs; i++) {
                            for(int j=0; j<configs[i].numFuelTypes; j++) {
                                if(configs[i].fuelTypes[j] == "FREQ") {
                                    configs[i].currentValue = val;
                                    configs[i].lastUpdateSuccess = true;
                                    break;
                                }
                            }
                        }
                        xSemaphoreGive(mutex);
                    }
                    logMsg("Grid Frequency updated: %.3f Hz (from %d pts)", val, dataPoints);
                    success = true;
                } else {
                    logMsg("Freq error: %d pts but no valid frequency found", dataPoints);
                }
            } else {
                logMsg("Freq error: API returned empty array (Window: 5m)");
            }
        }
    } else {
        logMsg("Freq error: API returned HTTP code %d", code);
    }
    
    http.end();

    if (!success) {
        // Explicitly mark FREQ gauges as failed if we didn't get a value
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            for(int i=0; i<numConfigs; i++) {
                for(int j=0; j<configs[i].numFuelTypes; j++) {
                    if(configs[i].fuelTypes[j] == "FREQ") {
                        configs[i].lastUpdateSuccess = false;
                        break;
                    }
                }
            }
            xSemaphoreGive(mutex);
        }
    }
    return success;
}

bool DataManager::fetchElexonGeneration(float* nextValues, float& localDemandAdjust) {
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(client, "https://data.elexon.co.uk/bmrs/api/v1/generation/outturn/current?format=json");
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    
    int code = http.GET();
    if(code == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
            JsonArray arr = doc.as<JsonArray>();
            for(JsonObject item : arr) {
                const char* ft = item["fuelType"];
                float val = item["currentUsage"].as<float>() / 1000.0;
                
                if (ft) {
                    if (strcmp(ft, "SOLAR") == 0) localDemandAdjust += val;
                    if (val < 0) localDemandAdjust += fabs(val);
 
                    for(int i=0; i<numConfigs; i++) {
                        for(int j=0; j<configs[i].numFuelTypes; j++) {
                            if (configs[i].fuelTypes[j] == ft) {
                                nextValues[i] += val;
                                break;
                            }
                        }
                    }
                }
            }
            http.end();
            return true;
        } else {
            logMsg("Elexon Gen JSON parse failed: %s", error.c_str());
        }
    } else {
        logMsg("Elexon Gen API returned HTTP code %d", code);
    }
    http.end(); return false;
}

bool DataManager::fetchSheffieldSolar(float* nextValues, float& localSolar) {
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.begin(client, "https://api.pvlive.uk/pvlive/api/v4/pes/0");
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    if(http.GET() == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            if (doc["data"].size() > 0) {
                float val = doc["data"][0][2].as<float>() / 1000.0;
                localSolar = val;
                for(int i=0; i<numConfigs; i++) {
                    for(int j=0; j<configs[i].numFuelTypes; j++) {
                        if(configs[i].fuelTypes[j] == "SOLAR") {
                            nextValues[i] += val;
                            break;
                        }
                    }
                }
                http.end();
                return true;
            }
        }
    }
    http.end(); return false;
}
