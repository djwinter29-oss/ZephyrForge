# Validation and qualification

[Documentation](../README.md) / Validation

This page summarizes recorded hardware results and the checks still needed. ESP32-S3 N16R8 has limited functional
checks and measurements for selected setups. Pico-family and ESP32-C6 images have software checks only; none of
these targets has production qualification.

- [Board coverage](#board-coverage) and [open S3 checks](#esp32-s3-checks-still-needed)
- [Run and record qualification](firmware-qualification.md)
- [Dated validation records](#validation-records)

## Board coverage

| Target | Hardware evidence |
| --- | --- |
| ESP32-S3-DevKitC-1 N16R8 | Flashing and boot reported working in the [2026-11-14 report](records/2026-11-14-hardware-tests.md). That report lacks firmware revision, image hashes, and PSRAM memory-test output. The later [native SDMMC record](records/2026-11-20-sdmmc-performance.md) identifies its flashed image by hash. |
| ESP32-C6-DevKitC-1 N8 | No recorded flashing, boot, BLE, Wi-Fi, or card checks on hardware. [First-key commissioning](../guides/esp32c6-commissioning.md) has software tests only. |
| Pico, Pico W, Pico 2, Pico 2 W | No recorded UF2 flashing, boot, USB HID/MSC, UART, PIO SD, SPI storage, or W-board Wi-Fi checks on hardware. |

## ESP32-S3 checks still needed

"Reported working" below refers to the [functional hardware report](records/2026-11-14-hardware-tests.md), not
an independently measured stress test. Linked follow-up records identify their own images, equipment, and limits.

| Area | Recorded result | Still to test or record |
| --- | --- | --- |
| USB HID | Dashboard connection reported working; [sampled reads and handle reopens](records/2026-11-20-firmware-qualification.md) measured | Broader timing, host compatibility, and physical reconnects |
| BLE | Monitoring and authenticated control reported working | MTU variation, reconnect stress, and radio coexistence |
| Wi-Fi | Provisioning and connection reported working; [scan stress](records/2026-11-20-firmware-qualification.md) measured | Failure recovery, lease renewal, coexistence, and long-duration behavior |
| Boot settings | Save and reboot reported working | Interrupted erase/program, endurance, and individual CPU-clock profiles |
| SD card | Native SDMMC, SPI, detection, and replacement reported working; [write failures and the 10 mA pad-drive fix](records/2026-11-20-sd-write-reliability.md) recorded on one card | Card/wiring matrix, sustained I/O, and fault recovery |
| USB MSC | Handoff and return reported working | OS/card matrix, repeated cycles, data integrity, and interrupted handoff |
| Diagnostic shells | UART and Wi-Fi TCP shells reported working | Admission/reconnect stress and concurrent HID/BLE responsiveness |
| Storage benchmark | [Installed-image](records/2026-11-20-firmware-qualification.md) and [40 MHz image](records/2026-11-20-sdmmc-performance.md) checks recorded on one card | Removal/fault cleanup and additional cards |
| Onboard RGB LED | LED controls reported working | Exact board revision/pin, waveform timing, and optical measurements |
| Module memory inventory | Live diagnostics reported working | Raw PSRAM/flash values, reservations, and memory-accounting checks |
| Dashboards | Live USB HID connection and diagnostics reported working | Per-dashboard/OS/browser hardware matrix and raw dashboard measurements |

Power-loss, soak, performance, RF, EMC, and regulatory qualification remain open. One functional report or one
card's benchmark does not establish compatibility with other hardware or workloads.
SD pin allocations and pad-drive settings are unqualified outside recorded setups; Pico storage, including the PIO
host, has no hardware records.

## What software checks establish

The [firmware tests](../development/firmware.md) cover portable policy and report handling with fake flash and
platform hooks, and Wi-Fi lifecycles with controlled Zephyr native interfaces. Portable SD encoding tests do not
exercise PIO timing. [Host tests](../development/host-tools.md) cover codecs, command recovery, and dashboard behavior
with fake devices. Benchmark safety checks use simulated media.

These checks help catch software regressions; they do not establish electrical behavior, over-the-air performance,
or physical reconnect reliability.

## Recording further checks

Use the [firmware qualification procedure](firmware-qualification.md) for safety prerequisites, opt-in target checks,
matrices, and acceptance thresholds. Record firmware/image identity, host/tool versions, board and card details,
procedures, durations, and observed results in a new dated record. Use backups and a dedicated expendable card for
destructive or fault-injection tests.

Add only observed results to dated records; the procedure is a plan, not evidence that a check passed.

## Validation records

These are past observations for their stated revision, configuration, tools, and test limits, not installation
instructions or evidence for the current release as a whole.

### Hardware and target checks

- 2026-11-20: [Native SDMMC clock and throughput](records/2026-11-20-sdmmc-performance.md)
- 2026-11-20: [Firmware software gates and qualification checks](records/2026-11-20-firmware-qualification.md)
- 2026-11-20: [Native SDMMC write reliability](records/2026-11-20-sd-write-reliability.md)
- 2026-11-14: [Functional hardware test report](records/2026-11-14-hardware-tests.md)

### Initial software checks (2026-11-07)

- [Initial software setup](records/2026-11-07-setup.md)
- [Native firmware unit-test setup](records/2026-11-07-ceedling.md)
- [USB upload wrapper](records/2026-11-07-usb-upload.md)
- [Direct dashboard completion](records/2026-11-07-direct-dashboard.md)
- [Direct dashboard independence](records/2026-11-07-direct-independence.md)
- [Shared bridge dashboard layout](records/2026-11-07-shared-web-layout.md)
- [Python host package layout](records/2026-11-07-python-package-layout.md)
- [Boot settings](records/2026-11-07-boot-settings.md)
- [USB HID, BLE and Wi-Fi implementation](records/2026-11-07-hid-ble-implementation.md)
- [N16R8 flash and PSRAM](records/2026-11-07-n16r8-flash-psram.md)
