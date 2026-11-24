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
Zephyr internals with large fake headers. A Zephyr native-simulator suite covers blocking Wi-Fi request isolation,
DHCP-bound completion and replacement-candidate fencing using a dummy interface and bounded driver substitutes.
The non-AIROC suite models ESP32 disconnect recovery and verifies that a status-query error does not declare an
established link down. A separate recovery test verifies that automatic saved-network joins wait for timed-out scans:

```sh
west build -b native_sim/native/64 firmware/tests/wifi/native -d build/wifi-native
build/wifi-native/zephyr/zephyr.exe
west build -b native_sim/native/64 firmware/tests/wifi/native-esp32 -d build/wifi-native-esp32
build/wifi-native-esp32/zephyr/zephyr.exe
west build -b native_sim/native/64 firmware/tests/wifi/native-recovery -d build/wifi-native-recovery
build/wifi-native-recovery/zephyr/zephyr.exe
```

Run this from the repository root in an initialized workspace with the native toolchain available.
It does not exercise RF, radio firmware or the SPI bus. USB/BLE stacks, real Wi-Fi behavior,
flash programming, clock changes and boot remain outside this native suite.
Pico target builds also regenerate the SD PIO header with the manifest-selected Pico HAL assembler and compare it
with the checked-in output, ignoring only the assembler version banner.

The [Host Validation workflow](../../.github/workflows/host-validation.yml) runs all native Wi-Fi suites,
independently of the reusable [target builds](../../.github/workflows/firmware-build.yml). A native test pass is not
target-build or hardware evidence.

## Zephyr target build

Board-family targets, storage profiles, build outputs, manual configuration and flashing are maintained in the
[firmware build guide](./firmware-build.md). It covers ESP32-S3 and the Pico family separately.

The management protocol runs over the board's native USB connector (USB HID, VID:PID `CAFE:4021`) and, on the
ESP32-S3, BLE GATT. The UART carries Zephyr diagnostic shell/log output and, on the ESP32-S3, the ROM bootloader
used for flashing; it has no dashboard management protocol. See the [console guide](../guides/console.md).

For the SPI report/control test variant, run:

```sh
bundle exec ceedling --mixin '={project: {build_root: build/ceedling-sd-spi}, defines: {test: [ZF_STORAGE_SPI=1, ZF_ENABLE_STORAGE_BENCHMARK=1]}}' test:test_hid_reports test:test_control
```

## USB upload wrapper

See the [firmware build guide](./firmware-build.md) for ESP32-S3 storage profiles and the
[USB flashing instructions](../guides/getting-started.md#2-build-now-flash-later). The wrapper does not flash Pico
UF2 images.

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
