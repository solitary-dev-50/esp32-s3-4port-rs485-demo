#include "AsyncConfigServer.h"
#include "ConfigManager.h"
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <sys/time.h>

const byte DNS_PORT = 53;
const char* AP_SSID = "Jishi-Agri-Setup";
const char* AP_PASSWORD = NULL;
const IPAddress AP_IP(192,168,4,1);
const IPAddress AP_GATEWAY(192,168,4,1);
const IPAddress AP_SUBNET(255,255,255,0);

AsyncConfigServer::AsyncConfigServer() : _server(nullptr), _dnsServer(nullptr) {}
AsyncConfigServer::~AsyncConfigServer() { stop(); }

void AsyncConfigServer::start() {
    Serial.println("Starting offline config mode (AP+STA)...");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.printf("AP started: %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    } else { Serial.println("AP failed!"); return; }
    _dnsServer = new DNSServer();
    if (_dnsServer) { _dnsServer->start(DNS_PORT, "*", AP_IP); Serial.println("DNS started."); }
    _server = new AsyncWebServer(80);
    if (_server) { setupRoutes(); _server->begin(); Serial.println("Web server started"); }
}

void AsyncConfigServer::stop() {
    if (_server) { _server->end(); delete _server; _server = nullptr; }
    if (_dnsServer) { _dnsServer->stop(); delete _dnsServer; _dnsServer = nullptr; }
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
}

void AsyncConfigServer::process() {
    if (_dnsServer) _dnsServer->processNextRequest();
    if (_verifyState == VERIFY_START) {
        Serial.printf("[VERIFY] Testing: %s\n", _testSsid.c_str());
        WiFi.disconnect();
        WiFi.begin(_testSsid.c_str(), _testPassword.c_str());
        _verifyStartTime = millis();
        _verifyState = VERIFY_WAITING;
    } else if (_verifyState == VERIFY_WAITING) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[VERIFY] OK - %s, IP=%s\n", _testSsid.c_str(), WiFi.localIP().toString().c_str());
            _verifyState = VERIFY_SUCCESS;
            ConfigManager::saveWifiCredentials(_testSsid, _testPassword);
            ConfigManager::setTimeConfigured(true);
            ConfigManager::commit();
            markSystemForRestart();
        } else if (millis() - _verifyStartTime > 5000) {
            Serial.printf("[VERIFY] FAIL (status=%d)\n", WiFi.status());
            WiFi.disconnect();
            _verifyState = VERIFY_FAIL;
        }
    }
    if (_shouldRestart && (millis() - _restartTimer > 1000)) {
        Serial.println("[CFG] Restarting...");
        ESP.restart();
    }
}

void AsyncConfigServer::markSystemForRestart() { _shouldRestart = true; _restartTimer = millis(); }

void AsyncConfigServer::setupRoutes() {
    using namespace std::placeholders;
    _server->on("/", HTTP_GET, std::bind(&AsyncConfigServer::handleRoot, this, _1));
    _server->onNotFound(std::bind(&AsyncConfigServer::handleNotFound, this, _1));
    _server->on("/api/wifi/scan", HTTP_GET, std::bind(&AsyncConfigServer::handleWifiScan, this, _1));
    _server->on("/api/setup/wifi/verify/status", HTTP_GET, std::bind(&AsyncConfigServer::handleWifiVerifyStatus, this, _1));
    _server->on("/api/setup/wifi/commit", HTTP_POST, std::bind(&AsyncConfigServer::handleWifiCommit, this, _1));
    auto bindPost = [this](const char* uri, void (AsyncConfigServer::*fn)(AsyncWebServerRequest*, const JsonDocument&)) {
        _server->on(uri, HTTP_POST,
            [](AsyncWebServerRequest*){}, NULL,
            [this, fn](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
                if (index==0) { JsonDocument doc; if (!deserializeJson(doc, (const char*)data, len)) (this->*fn)(req, doc); else req->send(400,"application/json","{\"error\":\"JSON\"}"); }
            });
    };
    bindPost("/api/setup/time", &AsyncConfigServer::handleSaveTime);
    bindPost("/api/setup/wifi", &AsyncConfigServer::handleSaveWifi);
    bindPost("/api/setup/wifi/verify", &AsyncConfigServer::handleWifiVerifyStart);
}

void AsyncConfigServer::handleRoot(AsyncWebServerRequest *request) {
    const char* html = R"rawliteral(<!DOCTYPE html><html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>JiShi-Agri Setup</title><style>body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;max-width:420px;margin:1em auto;padding:1em;background:#f0f2f5;color:#333}h1,h2{text-align:center}h1{font-size:1.5em;margin-bottom:.5em}h2{font-size:1.2em;margin-top:0;color:#555}.card{background:white;padding:1.5em;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,.08);margin-bottom:1.5em}label{display:block;margin-bottom:.5em;font-weight:600}input{box-sizing:border-box;width:100%;padding:.7em .5em;margin-bottom:1em;border:1px solid #ccc;border-radius:6px;font-size:1em}button{background:#007bff;color:white;padding:.8em 1.2em;border:none;border-radius:6px;cursor:pointer;width:100%;font-size:1em;font-weight:bold;margin-bottom:.4em}button:hover{background:#0056b3}button.secondary{background:#6c757d}button.green{background:#28a745}button.green:hover{background:#1e7e34}#status{margin-top:1em;font-weight:bold;text-align:center;padding:.5em;border-radius:6px}</style></head><body><h1>JiShi Agri / 基石农业</h1><div class="card"><h2>1. System Time / 系统时间 <span style="color:#dc3545">*必填</span></h2><p style="text-align:center;font-size:.9em;color:#666">Accurate time is required for scheduled tasks.<br>设备需要准确时间执行定时任务。</p><label for="time">Current Time / 当前时间:</label><input type="datetime-local" id="time" required><button onclick="setTime()">Save and Start / 保存并启动</button></div><div class="card"><h2>2. WiFi Setup / 网络配置 <span style="color:#6c757d">可选</span></h2><p style="font-size:.9em;color:#666">Connect to WiFi for remote access and online services.<br>连接WiFi后可远程访问设备和使用在线服务。</p><label for="ssid">WiFi Name (SSID) / WiFi名称:</label><input type="text" id="ssid" placeholder="Enter SSID"><label for="pass">Password / 密码:</label><input type="password" id="pass"><button class="green" onclick="verifyWifi()">Test Connection / 先测试</button><button onclick="setWifi()">Save and Restart / 保存并重启</button></div><div id="status"></div><script>window.onload=function(){var d=new Date();var t=new Date(d.getTime()-d.getTimezoneOffset()*60000).toISOString().slice(0,16);document.getElementById('time').value=t;};function show(s,e){var d=document.getElementById('status');d.innerText=s;d.style.color=e?'#d9534f':'#28a745';d.style.backgroundColor=e?'#f2dede':'#dff0d8'}function setTime(){var t=document.getElementById('time').value;if(!t){alert('Select time');return}show('Saving...');fetch('/api/setup/time',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({time:new Date(t).getTime()})}).then(function(r){return r.json()}).then(function(d){if(d.success){show('OK! Restarting...');setTimeout(function(){alert('Device will restart.')},500)}else{show('Error: '+d.message,true)}}).catch(function(){show('Request failed',true)})}function verifyWifi(){var ssid=document.getElementById('ssid').value;var pass=document.getElementById('pass').value;if(!ssid){alert('Enter SSID first');return}show('Testing...');fetch('/api/setup/wifi/verify',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pass})}).then(function(r){return r.json()}).then(function(d){if(d.success){show('Verifying...');pollVerifyStatus()}else{show('Error: '+d.message,true)}}).catch(function(){show('Request failed',true)})}function pollVerifyStatus(){var c=function(){fetch('/api/setup/wifi/verify/status').then(function(r){return r.json()}).then(function(d){if(d.state==='waiting'){show('Connecting... ('+d.elapsed+'s)')}else if(d.state==='success'){show('Connected! Saving...');commitWifi()}else if(d.state==='failed'){show('FAILED: wrong password or weak signal',true)}}).catch(function(){show('Status check failed',true)})};setInterval(c,1500);c()}function commitWifi(){fetch('/api/setup/wifi/commit',{method:'POST'}).then(function(r){return r.json()}).then(function(d){if(d.success){show('WiFi saved! Restarting...');setTimeout(function(){alert('Device will restart.')},500)}else{show('Commit failed: '+d.message,true)}})}function setWifi(){var ssid=document.getElementById('ssid').value;var pass=document.getElementById('pass').value;if(!ssid){alert('Enter SSID');return}show('Saving WiFi...');fetch('/api/setup/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pass})}).then(function(r){return r.json()}).then(function(d){if(d.success){show('OK! Restarting...')}else{show('Error: '+d.message,true)}})}function scanWifi(){}</script></body></html>)rawliteral";
    request->send(200, "text/html", html);
}

void AsyncConfigServer::handleNotFound(AsyncWebServerRequest *request) {
    String url = request->url();
    static unsigned long _lastLogMs = 0;
    static String _lastUrl;
    unsigned long now = millis();
    if (now - _lastLogMs > 2000 || url != _lastUrl) {
        Serial.printf("Captive path: %s %s\n", request->host().c_str(), url.c_str());
        _lastLogMs = now;
        _lastUrl = url;
    }
    if (url == "/generate_204" || url.indexOf("/generate_204_") == 0 ||
        url == "/hotspot-detect.html" || url == "/connecttest.txt" ||
        url == "/ncsi.txt" || url == "/fwlink" ||
        url == "/success.txt" || url == "/library/test/success.html") {
        handleRoot(request);
        return;
    }
    if (url.indexOf("/mmtls/") >= 0 || url == "/favicon.ico") {
        request->send(204);
        return;
    }
    request->send(200, "text/plain", "OK");
}

void AsyncConfigServer::handleWifiScan(AsyncWebServerRequest *request) {
    request->send(200, "application/json", "[]");
}

void AsyncConfigServer::handleSaveTime(AsyncWebServerRequest *request, const JsonDocument& doc) {
    if (!doc["time"].is<long long>()) { request->send(400,"application/json","{\"error\":\"Missing time\"}"); return; }
    long long ms = doc["time"]; time_t sec = ms/1000; suseconds_t usec = (ms%1000)*1000;
    struct timeval tv = {.tv_sec=sec, .tv_usec=usec}; settimeofday(&tv, NULL);
    ConfigManager::setTimeConfigured(true); ConfigManager::commit();
    request->send(200,"application/json","{\"success\":true}");
    markSystemForRestart();
}

void AsyncConfigServer::handleSaveWifi(AsyncWebServerRequest *request, const JsonDocument& doc) {
    if (!doc["ssid"].is<const char*>()) { request->send(400,"application/json","{\"error\":\"Missing ssid\"}"); return; }
    const char* ssid = doc["ssid"]; const char* pass = doc["password"] | "";
    Serial.printf("WiFi save: %s\n", ssid);
    ConfigManager::saveWifiCredentials(String(ssid), String(pass));
    ConfigManager::setTimeConfigured(true); ConfigManager::commit();
    request->send(200,"application/json","{\"success\":true}");
    markSystemForRestart();
}

void AsyncConfigServer::handleWifiVerifyStart(AsyncWebServerRequest *request, const JsonDocument& doc) {
    if (!doc["ssid"].is<const char*>()) { request->send(400,"application/json","{\"error\":\"Missing ssid\"}"); return; }
    if (_verifyState == VERIFY_WAITING) { request->send(409,"application/json","{\"error\":\"Busy\"}"); return; }
    _testSsid = doc["ssid"].as<String>(); _testPassword = doc["password"] | "";
    _verifyState = VERIFY_START;
    request->send(200,"application/json","{\"success\":true}");
}

void AsyncConfigServer::handleWifiVerifyStatus(AsyncWebServerRequest *request) {
    String json; json.reserve(128);
    json += "{\"state\":\"";
    switch(_verifyState){case VERIFY_IDLE:json+="idle";break;case VERIFY_START:json+="starting";break;case VERIFY_WAITING:json+="waiting";break;case VERIFY_SUCCESS:json+="success";break;case VERIFY_FAIL:json+="failed";break;}
    json += "\"";
    if(_verifyState==VERIFY_WAITING) json+=",\"elapsed\":"+String((millis()-_verifyStartTime)/1000);
    if(_verifyState==VERIFY_FAIL) json+=",\"reason\":\""+String(WiFi.status())+"\"";
    json+="}";
    request->send(200,"application/json",json);
}

void AsyncConfigServer::handleWifiCommit(AsyncWebServerRequest *request) {
    if(_verifyState!=VERIFY_SUCCESS){request->send(400,"application/json","{\"error\":\"No verification\"}");return;}
    ConfigManager::saveWifiCredentials(_testSsid,_testPassword);
    ConfigManager::setTimeConfigured(true); ConfigManager::commit();
    request->send(200,"application/json","{\"success\":true}");
    markSystemForRestart();
}
