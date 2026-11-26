# Documentation

**New to ZephyrForge?** Follow [Get a board running](getting-started.md) from image selection to live telemetry.

## Use a board

| Task | Read |
| --- | --- |
| Identify a board, connector, or reserved pin | [Boards and connections](hardware/README.md) |
| Wire and prepare an SD card | [SD-card wiring](hardware/sd-card-wiring.md) |
| Install or replace firmware | [Flashing firmware](guides/flashing.md) |
| Set the first Control key on a C6 | [C6 commissioning](hardware/esp32c6-commissioning.md) |
| Choose, install, or use a dashboard | [Dashboards](guides/dashboards/README.md) |
| Export or return an SD card | [USB drive handoff](reference/sd-storage.md#usb-drive-handoff) |
| Run a storage benchmark | [Storage benchmark](reference/sd-storage.md#optional-storage-benchmark) |
| Set up Wi-Fi or Bluetooth | [Wireless setup](guides/wireless.md) |
| Read logs or open a diagnostic terminal | [Diagnostic shells](guides/console.md) |

## Build and develop

| Task | Read |
| --- | --- |
| Understand services, startup, and locking | [Firmware architecture](development/architecture.md) |
| Build an image or enable/disable modules | [Firmware builds](development/firmware-build.md) |
| Add firmware behavior and run its tests | [Firmware development](development/firmware.md) |
| Change a host or dashboard and run its checks | [Host development](development/host-tools.md) |
| Package and publish a release | [Release procedure](development/releases.md) |
| Submit a change | [Contributing](../CONTRIBUTING.md) |

## Look up a contract

These pages own exact fields, APIs, and error semantics; operating instructions stay in the guides above.

| Contract | Contents |
| --- | --- |
| [USB HID](reference/usb-hid.md) | Device discovery, report layouts, commands, and host validation |
| [Bluetooth LE](reference/bluetooth-le.md) | Advertising, GATT, authentication, and BLE control; reuses USB report layouts |
| [Dashboard HTTP API](reference/http-api.md) | Python/.NET routes, JSON fields, responses, and errors |
| [Boot settings](reference/boot-settings.md) | Defaults, record format, load/save API, and flash safety |
| [SD storage](reference/sd-storage.md) | Drivers, mounting, exclusive ownership, and benchmarks |
| [Security boundaries](reference/security.md) | Management links, local hosts, and stored credentials |

## Check what has been tested

- [Validation and qualification](validation/README.md) - board coverage, open gaps, and dated records.
- [Firmware qualification procedure](validation/firmware-qualification.md) - how to run and record target checks.
