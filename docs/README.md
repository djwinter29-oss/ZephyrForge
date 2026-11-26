# Documentation

## Start here

- **New board:** [Get a board running](getting-started.md) takes you from image selection to live telemetry.
- **Firmware already installed:** [Choose a dashboard](guides/dashboards/README.md), then
  [connect and use it](guides/dashboard-use.md).
- **Building an application:** [Build firmware](development/firmware-build.md), then use
  [firmware development](development/firmware.md) for extension points and tests.

## Use a board

| Task | Read |
| --- | --- |
| Identify a board, connector, or reserved pin | [Boards and connections](hardware/README.md) |
| Install or replace firmware | [Flash firmware](guides/flashing.md) |
| Set the first Control key on a C6 | [Commission an ESP32-C6](guides/esp32c6-commissioning.md) |
| Read status, save settings, or recover a command | [Use a dashboard](guides/dashboard-use.md) |
| Prepare a card, export it over USB, or run a benchmark | [Use an SD card](guides/sd-card.md) |
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
| [SD storage](reference/sd-storage.md) | Driver configuration, mount lifecycle, ownership, and benchmark execution |
| [Security boundaries](reference/security.md) | Management links, local hosts, and stored credentials |

## Check what has been tested

- [Validation and qualification](validation/README.md) - board coverage, open gaps, and dated records.
- [Firmware qualification procedure](validation/firmware-qualification.md) - how to run and record target checks.
