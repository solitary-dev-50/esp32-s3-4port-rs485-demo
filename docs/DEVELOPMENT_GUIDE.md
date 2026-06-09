# Development Guide

## 1. Development Scope

This firmware is a minimal demo system for a 4-port RS485 acquisition and relay-control board.

It is not a full PLC. It is not a certified industrial product.

The current goal is to keep the demo stable, readable, and easy to verify:

- RS485 device polling
- Sensor data reading
- Relay status reading
- Manual relay control from the web console
- Boot self-test
- Local buzzer and RGB alarm indicator checks

Keep changes small. Avoid adding a second device table, a second relay-control path, or a large management system.

## 2. How to Add a New Sensor Device

Use this checklist before changing firmware:

1. Confirm the sensor protocol.
2. Confirm the Modbus address.
3. Confirm the function code.
4. Confirm the register start.
5. Confirm the register count.
6. Add the device to `g_devices[]`.
7. Add data parsing logic for the new sensor response.
8. Expose the parsed data in `/api/system/status`.
9. Add a display card in `data/index.html`.
10. Run `pio run`.
11. Test with serial logs and real hardware.

Rules:

- Do not reuse an existing Modbus address on the same bus.
- Do not change existing device channels unless the hardware wiring changed.
- Do not create a second device table.
- Do not hardcode fake sensor values in the web UI.
- Keep parsing logic matched to the verified sensor protocol.

## 3. How to Add a New Relay Device

Use this checklist before adding another relay device:

1. Confirm the relay Modbus address.
2. Confirm the RS485 port.
3. Confirm the coil count.
4. Add a `DEVICE_RELAY` entry to `g_devices[]`.
5. Use 0x01 Read Coils for status reading.
6. Use 0x05 Write Single Coil for output control.
7. The backend control interface must explicitly identify target, channel, and address.
8. The frontend request must include `target`; do not send only `id` and `state`.
9. Test ON and OFF with real hardware.

Rules:

- Do not hardcode the relay address inside low-level control functions.
- Do not guess the channel from "the first relay device".
- `controlRelayOnce` must receive channel and address explicitly.
- Do not let one relay device overwrite another relay device's status display.

## 4. How to Add a New Web Control Card

Recommended steps:

1. Add the card markup in `data/index.html`.
2. Add buttons or toggles for the expected actions.
3. Use `POST /api/control/relays` for relay control.
4. Make `target` unique and explicit.
5. Keep the request body small and clear.
6. After success, refresh the displayed state.
7. Test the button against real relay hardware.

Relay control request pattern:

```json
{
  "target": "multi",
  "id": 1,
  "state": true
}
```

Do not reuse an existing `target` unless it controls the same physical device.

## 5. How to Add a New Sensor Display Card

Recommended steps:

1. Read data from `/api/system/status`.
2. Display online/offline status.
3. Display value and unit.
4. Keep labels short and demo-friendly.
5. Avoid fake data in the UI.
6. Test with both normal data and offline device behavior.

The web page should show what the firmware actually reports. If the backend does not expose a field yet, add the backend field first and verify it with serial logs.

## 6. Debugging Checklist

Use this list when a device does not respond or the web state looks wrong:

- Is the device powered?
- Are RS485 A/B lines reversed?
- Is the device connected to the expected RS485 port?
- Is the Modbus address correct?
- Is the function code correct?
- Is the start register or coil correct?
- Is the register count or coil count correct?
- Is CRC OK?
- Is there a timeout?
- Is the device offline?
- Is the web request `target` wrong?
- Did the backend send the command to the wrong channel or address?
- Is a single relay device writing into the multi-relay state display?
- Is the browser showing a cached old page?

## 7. Safety Notes

The onboard relay outputs are intended for control signals.

For high-power loads, use an AC contactor or an intermediate relay that matches the load requirements.

Safety rules:

- Do not let the control board directly carry loads beyond its rating.
- Boot self-test must not automatically turn relay outputs on.
- High-risk actions should require a second confirmation in future product versions.
- Keep manual test actions clearly separated from boot self-test actions.
- Verify wiring before switching a load.

## 8. Build and Upload

Build firmware:

```bash
pio run
```

Firmware upload uses the current project configuration.

If `data/index.html` changes, upload the filesystem image as required by the current PlatformIO setup. A firmware build alone does not update the web page stored in the device filesystem.

After changing web files:

- Upload the filesystem image.
- Hard refresh the browser.
- If needed, clear browser cache to avoid loading an old page.

