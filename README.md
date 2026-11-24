# ZephyrForge

[![PR Check](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/pr-check.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/pr-check.yml)
[![Release](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)

ZephyrForge is a starting point for MCU applications built on Zephyr. It handles board diagnostics, boot settings,
wireless setup, and optional SD storage. You add your application's peripherals and processing.

The dashboards connect over USB HID or authenticated Bluetooth LE, depending on the board. UART is used for logs
and a diagnostic shell, not dashboard management.

## Boards

| Board | Management | SD options | Main limits |
| --- | --- | --- | --- |
| ESP32-S3-DevKitC-1 N16R8 | USB HID; authenticated BLE | Four-bit SDMMC, SPI, or none | Requires 16 MB flash and 8 MB octal PSRAM |
| ESP32-C6-DevKitC-1 N8 | Authenticated BLE | SPI or none | Requires [physical Control-key commissioning](docs/guides/boards/esp32c6-commissioning.md); no USB HID/MSC |
| Pico, Pico 2 | USB HID | Four-bit SDMMC, SPI, or none | No settings save, onboard LED control, or wireless |
| Pico W, Pico 2 W | USB HID | Four-bit SDMMC, SPI, or none | Temporary Wi-Fi joins; no settings save, LED control, or BLE |

ESP32-S3 N16R8 is the primary target and has limited hardware test results. Pico-family and ESP32-C6 images have
software checks but no hardware validation. See [qualification gaps](docs/validation/qualification-gaps.md) for
what has and has not been tested.

An SD card is optional. A `none` image leaves out SD drivers, the filesystem, and USB mass storage entirely.
Storage-enabled images require an external socket and the board-specific [SD wiring](docs/README.md#wire-an-sd-card).

## Get a board running

Start with [getting started](docs/guides/getting-started.md): choose an image, install it, and connect a dashboard.
For an existing setup, go straight to [flashing](docs/guides/flashing.md) or [wireless setup](docs/guides/wireless.md).

### Choose a dashboard

| Dashboard | Connection | Start here |
| --- | --- | --- |
| [Direct](host/direct/README.md) | WebHID or Web Bluetooth in desktop Chrome/Edge | [Open the hosted dashboard](https://djwinter29-oss.github.io/ZephyrForge/) |
| [Python](host/python/README.md) | USB HID through a local Python host | [Install the package](host/python/README.md#install) |
| [.NET](host/dotnet/ZephyrForge/README.md) | USB HID through a local .NET host | [Run a packaged host or source build](host/dotnet/ZephyrForge/README.md#run-the-host) |

Direct talks to the board from the browser. Python and .NET serve the same web dashboard through a local USB bridge.
Use one management client per board.

## Build your application

Fork the repository and pin the revision you validate. ZephyrForge uses one application core; keep blocking product
work out of its management loop and application lock.

- [Architecture](docs/reference/architecture.md): service responsibilities, startup, and concurrency.
- [Firmware builds](docs/development/firmware-build.md): workspace setup, board/storage selection, and outputs.
- [Firmware development](docs/development/firmware.md): extension points and tests.
- [Host development](docs/development/host-tools.md): dashboards, codecs, and software checks.

ZephyrForge shares [PicoForge](https://github.com/djwinter29-oss/PicoForge)'s purpose, but uses Zephyr rather than the
Pico SDK. It is not a compatible firmware port.

## Before using real data

- SD volumes are mounted without automatic formatting. USB drive access requires an explicit handoff; safely eject
  before returning the card to firmware. Back up data before using storage or running its optional write benchmark.
- BLE Control keys authenticate commands but do not encrypt the radio link. Saved credentials are unencrypted in flash.
- Settings use a single flash record. Losing power during a save can destroy the previous settings.
- The ESP32-S3 TCP diagnostic shell is unauthenticated plaintext. Use a trusted network and never port-forward it.
- Keep local bridge hosts on loopback. After an uncertain command, check the board and reconcile; do not replay it.

See [storage safety](docs/reference/sdcard.md#safety-and-ownership), [wireless setup](docs/guides/wireless.md), and the
[security policy](SECURITY.md) for details.

## Documentation and contributing

Use the [documentation index](docs/README.md) to find an operating procedure, API contract, or validation record.
For changes and publication, see [Contributing](CONTRIBUTING.md) and [Releases](docs/development/releases.md).

## License

[BSD 3-Clause](LICENSE). Third-party dependencies and radio blobs retain their own licenses.
