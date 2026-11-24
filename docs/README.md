# Documentation

For a first installation, follow [getting started](guides/getting-started.md). For an existing setup or a code
change, choose the relevant guide below.

## Set up and use a board

- [Hardware](guides/hardware.md) - identify your board, connectors, and electrical limits.
- [Flashing](guides/flashing.md) - install a release image or upload a source build.
- [Wireless](guides/wireless.md) - scan/test Wi-Fi, save credentials, and connect over BLE.
- [C6 commissioning](guides/boards/esp32c6-commissioning.md) - set the first BLE Control key through the flashing port.
- [Diagnostic shells](guides/console.md) - read logs and use the diagnostic terminal.

Dashboard installation and use: [Direct](../host/direct/README.md),
[Python](../host/python/README.md), or [.NET](../host/dotnet/ZephyrForge/README.md).

### Wire an SD card

Choose the guide for both your board and the image's storage bus. GPIO numbers are not interchangeable across boards.

| Board | Native four-bit SDMMC | SD-over-SPI |
| --- | --- | --- |
| ESP32-S3 N16R8 | [Wiring](guides/boards/esp32s3-sdmmc.md) | [Wiring](guides/boards/esp32s3-sd-spi.md) |
| ESP32-C6 N8 | Not supported | [Wiring](guides/boards/esp32c6-sd-spi.md) |
| Pico family | [Wiring](guides/boards/pico-sdmmc.md) | [Wiring](guides/boards/pico-sd-spi.md) |

## Build and develop

- [Architecture](reference/architecture.md) - service boundaries, startup, and locking.
- [Firmware builds](development/firmware-build.md) - workspace setup, board/storage selection, and image outputs.
- [Firmware development](development/firmware.md) - where to add application code and how to test it.
- [Host development](development/host-tools.md) - dashboards, codecs, packaging, and software checks.
- [Releases](development/releases.md) - package and publish a release.
- [Contributing](../CONTRIBUTING.md) - prepare a change for review.

## Look up a contract

Use these when implementing firmware or a client. They define the exact fields, behavior, and error handling.

- [USB HID](reference/usb-hid.md) - discovery, report layouts, control commands, and host validation.
- [Bluetooth LE](reference/bluetooth.md) - advertising, GATT, authentication, and BLE control.
- [Boot settings](reference/settings.md) - record format, defaults, load/save lifecycle, API, and flash safety.
- [SD storage](reference/sdcard.md) - driver configuration, mounting, ownership, reporting, and benchmarks.
- [Shared dashboard HTTP API](../host/web/README.md) - Python/.NET routes, JSON fields, and request/error semantics.

## Check what has been tested

- [Qualification gaps](validation/qualification-gaps.md) - what remains unverified.
- [Firmware qualification](validation/firmware-qualification.md) - target-test procedures and acceptance criteria.
- [Validation records](validation/records/README.md) - dated software and hardware observations.

Builds and simulated tests are not hardware qualification. Records apply to the revision, board, and configuration
they describe; they are not installation instructions for the current release.
