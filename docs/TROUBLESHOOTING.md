# RS485 / Modbus Troubleshooting Guide

Practical pitfalls and debugging notes for the 4-Port RS485 Control Board + Web Demo System.

This guide focuses on practical troubleshooting and common RS485/Modbus pitfalls. Before changing firmware, wiring, device addresses, or the web UI, it is better to first confirm that the verified baseline works.

## 1. Current Version Scope

The current version uses a verified demo configuration. The web page does not currently provide dynamic RS485 device configuration.

Web-based RS485 device configuration can be added as a later-version or customization service when needed.

## 2. Verified Baseline

The current demo baseline has been verified with the following RS485 devices:

| RS485 Port | Device                      | Modbus Address | Verified Function         |
| ---------- | --------------------------- | -------------- | ------------------------- |
| RS485-1    | Single Relay Tester         | 8              | Relay ON/OFF              |
| RS485-2    | 16-Channel Relay Controller | 20             | Relay 1-16 ON/OFF         |
| RS485-3    | Temp & Humidity Sensor 1    | 3              | Temperature/Humidity Read |
| RS485-4    | Temp & Humidity Sensor 2    | 6              | Temperature/Humidity Read |

Boot self-test verified: PASS devices=4/4

Before changing anything, first make sure the verified baseline works.

## 3. Recommendation: Change One Variable at a Time

Debug one layer at a time:

1. Confirm power supply.
2. Confirm RS485 A/B wiring.
3. Confirm the correct RS485 port.
4. Confirm the Modbus address.
5. Confirm the function code.
6. Confirm the register or coil address.
7. Confirm CRC and timeout behavior.
8. Modify firmware only after the basic link is clear.

It is better not to modify firmware, wiring, device address, and web UI at the same time. If several things change together, the real cause becomes harder to find.

## 4. Problem: No Response From RS485 Device

Check these items first:

- Device power supply
- Correct RS485 port
- A/B line reversed
- Wrong Modbus address
- Wrong baud rate
- Wrong function code
- Wrong register or coil address
- Device not sharing reference ground when required
- Cable or connector issue
- Missing or unsuitable termination in longer field wiring
- Device offline or damaged

If the log shows no RX data or a timeout, start with the physical layer and protocol parameters before changing parsing logic.

## 5. Problem: CRC Error

Common causes include:

- Noise
- Bad wiring
- Wrong baud rate
- Wrong frame format
- Wrong device response length expectation
- Half-duplex direction control issue
- Too short read timeout

CRC error means something was received, but the frame is not trustworthy.

## 6. Problem: Relay Control Goes to the Wrong Device

Relay control should include an explicit target. Using only relay id and state is easy to misunderstand in a multi-device setup.

Wrong idea:

```text
{ "id": 1, "state": true }
```

Correct idea:

```json
{
  "target": "single",
  "id": 1,
  "state": true
}
```

```json
{
  "target": "multi",
  "id": 1,
  "state": true
}
```

The backend should resolve target to explicit RS485 port + Modbus address + relay id.

- single target = RS485-1, addr=8, relay id only 1
- multi target = RS485-2, addr=20, relay id 1-16

A safer approach is to avoid automatically selecting the first relay device and avoid blindly hardcoding the address inside relay write logic. The log should show target, channel, address, relay id, state, and result.

Example logs:

```text
[WEB][RELAY] target=single channel=RS485-1 addr=8 relay=1 state=ON success
[WEB][RELAY] target=multi channel=RS485-2 addr=20 relay=16 state=ON success
```

## 7. Problem: Sensor Reads But Values Look Wrong

Check:

- Register start address
- Register count
- Function code 0x03 vs 0x04
- Scaling factor
- Signed or unsigned value
- Byte order or word order
- Unit conversion

In the current verified demo, the temperature and humidity sensors use:

- Function code: 0x04
- Start register: 0x0000
- Register count: 2
- Scaling: raw value / 10.0

Without the device protocol document, it is better to avoid adding new register definitions.

## 8. Problem: Web UI Looks Correct But Device Does Not Move

Check:

- Browser request body
- Whether target is correct
- Whether the backend parses target
- Whether target maps to the correct RS485 port and address
- Whether relay control receives explicit channel and address
- Whether serial logs show TX
- Whether RX echo or response is received
- Whether CRC is OK
- Whether the relay module actually moves

If there is no TX log, the problem is before RS485 transmission. If TX exists but there is no RX, check wiring, address, and the device. If RX and CRC are OK but the device does not move, check relay module wiring or the load side.

## 9. Problem: Works on Bench But Unstable in Field

The current system is a minimal demo system, not a certified industrial field product.

Field deployment should evaluate:

- Shielded twisted pair cable
- Proper grounding
- Termination resistor
- Biasing
- Surge/ESD protection
- Isolation
- Enclosure
- Power supply stability
- Cable routing away from high-power lines

For external communication, describe it as a demo kit, development kit, or integration foundation rather than a fully certified industrial controller.

## 10. Problem: High-Power Load Control

The board provides control signals and low-voltage control interfaces. High-power loads should be controlled through contactors or intermediate relays. The control board is not intended to act as a direct high-power load driver.

Examples:

- Pump
- Fan
- Lighting circuit
- Heater
- Motor
- Compressor

These loads should be controlled through suitable contactors, intermediate relays, and protection devices.

## 11. Boot Self-Test Notes

The boot self-test is used for:

- Buzzer test
- RGB alarm indicator test
- RS485 device online check
- Sensor read test
- Relay status read test

For this demo, boot self-test reads relay status only. It does not automatically turn relay outputs ON.

Example:

```text
[SELFTEST] PASS devices=4/4
```

## 12. Recommended Debugging Order

1. Power on the board.
2. Confirm buzzer/RGB self-test.
3. Confirm PASS devices=4/4.
4. Check current RS485 port mapping.
5. Check Modbus address.
6. Check function code.
7. Check TX log.
8. Check RX log.
9. Check CRC result.
10. Check parsed result.
11. Check web request target.
12. Check relay output or sensor value.

## 13. Keep the Baseline Stable Before Expanding Features

The current version is best treated as a verified demo configuration first.

Before the baseline is stable, the following items are better handled as later-version or customization work:

- Web-based RS485 device configuration
- Dynamic device table
- Rule engine
- Scheduling system
- PLC-like logic
- Voice control
- Cloud dashboard

This keeps the scope clear and gives buyers a working baseline for validation and secondary development.

## 14. Known Verified Log Examples

```text
[SELFTEST] PASS devices=4/4
```

```text
[WEB][RELAY] target=single channel=RS485-1 addr=8 relay=1 state=ON success
```

```text
[WEB][RELAY] target=multi channel=RS485-2 addr=20 relay=16 state=ON success
```

```text
[RS485-3] Parsed: [Temp & Humidity Sensor 1] - Temp: 30.xC, Hum: 40.x%
```

```text
[RS485-4] Parsed: [Temp & Humidity Sensor 2] - Temp: 32.xC, Hum: 30.x%
```

These log examples describe verified behavior in the current demo system. Actual logs may vary slightly by firmware version and device configuration.