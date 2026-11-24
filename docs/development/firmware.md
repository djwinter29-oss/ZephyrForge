# Firmware development

For workspace setup, board selection, and image outputs, use [firmware builds](firmware-build.md).
This guide covers application changes and the checks to run before submitting them.

## Where to put a change

Keep portable policy separate from Zephyr drivers and callbacks. Product-specific work belongs in its own service,
not in the management loop or under the application lock. Read the
[architecture](../reference/architecture.md) before changing startup, concurrency, or resource ownership.

| Directory under `firmware/` | Responsibility |
| --- | --- |
| `boards/` and `profiles/` | Pin assignments, memory layout, and build-selected features |
| `src/config/` | Board identity, facts, shared locking, and delayed reboot |
| `src/platform/` | Settings persistence and Zephyr/platform integration |
| `src/platform/rpi_pico/` | Pico hooks, PIO SD host, and portable SDIO codec |
| `src/app/control/` and `src/app/usb/` | Shared control policy, report encoding, and USB transport |
| `src/app/storage/` | Portable media ownership/mount policy and benchmark engine |
| `src/app/wireless/` | Staging, authentication, Wi-Fi, and BLE |
| `tests/` | Portable suites, Zephyr native tests, tooling checks, and opt-in hardware tests |

Add tests beside the relevant component. Mock narrow service interfaces rather than recreating Zephyr internals
with fake headers. Changes to reports or commands also need matching host and specification updates.

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
bundle exec ceedling test:control
bundle exec ceedling test:wireless_auth
bundle exec ceedling clobber test:all
```

The suites cover control admission and results, report layouts, storage ownership, benchmark cancellation,
credential staging, authentication, settings records, board facts, Wi-Fi policy, and SDIO encoding. They compile
production portable modules as C11 with warnings treated as errors. Flash and media operations use test doubles;
the tests do not mount, format, or write a real card.

Run report/control tests in both storage configurations when changing feature gates:

```sh
bundle exec ceedling --mixin '={project: {build_root: build/ceedling-sd-spi}, defines: {test: [ZF_STORAGE_SPI=1, ZF_ENABLE_STORAGE_BENCHMARK=1]}}' test:test_hid_reports test:test_control
bundle exec ceedling --mixin '={project: {build_root: build/ceedling-no-storage}, defines: {test: [ZF_ENABLE_STORAGE=0, ZF_ENABLE_STORAGE_BENCHMARK=0]}}' test:test_hid_reports test:test_control
```

## Zephyr native Wi-Fi tests

These suites use the real application and Zephyr scheduling with a dummy interface and controlled driver responses.
From the repository root in an initialized workspace with the native toolchain available:

```sh
west build -b native_sim/native/64 firmware/tests/wifi/native -d build/wifi-native
build/wifi-native/zephyr/zephyr.exe
west build -b native_sim/native/64 firmware/tests/wifi/native-esp32 -d build/wifi-native-esp32
build/wifi-native-esp32/zephyr/zephyr.exe
west build -b native_sim/native/64 firmware/tests/wifi/native-recovery -d build/wifi-native-recovery
build/wifi-native-recovery/zephyr/zephyr.exe
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

Use the [firmware qualification procedure](../validation/firmware-qualification.md) for authorized USB, storage,
BLE, and Wi-Fi checks. Its runner reuses the installed Python host package. The runner's own safety checks need
no hardware:

```sh
python3 -m unittest discover -s firmware/tests/hardware -v
```

Building or running software tests does not authorize flashing, card writes, or fault injection. Record physical
results separately in dated [validation records](../validation/records/README.md).
