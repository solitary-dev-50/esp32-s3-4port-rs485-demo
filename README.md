# 4-Port RS485 Control Board + Web Demo System

[中文说明](README_zh.md)

This project is an ESP32-S3 based 4-port RS485 acquisition and control demo.

It is meant to prove the basic board functions in a clear, repeatable way: RS485 communication, Modbus relay control, temperature and humidity reading, boot self-test, local buzzer test, RGB indicator test, and a small local web console.

It is not intended to be presented as a certified industrial PLC or a finished mass-production controller.

## What Is Included

- ESP32-S3 firmware for the RS485 demo board
- Local web demo console
- Fixed verified Modbus device configuration
- Boot self-test for local indicators and RS485 devices
- Serial diagnostic logs
- Interface and development documentation
- Troubleshooting notes for common RS485/Modbus issues

## Open Source Scope

The firmware, web console, interface documentation, troubleshooting guide, and demo examples are open for learning, integration, and secondary development.

The initial hardware production files, including schematic source files, PCB layout files, Gerber files, BOM, and pick-and-place files, are not included in this repository.

## Verified Demo Setup

| RS485 Port | Device                      | Address | Verified Function         |
| ---------- | --------------------------- | ------- | ------------------------- |
| RS485-1    | Single Relay Tester         | 8       | Relay ON/OFF              |
| RS485-2    | 16-Channel Relay Controller | 20      | Relay 1-16 ON/OFF         |
| RS485-3    | Temp & Humidity Sensor 1    | 3       | Temperature/Humidity Read |
| RS485-4    | Temp & Humidity Sensor 2    | 6       | Temperature/Humidity Read |

A captured boot self-test and polling session is available in [Verification Log](docs/VERIFICATION_LOG.md).

Boot self-test has been verified with:

```text
[SELFTEST] PASS devices=4/4
```

## Main Functions

- Read two Modbus temperature and humidity sensors
- Read relay status
- Control a single relay on RS485-1
- Control a 16-channel relay module on RS485-2
- Show sensor values and relay controls in a local web console
- Run boot self-test before normal polling
- Print serial logs for debugging
- Trigger local buzzer and RGB indicator tests

## Hardware Notes

The board is designed as a development and demo platform for RS485 acquisition and control.

- 4 independent RS485 ports
- RS485 A/B lines include TVS protection for ESD and transient suppression
- Each RS485 channel provides a 120Ω termination jumper position, so termination can be manually enabled or disabled with a jumper cap based on bus position and actual wiring needs
- Each RS485 channel uses an independent transceiver path
- Power input path includes fuse and Schottky diode protection
- Bulk and local decoupling capacitors are used for power stability

These protection designs help with development and field testing, but they should not be treated as certified industrial surge or ESD protection.

## Build

This project uses PlatformIO.

```bash
pio run
```

Upload and serial monitor commands depend on the local development environment and connected board.

## Web Console

The web console files are stored in:

```text
data/
```

The current web demo uses a fixed verified device setup. It does not provide dynamic RS485 device configuration from the web page.

## Project Structure

```text
src/        Firmware source code
data/       Local web demo console
docs/       Interface, development, and troubleshooting documents
```

## Documentation

- [Interface Document](docs/INTERFACE.md)
- [Development Guide](docs/DEVELOPMENT_GUIDE.md)
- [Troubleshooting Guide](docs/TROUBLESHOOTING.md)
- [中文避坑指南](docs/TROUBLESHOOTING_zh.md)
- [Verification Log](docs/VERIFICATION_LOG.md)

## Safety Notes

High-power loads should be controlled through contactors or intermediate relays. The board provides control signals and low-voltage control interfaces; it should not be used as a direct high-power load driver.

Boot self-test reads relay status only. It should not automatically turn relay outputs on.

## What This Project Is Not

- Not a certified industrial PLC
- Not a finished mass-production industrial controller
- Not a direct high-power load driver
- Not locked to agriculture
- Not only a relay board

## Current Scope

The current version is a fixed verified demo. The priority is to keep the baseline stable, easy to understand, and easy to verify before adding more features.

Possible future work or customization can include new Modbus devices, web dashboard changes, logging/history integration, alarm rules, or external equipment protocol adaptation.

## License

This project is released under the [MIT License](LICENSE).
