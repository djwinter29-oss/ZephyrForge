# SD storage and ownership

[Documentation](../README.md) / Reference / SD storage

Storage-enabled images use Zephyr disk access and FatFs to mount an existing volume at `/SD:` (disk name `SD`).
Firmware owns the card until the user explicitly hands it to USB mass storage on a board that supports it.
There is no automatic formatting or runtime bus fallback.
See [qualification gaps](../validation/README.md) for tested card and wiring combinations.

This is an SD memory-card interface, not an SDIO peripheral/function API.
Select the controller and optional modules through the [build guide](../development/firmware-build.md).
For preparation, dashboard actions, OS eject, and manual cleanup, use [Use an SD card](../guides/sd-card.md).

- [Driver and filesystem configuration](#driver-and-filesystem-configuration)
- [Mount lifecycle](#boot-and-runtime-mount-lifecycle)
- [USB ownership transitions](#usb-drive-handoff)
- [Benchmark execution](#optional-storage-benchmark)
- [Application integration and safety](#safety-and-ownership)

## Pin allocation

Use the [SD-card wiring guide](../hardware/sd-card-wiring.md) for physical allocations and
[Report `2`](usb-hid.md#report-id-2-board-configuration) for their wire encoding. A downstream wiring change MUST
keep hardware configuration and reported allocation consistent; validate the new allocation before use.

## Driver and filesystem configuration

| Layer | ESP32-S3-DevKitC-1 N16R8 | ESP32-C6-DevKitC-1 N8 | Raspberry Pi Pico boards |
| --- | --- | --- | --- |
| Native image controller | Espressif SDHC slot 0, compatible `espressif,esp32-sdhc-slot`, four-bit bus | Not supported | ZephyrForge PIO SD host on PIO1, compatible `zephyrforge,rpi-pico-pio-sdhc`, four-bit bus |
| SPI image controller | SPI3, compatible `zephyr,sdhc-spi-slot` | SPI2, compatible `zephyr,sdhc-spi-slot` | SPI0, compatible `zephyr,sdhc-spi-slot` |
| Native Kconfig driver | `CONFIG_SDHC_ESP32` | Not supported | `CONFIG_ZF_SDHC_RPI_PICO_PIO` |
| Requested native ceiling | 40 MHz | Not supported | 50 MHz; the PIO divider limits actual clock to one quarter of the SoC clock (31.25 MHz RP2040, 37.5 MHz RP2350) |
| Requested SPI ceiling | 25 MHz | 25 MHz | 25 MHz |
| Native image pads | `drive-strength = <10>` on CLK, CMD and DAT0-DAT3 | Not supported | Default drive; Schmitt-trigger inputs |

Requested ceilings are not measured runtime clocks or throughput. Every image uses the `zephyr,sdmmc-disk` disk
layer with disk name `SD` and FatFs mounted at `/SD:`, and enables GPIO/pinctrl, disk access, the SDMMC disk driver,
the filesystem API and FatFs. SPI images select `CONFIG_SPI_SDHC`.
Both `CONFIG_FS_FATFS_MOUNT_MKFS` and `CONFIG_FS_FATFS_MKFS` are explicitly
disabled: the application cannot automatically format a failed mount or call FatFs mkfs through these options.

### Pico PIO resources

The Pico native host is ported from PicoForge's PIO SDIO engine and implements the standard Zephyr SDHC API, so the
same SD stack, disk and filesystem layers run above it. It owns PIO1, using two state machines and the instruction
memory, which it reloads between reads and writes, plus one DMA channel claimed through pico-sdk. It cannot be
enabled together with Zephyr's RP DMA driver, which does not coordinate channel claims with pico-sdk.

### Clock negotiation

Native SDMMC starts with a one-bit bus at the probing clock and negotiates the card's advertised capabilities.
Cards that accept High Speed mode can use the board's native ceiling; cards without that support remain at the
legacy clock, at most 25 MHz requested. Zephyr's SD stack widens the bus to four bits only after a card accepts
High Speed, so a card without High Speed transfers data on DAT0 alone.
The ESP32 driver divides a 160 MHz source: a 25 MHz request produces about
22.9 MHz, while 40 MHz is exact. Requesting 50 MHz would still select 40 MHz with this driver.
The Pico host divides the system clock: High Speed runs at one quarter of it, and a 25 MHz legacy request produces
about 20.8 MHz on RP2040 and 25 MHz on RP2350. The Pico images do not change the system clock.
There is no UHS/1.8 V mode or automatic clock reduction after CRC errors.
A card's write-speed class does not determine its negotiated bus clock.
Keep the ESP32-S3 10 mA pad drive and qualify writes on the actual wiring; if higher-speed operation is unreliable,
set `max-bus-freq = <25000000>` in the native board overlay and rebuild rather than replaying failed writes.
On the Pico boards that setting withdraws High Speed, leaving cards on the one-bit legacy bus.

## Boot and runtime mount lifecycle

Storage is initialized after boot settings, CPU clock application and telemetry initialization. Its worker starts after
the management transports, so card detection and mounting do not delay USB or BLE startup. An inserted boot-time card
is mounted after the worker starts; startup continues after a logged mount failure.

A separate worker polls card detection nominally every 50 ms and requires 100 ms stable insertion. Insertion, including
insertion after startup, mounts the existing FAT volume. Failed initialization/mount attempts retry every
2 seconds after the previous attempt completes while a card is detected. Successful hardware initialization requires a
nonzero sector count and 512-byte sectors; failed geometry validation releases the medium rather than advertising
usable storage.
Hardware metadata is collected before mounting. A failed mount leaves the initialized card's CID, capacity and
configured bus facts available, with filesystem format unknown. Retries clean up the previous filesystem registration
and disk references before reinitializing.

An observed removal, even shorter than the debounce period, invalidates current card facts and revokes USB ownership.
Absence is confirmed after 100 ms stability; raw absence already rejects USB I/O before the worker handles it.
Replacement mounts for firmware and **never automatically re-exports** to USB.
Card-detect read errors are logged, invalidate presence and media facts, and revoke USB access; recovery requires
stable detection before remounting. No operation formats or repairs a card.

Unmount failure prevents granting another owner; later initialization retries cleanup. The Zephyr forced disk
deinitialization API resets disk references but does not propagate the driver's deinitialization result, so
cleanup is not a verified hardware reset. Power-cycle a card if it cannot be reinitialized.

## USB drive handoff

USB export is optional: an image without MSC retains firmware-side mounting but exposes no LUN or handoff command;
see [module selection](../development/firmware-build.md#optional-modules).
Export requires initialized hardware and valid geometry, not a mounted filesystem. Firmware unmounts FAT before
exporting raw 512-byte sectors. Return revokes USB access before remounting FAT.
While firmware owns the card, including during transitions and benchmarks, the LUN reports no medium immediately
instead of holding host commands until storage work finishes.

[Report `10`](usb-hid.md#report-id-10-storage-status) exposes ownership, transition state, and failure.
The [USB control contract](usb-hid.md#msc-command-rules) defines OS-eject confirmation, deferred execution,
admission versus completion, and uncertain-command recovery.

## Optional storage benchmark

The optional benchmark runs on the storage worker; see
[build configuration](../development/firmware-build.md#configuration-and-storage-profiles) for defaults.

The worker writes a deterministic pattern to `SD:/EFTEST.BIN`, closes it, reads and verifies it, then removes the
file. An existing file of that name is never overwritten or removed. The volume must already be mounted for firmware.
Free-space checks reserve the requested size plus one 32 KiB chunk. The benchmark uses a 32 KiB internal-RAM buffer.

### Admission, progress and timing

File-existence and free-space checks run after queued admission, not in the command callback.
Firmware storage samples are invalid during the run; settings, reboot and MSC handoff are blocked. Management
remains responsive while the worker performs storage I/O. [Report `11`](usb-hid.md#report-id-11-storage-benchmark-status)
defines progress, throughput, and terminal results; [benchmark command rules](usb-hid.md#benchmark-command-rules)
define admission.

Write timing includes pattern generation, file allocation, writes and final close/sync. Read timing includes reads,
verification and final close. Throughput is completed bytes divided by elapsed time, not a bus-frequency estimate.
A zero elapsed time produces unavailable throughput (`--`), not a fabricated rate.

### Cancellation and cleanup

Cancel requests stop at the next cooperative checkpoint and attempt cleanup. Short/failed I/O is never replayed.
Observed removal stops further access to that media, including close/unlink; replacement media is not used for cleanup.
A failed close or cleanup can leave `EFTEST.BIN` behind; follow the
[operator cleanup procedure](../guides/sd-card.md#cancel-or-clean-up).

## Safety and ownership

Application code must respect the same ownership boundary as management commands:

- Firmware and USB have exclusive ownership; they never mount/access the same medium concurrently.
- Downstream filesystem users must participate in ownership serialization, check every return value and close/sync
  files before handoff. Keep blocking storage work out of the management loop and application lock.

Detection cannot prevent physical removal or make writes atomic. There is no implemented power-loss recovery or
sustained-recording guarantee. Follow the [card handling precautions](../guides/sd-card.md#before-you-start) before
using real media.
