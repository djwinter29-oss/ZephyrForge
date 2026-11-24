# Qualification gaps

Functional hardware testing on ESP32-S3-DevKitC-1 N16R8 has been reported with no issues in the listed checks.
See the [hardware test report](records/2026-11-14-hardware-tests.md) for coverage, provenance and missing configuration
details. This is functional evidence, not production qualification.

| Area | Current boundary |
| --- | --- |
| ESP32-S3 target | N16R8 flashing and boot reported working; firmware revision, image hashes and PSRAM memory-test output not recorded |
| ESP32-C6 target | No-storage and SD-over-SPI images are software builds only; no flashing, boot, BLE, Wi-Fi or card behavior has hardware evidence. Fresh-board authenticated BLE setup is blocked because there is no USB HID key-provisioning path |
| Raspberry Pi Pico targets | Pico, Pico W, Pico 2 and Pico 2 W images are software builds only; UF2 flashing, boot, USB HID/MSC, the UART console, the PIO SD host, SPI storage and W Wi-Fi have no hardware evidence; W BLE transport is unsupported |
| Firmware unit tests | Ceedling covers the portable control dispatcher, report encoding, SD lifecycle, wireless staging/auth, SHA-256, boot-settings record, Pico board facts and the PIO SD host's wire encoding and clock planning with mocked platform hooks and a fake flash; no Zephyr execution or PIO timing |
| USB HID | Dashboard connection and [sampled reads/handle reopens](records/2026-11-20-firmware-qualification.md) observed; broader timing, host compatibility and physical reconnect behavior remain unqualified |
| BLE | Monitoring and authenticated control reported working; MTU variation, reconnect stress and radio coexistence remain unqualified |
| Wi-Fi | Provisioning, connection and [scan stress](records/2026-11-20-firmware-qualification.md) observed; failure recovery, lease renewal, coexistence and long-duration behavior remain unqualified |
| Boot settings | Save and reboot reported working; interrupted erase/program, endurance and individual CPU-clock profiles not documented in the report |
| SD card | Native four-bit SDMMC, SPI, detection and replacement reported working; one card's write failures and 10 mA pad-drive fix are [recorded](records/2026-11-20-sd-write-reliability.md); card/wiring matrix, sustained I/O and fault recovery remain unqualified |
| USB MSC | Handoff and return reported working; no OS/card matrix, repeated-cycle data or interrupted-handoff results recorded; see [SD ownership](../reference/sdcard.md#usb-drive-handoff) |
| Diagnostic shells | UART and Wi-Fi TCP shells reported working; admission/reconnect stress and concurrent HID/BLE responsiveness remain unqualified |
| Storage benchmark | Portable safety and host codecs tested; [installed-image checks](records/2026-11-20-firmware-qualification.md) and [flashed 40 MHz image checks](records/2026-11-20-sdmmc-performance.md) recorded on one card; removal/fault cleanup and card matrices remain open. C6 benchmarking is not built because its start command requires USB HID |
| Onboard RGB LED | LED controls reported working; exact board revision/pin, waveform timing and optical measurements not recorded |
| Module memory inventory | Live board diagnostics reported working; raw PSRAM/flash inventory values, reservations and memory-accounting checks not supplied |
| Dashboards | Live USB HID connection and diagnostics reported working; no per-dashboard/OS/browser hardware matrix or raw measurements recorded. C6 has no USB HID dashboard path |
| Security | BLE Control key authentication without link encryption; unencrypted flash; physical USB/UART control; unauthenticated plaintext diagnostic TCP; loopback hosts |
| Variants | Only N16R8 is configured for the ESP32-S3; N4/N8/octal-flash modules and other S3 boards are not targets. ESP32-C6 support is limited to the configured DevKitC-1 N8; Pico-compatible boards other than the four listed are not targets |
| Product readiness | No power-loss, soak, performance, RF, EMC or regulatory qualification |

Further qualification should record firmware/image identity, host/tool versions, board and card details,
procedures, durations and observed results. Use backups and a dedicated card for destructive or fault-injection tests.
Historical software-only records remain unchanged; neither they nor one functional report establish compatibility
with every hardware or workload combination.

The [firmware qualification procedure](firmware-qualification.md) defines opt-in target checks, matrices,
acceptance thresholds and safety requirements for closing the SD/MSC and USB/BLE/Wi-Fi lifecycle gaps.
Having a procedure or software-tested runner does not close those physical gaps.
