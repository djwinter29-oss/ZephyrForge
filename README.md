# ZephyrForge

[![PR Check](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/pr-check.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/pr-check.yml)
[![Release](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)

**Quick links:** [Releases](https://github.com/djwinter29-oss/ZephyrForge/releases) · [Hardware setup](docs/hardware/README.md) · [Direct Dashboard](https://djwinter29-oss.github.io/ZephyrForge/) · [PyPI package](https://pypi.org/project/zephyrforge/)

ZephyrForge is a starting point for MCU applications built on Zephyr. It handles board diagnostics, boot settings,
wireless setup, and optional SD storage. You add your application's peripherals and processing.

The dashboards connect over USB HID or authenticated Bluetooth LE, depending on the board. UART carries console
logs and a diagnostic shell, not dashboard management; Espressif boards also use it for flashing, while Pico boards
flash UF2 over USB.

The primary target is ESP32-S3-DevKitC-1 N16R8; ESP32-C6 and Raspberry Pi Pico variants are also available.
Check [boards and connections](docs/hardware/README.md) for exact models, memory requirements, and supported links.

## Start here

| What you want to do | Start with |
| --- | --- |
| Set up a new board | [Get a board running](docs/getting-started.md) |
| Connect to installed firmware | [Choose a dashboard](docs/guides/dashboards/README.md) |
| Read status, change settings, or recover a command | [Use a dashboard](docs/guides/dashboard-use.md) |
| Build firmware or select optional modules | [Firmware builds](docs/development/firmware-build.md) |
| Find a development guide, API, or test record | [Documentation](docs/README.md) |

## Build your application

Fork the repository and pin the revision you validate. Start with the [architecture](docs/development/architecture.md)
for service boundaries and concurrency, then [firmware development](docs/development/firmware.md) for extension
points and tests. ZephyrForge uses one application core.

ZephyrForge shares [PicoForge](https://github.com/djwinter29-oss/PicoForge)'s purpose, but uses Zephyr rather than the
Pico SDK. It is not a compatible firmware port.

## Safety and validation

Review the [security boundaries](docs/reference/security.md) before connecting networks and the
[SD-card guide](docs/guides/sd-card.md) before using a card with data.
[Validation and qualification](docs/validation/README.md) separates recorded hardware evidence
from software checks and open gaps.

## Contributing

For changes and publication, see [Contributing](CONTRIBUTING.md) and the [release procedure](docs/development/releases.md).

## License

[BSD 3-Clause](LICENSE). Third-party dependencies and radio blobs retain their own licenses.
