# Getting started

Install firmware and verify live telemetry with Direct in desktop Chrome or Edge.

## 1. Choose your board and image

Check the exact model and connector labels in [Hardware Setup](hardware.md). If you are setting up a board
without an SD card, choose the `none` image. For an SD-enabled image, use the
[matching wiring guide](hardware.md#sd-card-wiring).

## 2. Install firmware

- **ESP32-S3 or Pico family:** [flash a release image](flashing.md#flash-a-release-image) for your exact board and
  storage choice. If you already have a [source build](../development/firmware-build.md), follow the
  [source upload steps](flashing.md#from-a-source-build).
- **Factory-fresh ESP32-C6:** follow [first-key commissioning](boards/esp32c6-commissioning.md), which starts
  with building and flashing the image. Keep the matching initialized build directory for the key utility.
  If the board already has a Control key, you can [flash a release image](flashing.md#esp32-c6-release-image).

## 3. Connect with Direct

Open [the hosted dashboard](https://djwinter29-oss.github.io/ZephyrForge/) in desktop Chrome or Edge.
Close other management clients and choose the connection for your board:

- **ESP32-S3 or Pico family:** connect the ESP32-S3 port labeled **USB** or the Pico's micro-USB port.
  Choose **USB**, then **Select board**, and grant browser access.
- **ESP32-C6:** choose **Bluetooth**, enter the saved Control key, then select `ZephyrForge-<serial>`.

For local or offline use, see [Direct launch options](../../host/direct/README.md#choose-how-to-run-it).
For a local USB bridge on ESP32-S3 or Pico, see the [Python](../../host/python/README.md#install) or
[.NET](../../host/dotnet/ZephyrForge/README.md#run-the-host) host guide.

## 4. Verify live telemetry

Confirm the board model, serial number, and firmware version. Wait for fresh telemetry, such as a changing uptime.
An SD-enabled image can report a missing card or mount failure while board management is working.

If the browser cannot open a USB connection on Linux, check [HID permissions](../../host/direct/README.md#linux-usb-permissions).
For startup problems, check the [UART log](console.md#uart). Once connected, see [Wireless Setup](wireless.md)
or the [storage and USB handoff reference](../reference/sdcard.md#usb-drive-handoff).
