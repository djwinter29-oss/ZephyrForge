# Functional hardware test report

On **2026-11-14**, a tester reported programming and testing an **ESP32-S3-DevKitC-1 N16R8**. The report marked all
checks below as passed and noted no issues. These are reported manual hardware results, not automated test results.

## Reported results

| Check | Reported result |
| --- | --- |
| Firmware flashing and boot | Pass |
| USB HID dashboard connection | Pass |
| Board identity and live diagnostics | Pass |
| LED controls | Pass |
| Settings save and reboot | Pass |
| Wi-Fi provisioning and connection | Pass |
| Bluetooth LE monitoring and authenticated control | Pass |
| Native four-bit SDMMC storage | Pass |
| SD-over-SPI storage | Pass |
| Card detection and replacement | Pass |
| USB drive handoff and return | Pass |
| UART diagnostic shell | Pass |
| Wi-Fi TCP diagnostic shell | Pass |

## Configuration and evidence limits

The board/module target was confirmed. The report did not specify the execution date, firmware revision or image
hashes, host application versions, operating system/browser, SDK/tool versions, SD-card model, exact wiring or
test procedures. No raw logs, measurements, cycle counts or test durations accompanied the report.
It does not establish a pass for every combination of dashboard, host OS, card or board revision.

The checks did not measure sustained throughput, benchmark results, memory-accounting accuracy, power-loss recovery,
removal-during-write safety, endurance, radio coexistence, RF/EMC compliance or production qualification.

See [qualification gaps](../qualification-gaps.md) for the current boundaries.
