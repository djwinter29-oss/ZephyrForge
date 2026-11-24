# Qualification gaps

This page summarizes recorded hardware results and the checks still needed. ESP32-S3 N16R8 has limited functional
checks and measurements for selected setups. Pico-family and ESP32-C6 images have software checks only; none of
these targets has production qualification.

## Board coverage

| Target | Hardware evidence |
| --- | --- |
| ESP32-S3-DevKitC-1 N16R8 | Flashing and boot reported working in the [2026-11-14 report](records/2026-11-14-hardware-tests.md). That report lacks firmware revision, image hashes, and PSRAM memory-test output. The later [native SDMMC record](records/2026-11-20-sdmmc-performance.md) identifies its flashed image by hash. |
| ESP32-C6-DevKitC-1 N8 | No recorded flashing, boot, BLE, Wi-Fi, or card checks on hardware. [First-key commissioning](../guides/boards/esp32c6-commissioning.md) has software tests only. |
| Pico, Pico W, Pico 2, Pico 2 W | No recorded UF2 flashing, boot, USB HID/MSC, UART, PIO SD, SPI storage, or W-board Wi-Fi checks on hardware. |

Other board variants are not supported targets: ESP32-S3 N4/N8 and octal-flash modules need different configuration,
as do other S3 boards, C6 boards other than the configured DevKitC-1 N8, and Pico-compatible boards outside the four
listed above.

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

## Product and security limits

- Pico W images have no BLE transport. C6 has no native USB HID dashboard path or USB-controlled benchmark.
- BLE Control keys authenticate commands, but do not encrypt the link. Saved credentials are unencrypted in flash.
  USB management and UART commissioning rely on physical access, and the diagnostic TCP shell is unauthenticated
  plaintext. Local hosts stay on loopback. See [wireless security boundaries](../guides/wireless.md#security-boundaries).

## Recording further checks

Use the [firmware qualification procedure](firmware-qualification.md) for safety prerequisites, opt-in target checks,
matrices, and acceptance thresholds. Record firmware/image identity, host/tool versions, board and card details,
procedures, durations, and observed results in a new dated record. Use backups and a dedicated expendable card for
destructive or fault-injection tests.

Add only observed results to dated records; the procedure is a plan, not evidence that a check passed.
