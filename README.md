# ZephyrForge

[![PR Check](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/pr-check.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/pr-check.yml)
[![Release](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)

**Quick links:** [Releases](https://github.com/djwinter29-oss/ZephyrForge/releases) · [Hardware setup](docs/hardware/README.md) · [Direct Dashboard](https://djwinter29-oss.github.io/ZephyrForge/) · [PyPI package](https://pypi.org/project/zephyrforge/)

ZephyrForge is a starting point for MCU applications built on Zephyr. It handles board diagnostics, boot settings,
wireless setup, and optional SD storage. You add your application's peripherals and processing.

The dashboards connect over USB HID or authenticated Bluetooth LE, depending on the board. UART is used for logs
and a diagnostic shell, not dashboard management.

## Get a board running

Start with [Get a board running](docs/getting-started.md): choose an image, install it, and connect a dashboard.
The primary target is ESP32-S3-DevKitC-1 N16R8; ESP32-C6 and Raspberry Pi Pico variants are also available.
Check [boards and connections](docs/hardware/README.md) for exact models, memory requirements, and supported links.
SD storage is optional.

Already running firmware? [Choose a dashboard](docs/guides/dashboards/README.md) or use the
[documentation index](docs/README.md) to find a task or contract.

## Build your application

Fork the repository and pin the revision you validate. ZephyrForge uses one application core; keep blocking product
work out of its management loop and application lock.

- [Architecture](docs/development/architecture.md): service responsibilities, startup, and concurrency.
- [Firmware builds](docs/development/firmware-build.md): workspace setup, board/storage selection, and outputs.
- [Firmware development](docs/development/firmware.md): extension points and tests.
- [Host development](docs/development/host-tools.md): dashboards, codecs, and software checks.

ZephyrForge shares [PicoForge](https://github.com/djwinter29-oss/PicoForge)'s purpose, but uses Zephyr rather than the
Pico SDK. It is not a compatible firmware port.

## Safety and validation

Review the [security boundaries](docs/reference/security.md) and
[SD ownership and safety](docs/reference/sd-storage.md#safety-and-ownership) before connecting networks or using a
card with data. [Validation and qualification](docs/validation/README.md) separates recorded hardware evidence
from software checks and open gaps.

## Contributing

For changes and publication, see [Contributing](CONTRIBUTING.md) and the [release procedure](docs/development/releases.md).

## License

[BSD 3-Clause](LICENSE). Third-party dependencies and radio blobs retain their own licenses.
