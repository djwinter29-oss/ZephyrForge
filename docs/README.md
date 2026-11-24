# Documentation

New to the project? Start with [getting started](guides/getting-started.md). You do not need to read the references
to connect a board or use a dashboard.

## Set up and use a board

| Task | Guide |
| --- | --- |
| Choose a board, firmware image, and dashboard | [Getting started](guides/getting-started.md) |
| Check connectors, memory variants, and electrical limits | [Hardware](guides/hardware.md) |
| Install a release image or upload a source build | [Flashing](guides/flashing.md) |
| Scan/test Wi-Fi, save credentials, or connect over BLE | [Wireless](guides/wireless.md) |
| Set the first BLE Control key on an ESP32-C6 | [C6 commissioning](guides/boards/esp32c6-commissioning.md) |
| Read logs or use the diagnostic terminal | [Console](guides/console.md) |
| Run a dashboard | [Direct](../host/direct/README.md), [Python](../host/python/README.md), [.NET](../host/dotnet/ZephyrForge/README.md) |

### Wire an SD card

Choose the guide for both your board and the image's storage bus. GPIO numbers are not interchangeable across boards.

| Board | Native four-bit SDMMC | SD-over-SPI |
| --- | --- | --- |
| ESP32-S3 N16R8 | [Wiring](guides/boards/esp32s3-sdmmc.md) | [Wiring](guides/boards/esp32s3-sd-spi.md) |
| ESP32-C6 N8 | Not supported | [Wiring](guides/boards/esp32c6-sd-spi.md) |
| Pico family | [Wiring](guides/boards/pico-sdmmc.md) | [Wiring](guides/boards/pico-sd-spi.md) |

## Build and develop

| Task | Guide |
| --- | --- |
| Understand service boundaries, startup, and locking | [Architecture](reference/architecture.md) |
| Create a workspace and choose a board/storage build | [Firmware builds](development/firmware-build.md) |
| Extend firmware and run its tests | [Firmware development](development/firmware.md) |
| Change a host, dashboard, or codec | [Host development](development/host-tools.md) |
| Package and publish a release | [Releases](development/releases.md) |
| Submit a change | [Contributing](../CONTRIBUTING.md) |

## Look up a contract

These are the authoritative references. Guides link here for exact fields and behavior rather than repeating them.

| Reference | Covers |
| --- | --- |
| [USB HID](reference/usb-hid.md) | Discovery, report layouts, control commands, and host validation |
| [Bluetooth LE](reference/bluetooth.md) | Advertising, GATT, authentication, and BLE control |
| [Boot settings](reference/settings.md) | Record format, defaults, load/save lifecycle, API, and flash safety |
| [SD storage](reference/sdcard.md) | Driver configuration, mounting, ownership, reporting, and benchmark behavior |
| [Shared dashboard HTTP API](../host/web/README.md) | Python/.NET routes, JSON fields, and request/error semantics |

## Check what has been tested

- [Qualification gaps](validation/qualification-gaps.md): what remains unverified.
- [Firmware qualification](validation/firmware-qualification.md): authorized target-test procedures and acceptance criteria.
- [Validation records](validation/records/README.md): dated software and hardware observations.

Builds and simulated tests are not hardware qualification. Records apply to the revision, board, and configuration
they describe; they are not installation instructions for the current release.
