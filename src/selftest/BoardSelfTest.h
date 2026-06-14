#ifndef BOARD_SELF_TEST_H
#define BOARD_SELF_TEST_H

#include <Arduino.h>

void boardBuzzerBeep(uint16_t freqHz, uint16_t durationMs);
bool boardSetAlarmLedColor(uint8_t r, uint8_t g, uint8_t b);

struct BoardSelfTestResult {
    uint8_t totalDevices = 0;
    uint8_t onlineDevices = 0;
    bool hasWarning = false;
    bool allPassed = false;
    bool buzzerTriggered = false;
    bool alarmLedTriggered = false;
};

class BoardSelfTest {
public:
    BoardSelfTestResult run();
private:
    void testBuzzer(BoardSelfTestResult& r);
    void testAlarmLed(BoardSelfTestResult& r);
    bool testOneDevice(uint8_t deviceIndex, int retries);
};

#endif
