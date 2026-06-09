#ifndef ASYNC_CONFIG_SERVER_H
#define ASYNC_CONFIG_SERVER_H
#include <Arduino.h>
#include <ArduinoJson.h>
class AsyncWebServer; class DNSServer; class AsyncWebServerRequest;

class AsyncConfigServer {
public:
    enum VerifyState { VERIFY_IDLE, VERIFY_START, VERIFY_WAITING, VERIFY_SUCCESS, VERIFY_FAIL };
    AsyncConfigServer();
    ~AsyncConfigServer();
    void start();
    void stop();
    void process();
private:
    AsyncWebServer* _server;
    DNSServer*      _dnsServer;
    bool            _shouldRestart = false;
    unsigned long   _restartTimer  = 0;
    VerifyState     _verifyState      = VERIFY_IDLE;
    String          _testSsid;
    String          _testPassword;
    unsigned long   _verifyStartTime  = 0;
    void setupRoutes();
    void markSystemForRestart();
    void handleRoot(AsyncWebServerRequest *request);
    void handleSaveTime(AsyncWebServerRequest *request, const JsonDocument& doc);
    void handleSaveWifi(AsyncWebServerRequest *request, const JsonDocument& doc);
    void handleWifiScan(AsyncWebServerRequest *request);
    void handleWifiVerifyStart(AsyncWebServerRequest *request, const JsonDocument& doc);
    void handleWifiVerifyStatus(AsyncWebServerRequest *request);
    void handleWifiCommit(AsyncWebServerRequest *request);
    void handleNotFound(AsyncWebServerRequest *request);
};
#endif