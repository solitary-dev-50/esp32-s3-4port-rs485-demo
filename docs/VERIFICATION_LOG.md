# Verification Log

This document records one verified boot and polling session for the 4-Port RS485 Control Board + Web Demo System.

The log is kept as a practical verification record. It is not a certification report.

## Test Environment

- Serial monitor: COM11
- Baud rate: 115200 8-N-1
- System time in log: 2026-06-09 20:14:23
- Device IP in log: 192.168.3.33

Note: the local PlatformIO monitor printed an obsolete PIO Core warning before the firmware log. That warning belongs to the local development environment and is not part of the board firmware verification.

## Verified Result Summary

| Item | Result |
| ---- | ------ |
| SPIFFS initialization | PASS |
| Mutex creation | PASS |
| SC16 SPI initialization | PASS |
| RS485-1 initialization | PASS |
| RS485-2 initialization | PASS |
| RS485-3 initialization | PASS |
| RS485-4 initialization | PASS |
| Buzzer self-test trigger | PASS |
| RGB indicator self-test trigger | PASS |
| Boot self-test | PASS devices=4/4 |
| WiFi connection | PASS |
| NTP sync | PASS |
| Web server start | PASS |
| Normal RS485 polling | PASS |

## Verified Device Setup

| RS485 Port | Device                      | Address | Type   | Verified Result |
| ---------- | --------------------------- | ------- | ------ | --------------- |
| RS485-1    | Single Relay Tester         | 8       | Relay  | Status read OK |
| RS485-2    | 16-Channel Relay Controller | 20      | Relay  | Status read OK |
| RS485-3    | Temp & Humidity Sensor 1    | 3       | Sensor | Temperature/Humidity read OK |
| RS485-4    | Temp & Humidity Sensor 2    | 6       | Sensor | Temperature/Humidity read OK |

## Boot Initialization Excerpt

```text
SPIFFS init OK
Mutex created OK
SC16 SPI init: CS=21, SCK=42, MISO=41, MOSI=40, speed=1000000, mode=0
Initializing RS485 ports:
  RS485-1: OK
  RS485-2: OK
SC16 CH A self test OK
SC16 CH A begin baud=38400 divisor=24 OK
  RS485-3: OK
SC16 CH B self test OK
SC16 CH B begin baud=38400 divisor=24 OK
  RS485-4: OK
```

## Boot Self-Test Excerpt

```text
[SELFTEST] Board self-test started
[SELFTEST][BUZZER] beep triggered
[SELFTEST][ALARM_LED] rgb test triggered
[SELFTEST][RS485-1] start name=Single Relay Tester addr=8 type=Relay
[SELFTEST][RS485-1] Single Relay Tester online relay status read OK
[SELFTEST][RS485-2] start name=16-Channel Relay Controller addr=20 type=Relay
[SELFTEST][RS485-2] 16-Channel Relay Controller online relay status read OK
[SELFTEST][RS485-3] start name=Temp & Humidity Sensor 1 addr=3 type=Sensor
[SELFTEST][RS485-3] Temp & Humidity Sensor 1 online Temp=28.7C Hum=40.8%
[SELFTEST][RS485-4] start name=Temp & Humidity Sensor 2 addr=6 type=Sensor
[SELFTEST][RS485-4] Temp & Humidity Sensor 2 online Temp=30.2C Hum=31.8%
[SELFTEST] PASS devices=4/4
[SELFTEST] Done
```

## Network and Web Server Excerpt

```text
WiFi credentials loaded: SSID=Ji Shi
WiFi connected!
IP: 192.168.3.33
NTP sync OK
Timezone: CST-8 (UTC+8)
System time: 2026-06-09 20:14:23
Web server started
Current RS485 Port: RS485-1
```

## Normal Polling Excerpts

### RS485-1 Single Relay Tester

```text
[RS485-1] --> Request: [Single Relay Tester], addr:8, type:Relay
[RS485-1] TX: 08 01 00 00 00 10 3D 5F
[RS485-1] RX: 08 01 02 0E 00 61 9D
[RS485-1] CRC OK - Parsing
[RS485-1] Relay coil data (2 bytes): 0E 00
[RS485-1] Single relay status read OK
```

### RS485-2 16-Channel Relay Controller

```text
[RS485-2] --> Request: [16-Channel Relay Controller], addr:20, type:Relay
[RS485-2] TX: 14 01 00 00 00 10 3F 03
[RS485-2] RX: 14 01 02 00 00 B4 3F
[RS485-2] CRC OK - Parsing
[RS485-2] Relay coil data (2 bytes): 00 00
[RS485-2] Relay states updated
```

### RS485-3 Temp & Humidity Sensor 1

```text
[RS485-3] --> Request: [Temp & Humidity Sensor 1], addr:3, type:Sensor
[RS485-3] TX: 03 04 00 00 00 02 70 29
[RS485-3] RX: 03 04 04 01 1E 01 96 38 40
[RS485-3] CRC OK - Parsing
[RS485-3] Parsed: [Temp & Humidity Sensor 1] - Temp: 28.6C, Hum: 40.6%
```

### RS485-4 Temp & Humidity Sensor 2

```text
[RS485-4] --> Request: [Temp & Humidity Sensor 2], addr:6, type:Sensor
[RS485-4] TX: 06 04 00 00 00 02 70 7C
[RS485-4] RX: 06 04 04 01 2E 01 3E 6C F1
[RS485-4] CRC OK - Parsing
[RS485-4] Parsed: [Temp & Humidity Sensor 2] - Temp: 30.2C, Hum: 31.8%
```

## Repeated Polling Stability

The captured session shows repeated polling cycles with successful CRC checks and parsed results:

- RS485-1 relay status was read repeatedly.
- RS485-2 relay status was read repeatedly.
- RS485-3 temperature and humidity values were read repeatedly.
- RS485-4 temperature and humidity values were read repeatedly.

Example later readings:

```text
[RS485-3] Parsed: [Temp & Humidity Sensor 1] - Temp: 28.7C, Hum: 39.8%
[RS485-4] Parsed: [Temp & Humidity Sensor 2] - Temp: 30.2C, Hum: 31.3%
[RS485-3] Parsed: [Temp & Humidity Sensor 1] - Temp: 28.6C, Hum: 39.9%
[RS485-4] Parsed: [Temp & Humidity Sensor 2] - Temp: 30.2C, Hum: 31.4%
```

## Notes

- `SC16 CH A` and `SC16 CH B` logs refer to the internal channels of the SC16 UART expansion chip.
- `RS485-3` maps to `SC16 CH A`.
- `RS485-4` maps to `SC16 CH B`.
- This verification log records a working demo baseline. It should not be treated as industrial certification or production qualification.
