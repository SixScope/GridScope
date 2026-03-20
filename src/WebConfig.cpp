#include "../include/WebConfig.h"
#include "../include/DataManager.h"
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


extern void updateDisplays();

WebConfig::WebConfig() : server(80), events("/events") {
    for(int i=0; i<6; i++) currentConfig.screenData[i] = (DisplayDataType)i;
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
        request->send(200, "text/html", buildHtml());
    });
    server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request){
        logMsg("WebConfig: Saving new configuration...");
        for(int i = 0; i < 6; i++) {
            String p = "s" + String(i + 1);
            if(request->hasParam(p, true)) currentConfig.screenData[i] = (DisplayDataType)request->getParam(p, true)->value().toInt();
        }
        saveConfig();
        updateDisplays(); // Refresh physical screens immediately
        request->redirect("/");
    });
    server.begin();
    active = true;
    logMsg("WebConfig server active.");
}

void WebConfig::log(const char* msg) { 
    if (active) events.send(msg, "log", millis()); 
}

Config WebConfig::getConfig() { return currentConfig; }

void WebConfig::loadConfig() {
    preferences.begin("gridscope", true);
    for(int i=0; i<6; i++) {
        String k = "s" + String(i+1);
        currentConfig.screenData[i] = (DisplayDataType)preferences.getInt(k.c_str(), i);
    }
    preferences.end();
}

void WebConfig::saveConfig() {
    preferences.begin("gridscope", false);
    for(int i=0; i<6; i++) {
        String k = "s" + String(i+1);
        preferences.putInt(k.c_str(), (int)currentConfig.screenData[i]);
    }
    preferences.end();
}

String WebConfig::buildHtml() {
    String html = "<html><head><title>GridScope</title><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;margin:20px;background:#121212;color:#eee;} .header{text-align:center;padding:30px 20px;margin-bottom:30px;background: linear-gradient(135deg, #2c3e50, #000);border-radius:15px;border-bottom: 4px solid #f39c12;} .logo-img{width:240px;height:auto;max-width:80%;margin-bottom:10px;border-radius:8px;} .card{background:#1e1e1e;padding:25px;border-radius:12px;max-width:550px;margin:auto;border:1px solid #333;box-shadow: 0 10px 20px rgba(0,0,0,0.3);} select{padding:12px;margin:8px 0;width:100%;background:#333;color:#fff;border:1px solid #444;border-radius:6px;} button{background:#f39c12;color:black;font-weight:bold;padding:15px;border:none;border-radius:6px;width:100%;cursor:pointer;margin-top:15px;transition:background 0.3s;} button:hover{background:#e67e22;} #log{background:#000;color:#00ff41;padding:15px;height:250px;overflow-y:scroll;font-family:'Consolas', monospace;font-size:13px;margin-top:25px;border:1px solid #444;border-radius:8px;line-height:1.4;}</style></head><body>";
    html += "<div class='header'><img src='/logo.png' class='logo-img'><div style='color:#bdc3c7;margin-top:5px;letter-spacing:1px;font-weight:bold;'>REAL-TIME GRID MONITOR</div></div>";
    html += "<div class='card'><h2>Display Configuration</h2><form action='/save' method='POST'>";
    
    int numGauges = dataMgr.getNumConfigs();
    for(int i=0; i<6; i++) {
        html += "<label>Screen " + String(i+1) + "</label><select name='s" + String(i+1) + "'>";
        for(int j=0; j<numGauges; j++) {
            html += "<option value='" + String(j) + "'" + (currentConfig.screenData[i] == j ? " selected" : "") + ">" + dataMgr.getGaugeName(j) + "</option>";
        }
        html += "</select>";
    }
    html += "<button type='submit'>Save Configuration</button></form><h3>System Log</h3><div id='log'></div></div>";
    html += "<script>var source = new EventSource('/events'); source.addEventListener('log', function(e) { var log = document.getElementById('log'); log.innerHTML += e.data + '<br>'; log.scrollTop = log.scrollHeight; }, false);</script></body></html>";
    return html;
}
