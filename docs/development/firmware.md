# Firmware development

## Source layout

The firmware is grouped by role: board configuration, platform integration and application services.
Keep hardware-dependent integration separate from portable management logic.

| Directory | Contents |
| --- | --- |
| `src/config/` | Board identity, UID/serial, reset cause, memory inventory, board facts, app lock and delayed reboot |
| `src/platform/` | Portable boot settings and platform hooks; Zephyr settings flash, CPU clock, RGB LED, SD worker, USB drive disk and benchmark file backend |
| `src/platform/rpi_pico/` | Raspberry Pi Pico board hooks and the PIO SD host with its portable SDIO codec |
| `src/app/` | Shared Zephyr service interface and telemetry |
| `src/app/control/` | Portable Report 20 control dispatcher shared by USB and BLE |
| `src/app/usb/` | USB HID integration and portable report encoding |
| `src/app/storage/` | Portable SD presence, mount-retry and ownership policy, and the optional benchmark engine |
| `src/app/wireless/` | Portable staging, command authentication and SHA-256/HMAC |
| `src/app/wireless/bluetooth/` | BLE GATT service and advertising |
| `src/app/wireless/wifi/` | Wi-Fi saved-network joins, candidate tests and scans |

Native tests are grouped by the same service roles.

## Host-native unit tests

ZephyrForge uses Ceedling 1.1.10 with Unity and CMock. Install Ruby 3.3,
Bundler 2.6.9 and a native GCC toolchain (`gcc` on PATH). These tests need neither
Zephyr nor target hardware. From the repository root:

```sh
gem install bundler -v 2.6.9
bundle config set --local path build/gems
bundle install
bundle exec ceedling test:all
```

`Gemfile` pins Ceedling and `Gemfile.lock` locks its dependencies. Commit the lockfile
when intentionally updating tools. Local gems, mock code, runners and test binaries
are ignored build products, not firmware dependencies.

```sh
# One suite
bundle exec ceedling test:control
bundle exec ceedling test:wireless_auth

# Regenerate runners/mocks and rebuild tests
bundle exec ceedling clobber test:all
```

The native build exercises portable production logic through narrow platform interfaces.
Zephyr stack integration, hardware sampling, SD mounting, flash access and clock application are excluded:

| Suite | Production logic exercised |
| --- | --- |
| Control | Shared opcode dispatcher: status/error replies, LED, MSC and benchmark admission, Wi-Fi base/key staging, test/cancel/scan, settings, reboot, resync and unsupported opcodes |
| Reports | Exact USB HID/BLE report byte layouts, little-endian fields and flags |
| Storage | SD presence debounce, paced mount retry, USB/firmware ownership handoff and card-change cancellation over mocked media operations |
| Benchmark | Temporary-file write/read verification and cancellation/cleanup safety over an in-memory backend |
| Authentication | HMAC-SHA-256 command authentication, nonces, replay rejection and key-change handling |
| Wireless staging | Staged Wi-Fi credentials, password rules, expiry, tested flag and key-only commits |
| Cryptography | SHA-256/HMAC vectors |
| Settings | [Boot settings](../reference/settings.md) record layout, CRC-32, validation, staging, discard, save failure and next-boot snapshots over an in-memory flash fake |
| Pico boards | Pico identity and board facts; non-W radio rejection; W Wi-Fi capability, scan admission and unsupported LED, settings-save and BLE key staging |
| SDIO codec | Pico PIO SD host command/response CRC7 framing, one- and four-bit data images with CRC16 and bus clock planning |

Platform hooks are CMock-generated mocks. **The tests do not compile the SD drivers, mount a card,
format, write or test SD storage**; the SDIO codec suite checks wire encoding only, not PIO timing.
The Zephyr application calls these same portable modules;
tests do not duplicate them. Native compilation enables C11 and treats compiler warnings as errors.

Add portable logic tests here and mock only narrow service interfaces. Do not recreate
Zephyr internals with large fake headers. A Zephyr native-simulator test covers blocking Wi-Fi request isolation,
DHCP-bound completion and replacement-candidate fencing using a dummy interface and bounded driver substitutes:

```sh
west build -b native_sim/native/64 firmware/tests/wifi/native -d build/wifi-native
build/wifi-native/zephyr/zephyr.exe
```

Run this from the repository root in an initialized workspace with the native toolchain available.
It does not exercise RF, radio firmware or the SPI bus. USB/BLE stacks, real Wi-Fi behavior,
flash programming, clock changes and boot remain outside this native suite.

The `firmware-unit` job in [Host Validation](../../.github/workflows/host-validation.yml)
runs the same command independently of the reusable [target builds](../../.github/workflows/firmware-build.yml).
Both must pass for firmware changes; a native test pass is not target-build or hardware evidence.

## Zephyr target build

`ZF_ENABLE_STORAGE_BENCHMARK` is a CMake option defaulting to `OFF`. The build wrapper currently supplies
`-DZF_ENABLE_STORAGE_BENCHMARK=ON`; it enables dashboard-controlled runs, not automatic writes.
For a manual build, choose `west build ... -- -DZF_ENABLE_STORAGE_BENCHMARK=OFF` to omit it.
An existing CMake cache retains its chosen value, so use a fresh build directory to verify the default.
See the [benchmark contract](../reference/sdcard.md#optional-storage-benchmark) before authorizing card writes.

Zephyr 4.3.2 and imported module commits are selected by `west.yml`; SDK 0.17.4 is the toolchain.
Workspace setup applies the tracked patches under `patches/zephyr/v4.3.2/` to that pinned Zephyr checkout.
The SD patch retains the genuine initialization CID and adds public `DISK_IOCTL_GET_SD_CID` (16-byte buffer in wire
byte order; `-ENODATA` without an initialized CID). It also propagates SPI CID/CSD read failures.
The pinctrl patch adds an optional Espressif `drive-strength` pin-group property (5, 10, 20 or 40 mA) for output
pads; omitted groups keep their current drive. Every target build uses the SD patch; only ESP32-S3 builds use the
pinctrl property.
Application uses a forward `git apply --check`, or a reverse check for an already-applied patch; mismatches stop setup.
For an already-initialized workspace, activate its venv and run
`bash tools/firmware/apply-zephyr-patches.sh "$(west list zephyr -f '{abspath}')"` before a manual target build.
The helper verifies the workspace checkout and exact pinned commit, including shallow workspaces without tags;
it never selects an arbitrary dependency checkout.
The wrappers bootstrap missing prerequisites on Debian/Ubuntu-compatible Linux, including Raspberry Pi OS.
Missing host packages are installed through `apt-get` with `sudo` when not root; a password may be requested.
On other systems, install the reported host prerequisites with your package manager and rerun the script.
If the checkout is not already in a workspace, its parent directory becomes the workspace; `.west`, `.venv`,
Zephyr and modules are placed there. An existing ancestor workspace is reused rather than nested.
The build and flash scripts create or reuse `.venv` at the workspace root and activate it for their subprocesses.
They install `west` and the workspace's Zephyr base and Espressif Python requirements on each invocation;
already satisfied dependencies are reused. Set `PYTHON` to choose the interpreter when creating the venv
(for example `PYTHON=python3.12`), or `VENV_DIR` to use another venv. An unrelated active venv is not selected automatically.
Activation does not change the calling shell; use `. .venv/bin/activate` from the workspace for manual `west` commands.
They update modules to the pinned manifest, fetch the Espressif radio blobs for ESP32-S3 builds and install SDK 0.17.4
with the selected board's toolchain (`xtensa-espressif_esp32s3_zephyr-elf` or `arm-zephyr-eabi`) if no SDK with that
toolchain is found in `ZEPHYR_SDK_INSTALL_DIR`, the home directory or `/opt`.
First use requires network access and several GB of free space. Installation and build errors stop execution;
no upload is attempted after a failed build. See [manual workspace setup](../guides/getting-started.md#1-prepare-a-zephyr-workspace)
if you prefer to provision dependencies yourself.
Run from the checkout:

```sh
bash tools/firmware/build.sh
```

The default image uses native four-bit SDMMC. Build the alternate SD-over-SPI image with:

```sh
bash tools/firmware/build.sh --storage sd-spi
```

Outputs are separate: `build/firmware/zephyr/zephyr.bin` (native) and
`build/firmware-sd-spi/zephyr/zephyr.bin` (SPI). `--storage sdmmc` explicitly selects the default.
Both retain card detection, FAT mounting and explicit USB drive handoff; neither formats a card.
Use the matching [SD wiring](../reference/sdcard.md#pin-allocation) and disconnect card power when changing buses.

`--board` selects the target: `esp32s3` (the default), `pico`, `pico_w`, `pico2` or `pico2_w`, which build the
Zephyr targets `rpi_pico/rp2040`, `rpi_pico/rp2040/w`, `rpi_pico2/rp2350a/m33` and `rpi_pico2/rp2350a/m33/w`.
Each Pico board has the same two storage images:

```sh
bash tools/firmware/build.sh --board pico
bash tools/firmware/build.sh --board pico2_w --storage sd-spi
```

Pico outputs are UF2 images in a board-specific directory, for example `build/firmware-pico/zephyr/zephyr.uf2`
and `build/firmware-pico2_w-sd-spi/zephyr/zephyr.uf2`. Non-W Pico builds need no binary blobs.
The W wrapper fetches the CYW43439 Wi-Fi firmware and Murata-1YN CLM blob from the pinned Infineon HAL.

Host Validation also compiles the report and control tests for SPI with benchmarking enabled:

```sh
bundle exec ceedling --mixin '={project: {build_root: build/ceedling-sd-spi}, defines: {test: [ZF_STORAGE_SPI=1, ZF_ENABLE_STORAGE_BENCHMARK=1]}}' test:test_hid_reports test:test_control
```

For changes to board/configuration selection, use a pristine build from the workspace:

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu zephyrforge/firmware \
  -d zephyrforge/build/firmware -- \
  -DCONF_FILE="prj.conf;wifi.conf;bluetooth.conf;boards/espressif/esp32s3_devkitc_n16r8.conf" \
  -DDTC_OVERLAY_FILE="boards/espressif/esp32s3_devkitc_n16r8.overlay;boards/espressif/sdmmc.overlay"
```

Inspect generated `build/firmware/zephyr/.config` and `zephyr.dts`, not just source intent.
Both FAT mkfs options must remain disabled. A successful build does not qualify card wiring or mounting.
Manual ESP32-S3 builds apply the N16R8 board overlay before the storage overlay.
ESP32-S3 and W Pico builds share Wi-Fi application configuration. ESP32-S3 additionally selects the common
Bluetooth configuration and N16R8-specific memory, coexistence, TCP shell and LED settings.
Non-W Pico builds use only the shared application configuration plus Zephyr's board and driver defaults.
For SPI, replace `boards/espressif/sdmmc.overlay` with `boards/espressif/sd_spi.overlay`, retaining the board overlay
and configuration files and using a separate build directory.
Manual Pico builds list the storage overlay, then the MCU overlay, then the radio overlay for W boards:

```sh
west build -p always -b rpi_pico/rp2040/w zephyrforge/firmware -d zephyrforge/build/firmware-pico_w -- \
  -DCONF_FILE="prj.conf;wifi.conf;boards/rpi_pico/w.conf" \
  -DDTC_OVERLAY_FILE="boards/rpi_pico/sdmmc.overlay;boards/rpi_pico/rp2040.overlay;boards/rpi_pico/w.overlay"
```

Use `sd_spi.overlay` for SPI and `rp2350.overlay` for the Pico 2 boards, and omit `w.overlay` on boards without a
radio, using only `prj.conf` for those non-W builds. When bypassing the wrapper, fetch the W radio blobs first:

```sh
west blobs fetch hal_infineon --allow-regex 'img/whd/resources/(firmware/COMPONENT_43439/43439A0\.bin|clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0\.clm_blob)$'
```
The management protocol runs over the board's native USB connector (USB HID, VID:PID `CAFE:4021`)
and, on the ESP32-S3, BLE GATT.
The UART carries the Zephyr diagnostic shell/log output and, on the ESP32-S3, the ROM bootloader used for flashing;
it has no dashboard management protocol. Shell commands and TCP exposure are defined in the
[console guide](../guides/console.md).

The reusable build also freezes the resolved west manifest and attaches binaries, configuration,
dependency licenses and software-only flashing instructions. PRs and tagged releases use this same
build path. An optional `ZEPHYRFORGE_VERSION=MAJOR.MINOR.PATCH` environment override sets the firmware
version for release builds; an empty/unset override preserves the development default.
Invalid overrides fail configuration rather than silently using another version.
See [release automation](../deployment/release.md) for package contents and preflight.

## USB upload wrapper

Use [the USB flashing instructions](../guides/getting-started.md#2-build-now-flash-later).
`tools/firmware/flash.sh` wraps `west flash` with the ESP32 runner, 460800 upload baud,
`--skip-rebuild` and `--no-erase`. If `--port PORT` is omitted, it selects a likely USB-UART
port, preferring Silicon Labs CP210x devices; tied candidates require an explicit port.
`--baud` overrides upload speed; `--dry-run` validates artifacts and prints the command
without connecting to or flashing a device. Missing or empty required build artifacts trigger a build first;
complete existing builds are reused. Select `--storage sd-spi` for the alternate image, or `--storage sdmmc`
for native. Builds with the wrong board or storage driver are rejected. Runner errors propagate to the caller.
The wrapper flashes only the ESP32-S3; copy Pico UF2 images to the BOOTSEL drive as described in the
[flashing instructions](../guides/getting-started.md#2-build-now-flash-later).

`--dry-run` also prepares dependencies and builds missing firmware.
Use `--help` and `--dry-run` to inspect it before uploading to hardware.
Software-only wrapper tests use mocked installers and runners:
`python3 -m unittest discover -s firmware/tests/tooling -v`.

Do not connect an SD card or run storage tests for software-only setup.
Compilation does not validate initialization, USB enumeration, BLE/Wi-Fi coexistence, or filesystem reliability.
Record software evidence separately from future hardware observations in [validation records](../validation/records/README.md).

## Target qualification

Use the [firmware qualification procedure](../validation/firmware-qualification.md) for opt-in USB target checks,
SD/MSC card matrices and USB/BLE/Wi-Fi lifecycle stress. Its runner reuses the installed Python host package;
software-only safety tests run with `python3 -m unittest discover -s firmware/tests/hardware -v`.
Neither these safety tests nor successful target builds supply physical qualification evidence.
