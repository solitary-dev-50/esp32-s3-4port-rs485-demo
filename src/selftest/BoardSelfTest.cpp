#include "Config.h"
#include "BoardSelfTest.h"
#include "ModbusMessage.h"
#include "RTUutils.h"
#include <SPI.h>

#include "driver/rmt.h"
#include "esp_err.h"

// --- Extern declarations (defined in main.cpp) ---
extern SemaphoreHandle_t modbusMutex;
extern void rs485Write(uint8_t ch, const uint8_t* data, size_t len);
extern int  rs485Read(uint8_t ch, uint8_t* buf, size_t maxLen, uint32_t timeoutMs);
extern void rs485Flush(uint8_t ch);
extern const char* getChannelName(uint8_t ch);

// ===================================================================
// Local helpers
// ===================================================================

static const int SELFTEST_DEVICE_RETRIES = 3;
static const uint32_t SELFTEST_READ_TIMEOUT_MS = RESPONSE_TIMEOUT_MS;
static const uint16_t SELFTEST_ALARM_LED_COLOR_HOLD_MS = 800;
static const uint16_t SELFTEST_ALARM_LED_OFF_HOLD_MS = 150;

// 2812 / WS2812 style single LED settings
static const rmt_channel_t SELFTEST_LED_RMT_CHANNEL = RMT_CHANNEL_0;
static bool g_ledRmtReady = false;

// RMT clock: APB 80MHz / 2 = 40MHz, 1 tick = 25ns
static const uint8_t RMT_CLK_DIV = 2;

// 2812 timing, converted to 40MHz ticks.
// One bit is about 1.25us.
// 0: high about 0.35us, low about 0.90us
// 1: high about 0.80us, low about 0.45us
static const uint16_t WS2812_T0H = 14;  // 14 * 25ns = 350ns
static const uint16_t WS2812_T0L = 36;  // 36 * 25ns = 900ns
static const uint16_t WS2812_T1H = 32;  // 32 * 25ns = 800ns
static const uint16_t WS2812_T1L = 18;  // 18 * 25ns = 450ns

void boardBuzzerBeep(uint16_t freqHz, uint16_t durationMs) {
    if (freqHz == 0 || durationMs == 0) {
        digitalWrite(PIN_BUZZER, LOW);
        return;
    }

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    const uint32_t halfPeriodUs = 1000000UL / (uint32_t)freqHz / 2UL;
    const uint32_t cycles = ((uint32_t)durationMs * 1000UL) / (halfPeriodUs * 2UL);

    for (uint32_t i = 0; i < cycles; i++) {
        digitalWrite(PIN_BUZZER, HIGH);
        delayMicroseconds(halfPeriodUs);
        digitalWrite(PIN_BUZZER, LOW);
        delayMicroseconds(halfPeriodUs);
    }

    digitalWrite(PIN_BUZZER, LOW);
}

static bool initLedRmt() {
    if (g_ledRmtReady) {
        return true;
    }

    pinMode(PIN_LED_STRIP_DATA, OUTPUT);
    digitalWrite(PIN_LED_STRIP_DATA, LOW);

    rmt_config_t config = {};
    config.rmt_mode = RMT_MODE_TX;
    config.channel = SELFTEST_LED_RMT_CHANNEL;
    config.gpio_num = (gpio_num_t)PIN_LED_STRIP_DATA;
    config.mem_block_num = 1;
    config.clk_div = RMT_CLK_DIV;

    config.tx_config.loop_en = false;
    config.tx_config.carrier_en = false;
    config.tx_config.carrier_freq_hz = 0;
    config.tx_config.carrier_duty_percent = 0;
    config.tx_config.carrier_level = RMT_CARRIER_LEVEL_LOW;
    config.tx_config.idle_output_en = true;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        Serial.printf("[SELFTEST][ALARM_LED] RMT config failed: %d\n", (int)err);
        return false;
    }

    err = rmt_driver_install(SELFTEST_LED_RMT_CHANNEL, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.printf("[SELFTEST][ALARM_LED] RMT driver install failed: %d\n", (int)err);
        return false;
    }

    g_ledRmtReady = true;
    return true;
}

static void buildWs2812Items(uint8_t r, uint8_t g, uint8_t b, rmt_item32_t* items) {
    // Most 2812 / WS2812 compatible LEDs use GRB order.
    uint8_t bytes[3] = { g, r, b };

    int itemIndex = 0;
    for (int byteIndex = 0; byteIndex < 3; byteIndex++) {
        uint8_t value = bytes[byteIndex];

        for (int bit = 7; bit >= 0; bit--) {
            bool one = value & (1 << bit);

            items[itemIndex].level0 = 1;
            items[itemIndex].duration0 = one ? WS2812_T1H : WS2812_T0H;
            items[itemIndex].level1 = 0;
            items[itemIndex].duration1 = one ? WS2812_T1L : WS2812_T0L;

            itemIndex++;
        }
    }
}

bool boardSetAlarmLedColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!initLedRmt()) {
        return false;
    }

    rmt_item32_t items[24];
    buildWs2812Items(r, g, b, items);

    esp_err_t err = rmt_write_items(SELFTEST_LED_RMT_CHANNEL, items, 24, true);
    if (err != ESP_OK) {
        Serial.printf("[SELFTEST][ALARM_LED] RMT write failed: %d\n", (int)err);
        return false;
    }

    err = rmt_wait_tx_done(SELFTEST_LED_RMT_CHANNEL, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        Serial.printf("[SELFTEST][ALARM_LED] RMT wait failed: %d\n", (int)err);
        return false;
    }

    // 2812 latch/reset time. Keep low long enough.
    delayMicroseconds(300);
    return true;
}

static bool validateCrc(const uint8_t* buf, int len) {
    if (buf == nullptr || len < 4) {
        return false;
    }

    uint16_t receivedCrc = ((uint16_t)buf[len - 1] << 8) | buf[len - 2];
    uint16_t calculatedCrc = RTUutils::calcCRC(buf, len - 2);

    return calculatedCrc == receivedCrc;
}

static const char* deviceTypeName(uint8_t type) {
    return (type == DEVICE_SENSOR) ? "Sensor" : "Relay";
}

// ===================================================================
// Buzzer test
// ===================================================================
void BoardSelfTest::testBuzzer(BoardSelfTestResult& r) {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    // 2.5kHz / 200ms. Works better than a single DC HIGH pulse,
    // especially if the buzzer is passive.
    boardBuzzerBeep(2500, 200);

    r.buzzerTriggered = true;
    Serial.println("[SELFTEST][BUZZER] beep triggered");
}

// ===================================================================
// Alarm LED test
// ===================================================================
void BoardSelfTest::testAlarmLed(BoardSelfTestResult& r) {
    pinMode(PIN_LED_STRIP_DATA, OUTPUT);
    digitalWrite(PIN_LED_STRIP_DATA, LOW);
    delayMicroseconds(300);

    bool ok = true;

    // Red
    ok = boardSetAlarmLedColor(60, 0, 0) && ok;
    delay(SELFTEST_ALARM_LED_COLOR_HOLD_MS);

    // Green
    ok = boardSetAlarmLedColor(0, 60, 0) && ok;
    delay(SELFTEST_ALARM_LED_COLOR_HOLD_MS);

    // Blue
    ok = boardSetAlarmLedColor(0, 0, 60) && ok;
    delay(SELFTEST_ALARM_LED_COLOR_HOLD_MS);

    // Off
    ok = boardSetAlarmLedColor(0, 0, 0) && ok;
    delay(SELFTEST_ALARM_LED_OFF_HOLD_MS);

    r.alarmLedTriggered = ok;

    if (ok) {
        Serial.println("[SELFTEST][ALARM_LED] rgb test triggered");
    } else {
        Serial.println("[SELFTEST][ALARM_LED] rgb test trigger failed");
    }
}

// ===================================================================
// Single device test
// ===================================================================
bool BoardSelfTest::testOneDevice(uint8_t deviceIndex, int retries) {
    if (deviceIndex >= g_device_count) {
        return false;
    }

    DeviceConfig_t& device = g_devices[deviceIndex];
    uint8_t ch = device.channel;

    Serial.printf("[SELFTEST][%s] start name=%s addr=%d type=%s\n",
                  getChannelName(ch),
                  device.name,
                  device.address,
                  deviceTypeName(device.type));

    for (int attempt = 0; attempt < retries; attempt++) {
        if (attempt > 0) {
            delay(100);
        }

        if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;
        }

        ModbusMessage request;

        if (device.type == DEVICE_SENSOR) {
            request.setMessage(device.address,
                               READ_INPUT_REGISTER,
                               device.register_addr,
                               device.register_count);
        } else {
            // Safety rule: relay self-test only reads coil status.
            // Never turn relay ON/OFF in boot self-test.
            request.setMessage(device.address,
                               READ_COIL,
                               device.register_addr,
                               16);
        }

        RTUutils::addCRC(request);

        rs485Flush(ch);
        rs485Write(ch, request.data(), request.size());

        uint8_t buf[50];
        int n = rs485Read(ch, buf, sizeof(buf), SELFTEST_READ_TIMEOUT_MS);

        xSemaphoreGive(modbusMutex);

        if (n <= 3) {
            continue;
        }

        if (!validateCrc(buf, n)) {
            continue;
        }

        if (buf[0] != device.address) {
            continue;
        }

        // Sensor response:
        // addr + function + byteCount + data(4 bytes) + crc(2 bytes) = 9 bytes
        if (device.type == DEVICE_SENSOR && buf[1] == READ_INPUT_REGISTER) {
            if (n < 9 || buf[2] < 4) {
                continue;
            }

            uint16_t tempRaw = ((uint16_t)buf[3] << 8) | buf[4];
            uint16_t humRaw  = ((uint16_t)buf[5] << 8) | buf[6];

            float temp = (tempRaw >= 10000)
                ? (-1.0f * (float)(tempRaw - 10000) * 0.1f)
                : ((float)tempRaw * 0.1f);

            float hum = (float)humRaw * 0.1f;

            Serial.printf("[SELFTEST][%s] %s online Temp=%.1fC Hum=%.1f%%\n",
                          getChannelName(ch),
                          device.name,
                          temp,
                          hum);
            return true;
        }

        // Relay response:
        // addr + function + byteCount + data + crc
        if (device.type == DEVICE_RELAY && buf[1] == READ_COIL) {
            if (n < 6 || buf[2] < 1) {
                continue;
            }

            Serial.printf("[SELFTEST][%s] %s online relay status read OK\n",
                          getChannelName(ch),
                          device.name);
            return true;
        }
    }

    if (device.type == DEVICE_SENSOR) {
        Serial.printf("[SELFTEST][%s] %s offline or sensor data abnormal\n",
                      getChannelName(ch),
                      device.name);
    } else {
        Serial.printf("[SELFTEST][%s] %s offline relay status read failed\n",
                      getChannelName(ch),
                      device.name);
    }

    return false;
}

// ===================================================================
// run()
// ===================================================================
BoardSelfTestResult BoardSelfTest::run() {
    BoardSelfTestResult result;
    result.totalDevices = g_device_count;
    result.onlineDevices = 0;

    Serial.println("\n[SELFTEST] Board self-test started");

    // 1. Buzzer
    testBuzzer(result);

    // 2. Alarm LED
    testAlarmLed(result);

    // 3. Device loop
    for (uint8_t i = 0; i < g_device_count; i++) {
        if (testOneDevice(i, SELFTEST_DEVICE_RETRIES)) {
            result.onlineDevices++;
        }
    }

    result.allPassed = (result.onlineDevices == result.totalDevices);
    result.hasWarning = !result.allPassed;

    Serial.printf("[SELFTEST] %s devices=%d/%d\n",
                  result.allPassed ? "PASS" : "WARN",
                  result.onlineDevices,
                  result.totalDevices);

    // Final short beep. PASS = 1 beep, WARN = 2 beeps.
    uint8_t beeps = result.allPassed ? 1 : 2;
    for (uint8_t i = 0; i < beeps; i++) {
        boardBuzzerBeep(2500, 120);
        delay(180);
    }

    // Final LED status:
    // PASS: green 1 second, then off.
    // WARN: yellow 1 second, then off.
    if (result.allPassed) {
        boardSetAlarmLedColor(0, 50, 0);
    } else {
        boardSetAlarmLedColor(50, 35, 0);
    }
    delay(1000);
    boardSetAlarmLedColor(0, 0, 0);

    Serial.println("[SELFTEST] Done\n");
    return result;
}
