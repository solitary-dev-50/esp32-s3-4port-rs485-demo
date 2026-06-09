# Hardware Protection Notes

This document describes the on-board protection and stability-related hardware design used in the 4-Port RS485 Control Board + Web Demo System.

It is a hardware design note, not a certification report.

## RS485 Line Protection

Each RS485 A/B differential line includes TVS protection design for ESD and transient suppression.

This helps suppress common wiring, plugging, and field-testing transient events on RS485 lines. It should not be described as certified surge or lightning protection.

## RS485 Termination

Each RS485 channel provides a jumper-selectable 120Ω termination resistor.

Termination can be enabled or disabled manually with a jumper cap depending on bus position, wiring length, and actual RS485 topology.

## RS485 Transceiver Circuitry

Each RS485 channel uses independent RS485 transceiver circuitry.

This keeps port mapping clear and helps isolate wiring, device, or communication issues during development and testing.

## Power-Input Protection

The board includes a protected power-input path with fuse and Schottky diode design.

This design helps reduce risk from input-side faults such as overcurrent, short-circuit events, or reverse-current paths during development and testing.

## Power Stability

Bulk and local decoupling capacitors are used for power stability.

These capacitors help support stable operation during WiFi activity, RS485 switching, and multi-device communication.

## Safety Limitations

This board is a development and demo platform. It is not an industrial surge-certified or lightning-certified product.

The board provides control signals and low-voltage control interfaces. It is not intended for direct mains or high-voltage load control. High-power loads should be controlled through suitable contactors, intermediate relays, and protection devices.
