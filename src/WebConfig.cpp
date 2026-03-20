#include "../include/WebConfig.h"
#include "../include/DataManager.h"
#include <stdarg.h>

WebConfig webConfig;

void logMsg(const char* format, ...) {
    char loc_buf[128];
    va_list arg;
    va_start(arg, format);
    vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
    va_end(arg);
    Serial.println(loc_buf);
    if (webConfig.active) webConfig.log(loc_buf);
}

WebConfig::WebConfig() : server(80), events("/events") {
    for(int i=0; i<6; i++) currentConfig.screenData[i] = (DisplayDataType)i;
}

void WebConfig::begin() {
    loadConfig();
    server.addHandler(&events);
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send(200, "text/html", buildHtml());
    });
    server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request){
        for(int i = 0; i < 6; i++) {
            String p = "s" + String(i + 1);
            if(request->hasParam(p, true)) currentConfig.screenData[i] = (DisplayDataType)request->getParam(p, true)->value().toInt();
        }
        saveConfig();
        request->redirect("/");
    });
    server.begin();
    active = true; // Now safe to stream events
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
    html += "<style>body{font-family:sans-serif;margin:20px;background:#121212;color:#eee;} .card{background:#1e1e1e;padding:20px;border-radius:10px;max-width:500px;margin:auto;border:1px solid #333;} select{padding:10px;margin:5px 0;width:100%;background:#333;color:#fff;border:1px solid #444;} button{background:#4CAF50;color:white;padding:15px;border:none;border-radius:5px;width:100%;cursor:pointer;margin-top:10px;} #log{background:#000;color:#0f0;padding:10px;height:200px;overflow-y:scroll;font-family:monospace;font-size:12px;margin-top:20px;border:1px solid #333;}</style></head><body>";
    html += "<div class='card'><h2>GridScope Settings</h2><form action='/save' method='POST'>";
    
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
