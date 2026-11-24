# Firmware builds

This guide covers target selection, storage profiles, output locations and manual Zephyr configuration.
For workspace setup and native tests, see the [firmware development guide](./firmware.md).

## Shared build behavior

From the repository root, the build wrapper selects the default ESP32-S3 SDMMC image:

```sh
bash tools/firmware/build.sh
```

The wrapper creates or reuses the Zephyr workspace and Python virtual environment, installs the pinned west
dependencies and SDK 0.17.4 toolchain, applies the tracked Zephyr patches and builds the selected target.
First use requires network access and several GB of free space. On Debian/Ubuntu-compatible Linux, missing host
packages are installed through `apt-get`; other systems should install the reported prerequisites with their package
manager. See [manual workspace setup](../guides/getting-started.md#1-prepare-a-zephyr-workspace) to provision
dependencies yourself.

The manifest in `west.yml` selects Zephyr 4.3.2 and imported module commits. Use
`bash tools/firmware/apply-zephyr-patches.sh "$(west list zephyr -f '{abspath}')"` before a manual target build.
The helper verifies the expected Zephyr checkout before applying patches.

`PYTHON` selects the interpreter used to create the workspace venv (for example `PYTHON=python3.12`).
`VENV_DIR` selects another venv; relative paths are resolved from the current directory. The scripts do not
automatically select an unrelated active venv. To run manual west commands, activate the workspace venv yourself.
`firmware/prj.conf` holds settings common to every image. Optional feature fragments live in `firmware/profiles/`:
`usb.conf` (native USB HID; ESP32-S3 and Pico), `wifi.conf` (ESP32 and Pico W), `bluetooth.conf` (ESP32),
`tcp-shell.conf` (Wi-Fi diagnostic shell; ESP32-S3 only), `storage.conf` (SD disk access and FAT/exFAT) and
`usb-msc.conf` (USB mass storage; only with both `usb.conf` and `storage.conf`). Storage is opt-in: a no-storage
image omits `storage.conf`, `usb-msc.conf` and the storage overlay.

Board hardware lives under `firmware/boards/`, one folder per board family:

| Folder | Contents |
| --- | --- |
| `esp32s3/` | `board.conf` (N16R8), `board.overlay` (memory/HID/LED); SDMMC and SPI storage overlays |
| `esp32c6/` | `board.overlay` (8 MB flash); `storage/sd_spi.overlay` |
| `rpi_pico/` | `board.overlay` (HID/temp); `soc/` flash; `storage/` SD wiring; `wireless/` W boards |

Apply the board overlay first, then the SoC overlay. Storage-enabled builds add exactly one storage wiring overlay;
W boards also add the wireless overlay.

`--board` selects a board family target. `--storage` selects a storage profile; it does not select wiring.
The ESP32-S3 is the default board, and SDMMC is the default storage profile. ESP32-C6 defaults to `none`.

| Storage profile | Build behavior | Benchmark |
| --- | --- | --- |
| `sdmmc` | Native SDMMC; available on ESP32-S3 and Pico families | Included where USB control is available |
| `sd-spi` | SD over SPI; available on ESP32-S3, ESP32-C6 and Pico families | Included except ESP32-C6 |
| `none` | No SD, disk-access, FAT or USB MSC support | Omitted |

Both storage-enabled profiles retain no-auto-format behavior. They do not automatically expose or modify the card over
USB; the host must request an ownership handoff. A no-storage image advertises no storage transport capability, and
its storage report remains present but unconfigured. Storage control operations are unsupported. See the
[USB HID specification](../reference/usb-hid.md) for capability and report behavior.

`ZF_ENABLE_STORAGE_BENCHMARK` is a CMake option defaulting to `OFF`. The wrapper enables it for storage-enabled
ESP32-S3 and Pico images, not for `none` or ESP32-C6; the C6 has no USB HID path for its USB-only start command.
Benchmarking requires storage.
See the [benchmark contract](../reference/sdcard.md#optional-storage-benchmark) before authorizing card writes.

## ESP32-S3 family

The supported target is ESP32-S3-DevKitC-1 N16R8, configured as one PROCPU application with 16 MB flash and 8 MB
octal PSRAM. All three storage profiles are available:

```sh
bash tools/firmware/build.sh --board esp32s3 --storage sdmmc
bash tools/firmware/build.sh --board esp32s3 --storage sd-spi
bash tools/firmware/build.sh --board esp32s3 --storage none
```

The default image is equivalent to the first command. Output directories and primary images:

| Profile | Output |
| --- | --- |
| `sdmmc` | `build/firmware/zephyr/zephyr.bin` |
| `sd-spi` | `build/firmware-sd-spi/zephyr/zephyr.bin` |
| `none` | `build/firmware-no-storage/zephyr/zephyr.bin` |

ESP32-S3 builds use the Xtensa toolchain and fetch the Espressif radio blobs. The no-storage build keeps the N16R8
board configuration and USB HID device overlay but omits the SD bus overlay. Flash an ESP32-S3 image with the
matching profile, for example:

```sh
bash tools/firmware/flash.sh --storage none --port /dev/ttyUSB0
```

The flash wrapper checks that the image's storage configuration matches the selected profile before uploading.
It does not erase the full chip.

Manual builds use the same pinned workspace and patches. Add the storage overlay, `profiles/storage.conf` and
`profiles/usb-msc.conf` only for storage-enabled images:

```sh
# Native SDMMC
west build -p always -b esp32s3_devkitc/esp32s3/procpu zephyrforge/firmware \
  -d zephyrforge/build/firmware -- \
  -DCONF_FILE="prj.conf;profiles/wifi.conf;profiles/bluetooth.conf;profiles/tcp-shell.conf;boards/esp32s3/board.conf;profiles/usb.conf;profiles/storage.conf;profiles/usb-msc.conf" \
  -DDTC_OVERLAY_FILE="boards/esp32s3/board.overlay;boards/esp32s3/storage/sdmmc.overlay"

# No storage
west build -p always -b esp32s3_devkitc/esp32s3/procpu zephyrforge/firmware \
  -d zephyrforge/build/firmware-no-storage -- \
  -DCONF_FILE="prj.conf;profiles/wifi.conf;profiles/bluetooth.conf;profiles/tcp-shell.conf;boards/esp32s3/board.conf;profiles/usb.conf" \
  -DDTC_OVERLAY_FILE="boards/esp32s3/board.overlay" \
  -DZF_ENABLE_STORAGE=OFF -DZF_ENABLE_STORAGE_BENCHMARK=OFF
```

For SPI, replace `sdmmc.overlay` with the standalone `sd_spi.overlay` and use a separate build directory.
The ESP32-S3 N16R8 board configuration supplies only module memory, heap and LED settings. Wi-Fi, Bluetooth and
TCP shell profiles are selected for all ESP32-S3 storage profiles. The ESP32-C6 needs no board configuration file:
shared profiles and Zephyr's board defaults cover it.

## ESP32-C6 family

The supported software target is ESP32-C6-DevKitC-1 N8, using Zephyr's
`esp32c6_devkitc/esp32c6/hpcore` target. It has 8 MB module flash, no PSRAM, no board LED and no native USB HID
device. `--board esp32c6` defaults to `none`; use `--storage sd-spi` for the SPI2 card overlay. Native SDMMC is
rejected. Builds use the RISC-V toolchain and fetch the pinned Espressif radio blobs.

```sh
bash tools/firmware/build.sh --board esp32c6
bash tools/firmware/build.sh --board esp32c6 --storage sd-spi
bash tools/firmware/flash.sh --board esp32c6 --port /dev/ttyUSB0
```

The output directories are `build/firmware-esp32c6-no-storage` and
`build/firmware-esp32c6-sd-spi`; both produce `zephyr/zephyr.bin`. The no-storage profile is the default.
The SPI image reports GPIO2 MISO, GPIO6 SCK, GPIO7 MOSI, GPIO10 CS and active-low card detect on GPIO3; it disables
I2C0 because GPIO6/7 are shared. See the [C6 SPI wiring guide](../guides/boards/esp32c6-sd-spi.md) before connecting
a card. The pin allocation is software-configured and has not been physically qualified.

The C6 has no native USB HID peripheral, so it cannot use the USB-only Python or .NET dashboards. Direct can use its
authenticated BLE service only after a Control key is already saved. The current repository has no way to provision
that key to a fresh C6; BLE therefore remains off on a default image. This is a known management limitation, not
replaced with unauthenticated access. Wi-Fi credential provisioning and the storage benchmark are also unavailable
without USB HID. Do not treat successful compilation as board qualification.

Manual builds use the same pinned workspace. No-storage command:

```sh
west build -p always -b esp32c6_devkitc/esp32c6/hpcore zephyrforge/firmware \
  -d zephyrforge/build/firmware-esp32c6-no-storage -- \
  -DCONF_FILE="prj.conf;profiles/wifi.conf;profiles/bluetooth.conf" \
  -DDTC_OVERLAY_FILE="boards/esp32c6/board.overlay" \
  -DZF_ENABLE_STORAGE=OFF -DZF_ENABLE_STORAGE_BENCHMARK=OFF
```

For SD-over-SPI, replace the output directory with `firmware-esp32c6-sd-spi` and append
`boards/esp32c6/storage/sd_spi.overlay` to `DTC_OVERLAY_FILE` and `profiles/storage.conf` to `CONF_FILE`. Omit the
USB-only `profiles/usb-msc.conf`; keep the benchmark option `OFF`.

## Raspberry Pi Pico family

The wrapper supports the following Zephyr targets:

| `--board` | Zephyr target | Radio |
| --- | --- | --- |
| `pico` | `rpi_pico/rp2040` | None |
| `pico_w` | `rpi_pico/rp2040/w` | Wi-Fi |
| `pico2` | `rpi_pico2/rp2350a/m33` | None |
| `pico2_w` | `rpi_pico2/rp2350a/m33/w` | Wi-Fi |

Each board supports `sdmmc`, `sd-spi` and `none`. Examples:

```sh
bash tools/firmware/build.sh --board pico
bash tools/firmware/build.sh --board pico2_w --storage sd-spi
bash tools/firmware/build.sh --board pico2 --storage none
```

Pico images are UF2 files. Example output directories:

| Board/profile | Output |
| --- | --- |
| `pico` / `sdmmc` | `build/firmware-pico/zephyr/zephyr.uf2` |
| `pico_w` / `sd-spi` | `build/firmware-pico_w-sd-spi/zephyr/zephyr.uf2` |
| `pico2` / `none` | `build/firmware-pico2-no-storage/zephyr/zephyr.uf2` |

Pico builds use the Arm toolchain. Non-W builds do not fetch radio blobs. W builds enable the shared Wi-Fi
configuration and fetch the CYW43439 firmware and Murata-1YN CLM blob from the pinned Infineon HAL. Pico storage
profiles use the project's PIO SD host for native four-bit SDMMC or SPI0 for SD-over-SPI. The no-storage profile
does not add the SD bus, card-detect GPIO, disk, FAT or MSC device tree and driver configuration.

The wrapper does not flash Pico boards. Copy the UF2 image to the board's BOOTSEL drive using the
[Pico flashing procedure](../guides/getting-started.md#2-build-now-flash-later).

For manual Pico builds, select the board overlay and the SoC overlay. Add the storage overlay for storage-enabled
builds. W boards also need the radio overlay and config:

```sh
# Pico W, native SDMMC
west build -p always -b rpi_pico/rp2040/w zephyrforge/firmware \
  -d zephyrforge/build/firmware-pico_w -- \
  -DCONF_FILE="prj.conf;profiles/wifi.conf;boards/rpi_pico/wireless/w.conf;profiles/usb.conf;profiles/storage.conf;profiles/usb-msc.conf" \
  -DDTC_OVERLAY_FILE="boards/rpi_pico/board.overlay;boards/rpi_pico/soc/rp2040.overlay;boards/rpi_pico/storage/sdmmc.overlay;boards/rpi_pico/wireless/w.overlay"

# Pico 2, no storage
west build -p always -b rpi_pico2/rp2350a/m33 zephyrforge/firmware \
  -d zephyrforge/build/firmware-pico2-no-storage -- \
  -DCONF_FILE="prj.conf;profiles/usb.conf" \
  -DDTC_OVERLAY_FILE="boards/rpi_pico/board.overlay;boards/rpi_pico/soc/rp2350.overlay" \
  -DZF_ENABLE_STORAGE=OFF -DZF_ENABLE_STORAGE_BENCHMARK=OFF
```

The `sd_spi.overlay` is standalone; select it instead of `storage/sdmmc.overlay`. Omit `profiles/wifi.conf`, `w.conf`
and `w.overlay` on non-W boards. Before manually building a W image, fetch the matching blobs:

```sh
west blobs fetch hal_infineon --allow-regex 'img/whd/resources/(firmware/COMPONENT_43439/43439A0\.bin|clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0\.clm_blob)$'
```

## Build verification

For configuration changes, use a pristine build and inspect the generated `.config` and `zephyr.dts`.
No-storage builds must have `CONFIG_USBD_MSC_CLASS`, `CONFIG_DISK_ACCESS` and `CONFIG_FILE_SYSTEM` unset.
Storage-enabled builds must keep both `CONFIG_FS_FATFS_MOUNT_MKFS` and `CONFIG_FS_FATFS_MKFS` disabled.
A successful software build does not qualify SD
wiring, card detection, mount behavior or USB ownership on physical hardware.

CI builds the supported targets and packages each image with build identity and dependency license notices. Firmware
packages do not include the west manifest, generated configuration or flashing instructions; see the
[release package contents](../deployment/release.md#publish-a-stable-tag) and the linked flashing guides.
`ZEPHYRFORGE_VERSION=MAJOR.MINOR.PATCH` sets the release firmware version. Each component must be from 0 to 255 to
fit the firmware identity report. The wrapper passes the version as a CMake cache value so reusing a build directory
with a different version rebuilds the image and its build-time identity snapshot. Invalid values fail configuration.
See [release automation](../deployment/release.md).
