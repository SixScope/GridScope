#include "WebConfig.h"
#include "DataManager.h"
#include <stdarg.h>

WebConfig webConfig;

void logMsg(const char* format, ...) {
    char loc_buf[160];
    struct tm timeinfo;
    bool hasTime = false;
    
    if (getLocalTime(&timeinfo, 10)) { // 10ms timeout
        if (timeinfo.tm_year > 100) { // Year is > 2000
            strftime(loc_buf, 32, "[%Y-%m-%d %H:%M:%S] ", &timeinfo);
            hasTime = true;
        }
    }
    
    if (!hasTime) strcpy(loc_buf, "[No Time] ");

    char* logPtr = loc_buf + strlen(loc_buf);
    va_list arg;
    va_start(arg, format);
    vsnprintf(logPtr, sizeof(loc_buf) - (logPtr - loc_buf), format, arg);
    va_end(arg);

    Serial.println(loc_buf);
    webConfig.log(loc_buf);
}


extern void updateDisplays(bool force = false);

WebConfig::WebConfig() : server(80), events("/events") {
    configMutex = xSemaphoreCreateMutex();
    for(int i=0; i<6; i++) currentConfig.screenData[i] = (DisplayDataType)i;
    currentConfig.minPwm = 10;
    currentConfig.maxLdrPct = 80;
}

void WebConfig::begin() {
    loadConfig();
    logMsg("WebConfig starting on port 80...");
    server.addHandler(&events);
    
    // Serve logo image
    server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/logo.png", "image/png");
    });

    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        logMsg("WebConfig: Client requested '/'");
        request->send(200, "text/html", buildHtml());
    });
    server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request){
        logMsg("WebConfig: Saving new configuration...");
        if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
            for(int i = 0; i < 6; i++) {
                String p = "s" + String(i + 1);
                if(request->hasParam(p, true)) currentConfig.screenData[i] = (DisplayDataType)request->getParam(p, true)->value().toInt();
            }
            if(request->hasParam("minPwm", true)) currentConfig.minPwm = request->getParam("minPwm", true)->value().toInt();
            if(request->hasParam("maxLdr", true)) currentConfig.maxLdrPct = request->getParam("maxLdr", true)->value().toInt();
            xSemaphoreGive(configMutex);
        }
        saveConfig();
        forceDisplaysRefresh = true; // Safe thread-safe refresh trigger
        request->redirect("/");
    });
    server.on("/set_brightness", HTTP_GET, [this](AsyncWebServerRequest *request){
        if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
            if(request->hasParam("minPwm")) {
                currentConfig.minPwm = request->getParam("minPwm")->value().toInt();
            }
            if(request->hasParam("maxLdr")) {
                currentConfig.maxLdrPct = request->getParam("maxLdr")->value().toInt();
            }
            xSemaphoreGive(configMutex);
        }
        request->send(200, "text/plain", "OK");
    });
    server.on("/set_display", HTTP_GET, [this](AsyncWebServerRequest *request){
        bool updated = false;
        if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
            for(int i = 0; i < 6; i++) {
                String p = "s" + String(i + 1);
                if(request->hasParam(p)) {
                    int val = request->getParam(p)->value().toInt();
                    currentConfig.screenData[i] = (DisplayDataType)val;
                    updated = true;
                    logMsg(("WebConfig: Live updated Screen " + String(i + 1) + " to gauge " + String(val)).c_str());
                }
            }
            xSemaphoreGive(configMutex);
        }
        if(updated) {
            saveConfig();
            forceDisplaysRefresh = true; // Safe thread-safe refresh trigger
        }
        request->send(200, "text/plain", "OK");
    });
    server.begin();
    active = true;
    logMsg("WebConfig server active.");
}

void WebConfig::log(const char* msg) { 
    if (active) events.send(msg, "log", millis()); 
}

Config WebConfig::getConfig() {
    Config cfg;
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        cfg = currentConfig;
        xSemaphoreGive(configMutex);
    }
    return cfg;
}

void WebConfig::loadConfig() {
    preferences.begin("gridcfg", true);
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        for(int i=0; i<6; i++) {
            String k = "s" + String(i+1);
            currentConfig.screenData[i] = (DisplayDataType)preferences.getInt(k.c_str(), i);
        }
        currentConfig.minPwm = preferences.getUChar("minPwm", 10);
        currentConfig.maxLdrPct = preferences.getUChar("maxLdr", 80);
        xSemaphoreGive(configMutex);
    }
    preferences.end();
}

void WebConfig::saveConfig() {
    preferences.begin("gridcfg", false);
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        for(int i=0; i<6; i++) {
            String k = "s" + String(i+1);
            preferences.putInt(k.c_str(), (int)currentConfig.screenData[i]);
        }
        preferences.putUChar("minPwm", currentConfig.minPwm);
        preferences.putUChar("maxLdr", currentConfig.maxLdrPct);
        xSemaphoreGive(configMutex);
    }
    preferences.end();
}

String WebConfig::buildHtml() {
    Config cfg;
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        cfg = currentConfig;
        xSemaphoreGive(configMutex);
    }
    String html = "<html><head><title>GridScope</title><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;margin:20px;background:#121212;color:#eee;} "
            ".header{text-align:center;padding:20px 20px;margin-bottom:30px;background: linear-gradient(135deg, #2c3e50, #000);border-radius:15px;border-bottom: 4px solid #f39c12;} "
            ".logo-img{width:280px;height:auto;max-width:90%;border-radius:8px;} "
            ".card{background:#1e1e1e;padding:25px;border-radius:12px;max-width:550px;margin:auto;border:1px solid #333;box-shadow: 0 10px 20px rgba(0,0,0,0.3);} "
            "select{padding:12px;margin:8px 0;width:100%;background:#333;color:#fff;border:1px solid #444;border-radius:6px;} "
            "button{background:#f39c12;color:black;font-weight:bold;padding:15px;border:none;border-radius:6px;width:100%;cursor:pointer;margin-top:15px;transition:background 0.3s;} "
            "button:hover{background:#e67e22;} "
            "#log{background:#000;color:#00ff41;padding:15px;height:250px;overflow-y:scroll;font-family:'Consolas', monospace;font-size:13px;margin-top:25px;border:1px solid #444;border-radius:8px;line-height:1.4;} "
            ".slider{-webkit-appearance:none;width:100%;height:8px;border-radius:4px;background:#333;outline:none;margin:8px 0;transition:background 0.2s;} "
            ".slider::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:20px;height:20px;border-radius:50%;background:#f39c12;cursor:pointer;box-shadow:0 0 5px rgba(0,0,0,0.5);transition:transform 0.1s;} "
            ".slider::-webkit-slider-thumb:hover{transform:scale(1.2);} "
            ".slider::-moz-range-thumb{width:20px;height:20px;border-radius:50%;background:#f39c12;cursor:pointer;border:none;box-shadow:0 0 5px rgba(0,0,0,0.5);transition:transform 0.1s;} "
            ".slider::-moz-range-thumb:hover{transform:scale(1.2);}"
            "</style></head><body>";
    html += "<div class='header'><img src='/logo.png' class='logo-img'></div>";
    html += "<div class='card'><h2>Display & Brightness Settings</h2><form action='/save' method='POST'>";
    
    int numGauges = dataMgr.getNumConfigs();
    for(int i=0; i<6; i++) {
        html += "<label>Screen " + String(i+1) + "</label><select name='s" + String(i+1) + "' onchange='updateDisplayLive(this.name, this.value)'>";
        for(int j=0; j<numGauges; j++) {
            html += "<option value='" + String(j) + "'" + (cfg.screenData[i] == j ? " selected" : "") + ">" + dataMgr.getGaugeName(j) + "</option>";
        }
        html += "</select>";
    }
    
    html += "<h3 style='margin-top:25px;border-top:1px solid #333;padding-top:15px;color:#f39c12;'>Backlight Brightness</h3>";
    
    html += "<div style='margin-bottom:15px;'>";
    html += "  <div style='display:flex;justify-content:space-between;margin-bottom:5px;'>";
    html += "    <label>Minimum Brightness (0-255) <span style='font-size:11px;color:#aaa;'>(Default fixed level if LDR is missing)</span></label>";
    html += "    <span id='minPwmVal' style='color:#f39c12;font-weight:bold;'>" + String(cfg.minPwm) + "</span>";
    html += "  </div>";
    html += "  <input type='range' name='minPwm' min='0' max='255' value='" + String(cfg.minPwm) + "' class='slider' oninput='document.getElementById(\"minPwmVal\").innerText=this.value; updateBrightnessLive(\"minPwm\", this.value);'>";
    html += "</div>";
    

    
    html += "<div style='margin-bottom:15px;'>";
    html += "  <div style='display:flex;justify-content:space-between;margin-bottom:5px;'>";
    html += "    <label>Daylight Threshold LDR (1-100%) <span style='font-size:11px;color:#aaa;'>(LDR percentage that triggers maximum brightness)</span></label>";
    html += "    <span id='maxLdrVal' style='color:#f39c12;font-weight:bold;'>" + String(cfg.maxLdrPct) + "%</span>";
    html += "  </div>";
    html += "  <input type='range' name='maxLdr' min='1' max='100' value='" + String(cfg.maxLdrPct) + "' class='slider' oninput='document.getElementById(\"maxLdrVal\").innerText=this.value+\"%\"; updateBrightnessLive(\"maxLdr\", this.value);'>";
    html += "</div>";
    
    html += "<button type='submit'>Save Configuration</button></form><h3>System Log</h3><div id='log'></div></div>";
    html += "<script>var source = new EventSource('/events'); source.addEventListener('log', function(e) { var log = document.getElementById('log'); log.innerHTML += e.data + '<br>'; log.scrollTop = log.scrollHeight; }, false); var debounceTimer; function updateBrightnessLive(param, value) { clearTimeout(debounceTimer); debounceTimer = setTimeout(function() { fetch('/set_brightness?' + param + '=' + value); }, 50); } function updateDisplayLive(param, value) { fetch('/set_display?' + param + '=' + value); }</script></body></html>";
    return html;
}
