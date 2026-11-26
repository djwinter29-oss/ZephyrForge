# Firmware builds

[Documentation](../README.md) / Development / Firmware builds

- [Build wrapper and output files](#build-with-the-wrapper)
- [Enable or disable optional modules](#optional-modules)
- [Build verification](#build-verification)
- [Customize an existing workspace](#custom-configuration-and-existing-workspaces)
- [Set up a manual workspace](#manual-workspace-setup)
- [Configure Pico W's Wi-Fi country](#raspberry-pi-pico-family)

For application changes and portable tests, see [firmware development](firmware.md).

## Build with the wrapper

Run the wrapper from the repository root. The first build needs network access and several GB of free space.
The wrapper prepares the Zephyr workspace and Python virtual environment, installs the pinned dependencies and
SDK 0.17.4, and applies the project's Zephyr patches. The `west.yml` manifest pins Zephyr 4.3.2 and its modules.
On Debian/Ubuntu-compatible Linux, the wrapper installs missing host packages through `apt-get`. On other
systems, install the prerequisites it reports with your package manager.

| Board and default storage | Command | Primary image |
| --- | --- | --- |
| ESP32-S3 N16R8, SDMMC | `bash tools/firmware/build.sh` | `build/firmware/zephyr/zephyr.bin` |
| ESP32-C6 N8, no storage | `bash tools/firmware/build.sh --board esp32c6` | `build/firmware-esp32c6-no-storage/zephyr/zephyr.bin` |
| Pico family, SDMMC | `bash tools/firmware/build.sh --board pico` | `build/firmware-pico/zephyr/zephyr.uf2` |

For Pico W, Pico 2, or Pico 2 W, replace `pico` with `pico_w`, `pico2`, or `pico2_w` in the command
and output directory. For W boards, check the [Wi-Fi country setting](#raspberry-pi-pico-family) before use.

### Configuration and storage profiles

Pass `--storage` to select a profile and its board-specific overlay. For example, build an ESP32-S3 SPI image
with `bash tools/firmware/build.sh --storage sd-spi`.

| Profile | Boards | Output directory suffix |
| --- | --- | --- |
| `sdmmc` | ESP32-S3 and Pico families | No suffix |
| `sd-spi` | All supported boards | `-sd-spi` |
| `none` | All supported boards | `-no-storage` |

The build directory starts with `build/firmware`, followed by `-esp32c6` or the Pico board identifier for
those boards, then the profile suffix. The image filename is the same across profiles for each board.

By default, the wrapper enables the benchmark for storage-enabled ESP32-S3 and Pico builds. For a manual build,
`ZF_ENABLE_STORAGE_BENCHMARK` defaults to `OFF`. The benchmark requires storage and USB HID control.
See the [SD reference](../reference/sd-storage.md) for storage behavior and the
[benchmark contract](../reference/sd-storage.md#optional-storage-benchmark) before running it.

### Optional modules

Pass `--without MODULE` (repeatable) to omit a module the board would otherwise include:

| Module | Effect when omitted | Manual selection |
| --- | --- | --- |
| `wifi` | No Wi-Fi driver; also omits `tcp-shell` and the Pico W wireless overlay | Omit `wifi.conf`; on Pico W, also omit `w.conf` and `w.overlay` |
| `bluetooth` | No BLE peripheral or Control-key authentication; rejected on ESP32-C6, whose only management link is BLE | Omit `bluetooth.conf` |
| `tcp-shell` | No Wi-Fi diagnostic shell | Omit `tcp-shell.conf` |
| `usb-msc` | Keeps storage but cannot export the card over USB; card-export commands report unsupported | Omit `usb-msc.conf` |
| `benchmark` | No storage benchmark | Set `ZF_ENABLE_STORAGE_BENCHMARK=OFF` |

Omitted radios are never started, reported as enabled, or accepted by radio-switch commands; with both omitted,
the image does not advertise wireless management. Each omitted module appends `-no-MODULE` to the build directory,
so customized images never replace the default ones used by the flashing and release wrappers. Suffixes are always
appended in this order: `wifi`, `bluetooth`, `tcp-shell`, `usb-msc`, `benchmark`. Omitting Wi-Fi also omits
`tcp-shell`, so a build without Wi-Fi receives both suffixes.

Omitting a module at build time does not modify saved settings. To enable it again on a supported board, rebuild
without that `--without` switch.
For example:

```sh
bash tools/firmware/build.sh --without wifi --without bluetooth
bash tools/firmware/build.sh --without usb-msc --without benchmark
```

An image without Wi-Fi still reports BLE state and saved/staged credentials when BLE is included.

After building, follow the [source upload steps](../guides/flashing.md#from-a-source-build). On a fresh ESP32-C6,
retain the build directory for [first-key commissioning](../hardware/esp32c6-commissioning.md).

## Build verification

For configuration changes, use a pristine build and inspect the generated `.config` and `zephyr.dts`.
No-storage builds must have `CONFIG_USBD_MSC_CLASS`, `CONFIG_DISK_ACCESS` and `CONFIG_FILE_SYSTEM` unset.
Storage-enabled builds must keep both `CONFIG_FS_FATFS_MOUNT_MKFS` and `CONFIG_FS_FATFS_MKFS` disabled.
A successful build checks software only, not physical wiring, card detection, mounting, or USB ownership.

CI builds the supported targets and packages each image with build identity and dependency license notices. Firmware
packages do not include the west manifest, generated configuration or flashing instructions; see the
[release package contents](releases.md#release-assets) and the linked flashing guides.
`ZEPHYRFORGE_VERSION=MAJOR.MINOR.PATCH` sets the release firmware version. Each component must be from 0 to 255 to
fit the firmware identity report. The wrapper passes the version as a CMake cache value so reusing a build directory
with a different version rebuilds the image and its build-time identity snapshot. Invalid values fail configuration.
See [release automation](releases.md).

## Custom configuration and existing workspaces

The wrapper builds a standalone Zephyr application. It rejects `build.cmake-args` settings that override its
selected board, firmware profile, version, Zephyr base, SDK, or toolchain. Use a manual west build for those changes.

`PYTHON` selects the interpreter used to create the workspace venv (for example `PYTHON=python3.12`).
`VENV_DIR` selects another venv; relative paths are resolved from the current directory. The scripts do not
automatically select an unrelated active venv. To run manual west commands, activate the workspace venv yourself.

For a moved checkout, the wrappers update `manifest.path` only if its old directory no longer exists and a local
firmware build cache identifies it as the previous application source. They do not redirect another manifest
checkout. A relocated build cache is cleaned only in the selected build directory, never at its old cached path.
West's automatic pristine handling covers a moved application source.

Use the [manual workspace setup](#manual-workspace-setup) to manage dependencies yourself. Then select
[ESP32-S3](#esp32-s3-family), [ESP32-C6](#esp32-c6-family), or [Pico](#raspberry-pi-pico-family).

## Manual workspace setup

Use this instead of automatic setup when you need to manage dependencies yourself. The Linux example uses
Python 3.12, Zephyr 4.3.2, and Zephyr SDK 0.17.4. Install Zephyr's
[host prerequisites](https://docs.zephyrproject.org/4.3.0/develop/getting_started/index.html), including CMake,
Ninja, the device-tree compiler, compiler utilities, and Python venv support.

Run manual `west` commands from the workspace root with the application checkout named `zephyrforge`.

From outside this repository:

```sh
mkdir zephyrforge-workspace
cd zephyrforge-workspace
git clone https://github.com/djwinter29-oss/ZephyrForge zephyrforge
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install west
west init -l zephyrforge
west update --narrow -o=--depth=1
python -m pip install -r zephyr/scripts/requirements-base.txt
```

For ESP32-S3, install the Espressif requirements, radio blobs, and Xtensa toolchain:

```sh
python -m pip install -r modules/hal/espressif/zephyr/requirements.txt
west blobs fetch hal_espressif
west sdk install --version 0.17.4 --toolchains xtensa-espressif_esp32s3_zephyr-elf
```

ESP32-C6 uses the same Espressif requirements and blobs, but the `riscv64-zephyr-elf` SDK toolchain.
Pico boards use `arm-zephyr-eabi` and do not need Espressif requirements or blobs; W boards need the
[Infineon blobs](#raspberry-pi-pico-family). Radio blobs have their own licenses.
If SDK discovery fails, set `ZEPHYR_SDK_INSTALL_DIR` to the installed SDK directory.

Apply the project's patches before a manual target build:

```sh
bash zephyrforge/tools/firmware/apply-zephyr-patches.sh "$(west list zephyr -f '{abspath}')"
```

For an existing checkout, put it at `zephyrforge-workspace/zephyrforge` or use a Linux symlink, then run
`west init -l zephyrforge` in the new workspace. Do not nest a second workspace inside an existing one.
The manifest pins Zephyr and imports only the required modules; do not substitute an unrelated Zephyr checkout.

### Manual overlays and fragments

The wrapper is the supported path for standard images. For custom `west build` configurations, start with
`firmware/prj.conf`, select feature fragments, then combine the board and storage overlays below. USB mass storage
requires both `profiles/usb.conf` and `profiles/storage.conf`. A no-storage image omits
`profiles/storage.conf`, `profiles/usb-msc.conf`, and the storage overlay, and sets `ZF_ENABLE_STORAGE=OFF` and
`ZF_ENABLE_STORAGE_BENCHMARK=OFF`.

Paths in `CONF_FILE` and `DTC_OVERLAY_FILE` are relative to `firmware/`. Select the fragments and overlays for the
target:

- **ESP32-S3:** `profiles/wifi.conf`, `profiles/bluetooth.conf`, `profiles/tcp-shell.conf`, `profiles/usb.conf`,
  and `boards/esp32s3/board.conf`; overlays `boards/esp32s3/board.overlay` and either
  `boards/esp32s3/storage/sdmmc.overlay` or `boards/esp32s3/storage/sd_spi.overlay`.
- **ESP32-C6:** `profiles/wifi.conf` and `profiles/bluetooth.conf`; overlays `boards/esp32c6/board.overlay` and
  `boards/esp32c6/storage/sd_spi.overlay`.
- **Pico family:** `profiles/usb.conf`; storage builds also use `profiles/storage.conf`. W boards add
  `profiles/wifi.conf` and `boards/rpi_pico/wireless/w.conf`. All use `boards/rpi_pico/board.overlay` and the
  matching `boards/rpi_pico/soc/rp2040.overlay` or `boards/rpi_pico/soc/rp2350.overlay`. Storage builds add
  `boards/rpi_pico/storage/sdmmc.overlay` or `boards/rpi_pico/storage/sd_spi.overlay`; W boards also add
  `boards/rpi_pico/wireless/w.overlay`.

All storage-enabled builds include `profiles/storage.conf`. Include `profiles/usb-msc.conf` only when both
`profiles/usb.conf` and `profiles/storage.conf` are selected. Apply the board overlay before the SoC, storage, and
wireless overlays. The example builds the standard ESP32-S3 native SDMMC image from an initialized workspace:

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu zephyrforge/firmware \
  -d zephyrforge/build/firmware -- \
  -DCONF_FILE="prj.conf;profiles/wifi.conf;profiles/bluetooth.conf;profiles/tcp-shell.conf;boards/esp32s3/board.conf;profiles/usb.conf;profiles/storage.conf;profiles/usb-msc.conf" \
  -DDTC_OVERLAY_FILE="boards/esp32s3/board.overlay;boards/esp32s3/storage/sdmmc.overlay" \
  -DZF_ENABLE_STORAGE_BENCHMARK=ON
```

## ESP32-S3 family

The N16R8 target uses one PROCPU application and the Xtensa toolchain. The `board.conf` selects the module's memory,
heap, and LED configuration.

## ESP32-C6 family

The N8 target uses the high-performance core and RISC-V toolchain. Its only storage option is SPI2; see the
[SD-card wiring guide](../hardware/sd-card-wiring.md). For dashboard availability and first use, see
[Getting Started](../getting-started.md).

## Raspberry Pi Pico family

The wrapper supports the following Zephyr targets:

| `--board` | Zephyr target | Radio |
| --- | --- | --- |
| `pico` | `rpi_pico/rp2040` | None |
| `pico_w` | `rpi_pico/rp2040/w` | Wi-Fi |
| `pico2` | `rpi_pico2/rp2350a/m33` | None |
| `pico2_w` | `rpi_pico2/rp2350a/m33/w` | Wi-Fi |

Pico builds use the Arm toolchain. Non-W builds do not fetch radio blobs. W builds enable Wi-Fi and fetch the
CYW43439 firmware and Murata-1YN CLM blob from the pinned Infineon HAL. Pico storage profiles use the project's
PIO SD host for native four-bit SDMMC or SPI0 for SD-over-SPI. The `none` profile omits SD drivers and device-tree
nodes.

Before a manual W-board build, fetch the pinned radio blobs:

```sh
west blobs fetch hal_infineon --allow-regex 'img/whd/resources/(firmware/COMPONENT_43439/43439A0\.bin|clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0\.clm_blob)$'
```

The W-board release images use `WHD_COUNTRY_AUSTRALIA`. For a custom build, select the country that matches the
board's regulatory region. Create a Kconfig fragment with the selected value, for example:

```text
CONFIG_WHD_WIFI_COUNTRY="WHD_COUNTRY_UNITED_STATES"
```

For a manual W-board build, save the country fragment (here, `/absolute/path/to/pico-country.conf`) and use it as
an extra Kconfig fragment. This complete Pico W SDMMC example uses the country value shown above:

```sh
west build -p always -b rpi_pico/rp2040/w zephyrforge/firmware \
  -d zephyrforge/build/firmware-pico-w-country -- \
  -DCONF_FILE="prj.conf;profiles/wifi.conf;boards/rpi_pico/wireless/w.conf;profiles/usb.conf;profiles/storage.conf;profiles/usb-msc.conf" \
  -DEXTRA_CONF_FILE="/absolute/path/to/pico-country.conf" \
  -DDTC_OVERLAY_FILE="boards/rpi_pico/board.overlay;boards/rpi_pico/soc/rp2040.overlay;boards/rpi_pico/storage/sdmmc.overlay;boards/rpi_pico/wireless/w.overlay" \
  -DZF_ENABLE_STORAGE=ON -DZF_ENABLE_STORAGE_BENCHMARK=ON
```

For Pico 2 W, use target `rpi_pico2/rp2350a/m33/w` and replace the SoC overlay with
`boards/rpi_pico/soc/rp2350.overlay`. The build wrapper does not accept country customization. Use a release or
wrapper-built image only where its Australia country setting is appropriate. For a manual SPI build, select
`sd_spi.overlay` instead of `storage/sdmmc.overlay`. Standard target and storage combinations are simpler to build
with the wrapper.
