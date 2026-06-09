// ======================================================================================
// JISHI RS485 Controller - 4-Channel RS485 Demo Firmware
// HW: JS-V1.5  |  ESP32-S3
// ======================================================================================
//  4-Channel Demo Version
//  - CH1/CH2: ESP32 native UART + GPIO direction control
//  - CH3/CH4: SC16IS752 SPI-UART bridge (auto RTS direction)
//  - Serial: ch <1-4>  |  w5 <coil> <on|off>  |  w6 <reg> <val>
//  - Captive Portal: auto AP mode for initial WiFi + time config
// ======================================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "ModbusMessage.h"
#include "RTUutils.h"
#include "Config.h"
#include "SystemState.h"
#include "SC16IS752.h"
#include "ConfigManager.h"
#include "selftest/BoardSelfTest.h"
#include "api/AsyncConfigServer.h"
#include <WiFiUdp.h>
#include <NTPClient.h>

// ===================================================================
// RS485 Channel HAL
// ===================================================================
uint8_t channel = CHANNEL_1;
SC16IS752 g_sc16(PIN_SC16_CS);

const char* getChannelName(uint8_t ch) {
    switch (ch) {
        case CHANNEL_1: return "RS485-1";
        case CHANNEL_2: return "RS485-2";
        case CHANNEL_3: return "RS485-3";
        case CHANNEL_4: return "RS485-4";
        default: return "??";
    }
}

void rs485SetDirection(uint8_t ch, bool tx) {
    switch (ch) {
        case CHANNEL_1:
            digitalWrite(PIN_RS485_CH1_DE_RE, tx ? HIGH : LOW);
            break;
        case CHANNEL_2:
            digitalWrite(PIN_RS485_CH2_DE_RE, tx ? HIGH : LOW);
            break;
        default: break;
    }
}

void rs485Write(uint8_t ch, const uint8_t* data, size_t len) {
    switch (ch) {
        case CHANNEL_1:
            rs485SetDirection(ch, true);
            delayMicroseconds(500);
            Serial1.write(data, len);
            Serial1.flush();
            delayMicroseconds(100);
            rs485SetDirection(ch, false);
            delayMicroseconds(100);
            break;
        case CHANNEL_2:
            rs485SetDirection(ch, true);
            delayMicroseconds(500);
            Serial2.write(data, len);
            Serial2.flush();
            delayMicroseconds(100);
            rs485SetDirection(ch, false);
            delayMicroseconds(100);
            break;
        case CHANNEL_3:
        case CHANNEL_4:
            g_sc16.write((ch == CHANNEL_3) ? SC16_CH_A : SC16_CH_B, data, len);
            break;
    }
}

int rs485Read(uint8_t ch, uint8_t* buf, size_t maxLen, uint32_t timeoutMs) {
    switch (ch) {
        case CHANNEL_1: {
            uint32_t t0 = millis();
            while (millis() - t0 < timeoutMs) {
                if (Serial1.available()) { delay(5); return (int)Serial1.readBytes(buf, maxLen); }
            }
            return 0;
        }
        case CHANNEL_2: {
            uint32_t t0 = millis();
            while (millis() - t0 < timeoutMs) {
                if (Serial2.available()) { delay(5); return (int)Serial2.readBytes(buf, maxLen); }
            }
            return 0;
        }
        case CHANNEL_3:
        case CHANNEL_4:
            return g_sc16.read((ch == CHANNEL_3) ? SC16_CH_A : SC16_CH_B, buf, maxLen, timeoutMs);
        default: return 0;
    }
}

void rs485Flush(uint8_t ch) {
    switch (ch) {
        case CHANNEL_1: while (Serial1.available()) Serial1.read(); break;
        case CHANNEL_2: while (Serial2.available()) Serial2.read(); break;
        case CHANNEL_3: g_sc16.flushRX(SC16_CH_A); break;
        case CHANNEL_4: g_sc16.flushRX(SC16_CH_B); break;
    }
}

// ===================================================================
// Global objects
// ===================================================================
uint8_t current_device_index = 0;
unsigned long last_request_time = 0;

AsyncWebServer server(WEB_SERVER_PORT);
SemaphoreHandle_t modbusMutex = NULL;

// --- Captive Portal globals ---
AsyncConfigServer configServer;
enum SystemMode { CONFIG_MODE, NORMAL_MODE };
SystemMode currentMode;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com", 0, 3600000);

// ===================================================================
// Relay control
// ===================================================================
bool controlRelayOnce(uint8_t channel, uint8_t address, int relayId, bool state);
bool writeSingleCoilRaw(uint8_t slaveAddr, uint16_t coilAddress, bool state);
bool writeHoldingRegRaw(uint8_t slaveAddr, uint16_t regAddress, uint16_t value);
void processSerialCommand();

// Helper: find relay controller channel from device table
RS485Channel getRelayChannel() {
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].type == DEVICE_RELAY) return g_devices[i].channel;
    }
    return CHANNEL_1;
}

bool controlRelay(uint8_t channel, uint8_t address, int relayId, bool state) {
    const int MAX_RETRIES = 3;
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        if (retry > 0) {
            Serial.printf("[%s] Relay %d retry %d...\n", getChannelName(channel), relayId, retry);
            delay(100);
        }
        bool result = controlRelayOnce(channel, address, relayId, state);
        if (result) return true;
    }
    Serial.printf("[%s] Relay %d failed after %d retries\n", getChannelName(channel), relayId, MAX_RETRIES);
    return false;
}

bool controlRelayOnce(uint8_t channel, uint8_t address, int relayId, bool state) {
    if (relayId < 1 || relayId > 16) {
        Serial.printf("[%s] Error: Relay ID %d out of range (1-16)\n", getChannelName(channel), relayId);
        return false;
    }
    if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        Serial.printf("[%s] Error: Modbus busy, relay %d control failed\n", getChannelName(channel), relayId);
        return false;
    }
    setModbusBusy(true);
    ModbusMessage request;
    uint16_t coilAddress = relayId - 1;
    uint16_t coilValue = state ? 0xFF00 : 0x0000;
    request.setMessage(address, WRITE_COIL, coilAddress, coilValue);
    RTUutils::addCRC(request);
    rs485Flush(channel);
    Serial.printf("[%s] TX relay %d: ", getChannelName(channel), relayId);
    for(auto& byte : request) Serial.printf("%02X ", byte);
    Serial.println();
    rs485Write(channel, request.data(), request.size());
    uint8_t buffer[16];
    size_t received_len = 0;
    unsigned long startTime = millis();
    unsigned long timeout = 300;
    received_len = rs485Read(channel, buffer, sizeof(buffer), timeout);
    if (received_len > 3) {
        Serial.printf("[%s] RX: ", getChannelName(channel));
        for(size_t i = 0; i < received_len; i++) Serial.printf("%02X ", buffer[i]);
        Serial.println();
        uint16_t received_crc = (buffer[received_len - 1] << 8) | buffer[received_len - 2];
        uint16_t calculated_crc = RTUutils::calcCRC(buffer, received_len - 2);
        if (calculated_crc == received_crc) {
            if (address == 20) {
                updateRelayState(relayId, state);
            }
            Serial.printf("[%s] CRC OK - Relay %d: %s\n", getChannelName(channel), relayId, state ? "ON" : "OFF");
            setModbusBusy(false);
            xSemaphoreGive(modbusMutex);
            return true;
        } else {
            Serial.printf("[WEB][RELAY] crc error\n");
            Serial.printf("[%s] CRC error\n", getChannelName(channel));
        }
    } else {
        Serial.printf("[WEB][RELAY] modbus timeout\n");
        Serial.printf("[%s] No response\n", getChannelName(channel));
    }
    setModbusBusy(false);
    xSemaphoreGive(modbusMutex);
    return false;
}

// ===================================================================
// Serial raw frame write commands
// ===================================================================
bool writeSingleCoilRaw(uint8_t slaveAddr, uint16_t coilAddress, bool state) {
    if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        Serial.printf("[%s] Error: Modbus busy, w5 mutex failed\n", getChannelName(channel));
        return false;
    }
    setModbusBusy(true);
    ModbusMessage req;
    uint16_t coilValue = state ? 0xFF00 : 0x0000;
    req.setMessage(slaveAddr, WRITE_COIL, coilAddress, coilValue);
    RTUutils::addCRC(req);
    rs485Flush(channel);
    Serial.printf("[%s] TX w5: ", getChannelName(channel));
    for (auto &b : req) Serial.printf("%02X ", b);
    Serial.println();
    rs485Write(channel, req.data(), req.size());
    uint8_t buf[32]; size_t n = 0; unsigned long t0 = millis(); const unsigned long timeout = 300;
    n = rs485Read(channel, buf, sizeof(buf), timeout);
    bool ok = false;
    if (n > 3) {
        Serial.printf("[%s] RX w5: ", getChannelName(channel));
        for (size_t i = 0; i < n; i++) Serial.printf("%02X ", buf[i]);
        Serial.println();
        uint16_t rcrc = (buf[n - 1] << 8) | buf[n - 2];
        uint16_t ccrc = RTUutils::calcCRC(buf, n - 2);
        if (ccrc == rcrc) {
            uint8_t fc = buf[1];
            if (fc & 0x80) {
                Serial.printf("[%s] Exception: FC=0x%02X, Error=0x%02X\n", getChannelName(channel), fc, buf[2]);
            } else {
                ok = (buf[0] == slaveAddr && fc == WRITE_COIL &&
                      (buf[2] << 8 | buf[3]) == coilAddress &&
                      (buf[4] << 8 | buf[5]) == coilValue);
                Serial.printf("[%s] %s\n", getChannelName(channel), ok ? "CRC OK - w5 echo match" : "w5 echo mismatch");
            }
        } else {
            Serial.printf("[%s] CRC error: calc=%04X recv=%04X\n", getChannelName(channel), ccrc, rcrc);
        }
    } else {
        Serial.printf("[%s] No response (cmd may have taken effect)\n", getChannelName(channel));
    }
    setModbusBusy(false);
    xSemaphoreGive(modbusMutex);
    return ok;
}

bool writeHoldingRegRaw(uint8_t slaveAddr, uint16_t regAddress, uint16_t value) {
    if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        Serial.printf("[%s] Error: Modbus busy, w6 mutex failed\n", getChannelName(channel));
        return false;
    }
    setModbusBusy(true);
    ModbusMessage req;
    const uint8_t WRITE_SINGLE_HOLDING = 0x06;
    req.setMessage(slaveAddr, WRITE_SINGLE_HOLDING, regAddress, value);
    RTUutils::addCRC(req);
    rs485Flush(channel);
    Serial.printf("[%s] TX w6: ", getChannelName(channel));
    for (auto &b : req) Serial.printf("%02X ", b);
    Serial.println();
    rs485Write(channel, req.data(), req.size());
    uint8_t buf[32]; size_t n = 0; unsigned long t0 = millis(); const unsigned long timeout = 300;
    n = rs485Read(channel, buf, sizeof(buf), timeout);
    bool ok = false;
    if (n > 3) {
        Serial.printf("[%s] RX w6: ", getChannelName(channel));
        for (size_t i = 0; i < n; i++) Serial.printf("%02X ", buf[i]);
        Serial.println();
        uint16_t rcrc = (buf[n - 1] << 8) | buf[n - 2];
        uint16_t ccrc = RTUutils::calcCRC(buf, n - 2);
        if (ccrc == rcrc) {
            uint8_t fc = buf[1];
            if (fc & 0x80) {
                Serial.printf("[%s] Exception: FC=0x%02X, Error=0x%02X\n", getChannelName(channel), fc, buf[2]);
            } else {
                ok = (buf[0] == slaveAddr && fc == 0x06 &&
                      (buf[2] << 8 | buf[3]) == regAddress &&
                      (buf[4] << 8 | buf[5]) == value);
                Serial.printf("[%s] %s\n", getChannelName(channel), ok ? "CRC OK - w6 echo match" : "w6 echo mismatch");
            }
        } else {
            Serial.printf("[%s] CRC error: calc=%04X recv=%04X\n", getChannelName(channel), ccrc, rcrc);
        }
    } else {
        Serial.printf("[%s] No response\n", getChannelName(channel));
    }
    setModbusBusy(false);
    xSemaphoreGive(modbusMutex);
    return ok;
}

void processSerialCommand() {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;
    if (line.startsWith("ch")) {
        int idx = line.indexOf(' ');
        if (idx > 0) {
            int chNum = line.substring(idx + 1).toInt();
            if (chNum >= 1 && chNum <= 4) {
                channel = (uint8_t)(chNum - 1);
                Serial.printf("Switched RS485 Port: %s\n", getChannelName(channel));
            } else {
                Serial.println("Usage: ch <1-4>");
            }
        } else {
            Serial.printf("Current RS485 Port: %s\n", getChannelName(channel));
        }
        return;
    }
    if (line.startsWith("w5")) {
        int coil = -1; String s;
        int idx1 = line.indexOf(' ');
        int idx2 = line.indexOf(' ', idx1 + 1);
        if (idx1 > 0 && idx2 > idx1) {
            coil = line.substring(idx1 + 1, idx2).toInt();
            s = line.substring(idx2 + 1); s.trim(); s.toLowerCase();
            bool st = (s == "on" || s == "1" || s == "true");
            if (coil >= 1 && coil <= 16) {
                uint16_t coilAddr = (uint16_t)(coil - 1);
                writeSingleCoilRaw(20, coilAddr, st);
            } else {
                Serial.println("Usage: w5 <1-16> <on|off|1|0>");
            }
        } else {
            Serial.println("Usage: w5 <1-16> <on|off|1|0>");
        }
        return;
    }
    if (line.startsWith("w6")) {
        int idx1 = line.indexOf(' ');
        int idx2 = line.indexOf(' ', idx1 + 1);
        if (idx1 > 0 && idx2 > idx1) {
            String rs = line.substring(idx1 + 1, idx2); rs.trim();
            String vs = line.substring(idx2 + 1); vs.trim();
            uint16_t reg = (uint16_t)strtoul(rs.c_str(), nullptr, 0);
            uint16_t val = (uint16_t)strtoul(vs.c_str(), nullptr, 0);
            writeHoldingRegRaw(20, reg, val);
        } else {
            Serial.println("Usage: w6 <reg_hex> <val_hex>  e.g. w6 0034 0000");
        }
        return;
    }
    Serial.println("Unknown command. Supported: ch / w5 / w6");
}

// ===================================================================
// Web handlers
// ===================================================================
void handleRelayControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    String body = "";
    for (size_t i = 0; i < len; i++) body += (char)data[i];
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
        request->send(400, "application/json", "{\"error_code\":\"INVALID_JSON\",\"message\":\"JSON format error\"}");
        return;
    }
    if (!doc["target"].is<const char*>()) {
        Serial.println("[WEB][RELAY] missing target");
        request->send(400, "application/json", "{\"success\":false,\"message\":\"missing target\"}");
        return;
    }
    if (!doc["id"].is<int>() || !doc["state"].is<bool>()) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"missing id or state\"}");
        return;
    }
    String target = doc["target"].as<String>();
    int relayId = doc["id"];
    bool state = doc["state"];

    uint8_t addr = 0;
    if (target == "single") {
        addr = 8;
        if (relayId != 1) {
            Serial.printf("[WEB][RELAY] target=single relay=%d invalid relay id\n", relayId);
            request->send(400, "application/json", "{\"success\":false,\"message\":\"invalid relay id\"}");
            return;
        }
    } else if (target == "multi") {
        addr = 20;
        if (relayId < 1 || relayId > 16) {
            Serial.printf("[WEB][RELAY] target=multi relay=%d invalid relay id\n", relayId);
            request->send(400, "application/json", "{\"success\":false,\"message\":\"invalid relay id\"}");
            return;
        }
    } else {
        Serial.printf("[WEB][RELAY] target=%s invalid target\n", target.c_str());
        request->send(400, "application/json", "{\"success\":false,\"message\":\"invalid target\"}");
        return;
    }

    bool found = false;
    uint8_t relayChannel = CHANNEL_1;
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].type == DEVICE_RELAY && g_devices[i].address == addr) {
            relayChannel = g_devices[i].channel;
            found = true;
            break;
        }
    }
    if (!found) {
        Serial.printf("[WEB][RELAY] target=%s addr=%d relay device not found\n", target.c_str(), addr);
        request->send(404, "application/json", "{\"success\":false,\"message\":\"relay device not found\"}");
        return;
    }
    if (isModbusBusy()) {
        request->send(503, "application/json", "{\"success\":false,\"message\":\"System busy, please retry\"}");
        return;
    }
    bool success = controlRelay(relayChannel, addr, relayId, state);
    if (success) {
        Serial.printf("[WEB][RELAY] target=%s channel=RS485-%d addr=%d relay=%d state=%s success\n",
                      target.c_str(),
                      relayChannel + 1,
                      addr,
                      relayId,
                      state ? "ON" : "OFF");
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Command sent\",\"is_pending\":true}");
    } else {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Relay control failed\"}");
    }
}

void handleSystemStatus(AsyncWebServerRequest *request) {
    JsonDocument doc;
    SystemState_t state = getSystemState();
    doc["timestamp"] = millis();
    doc["work_mode"] = "manual";
    doc["is_night_mode"] = false;
    doc["current_channel"] = (int)channel;
    doc["channel_name"] = getChannelName(channel);
    JsonObject canopy = doc["canopy"].to<JsonObject>();
    JsonObject canopyTemp_obj = canopy["temperature"].to<JsonObject>();
    canopyTemp_obj["value"] = state.canopy_sensor.temperature;
    canopyTemp_obj["online"] = state.canopy_sensor.online;
    JsonObject canopyHum_obj = canopy["humidity"].to<JsonObject>();
    canopyHum_obj["value"] = state.canopy_sensor.humidity;
    canopyHum_obj["online"] = state.canopy_sensor.online;
    JsonObject rootZone = doc["root_zone"].to<JsonObject>();
    JsonObject rootTemp_obj = rootZone["temperature"].to<JsonObject>();
    rootTemp_obj["value"] = state.root_sensor.temperature;
    rootTemp_obj["online"] = state.root_sensor.online;
    JsonObject rootHum_obj = rootZone["humidity"].to<JsonObject>();
    rootHum_obj["value"] = state.root_sensor.humidity;
    rootHum_obj["online"] = state.root_sensor.online;
    JsonObject nutrient = doc["nutrient_solution"].to<JsonObject>();
    JsonObject nutrientTemp_obj = nutrient["temperature"].to<JsonObject>();
    nutrientTemp_obj["value"] = 0.0;
    nutrientTemp_obj["online"] = false;
    JsonArray relays = doc["relays"].to<JsonArray>();
    for (int i = 0; i < RELAY_COUNT; i++) {
        JsonObject relay = relays.add<JsonObject>();
        relay["id"] = i + 1;
        relay["name"] = "Relay" + String(i + 1);
        relay["type"] = "unassigned";
        relay["state"] = state.relays[i].state;
        relay["is_pending"] = state.relays[i].is_pending;
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

// ===================================================================
// NTP time sync (blocking, with retry) - NEW
// ===================================================================
void syncTimeWithNTP_blocking() {
    Serial.println("NTP sync...");
    timeClient.begin();
    bool success = false;
    for (int i = 0; i < 5; i++) {
        if (timeClient.forceUpdate()) {
            unsigned long epochTime = timeClient.getEpochTime();
            struct timeval tv = { .tv_sec = (time_t)epochTime, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            success = true;
            Serial.printf("NTP sync OK\n");
            break;
        } else {
            Serial.printf("NTP retry %d/5...\n", i + 1);
            delay(2000);
        }
    }
    if (!success) Serial.println("NTP sync failed");
}

// ===================================================================
// WiFi connection (reads from ConfigManager) - MODIFIED
// ===================================================================
void connectToWifi() {
    String ssid, password;
    if (ConfigManager::loadWifiCredentials(ssid, password)) {
        Serial.printf("WiFi: %s ...\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), password.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println();
            Serial.println("WiFi connected!");
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());
            updateSystemConnectivity(true);
            delay(500);
            syncTimeWithNTP_blocking();
        } else {
            Serial.println();
            Serial.println("WiFi connection failed!");
            updateSystemConnectivity(false);
        }
    } else {
        Serial.println("No WiFi credentials saved. Running offline.");
    }
}

// ===================================================================
// setup() - dual mode entry point
// ===================================================================
void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    Serial.println("\n\n=======================================================");
    Serial.println("  JISHI RS485 Controller - 4-Channel Demo");
    Serial.println("  HW: JS-V1.5  |  FW: V2.1");
    Serial.println("=======================================================");

    ConfigManager::begin();

    if (ConfigManager::isTimeConfigured()) {
        // --- NORMAL MODE ---
        currentMode = NORMAL_MODE;
        Serial.println("Time configured, entering NORMAL mode.");

        if (!initSystemState()) {
            Serial.println("Error: System state init failed!");
            return;
        }

    delay(200);  // wait for flash after software reset
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS init failed");
        } else {
            Serial.println("SPIFFS init OK");
        }

        modbusMutex = xSemaphoreCreateMutex();
        if (modbusMutex == NULL) {
            Serial.println("Error: Mutex create failed");
        } else {
            Serial.println("Mutex created OK");
        }

        g_sc16.initSPI(PIN_SC16_SCK, PIN_SC16_MISO, PIN_SC16_MOSI);

        Serial.println("Initializing RS485 ports:");
        bool chOk[4] = {false};
        for (int i = 0; i < 4; i++) {
            switch (i) {
                case CHANNEL_1:
                    pinMode(PIN_RS485_CH1_DE_RE, OUTPUT);
                    digitalWrite(PIN_RS485_CH1_DE_RE, LOW);
                    Serial1.begin(RS485_BAUDRATE, SERIAL_8N1, PIN_RS485_CH1_RX, PIN_RS485_CH1_TX);
                    chOk[i] = true;
                    break;
                case CHANNEL_2:
                    pinMode(PIN_RS485_CH2_DE_RE, OUTPUT);
                    digitalWrite(PIN_RS485_CH2_DE_RE, LOW);
                    Serial2.begin(RS485_BAUDRATE, SERIAL_8N1, PIN_RS485_CH2_RX, PIN_RS485_CH2_TX);
                    chOk[i] = true;
                    break;
                case CHANNEL_3:
                    chOk[i] = g_sc16.beginChannel(SC16_CH_A, RS485_BAUDRATE);
                    break;
                case CHANNEL_4:
                    chOk[i] = g_sc16.beginChannel(SC16_CH_B, RS485_BAUDRATE);
                    break;
            }
            Serial.printf("  %s: %s\n", getChannelName(i), chOk[i] ? "OK" : "FAIL");
        }


        // --- Board Self-Test ---
        BoardSelfTest selfTest;
        BoardSelfTestResult stResult = selfTest.run();
        // --- End Self-Test ---

        connectToWifi();

        setenv("TZ", "CST-8", 1);
        tzset();
        Serial.println("Timezone: CST-8 (UTC+8)");

        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("System time: %s\n", time_str);

        if (WiFi.status() == WL_CONNECTED) {
            timeClient.begin();
        }

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            if (SPIFFS.exists("/index.html")) {
                request->send(SPIFFS, "/index.html", "text/html");
            } else {
                request->send(404, "text/plain", "File not found");
            }
        });

        server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(204);
        });

        server.on("/api/control/relays", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleRelayControl);
        server.on("/api/system/status", HTTP_GET, handleSystemStatus);

        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

        server.begin();
        Serial.println("Web server started");
        Serial.printf("Current RS485 Port: %s\n", getChannelName(channel));
        Serial.println("Commands: ch <1-4>  |  w5 <1-16> <on|off>  |  w6 <reg> <val>");

    } else {
        // --- CONFIG MODE ---
        currentMode = CONFIG_MODE;
        Serial.println("Time not configured, starting Captive Portal.");
        configServer.start();
    }
}

// ===================================================================
// loop() - dual mode
// ===================================================================
void loop() {
    if (currentMode == CONFIG_MODE) {
        configServer.process();
    } else {
        // NTP periodic update (non-blocking)
        if (WiFi.status() == WL_CONNECTED) {
            timeClient.update();
        }

        // Existing RS485 business logic
        if (Serial.available()) {
            processSerialCommand();
        }
        if (millis() - last_request_time > SENSOR_POLLING_INTERVAL_MS) {
            if (isModbusBusy()) {
                return;
            }
            if (xSemaphoreTake(modbusMutex, 0) != pdTRUE) {
                return;
            }
            last_request_time = millis();
            setModbusBusy(true);

            DeviceConfig_t& current_device = g_devices[current_device_index];

            Serial.println("\n-------------------------------------------------------");
            Serial.printf("[%s] --> Request: [%s], addr:%d, type:%s\n",
                          getChannelName(current_device.channel),
                          current_device.name, current_device.address,
                          (current_device.type == DEVICE_SENSOR) ? "Sensor" : "Relay");

            ModbusMessage request;
            if (current_device.type == DEVICE_SENSOR) {
                request.setMessage(current_device.address, READ_INPUT_REGISTER,
                                 current_device.register_addr, current_device.register_count);
            } else {
                request.setMessage(current_device.address, READ_COIL,
                                 current_device.register_addr, 16);
            }
            RTUutils::addCRC(request);
            rs485Flush(current_device.channel);
            Serial.printf("[%s] TX: ", getChannelName(current_device.channel));
            for(auto& byte : request) Serial.printf("%02X ", byte);
            Serial.println();
            rs485Write(current_device.channel, request.data(), request.size());

            uint8_t buffer[50];
            size_t received_len = rs485Read(current_device.channel, buffer, sizeof(buffer), RESPONSE_TIMEOUT_MS);

            if (received_len > 3) {
                Serial.printf("[%s] RX: ", getChannelName(current_device.channel));
                for(size_t i = 0; i < received_len; i++) Serial.printf("%02X ", buffer[i]);
                Serial.println();
                uint16_t received_crc = (buffer[received_len - 1] << 8) | buffer[received_len - 2];
                uint16_t calculated_crc = RTUutils::calcCRC(buffer, received_len - 2);
                if (calculated_crc == received_crc) {
                    std::vector<uint8_t> responseData(buffer, buffer + received_len - 2);
                    ModbusMessage response(responseData);
                    if (response.getFunctionCode() & 0x80) {
                        uint8_t errorCode;
                        response.get(2, errorCode);
                        ModbusError me((Error)errorCode);
                        Serial.printf("[%s] Modbus Error: %s\n", getChannelName(current_device.channel), (const char*)me);
                    } else if (response.getServerID() == current_device.address) {
                        Serial.printf("[%s] CRC OK - Parsing\n", getChannelName(current_device.channel));
                        if (current_device.type == DEVICE_SENSOR && response.getFunctionCode() == READ_INPUT_REGISTER) {
                            uint16_t temp_raw, humidity_raw;
                            response.get(3, temp_raw);
                            response.get(5, humidity_raw);
                            float temp_val = (temp_raw >= 10000) ? (-1.0f * (float)(temp_raw - 10000) * 0.1f) : ((float)temp_raw * 0.1f);
                            float humidity_val = (float)humidity_raw * 0.1f;
                            updateSensorData(current_device.address, temp_val, humidity_val, true);
                            Serial.printf("[%s] Parsed: [%s] - Temp: %.1fC, Hum: %.1f%%\n",
                                          getChannelName(current_device.channel), current_device.name, temp_val, humidity_val);
                        } else if (current_device.type == DEVICE_RELAY && response.getFunctionCode() == READ_COIL) {
                            uint8_t data_len = response[2];
                            Serial.printf("[%s] Relay coil data (%d bytes): ", getChannelName(current_device.channel), data_len);
                            for (int i = 0; i < data_len && i < 2; i++) {
                                uint8_t byte_data = response[3 + i];
                                if (current_device.address == 20) {
                                    for (int bit = 0; bit < 8; bit++) {
                                        int relay_index = i * 8 + bit;
                                        if (relay_index < RELAY_COUNT) {
                                            bool relay_state = (byte_data >> bit) & 0x01;
                                            updateRelayState(relay_index + 1, relay_state);
                                        }
                                    }
                                }
                                Serial.printf("%02X ", byte_data);
                            }
                            Serial.println();
                            if (current_device.address == 20) {
                                Serial.printf("[%s] Relay states updated\n", getChannelName(current_device.channel));
                            } else {
                                Serial.printf("[%s] Single relay status read OK\n", getChannelName(current_device.channel));
                            }
                        }
                    }
                } else {
                    Serial.printf("[%s] CRC error! (calc: %04X, recv: %04X)\n", getChannelName(current_device.channel), calculated_crc, received_crc);
                }
            } else if (received_len > 0) {
                Serial.printf("[%s] Packet too short (%d bytes)\n", getChannelName(current_device.channel), received_len);
            } else {
                Serial.printf("[%s] Timeout - no response\n", getChannelName(current_device.channel));
            }

            current_device_index = (current_device_index + 1) % g_device_count;
            setModbusBusy(false);
            xSemaphoreGive(modbusMutex);
        }

        delay(10);
    }
}
