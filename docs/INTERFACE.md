# Interface Specification

## 1. System Overview

This project is a low-cost 4-port RS485 acquisition and relay-control board with a web demo system.

The current firmware is a minimal demo system. It is used to prove the board-level hardware path and basic control flow.

Current demo capabilities:

- 4 RS485 ports
- 2 relay devices
- 2 temperature and humidity sensors
- Web demo console
- Boot self-test
- Local buzzer and RGB alarm indicator

This is not a certified industrial product. It is a demo and development foundation for RS485 acquisition, relay control, local alarm indication, and device online checks.

## 2. RS485 Port Mapping

The device table comes from the current `g_devices[]` firmware configuration.

| RS485 Port | Device Name | Type | Modbus Address | Register Start | Register Count |
| ---------- | ----------- | ---- | -------------- | -------------- | -------------- |
| RS485-1 | Single Relay Tester | Relay | 8 | 0x0000 | 1 |
| RS485-2 | 16-Channel Relay Controller | Relay | 20 | 0x0000 | 3 |
| RS485-3 | Temp & Humidity Sensor 1 | Sensor | 3 | 0x0000 | 2 |
| RS485-4 | Temp & Humidity Sensor 2 | Sensor | 6 | 0x0000 | 2 |

Note: relay status polling in the current firmware requests 16 coils for relay devices. The table above records the current `g_devices[]` fields.

## 3. Modbus Function Codes

| Function | Code | Used For |
| -------- | ---- | -------- |
| Read Coils | 0x01 | Read relay coil status |
| Read Input Registers | 0x04 | Read temperature and humidity input registers |
| Write Single Coil | 0x05 | Control one relay output |

## 4. Sensor Read Protocol

Temperature and humidity sensors are read through Modbus input registers.

- Addresses: 3 and 6
- Function: 0x04 Read Input Registers
- Start register: 0x0000
- Register count: 2
- Response data: temperature raw value and humidity raw value
- Scaling: value / 10.0

The current firmware reads two registers only. Do not assume extra registers unless the sensor protocol is verified first.

## 5. Relay Status Read Protocol

Relay status is read through Modbus coils.

- Addresses: 8 and 20
- Function: 0x01 Read Coils
- Start coil: 0x0000
- Coil count: 16 in the current runtime polling and self-test

Current relay devices:

- Single Relay Tester: currently uses Y1 only.
- 16-Channel Relay Controller: uses Y1 to Y16.

The current firmware updates the web 16-channel relay state table from address 20 only.

## 6. Relay Control Protocol

Relay control writes one coil at a time.

- Function: 0x05 Write Single Coil
- Coil index: relay id - 1
- ON value: 0xFF00
- OFF value: 0x0000

Control target rules:

- `single`: RS485-1, address 8, only relay id 1 is allowed.
- `multi`: RS485-2, address 20, relay id 1 to 16 is allowed.

The control path explicitly selects channel and address from the current device table. It must not guess the relay channel from the first relay device.

## 7. Web API

### GET /api/system/status

Purpose: read sensor data, relay status, and basic system status.

Confirmed response fields in the current firmware include:

- `timestamp`
- `work_mode`
- `is_night_mode`
- `current_channel`
- `channel_name`
- `canopy.temperature.value`
- `canopy.temperature.online`
- `canopy.humidity.value`
- `canopy.humidity.online`
- `root_zone.temperature.value`
- `root_zone.temperature.online`
- `root_zone.humidity.value`
- `root_zone.humidity.online`
- `nutrient_solution.temperature.value`
- `nutrient_solution.temperature.online`
- `relays[]`
- `relays[].id`
- `relays[].name`
- `relays[].type`
- `relays[].state`
- `relays[].is_pending`

Actual fields follow the current firmware implementation.

### POST /api/control/relays

Purpose: control a relay output from the web console.

Single relay request:

```json
{
  "target": "single",
  "id": 1,
  "state": true
}
```

Multi relay request:

```json
{
  "target": "multi",
  "id": 1,
  "state": true
}
```

Successful response in the current firmware:

```json
{
  "success": true,
  "message": "Command sent",
  "is_pending": true
}
```

Failure responses use `success: false` with a short `message`, for example:

```json
{
  "success": false,
  "message": "invalid relay id"
}
```

Confirmed failure messages include:

- `missing target`
- `missing id or state`
- `invalid relay id`
- `invalid target`
- `relay device not found`
- `System busy, please retry`
- `Relay control failed`

Confirmed success logs:

```text
[WEB][RELAY] target=single channel=RS485-1 addr=8 relay=1 state=ON success
[WEB][RELAY] target=multi channel=RS485-2 addr=20 relay=1 state=ON success
```

## 8. Boot Self-Test

The boot self-test runs after RS485 and SC16 initialization and before normal polling.

Current self-test checks:

- Buzzer triggered
- RGB alarm indicator triggered
- RS485-1 to RS485-4 device online check
- Sensor data read test
- Relay status read test
- Summary result such as `PASS devices=4/4`

Relay self-test only reads relay status. It must not automatically turn relay outputs on or off.

## 9. Verified Demo Configuration

Current verified demo combination:

- RS485-1 addr=8 Single Relay Tester
- RS485-2 addr=20 16-Channel Relay Controller
- RS485-3 addr=3 Temp & Humidity Sensor 1
- RS485-4 addr=6 Temp & Humidity Sensor 2

