# Getting started

This guide takes you from choosing an image to reading live board status. You can install a release without a
Zephyr workspace, or build from source if you want to change the firmware.

## 1. Choose your board and image

Check the module marking and connectors in the [hardware guide](hardware.md).

| Board | Management connection | Storage choices |
| --- | --- | --- |
| ESP32-S3-DevKitC-1 N16R8 | Native **USB** connector; BLE after key setup | SDMMC, SPI, or none |
| ESP32-C6-DevKitC-1 N8 | BLE after [physical key commissioning](boards/esp32c6-commissioning.md) | SPI or none |
| Pico, Pico W, Pico 2, Pico 2 W | Micro-USB | SDMMC, SPI, or none |

You do not need an SD card to use management. Choose `none` to leave out storage support altogether. For an
SD-enabled image, use the matching [wiring guide](../README.md#wire-an-sd-card); never change storage buses without
disconnecting card power.

## 2. Install firmware

- **Use a release:** download the archive for your exact board and storage choice, verify its checksum, and follow
  [flashing instructions](flashing.md).
- **Build from source:** follow [firmware builds](../development/firmware-build.md) to prepare the workspace and
  compile an image, then [upload that build](flashing.md#from-a-source-build).

Flashing replaces existing firmware. A successful upload does not prove that the application booted or that
USB, radios, or storage work; check the running board next.

## 3. Open a dashboard

Use one client per board. From the repository root:

| Dashboard | Launch | Open |
| --- | --- | --- |
| Direct | `bash tools/host/run-direct.sh --port 8000` | `http://127.0.0.1:8000/` |
| Python | `bash tools/host/run-python.sh web` | `http://127.0.0.1:8001/` |
| .NET | `bash tools/host/run-dotnet.sh` | `http://127.0.0.1:8002/` |

Direct also runs as a [hosted page](https://djwinter29-oss.github.io/ZephyrForge/). It needs desktop Chrome/Edge
with WebHID or Web Bluetooth, served over HTTPS or localhost. The local static server uses Python but does not
bridge device traffic.

Python requires Python 3.10+; .NET checkout launches require the .NET 10 SDK. Both connect through USB HID only.
For installation without a checkout, see the [Python](../../host/python/README.md) or
[.NET](../../host/dotnet/ZephyrForge/README.md) guide.

On Linux, run `bash tools/host/hid-permissions-setup.sh` once, then reconnect the board. Do not run a dashboard as
root. Python and .NET select the first board by default; use `--serial SERIAL` or `--device-path HID_PATH` to select
another. Without hardware, the board list is empty.

## 4. Check the connection

On ESP32-S3, connect the **USB** connector, not just the **UART** connector. Pico boards use the same micro-USB
cable for flashing and management. In Direct, choose **USB** and select the board; in Python or .NET, select it
from the board list. A C6 instead needs commissioning before Direct's **Bluetooth** option can connect.

Confirm the product name, board model, firmware version, and serial number. Wait for fresh telemetry before
using controls. An SD-enabled image can report a missing card or mount failure without preventing management.

For Wi-Fi tests and BLE key setup, continue with [wireless setup](wireless.md). Pico W credentials are temporary;
those boards cannot save settings. For boot failures or connection problems, read the [UART log](console.md#uart).

If a command times out or the connection drops, check the board and use **Re-sync**. Do not resend a command whose
outcome is unknown. See [qualification gaps](../validation/qualification-gaps.md) for the limits of current testing.
