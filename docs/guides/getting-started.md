# Getting started

For downloaded firmware, use [release-image flashing](#flash-a-release-image); no Zephyr workspace is needed.
The numbered steps below cover source builds. You can build and preview all three dashboards before
connecting hardware.

## Flash a release image

Download and extract the ZIP for your exact board and storage variant from the GitHub release.
Check `BUILD-INFO.txt` for the target, storage variant, version and source commit, and verify the ZIP
against the release's `SHA256SUMS.txt`. Firmware ZIPs contain no instruction file or documentation tree:
use the repository guides linked from that release, at its tag rather than the current default branch.
Save those pages before working offline.

Choose the matching wiring guide before connecting an SD card:

| Board family | Native four-bit SDMMC | SD-over-SPI |
| --- | --- | --- |
| ESP32-S3-DevKitC-1 N16R8 | [SDMMC wiring](boards/esp32s3-sdmmc.md) | [SPI wiring](boards/esp32s3-sd-spi.md) |
| Pico, Pico W, Pico 2, Pico 2 W | [SDMMC wiring](boards/pico-sdmmc.md) | [SPI wiring](boards/pico-sd-spi.md) |

Release workflows do not flash or qualify hardware. Read the
[qualification limits](../validation/qualification-gaps.md); successful upload does not prove application
boot, USB, radio or SD operation. Disconnect card power before switching storage variants.

### ESP32-S3 release image

Use only ESP32-S3-DevKitC-1 **N16R8**, with 16 MB flash and 8 MB octal PSRAM, PROCPU.
Check the module marking: do not use on N4/N8 or octal-flash N32R16V/N16R16VA modules.
Close serial clients and connect the **UART** USB-to-UART connector for flashing.
Create/activate a Python virtual environment and install esptool 5:

```sh
python -m pip install "esptool>=5,<6"
```

From the extracted ZIP directory, replace `PORT` with the actual serial port:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 zephyr.bin
```

This replaces image sectors without requesting a full-chip erase. Do not change the image offset or flash
parameters. If reset fails, hold BOOT, press/release RESET, release BOOT and retry.
After flashing, the **USB** connector carries HID management; the UART connector carries console logs
at 115200 baud, 8N1. The release ZIP is not a relocatable west build directory.

### Raspberry Pi Pico release image

Use only the exact Pico, Pico W, Pico 2 or Pico 2 W board named in `BUILD-INFO.txt`.
RP2040 and RP2350 images are not interchangeable.
Hold **BOOTSEL** while connecting the micro-USB cable. Copy the extracted `zephyr.uf2` to the drive
that appears: `RPI-RP2` for Pico/Pico W, `RP2350` for Pico 2/Pico 2 W.
Copying replaces existing firmware; the board restarts and the same connector then carries HID management.

For optional console logs at 115200 baud, 8N1, use a 3.3 V USB-UART adapter:
adapter RX to GP0 TX (header pin 1), adapter TX to GP1 RX (pin 2), GND to GND (pin 3).
Leave the adapter's supply output disconnected.

## 1. Prepare a Zephyr workspace

Default build target: ESP32-S3-DevKitC-1 N16R8, PROCPU, 16 MB flash and 8 MB octal PSRAM. The Raspberry Pi Pico,
Pico W, Pico 2 and Pico 2 W have their own [images](#raspberry-pi-pico-boards).
Check the module marking against the [hardware guide](hardware.md) before flashing.
Linux instructions below use Python 3.12, Zephyr 4.3.2, and Zephyr SDK 0.17.4.
For automatic setup on Debian/Ubuntu-compatible Linux or Raspberry Pi OS, run from your existing checkout:

```sh
tools/firmware/build.sh
```

The script installs missing host packages (requesting `sudo` when needed), creates/reuses a Python venv,
initializes a workspace in the checkout's parent directory if none exists, downloads pinned modules, plus radio blobs
for ESP32-S3 builds, and installs the selected board's SDK toolchain before building. First use requires network
access and several GB of disk space.
See [build environment options](../development/firmware.md#zephyr-target-build) to choose a Python interpreter or venv.
For a moved checkout, the wrappers update `manifest.path` only when its directory no longer exists and a local
firmware build cache identifies that directory as the previous application source. Other manifest checkouts
are not redirected.
Rebuilding cleans a relocated build cache in the selected build directory, never at its old cached path,
and uses West's automatic pristine handling for a moved application source directory.
The following procedure is an alternative for manual setup.

Install Zephyr's [host prerequisites](https://docs.zephyrproject.org/4.3.0/develop/getting_started/index.html)
(CMake, Ninja, device-tree compiler, compiler utilities and Python venv support).

From a directory **outside** this repository, create a workspace containing the source checkout:

```sh
mkdir zephyrforge-workspace
cd zephyrforge-workspace
git clone https://github.com/djwinter29-oss/ZephyrForge zephyrforge
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install west
west init -l zephyrforge
west update --narrow -o=--depth=1
python -m pip install -r zephyr/scripts/requirements-base.txt -r modules/hal/espressif/zephyr/requirements.txt
west blobs fetch hal_espressif
west sdk install --version 0.17.4 --toolchains xtensa-espressif_esp32s3_zephyr-elf
```

The repository's `west.yml` pins Zephyr and imports only its Espressif, Raspberry Pi Pico, CMSIS 6, FatFs and mbedTLS
dependencies. Espressif radio blobs are required for ESP32-S3 builds and have their own licenses. For Pico boards,
skip `west blobs fetch` and install the `arm-zephyr-eabi` toolchain instead. If SDK discovery fails, set
`ZEPHYR_SDK_INSTALL_DIR` to the installed SDK directory.

For an existing checkout, put it at `zephyrforge-workspace/zephyrforge` (or use a Linux symlink),
then run `west init -l zephyrforge` in a new, otherwise uninitialized workspace. Do not initialize
a second workspace inside an existing Zephyr workspace.

## 2. Build now; flash later

These steps build and flash the default ESP32-S3 image; the [Pico boards](#raspberry-pi-pico-boards) follow.
Run from the workspace created above; missing build prerequisites are prepared automatically:

```sh
bash zephyrforge/tools/firmware/build.sh
```

Outputs are in `zephyrforge/build/firmware/zephyr/`: `zephyr.elf` and `zephyr.bin`.
Both radios and native four-bit SDMMC support are enabled in the default build; no card is needed to compile.
For the alternate SD-over-SPI image, add `--storage sd-spi` to both build and flash commands.
See [image selection](../development/firmware.md#zephyr-target-build)
and use its matching [SD wiring](../reference/sdcard.md#pin-allocation).
The build uses PROCPU only, not a dual-core application.

When hardware arrives, attach the board's **UART** (USB-to-UART bridge) connector and close any serial terminal on it:

```sh
# Preview an upload command; the likely USB-UART port is detected automatically
bash zephyrforge/tools/firmware/flash.sh --dry-run

# Upload the existing build and reset the board
bash zephyrforge/tools/firmware/flash.sh
```

The [flash script](../../tools/firmware/flash.sh) detects a likely USB-UART port when `--port`
is omitted, preferring Silicon Labs CP210x devices. If equally likely ports are present, pass
`--port PORT` to choose one. It checks the build artifacts and board target, and uses Zephyr's
ESP32 runner to select the correct image offset and flash parameters. If required artifacts
are missing or empty, it builds first, including during `--dry-run`.
A complete existing build is reused; run the build script again after changing firmware.
It does not request a full-chip erase, but image sectors are
erased/replaced during flashing. Saved [boot settings](../reference/settings.md#flash-reservation) survive normal
uploads. No SD-card access is performed.

Replace the port with the real device, for example `COM4` on Windows with a Bash-capable
Zephyr environment. From a native Windows shell, use the equivalent
`west flash -d zephyrforge/build/firmware --skip-rebuild -r esp32 --esp-device COM4 --esp-baud-rate 460800 --no-erase`.
Use a data-capable USB cable and confirm your board is the matching N16R8 variant.
On Linux, identify the bridge using `python -m serial.tools.list_ports` from the Zephyr venv (pyserial is installed
with Zephyr's requirements); check serial permissions rather than running the flasher as root.

If automatic reset fails,
hold BOOT, press/release RESET, release BOOT, and retry. Flash only a matching module configuration;
flashing replaces its existing firmware. If uploading is unreliable, retry with
`--baud 115200` after checking the cable and power. The ESP32-S3 firmware is not a UF2 image.
After flashing, connect the **USB** connector as well and check live status in a dashboard; uploader success alone
is not proof that application boot or radios work. The UART connector keeps showing the Zephyr console log, for
example with `python -m serial.tools.miniterm /dev/ttyUSB0 115200`.

### Raspberry Pi Pico boards

Select the board with `--board pico`, `pico_w`, `pico2` or `pico2_w`, and add `--storage sd-spi` for the SPI image:

```sh
bash zephyrforge/tools/firmware/build.sh --board pico2 --storage sd-spi
```

This example writes `zephyrforge/build/firmware-pico2-sd-spi/zephyr/zephyr.uf2`; see
[image selection](../development/firmware.md#zephyr-target-build) for every output directory and the matching
[Pico SD wiring](../reference/sdcard.md#raspberry-pi-pico-boards). `flash.sh` supports only the ESP32-S3.
To flash a Pico, hold **BOOTSEL** while connecting its micro-USB cable, then copy `zephyr.uf2` to the drive that
appears: `RPI-RP2` for the Pico and Pico W, `RP2350` for the Pico 2 and Pico 2 W. The board restarts into
ZephyrForge, and the same cable then carries USB management. Copying replaces the existing firmware.
Pico W and Pico 2 W support [temporary Wi-Fi provisioning](wireless.md), but not Bluetooth LE. Non-W boards have no
wireless management; see the
[hardware guide](hardware.md#raspberry-pi-pico-targets) for the optional UART console.

## 3. Choose a dashboard

Run from the repository root (`cd zephyrforge`):

| Dashboard | Launch | Open |
| --- | --- | --- |
| Direct | `bash tools/host/run-direct.sh --port 8000` | `http://127.0.0.1:8000/` |
| Python | `bash tools/host/run-python.sh web`, or `zephyrforge web` after `pip install zephyrforge` | `http://127.0.0.1:8001/` |
| .NET | `bash tools/host/run-dotnet.sh` | `http://127.0.0.1:8002/` |

Direct needs supported desktop Chrome/Edge for WebHID or Web Bluetooth.
Its static server uses Python only as a convenience; HTTPS static hosting needs nothing installed.
Python uses Python 3.10+ with Flask and `hidapi`; .NET uses the .NET 10 SDK/runtime with HidSharp.
Python and .NET select the first ZephyrForge board, or the one given by `--serial SERIAL` (the 16 hex-digit UID) or
`--device-path HID_PATH`. On Linux, run `bash tools/host/hid-permissions-setup.sh` once, then reconnect the board;
do not run the dashboards as root. Without a board the hosts show an empty board list; there is no simulated mode.

## 4. First connection

Connect the board's **USB** connector. In Direct, click **USB** and choose the ZephyrForge HID device; in Python or
.NET, select the board in the list. Confirm product `ZephyrForge`, the board, firmware version and serial number.
An SD mount failure is expected without a card and does not prevent dashboard operation.
On the ESP32-S3, test and save a 2.4 GHz network, and optionally create a Bluetooth Control key, using
[wireless setup](wireless.md).
Run one host per board at a time, so Control commands do not interleave. A command whose outcome is uncertain is
never retried; confirm the
re-sync prompt after checking the board.

Hardware acceptance and all SD-card tests are deferred; see [qualification gaps](../validation/qualification-gaps.md).
