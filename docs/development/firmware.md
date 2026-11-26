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

A module is one folder under `src/` with a main header of the same name, such as `storage/storage.h`. Its other
files add a suffix to that name (`storage_msc.c`), and every public function and type starts with the module prefix
(`zf_storage_*`). A module may contain submodules that follow the same rule, such as `wireless/wifi/`. Disabled
Wi-Fi, Bluetooth and storage build the module's `*_none.c` file against the same header. A module declares the hooks
it needs and the owning module implements them: the benchmark declares `zf_benchmark_media_*` for storage to
provide. `report/report.h` also defines the decoded content of each report, named after the report
(`zf_storage_t`), so modules and transports share one layout. Smaller features, such as USB mass storage and the
benchmark, use `ZF_ENABLE_*` values from `module_config.h`; `capabilities.h` combines these with the selected
board and radio configuration for report identity and command admission. Shared radio and SSID values live in
`wireless/wireless_types.h`, independently of saved settings and report encoding.

`lib/` and `drivers/` are not modules. `lib/` holds portable helpers named after their subject (`sha256.h`,
`zf_sha256_*`). `drivers/` holds implementations of Zephyr APIs, grouped by Zephyr subsystem.

| Directory under `firmware/` | Responsibility |
| --- | --- |
| `Kconfig` | Module switches and the Zephyr subsystem defaults each module needs |
| `boards/` | Pin assignments, memory layout, and hardware overlays |
| `src/` | `main.c` startup and management loop; `zephyrforge.h` version and application lock |
| `src/report/` | Portable report layouts and encoders shared by USB, BLE and policy |
| `src/control/` | Portable command validation, admission and results; `control_stage` credential staging |
| `src/settings/` | Portable boot-settings record; `settings_flash` persistence |
| `src/board/` | Board identity, memory and configuration facts, and delayed reboot |
| `src/sysclk/` | CPU clock profiles, applied by one implementation per SoC family |
| `src/led/` | Status LED requests and state |
| `src/telemetry/` | Runtime telemetry sampling |
| `src/usb/` | Zephyr USB device stack, HID transport, descriptors and publication |
| `src/storage/` | SD card service; `storage_policy` mount/export policy; `storage_msc` USB mass-storage export |
| `src/benchmark/` | Storage benchmark engine |
| `src/wireless/` | Wireless Status report; `wifi/` and `ble/` services with their disabled variants |
| `src/lib/` | Portable helpers with no project dependencies |
| `src/drivers/` | Zephyr API implementations: the Pico PIO SD host with its SDIO codec, and the Pico W random source |
| `tests/unit/` | Portable Ceedling suites, mirroring `src/` |
| `tests/native/` | Zephyr `native_sim` suites |
| `tests/tooling/`, `tests/hardware/` | Build-tool checks and opt-in hardware tests |

Add a unit test under the matching `tests/unit/` folder. Mock narrow service interfaces rather than recreating
Zephyr internals with fake headers. Changes to reports or commands also need matching host and specification updates.
Keep board wiring in overlays, not runtime board code. Portable modules must not depend on transport implementations;
USB and BLE both consume the shared protocol. Disabled-service implementations belong with their service rather than
inside a particular board's hooks. Add a module as one `CONFIG_ZF_*` symbol that selects its Zephyr subsystems,
as described in [optional modules](firmware-build.md#optional-modules); do not add a second switch for it.
The Pico W random source is suitable for noncryptographic network identifiers only; authentication challenges
must use a cryptographically secure source.

### Worked example: benchmark size guard

The portable benchmark rejects a zero or oversized request before touching storage. Its focused `test_benchmark`
suite checks the failure result and confirms that no files were opened or deleted:

```sh
bundle exec ceedling test:test_benchmark
```

Images include this module when `CONFIG_ZF_STORAGE_BENCHMARK` is set, its default when storage and USB HID are
built; the build wrapper can omit it with `--without benchmark`. In a disabled build, the `test_control` suite
checks that the command returns unsupported, and `test_report` checks that the capability is not advertised.
Run the enabled and disabled [feature-gate checks](#storage-feature-gates) as well. See
[optional modules](firmware-build.md#optional-modules) for build selection.

## Host-native unit tests

Portable C tests use Ceedling, Unity, and CMock; they need neither Zephyr nor hardware.
Install Ruby 3.3, Bundler 2.6.9, and GCC (`gcc` on `PATH`). From the repository root:

```sh
gem install bundler -v 2.6.9
bundle config set --local path build/gems
bundle install
bundle exec ceedling test:all
```

`Gemfile` pins Ceedling and `Gemfile.lock` locks its dependencies. Update the lockfile when changing tools.
Generated mocks, runners, test binaries, and installed gems stay in ignored build directories.

For a focused check or a clean rebuild:

```sh
bundle exec ceedling test:test_control
bundle exec ceedling test:test_ble_auth
bundle exec ceedling clobber test:all
```

The suites cover control admission and results, report layouts, storage ownership, benchmark cancellation,
credential staging, authentication, settings records, board facts, Wi-Fi policy, and SDIO encoding. They compile
production portable modules as C11 with warnings treated as errors. Flash and media operations use test doubles;
the tests do not mount, format, or write a real card.

### Storage feature gates

Run the report and control suites with SPI, no storage, and storage without USB mass storage.

```sh
bundle exec ceedling --mixin '={project: {build_root: build/ceedling-sd-spi}, defines: {test: [ZF_STORAGE_SPI=1, ZF_ENABLE_STORAGE_BENCHMARK=1]}}' test:test_report test:test_control
bundle exec ceedling --mixin '={project: {build_root: build/ceedling-no-storage}, defines: {test: [ZF_ENABLE_STORAGE=0, ZF_ENABLE_STORAGE_BENCHMARK=0]}}' test:test_report test:test_control
bundle exec ceedling --mixin '={project: {build_root: build/ceedling-no-msc}, defines: {test: [ZF_ENABLE_USB_MSC=0]}}' test:test_report test:test_control
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
python3 -m unittest discover -s firmware/tests/tooling -v
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
