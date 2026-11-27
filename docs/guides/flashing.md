# Flashing firmware

[Documentation](../README.md) / Guides / Flashing

Choose the procedure for a downloaded release or an existing source build. Both replace firmware on the board.
Check the exact board and memory variant first; images for boards with the same MCU are not necessarily compatible.

- [Install a release image](#flash-a-release-image)
- [Upload a source build](#from-a-source-build)
- [Troubleshoot flashing or startup](#if-flashing-or-startup-fails)

## Flash a release image

For first use on a factory-fresh ESP32-C6, start with [first-key commissioning](esp32c6-commissioning.md).
It requires an initialized source build matching the flashed image.

On [GitHub Releases](https://github.com/djwinter29-oss/ZephyrForge/releases/latest), look for
`zephyrforge-firmware-vVERSION-BOARD-STORAGE.zip`. Match both filename fields to your setup.
The table below lists supported source-build combinations, not a promise that every combination is published.
If the selected release has no matching archive, use the [source-build procedure](../development/firmware-build.md).

| Board | `BOARD` in the filename | Source-build `STORAGE` values |
| --- | --- | --- |
| ESP32-S3-DevKitC-1 N16R8 | `esp32s3-devkitc` | `sdmmc`, `sd-spi`, `none` |
| ESP32-C6-DevKitC-1 N8 | `esp32c6-devkitc` | `sd-spi`, `none` |
| Raspberry Pi Pico / Pico W | `pico` / `pico_w` | `sdmmc`, `sd-spi`, `none` |
| Raspberry Pi Pico 2 / Pico 2 W | `pico2` / `pico2_w` | `sdmmc`, `sd-spi`, `none` |

For example, the v0.1.0 archive for a Pico 2 W without SD storage is `zephyrforge-firmware-v0.1.0-pico2_w-none.zip`.
Verify the downloaded ZIP against `SHA256SUMS.txt`, then extract it and confirm the target, storage variant,
version, and source commit in `BUILD-INFO.txt`.

Firmware ZIPs contain an image, build identity, and licenses, not a Zephyr build directory or offline documentation.
Use the guides linked from the release **at its tag**, and save them before working offline.
For SD-enabled images, check the matching [wiring guide](../hardware/sd-card-wiring.md). Before changing buses,
follow the [card power-cycle instruction](sd-card.md#before-you-start).

### ESP32-S3 release image

Use only ESP32-S3-DevKitC-1 **N16R8**: 16 MB flash, 8 MB octal PSRAM, PROCPU.
Do not use this image on N4/N8 or octal-flash N32R16V/N16R16VA modules.
Close serial clients and connect the **UART** USB-to-UART connector.

Create and activate a Python virtual environment, then install esptool 5:

```sh
python -m pip install "esptool>=5,<6"
```

From the extracted ZIP directory, replace `PORT` with the board's serial port:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 zephyr.bin
```

Do not change the offset or flash parameters. This replaces image sectors without requesting a full-chip erase.
After flashing, use the **USB** connector for HID management and the **UART** connector for the console at
115200 baud, 8N1.

### ESP32-C6 release image

Use this path for an ESP32-C6 with a saved Control key. For a factory-fresh board, follow
[first-key commissioning](esp32c6-commissioning.md).

Use only the ESP32-C6-DevKitC-1 N8 target named in `BUILD-INFO.txt`. Connect its serial flashing port, close other
serial clients, and install esptool 5 as above. From the extracted ZIP directory:

```sh
python -m esptool --chip esp32c6 --port PORT --baud 460800 write-flash 0x0 zephyr.bin
```

This writes the image without requesting a full-chip erase. Use the UART console for boot logs.
After boot, [connect over Bluetooth](wireless.md#bluetooth-le) with the saved Control key.

### Raspberry Pi Pico release image

Use the exact Pico, Pico W, Pico 2, or Pico 2 W image named in `BUILD-INFO.txt`.
RP2040 and RP2350 images are not interchangeable.

1. Hold **BOOTSEL** while connecting the micro-USB cable.
2. Copy `zephyr.uf2` to the drive that appears: `RPI-RP2` for Pico/Pico W, `RP2350` for Pico 2/Pico 2 W.
3. Wait for the board to restart. The same connector now carries USB HID management.

With a debug probe or `picotool`, you can flash the package's `zephyr.elf` instead; it is the same build.

For console logs, use a 3.3 V USB-UART adapter wired as shown in the
[Pico hardware guide](../hardware/README.md#raspberry-pi-pico-targets). Leave the adapter's supply output disconnected.

## From a source build

First [build the required board/storage image](../development/firmware-build.md). Run the following commands from
the repository root, with no other program using the board's flashing port.

### ESP32-S3 and ESP32-C6

The flash wrapper uses the existing build and Zephyr's ESP32 runner. Select the same board and storage profile used
to build it:

```sh
# Preview the default ESP32-S3 SDMMC upload
bash tools/firmware/flash.sh --port PORT --dry-run

# Upload and reset
bash tools/firmware/flash.sh --port PORT

# Examples for other images
bash tools/firmware/flash.sh --board esp32s3 --storage none --port PORT
bash tools/firmware/flash.sh --board esp32c6 --storage sd-spi --port PORT
```

Without `--port`, the wrapper detects a likely USB-UART port, preferring Silicon Labs CP210x devices. If several
ports are equally likely, it requires an explicit choice. Use `python -m serial.tools.list_ports` in the Zephyr
venv to identify ports on Linux; fix serial permissions rather than running the flasher as root.

The wrapper checks the board, storage profile, artifacts, and flash limits. Missing or empty artifacts cause a
build, **including during `--dry-run`**. A complete existing build is reused, so rebuild after changing source.
Uploads replace image sectors without requesting a full-chip erase. Saved
[boot settings](../reference/boot-settings.md#flash-reservation) survive normal uploads; the wrapper does not access
the SD card.

The flash wrapper selects default images only. For a build customized with `--without`, use manual
`west flash -d <custom-build-directory> --skip-rebuild --runner esp32 --esp-device PORT --no-erase`
from the initialized Zephyr environment, after verifying the selected board and image.

On Windows, use the wrapper from a Bash-capable Zephyr environment and pass a port such as `COM4`. For the default
ESP32-S3 image, the equivalent command from the workspace root in a native shell is:

```sh
west flash -d zephyrforge/build/firmware --skip-rebuild -r esp32 --esp-device COM4 --esp-baud-rate 460800 --no-erase
```

### Raspberry Pi Pico

The wrapper does not flash Pico boards. Take `zephyr.uf2` from the selected
[build output directory](../development/firmware-build.md#build-with-the-wrapper) and follow the
[BOOTSEL copy procedure](#raspberry-pi-pico-release-image) above.

## If flashing or startup fails

- Use a data-capable cable and check power and the board marking.
- On ESP32-S3, verify that the cable is in the **UART** connector. If automatic reset fails, hold BOOT, press and
  release RESET, release BOOT, and retry. For unreliable uploads, try `--baud 115200` after checking cable and power.
- Close serial terminals before uploading. Reopen the [console](console.md#uart) afterward at 115200 baud, 8N1.
- An upload success is not a boot check. Connect a dashboard and confirm fresh board status.
