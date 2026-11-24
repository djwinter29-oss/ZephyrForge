# ZephyrForge

[![PR Check](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/pr-check.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/pr-check.yml)
[![Release](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/ZephyrForge/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)

ZephyrForge is a Zephyr starting point for MCU applications. It provides board diagnostics, boot settings, wireless
setup, optional SD storage, and dashboards, leaving product-specific peripherals and processing to your application.

The first target is the ESP32-S3-DevKitC-1 N16R8. Pico-family and ESP32-C6 images are also available, with different
management and storage capabilities:

| Board | Management | SD options | Main limits |
| --- | --- | --- | --- |
| ESP32-S3-DevKitC-1 N16R8 | USB HID; authenticated BLE | Four-bit SDMMC, SPI, or none | Requires 16 MB flash and 8 MB octal PSRAM |
| ESP32-C6-DevKitC-1 N8 | Authenticated BLE | SPI or none | Requires physical Control-key commissioning; no USB HID/MSC |
| Pico, Pico 2 | USB HID | Four-bit SDMMC, SPI, or none | No settings save, onboard LED control, or wireless |
| Pico W, Pico 2 W | USB HID | Four-bit SDMMC, SPI, or none | Temporary Wi-Fi joins; no settings save, LED control, or BLE |

**Status:** ESP32-S3 has limited functional hardware evidence. Pico-family and ESP32-C6 support is software-build
only. See [qualification gaps](docs/validation/qualification-gaps.md) before relying on a board or storage setup.

## Get a board running

1. Check the [hardware guide](docs/guides/hardware.md) for your exact board and SD wiring.
2. Download a matching [firmware release](https://github.com/djwinter29-oss/ZephyrForge/releases/latest) and follow
   [flashing instructions](docs/guides/flashing.md), or [build from source](docs/development/firmware-build.md).
3. Open a dashboard and follow [getting started](docs/guides/getting-started.md).

An SD card is not required for basic management. A `none` image leaves out SD drivers, the filesystem and USB mass
storage entirely; storage-enabled images need the external socket and wiring shown in the hardware guide.

### Choose a dashboard

| Dashboard | Connection | Run it |
| --- | --- | --- |
| [Direct](host/direct/README.md) | WebHID or Web Bluetooth in desktop Chrome/Edge | [Open the hosted dashboard](https://djwinter29-oss.github.io/ZephyrForge/) |
| [Python](host/python/README.md) | USB HID through a local Python host | `pip install zephyrforge`, then `zephyrforge web` |
| [.NET](host/dotnet/ZephyrForge/README.md) | USB HID through a local .NET host | Download a host archive from Releases |

From a checkout, run one of these from the repository root:

```sh
bash tools/host/run-direct.sh --port 8000  # http://127.0.0.1:8000/
bash tools/host/run-python.sh web         # http://127.0.0.1:8001/
bash tools/host/run-dotnet.sh             # http://127.0.0.1:8002/
```

On Linux, run `bash tools/host/hid-permissions-setup.sh` once and reconnect the board. Use one management client per
board. Direct talks to the device in the browser; Python and .NET bridge USB to their shared web dashboard.
Without hardware, the board list is empty.

## Build your application

Fork the repository and pin the source revision and dependencies you validate. Keep product work in separate
services, pin assignments in board overlays, and blocking work out of the management loop and application lock.
ZephyrForge uses one application core.

- [Architecture](docs/reference/architecture.md): service responsibilities, startup, and concurrency.
- [Firmware builds](docs/development/firmware-build.md): workspace setup, board/storage selection, and outputs.
- [Firmware development](docs/development/firmware.md): extension points and tests.
- [Host development](docs/development/host-tools.md): dashboards, codecs, and software checks.

Protocol changes must be coordinated across firmware and affected hosts. The [USB HID](docs/reference/usb-hid.md)
and [Bluetooth LE](docs/reference/bluetooth.md) specifications define the contracts.

ZephyrForge shares [PicoForge](https://github.com/djwinter29-oss/PicoForge)'s purpose, but uses Zephyr rather than the
Pico SDK. It is not a compatible firmware port.

## Before using real data

- SD volumes are mounted without automatic formatting. USB drive access requires an explicit handoff; safely eject
  before returning the card to firmware. Back up data before using storage or running its optional write benchmark.
- BLE Control keys authenticate commands but do not encrypt the radio link. Saved credentials are unencrypted in flash.
- Settings use a single flash record, not a power-loss-atomic store.
- The ESP32-S3 TCP diagnostic shell is unauthenticated plaintext. Use a trusted network and never port-forward it.
- Keep local bridge hosts on loopback. After an uncertain command, check the board and reconcile; do not replay it.

See [storage safety](docs/reference/sdcard.md#safety-and-ownership), [wireless setup](docs/guides/wireless.md), and the
[security policy](SECURITY.md) for details.

## Documentation and contributing

Use the [documentation index](docs/README.md) to find an operating procedure, API contract, or validation record.
For changes and publication, see [Contributing](CONTRIBUTING.md) and [Releases](docs/development/releases.md).

## License

[BSD 3-Clause](LICENSE). Third-party dependencies and radio blobs retain their own licenses.
