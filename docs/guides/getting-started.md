# Getting started

Follow these four steps to install firmware and read live board status. ESP32-S3 and Pico release images need no
Zephyr workspace. A fresh ESP32-C6 also needs a matching source build for first-key commissioning.

## 1. Choose your board and image

Check the module marking and connectors in the [hardware guide](hardware.md).

| Board | Management connection | Storage choices |
| --- | --- | --- |
| ESP32-S3-DevKitC-1 N16R8 | Native **USB** connector; BLE after key setup | SDMMC, SPI, or none |
| ESP32-C6-DevKitC-1 N8 | BLE after [physical key commissioning](boards/esp32c6-commissioning.md) | SPI or none |
| Pico, Pico W, Pico 2, Pico 2 W | Micro-USB | SDMMC, SPI, or none |

You do not need an SD card to use management. Choose `none` to leave out storage support altogether. For an
SD-enabled image, use the matching [wiring guide](../README.md#wire-an-sd-card). Before changing storage buses,
follow the [card power-cycle instruction](../reference/sdcard.md#safety-and-ownership).

## 2. Install firmware

- **Use a release:** download the archive for your exact board and storage choice, verify its checksum, and follow
  [flashing instructions](flashing.md).
- **Build from source:** follow [firmware builds](../development/firmware-build.md) to prepare the workspace and
  compile an image, then [upload that build](flashing.md#from-a-source-build).

For a fresh **ESP32-C6**, complete [first-key commissioning](boards/esp32c6-commissioning.md) before connecting
over BLE. The utility needs a matching initialized build directory; a release binary alone is not enough because
the C6 has no USB HID management interface.

Flashing replaces existing firmware. A successful upload does not prove that the application booted or that
USB, radios, or storage work; check the running board next.

## 3. Open a dashboard

Choose one:

- **Direct:** [open the hosted dashboard](https://djwinter29-oss.github.io/ZephyrForge/) in desktop Chrome or Edge.
  It connects through WebHID or Web Bluetooth without a host application. For local or offline use, follow the
  [Direct guide](../../host/direct/README.md#choose-how-to-run-it).
- **Python:** [install the host](../../host/python/README.md#install) with Python 3.10 or newer.
- **.NET:** [run a packaged host](../../host/dotnet/ZephyrForge/README.md#run-the-host); it includes the runtime.

Python and .NET connect over USB HID only, so they cannot manage C6. Direct needs a browser/OS that supports the
chosen device API, with the page served over HTTPS or localhost.

### From a source checkout

These commands run from the repository root. Local Direct uses Python's standard library; Python host launches
need Python 3.10+; .NET source launches need the .NET 10 SDK.

| Dashboard | Launch | Open |
| --- | --- | --- |
| Direct | `bash tools/host/run-direct.sh --port 8000` | `http://127.0.0.1:8000/` |
| Python | `bash tools/host/run-python.sh web` | `http://127.0.0.1:8001/` |
| .NET | `bash tools/host/run-dotnet.sh` | `http://127.0.0.1:8002/` |

### Linux USB permissions

Run the [HID permission helper](../../tools/host/hid-permissions-setup.sh) once, then reconnect the board.
From a checkout, use `bash tools/host/hid-permissions-setup.sh`. Without a checkout, download it as
`hid-permissions-setup.sh`, make it executable, and run it. It may prompt for sudo to install the device rule;
do not run the dashboard itself as root.

## 4. Check the connection

Use one management client per board. On ESP32-S3, connect the **USB** connector, not just the **UART** connector.
Pico boards use the same micro-USB cable for flashing and management. In Direct, choose **USB** and select the board;
in Python or .NET, select it from the board list. For a commissioned C6, choose **Bluetooth** in Direct and enter
the saved Control key.

Python and .NET initially select the first discovered board. Use the board selector, or launch with
`--serial SERIAL` or `--device-path HID_PATH`, to choose another. Without hardware, the board list is empty.

Confirm the product name, board model, firmware version, and serial number. Wait for fresh telemetry before
using controls. An SD-enabled image can report a missing card or mount failure without preventing management.

For Wi-Fi tests and BLE key setup, continue with [wireless setup](wireless.md). Pico W credentials are temporary;
those boards cannot save settings. For boot failures or connection problems, read the [UART log](console.md#uart).

If a command times out or the connection drops, check the board and use **Re-sync**. Do not resend a command whose
outcome is unknown. See [qualification gaps](../validation/qualification-gaps.md) for the limits of current testing.
