# Firmware development

[Documentation](../README.md) / Development / Firmware

For workspace setup, board selection, and image outputs, use [firmware builds](firmware-build.md).
This guide covers application changes and the checks to run before submitting them.

- [Place a change](#where-to-put-a-change)
- [Run portable unit tests](#host-native-unit-tests) and [feature-gate checks](#storage-feature-gates)
- [Run Zephyr native tests](#zephyr-native-wi-fi-tests)
- [Check builds and tooling](#build-and-tooling-checks)
- [Board-specific ESP32-S3 LED integration](#esp32-s3-rgb-led-integration)

## Where to put a change

Keep portable policy separate from Zephyr drivers and callbacks. Product-specific work belongs in its own service,
not in the management loop or under the application lock. Read the
[architecture](architecture.md) before changing startup, concurrency, or resource ownership.

Group code by its owner, not by the transport that happens to use it. `src/` is one level deep: a folder is either a
layer (`app/`, `management/`, `protocol/`, `platform/`, `transport/`, `drivers/`) or a feature large enough to need
split files (`settings/`, `storage/`, `benchmark/`, `wifi/`). Add a small component to the layer it belongs to as a
file pair rather than a new folder, and do not create subfolders; file-name prefixes such as `usb_` and `ble_` group
related files. Feature folders expose a narrow service header, such as `storage/storage.h`, and keep their policy,
worker and hardware adapters together. `management/`
composes service snapshots, including the image's build facts, and handles commands; `protocol/` only defines
shared payload types and encodes bytes, and never depends on build configuration.
Wi-Fi is a station service, while Bluetooth is a management transport.

Name files and APIs after their responsibility. Keep small, single-responsibility features in one file.
For split implementations, use `_engine` or `_policy` for portable logic, `_service` for admission and scheduling, and
`_media` for physical I/O. A disabled feature replaces its prototypes with `static inline` stubs in its service header,
behind the same `CONFIG_ZF_*` Kconfig symbol, so calls compile away and no stub source is linked. Public functions are prefixed
with their owner: the folder for a feature (`zf_storage_*`, `zf_wifi_*`), or the file's component name inside a layer
folder (`zf_control_*`, `zf_board_*`, `zf_usb_device_*`, `zf_sha256_*`, `zf_sdio_*`). Management report composition uses
`zf_management_*` and wire encoding `zf_report_*`. Hooks between split files also name the implementing role, such as
`zf_storage_media_open()`, `zf_storage_service_publish()` and `zf_storage_export_attach()`. Includes are qualified from
`src/`, for example `"storage/storage.h"`.

The benchmark owns its engine and FatFs media adapter; storage grants exclusive media ownership rather than
implementing benchmark file operations. Board integration supplies hardware snapshots without reading saved settings or
encoding management reports. Radio-switch bits and the SSID limit are wire values owned by the protocol; settings owns
credential storage sizes, and `app/capabilities.h` derives the radios built into the image.

`app/` owns startup, runtime synchronization, version and build capabilities. Kconfig selects target features;
portable test definitions exercise the same feature gates. A helper lives beside its only consumer, as SHA-256 does
beside Bluetooth authentication, until a second owner needs it. `drivers/` holds implementations of Zephyr APIs.

| Directory under `firmware/` | Responsibility |
| --- | --- |
| `Kconfig` | Module switches and the Zephyr subsystem defaults each module needs |
| `boards/` | Pin assignments, memory layout, and hardware overlays |
| `src/app/` | Startup, management loop, application lock/wakeup, version and build capabilities |
| `src/management/` | Control admission/results, credential staging, telemetry sampling and report composition |
| `src/protocol/` | Wire layouts, shared payload types and stateless report encoding |
| `src/settings/` | Portable boot-settings record; separate flash read and async write adapters |
| `src/platform/` | Board facts (UID, reset cause, snapshots, reboot), per-SoC clock and memory inventory, and LED |
| `src/transport/` | USB device/HID and MSC integration (`usb_*`); BLE GATT, authentication and SHA-256 (`ble_*`, `sha256`) |
| `src/wifi/` | Station lifecycle, driver adapter that hides vendor event differences, portable policy/scan handling |
| `src/storage/` | SD ownership policy, worker/admission and disk/FAT media lifecycle |
| `src/benchmark/` | Portable benchmark engine, request/status service and FatFs media adapter |
| `src/drivers/` | Zephyr API implementations: the Pico PIO SD host with its SDIO codec, and the Pico W random source |
| `tests/unit/` | Portable Ceedling suites, mirroring `src/` |
| `tests/native/` | Zephyr `native_sim` suites |
| `tests/hardware/` | Opt-in firmware hardware tests |

Repository-wide build, package, documentation and tooling tests live at `tests/tools/`.

Add a unit test under the matching `tests/unit/` folder. Mock narrow service interfaces rather than recreating
Zephyr internals with fake headers. Changes to reports or commands also need matching host and specification updates.
Keep board wiring in overlays, not runtime board code. Portable modules must not depend on transport implementations;
USB and BLE both consume the shared protocol. Disabled-service stubs belong in their service header rather than
inside a particular board's hooks. Add a module as one `CONFIG_ZF_*` symbol that selects its Zephyr subsystems,
as described in [optional modules](firmware-build.md#optional-modules); do not add a second switch for it.
The Pico W random source is suitable for noncryptographic network identifiers only; authentication challenges
must use a cryptographically secure source.

### Worked example: benchmark size guard

The portable benchmark rejects a zero or oversized request before touching storage. Its focused `test_benchmark_engine`
suite checks the failure result and confirms that no files were opened or deleted:

```sh
ceedling test:test_benchmark_engine
```

Images include this module when `CONFIG_ZF_STORAGE_BENCHMARK` is set, its default when storage and USB HID are
built; the build wrapper can omit it with `--without benchmark`. In a disabled build, the `test_control` suite
checks that the command returns unsupported, and `test_report_builder` checks that the capability is not advertised.
Run the enabled and disabled [feature-gate checks](#storage-feature-gates) as well. See
[optional modules](firmware-build.md#optional-modules) for build selection.

## Host-native unit tests

Portable C tests use Ceedling, Unity, and CMock; they need neither Zephyr nor hardware.
Install Ruby 3.3 and GCC (`gcc` on `PATH`), then install the latest Ceedling. From the repository root:

```sh
gem install ceedling
ceedling test:all
```

Generated mocks, runners, and test binaries stay in ignored build directories.

For a focused check or a clean rebuild:

```sh
ceedling test:test_control
ceedling test:test_ble_auth
ceedling clobber test:all
```

The suites cover control admission and results, report layouts, storage ownership, benchmark cancellation,
credential staging, authentication, settings records, board facts, Wi-Fi policy, and SDIO encoding. They compile
production portable modules as C11 with warnings treated as errors. Flash and media operations use test doubles;
the tests do not mount, format, or write a real card.

### Storage feature gates

Run the report-composition and control suites with SPI, no storage, and storage without USB mass storage.

```sh
ceedling --mixin '={project: {build_root: build/ceedling-sd-spi}, defines: {test: [CONFIG_SPI_SDHC=1]}}' test:test_report_builder test:test_control
ceedling --mixin '={project: {build_root: build/ceedling-no-storage}, defines: {test: [CONFIG_ZF_STORAGE=0, CONFIG_ZF_USB_MSC=0, CONFIG_ZF_STORAGE_BENCHMARK=0]}}' test:test_report_builder test:test_control
ceedling --mixin '={project: {build_root: build/ceedling-no-msc}, defines: {test: [CONFIG_ZF_USB_MSC=0]}}' test:test_report_builder test:test_control
```

The no-MSC configuration retains storage while checking that card-export commands are unsupported.
The portable disabled-Wi-Fi suite also checks BLE and credential-staging reports without a network driver.

## Zephyr native Wi-Fi tests

These suites use the real application and Zephyr scheduling with a dummy interface and controlled driver responses.
From the repository root in an initialized workspace with the native toolchain available:

```sh
west build -b native_sim/native/64 firmware/tests/native/wifi -d build/native-wifi
build/native-wifi/zephyr/zephyr.exe
west build -b native_sim/native/64 firmware/tests/native/wifi-esp32 -d build/native-wifi-esp32
build/native-wifi-esp32/zephyr/zephyr.exe
west build -b native_sim/native/64 firmware/tests/native/wifi-recovery -d build/native-wifi-recovery
build/native-wifi-recovery/zephyr/zephyr.exe
```

- The AIROC-style worker suite checks blocking request isolation, DHCP-bound completion, candidate replacement,
  and scan completion during queue overflow.
- The ESP32-style suite checks disconnect reconciliation and paced retries after a failed status observation.
- The recovery suite checks explicit cleanup after AIROC interface-down, including queue overflow, and fences
  saved-network joins while a timed-out scan still owns the driver.

These are not radio tests: RF, radio firmware, the Pico shared SPI bus, and real DHCP behavior remain untested here.

## Build and tooling checks

Use [firmware builds](firmware-build.md) for target commands and generated-configuration checks. Native tests do not
compile every target driver. Pico builds also regenerate the SD PIO header with the manifest-selected assembler
and compare it with the checked-in output, ignoring only the assembler version banner.

The build, flash, packaging, and workspace wrappers have software-only tests:

```sh
python3 -m unittest discover -s tests/tools -v
```

[Host Validation](../../.github/workflows/host-validation.yml) runs the portable and native Wi-Fi suites separately
from the reusable [target builds](../../.github/workflows/firmware-build.yml).

## Target qualification

Use the [firmware qualification procedure](../validation/firmware-qualification.md) for USB, storage,
BLE, and Wi-Fi checks. See the [USB runner](../validation/firmware-qualification.md#runnable-usb-target-checks)
for installed-image checks and [runner unit tests](../validation/firmware-qualification.md#runner-unit-tests)
for software-only checks.

Builds and software tests do not flash boards, write cards, or perform fault injection. Record physical results
separately in dated [validation records](../validation/README.md#validation-records).

## ESP32-S3 RGB LED integration

The WS2812-compatible LED uses SPI2 MOSI only. Keep SPI2's output idle-low: the ESP32 default high idle level
prevents the required low reset interval. The driver sends eight-bit transfers at up to 6.4 MHz, uses a MOSI
pull-down and holds the reset interval low for 350 microseconds. The LED starts Off at low fixed intensity; a
transmission error disables control until reboot. Keep the configured pin and reported board facts aligned when
supporting another board revision.
