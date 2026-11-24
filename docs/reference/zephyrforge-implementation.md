# ZephyrForge Implementation Reference

This page records platform integration and measurement limits that are not wire contracts.
Hardware selection and wiring belong to the hardware guide; report layouts and supported operations belong
to the USB HID and Bluetooth LE specifications.

## Platform integration

| Area | ESP32-S3-DevKitC-1 N16R8 | ESP32-C6-DevKitC-1 N8 | Raspberry Pi Pico boards |
| --- | --- | --- | --- |
| Application core | Zephyr runs on PROCPU; the second core does not run an application workload | Zephyr runs on core 0; core 1 does not run an application workload |
| Application core | Zephyr runs on PROCPU; the second core does not run an application workload | Zephyr runs on the high-performance core | Zephyr runs on core 0; core 1 does not run an application workload |
| USB | Zephyr's `device_next` stack with the native USB OTG controller | No native HID controller | Zephyr's `device_next` stack with the native USB device controller |
| Bluetooth | Zephyr Bluetooth host with the ESP32-S3 controller and entropy source | Zephyr Bluetooth host; advertising requires a saved Control key | Not included; shared-SPI HCI is unsupported |
| Wi-Fi | Zephyr network management, DHCP and Espressif software coexistence | Zephyr network management, DHCP and Espressif software coexistence | W boards use AIROC/WHD, DHCP and asynchronous driver requests; non-W boards have no radio |
| Filesystem | Zephyr disk access and FatFs over the native Espressif SDMMC controller or SPI3 | Zephyr disk access and FatFs over SPI2; no USB MSC | Zephyr disk access and FatFs over the ZephyrForge PIO SD host or SPI0 |
| Settings | A fixed CRC-checked flash record, rather than Zephyr settings/NVS | The same fixed record and partition layout | The same record, loaded at boot but never saved |

## Measurement limits

- CPU clock telemetry normally reads the clock driver; if that read fails, the current implementation uses the
  build clock. A saved profile or boot snapshot does not prove that the requested clock was applied. The Pico images
  report their devicetree system clock, which they never change.
- Storage status is cached from boot. Its clock value is the configured ceiling, not a measured or negotiated
  frequency; fresh report delivery does not establish current card presence.

## Driver limits

Wi-Fi join events do not supply the 802.11 failure reason. Wireless diagnostics describe state, not a reliable
classification of authentication or access-point failures.

The pinned SD stack has a managed public-ioctl extension retaining CID during hardware initialization.
Storage reports retain identity and capacity independently of mounting, but do not expose negotiated bus state.
Mount errors are logged to UART, not carried as error codes in storage reports.

These are implementation limits, not hardware qualification. Observed evidence and outstanding physical tests
belong in the validation records and qualification gaps.
