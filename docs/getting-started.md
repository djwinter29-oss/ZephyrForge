# Get a board running

[Documentation](README.md) / Getting started

Install firmware and verify live telemetry with Direct in desktop Chrome or Edge.

## 1. Choose your board and image

Check the exact model and connector labels in [Boards and connections](hardware/README.md).
Choose a storage variant that matches your wiring, or `none` to omit SD support. An SD-enabled image can also boot
with no card.

## 2. Install firmware

- **ESP32-S3 or Pico family:** use a [matching release image](guides/flashing.md#flash-a-release-image) if one is
  published. Otherwise, make a [source build](development/firmware-build.md) and follow the
  [source upload steps](guides/flashing.md#from-a-source-build).
- **Factory-fresh ESP32-C6:** follow [first-key commissioning](guides/esp32c6-commissioning.md), which starts
  with building and flashing the image. Keep the matching initialized build directory for the key utility.
  If the board already has a Control key, use a matching release image if available, or
  [build and upload from source](guides/flashing.md#from-a-source-build).

## 3. Connect with Direct

Open [Direct](guides/dashboards/direct.md) in desktop Chrome or Edge. Use the hosted page for a matching release;
for a source build or an image variant the hosted page does not support, run Direct from the matching checkout.
Close other management clients and choose the connection for your board:

- **ESP32-S3 or Pico family:** connect the ESP32-S3 port labeled **USB** or the Pico's micro-USB port.
  Choose **USB**, then **Select board**, and grant browser access.
- **ESP32-C6:** choose **Bluetooth**, enter the saved Control key, then select `ZephyrForge-<serial>`.

For local, offline, or USB-bridge options, [choose a dashboard](guides/dashboards/README.md).

## 4. Verify live telemetry

Confirm the board model, serial number, and firmware version. Wait for fresh telemetry, such as a changing uptime.
An SD-enabled image can report a missing card or mount failure while board management is working.

If the browser cannot open USB on Linux, check
[HID permissions](guides/dashboard-use.md#linux-usb-permissions). For startup problems, check the
[UART log](guides/console.md#uart). Once connected, see [Wireless setup](guides/wireless.md) or
[Use an SD card](guides/sd-card.md).
