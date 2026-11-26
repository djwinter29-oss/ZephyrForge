# SD storage

Storage-enabled images use Zephyr disk access and FatFs to mount an existing volume at `/SD:` (disk name `SD`).
Firmware owns the card until the user explicitly hands it to USB mass storage on a board that supports it.
There is no automatic formatting or runtime bus fallback.
Hardware reports cover selected card and wiring combinations only; see
[qualification gaps](../validation/qualification-gaps.md).

ESP32-S3 and Pico images offer native four-bit SDMMC or SPI; C6 offers SPI only.
The [build guide](../development/firmware-build.md) also describes the `none` image, which omits SD and filesystem
support entirely. This is an SD memory-card interface, not an SDIO peripheral/function API.

For runtime behavior, see [mounting](#boot-and-runtime-mount-lifecycle), [USB handoff](#usb-drive-handoff),
or the [optional benchmark](#optional-storage-benchmark).

## Pin allocation

GPIO numbers are MCU signals, not physical header positions. Use the wiring guide for the exact board and storage
variant from [SD-card wiring](../guides/hardware.md#sd-card-wiring). Read
[safety and ownership](#safety-and-ownership) before changing buses.

### Reported allocation

Report `2` publishes storage GPIOs in CLK, CMD, DAT0, DAT1, DAT2, DAT3 order for native SDMMC, and in SCK, MOSI,
MISO, CS, unassigned, unassigned order for SPI.

Unassigned GPIOs are encoded as `255`; card detect is reported separately.
The reported values must match the corresponding [wiring guide](#pin-allocation).
If a downstream board changes the wiring, its hardware configuration and reported pin allocation MUST agree;
validate the new allocation before use.

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

Storage starts after boot settings, CPU clock application and telemetry initialization, before the transports.
An inserted boot-time card is mounted immediately. Startup continues after a logged mount failure.

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

The dashboard exposes **Enable USB drive** when this firmware advertises MSC and card hardware is initialized.
Native USB presents HID management plus a removable MSC LUN; it has no CDC console.
USB export is optional: an image without MSC retains firmware-side mounting but exposes no LUN or handoff command.
See [module selection](../development/firmware-build.md#optional-modules).
Firmware unmounts FAT before exporting raw 512-byte sectors. The native USB device briefly disconnects and
reconnects on export so an OS-ejected LUN can be loaded again.
While firmware owns the card, including during transitions and benchmarks, the LUN reports no medium immediately
instead of holding host commands until storage work finishes.

Before **Return to firmware**, safely eject the drive in the operating system, then explicitly confirm eject
in the dashboard. The native Zephyr class does not expose a public OS-eject notification; the confirmation is
the user's assertion, not firmware verification that the host has flushed every cache. Return revokes USB
access before remounting FAT. No unsafe force-return action exists.

USB opcode `3` acknowledges admission, not completed media work. The worker defers handoff long enough to let
the host read its result; Report `10` reports transition and failure. Never automatically resend an uncertain
command. See the [USB control contract](usb-hid.md#msc-command-rules) for exact flags and result semantics.

## Status reporting

Report `10` carries live detection/ownership snapshots, hardware identity, filesystem type and available capacity
over USB HID and BLE.
The [USB HID specification](usb-hid.md#report-id-10-storage-status) defines field values, flags, and validation.
Detection distinguishes confirmed absence from an inserted card whose mount failed.
During media work or USB ownership, the firmware SD sample is invalid and current SD fields are cleared.
Capacity counts the whole volume in 512-byte sectors, not free space. The reported clock is the configured
ceiling, not a runtime measurement. CID reports manufacturer/product/revision, serial and manufacturing date even
when no filesystem is mounted. Negotiated bus state is not reported.
The pinned SD stack includes a public-ioctl extension that retains CID during hardware initialization.

The mount error code is **only logged**, not retained in Report `10`. Hosts can distinguish detected absence and
hardware initialization from a mounted filesystem, but cannot classify a mount error from this report alone.
Use the UART log for diagnosis, and do not label every uninitialized sample "no card".

Hosts accept zero or one advertised storage transport and validate its pin layout and bus-mode reports. The C6 has
no native USB HID, so Direct BLE can read storage status but cannot start the USB-only benchmark or handoff commands.

## Optional storage benchmark

The dashboards offer a USB-controlled benchmark when capability bit 5 is advertised. It is disabled by default in
CMake; the build wrapper enables it for SD-enabled ESP32-S3 and Pico images, not for C6. See
[build configuration](../development/firmware-build.md#configuration-and-storage-profiles).
It never runs at boot or on insertion. Authenticated BLE can read its progress but cannot start or cancel it.

Back up the card before confirming a run. Choose 1–1024 MiB (dashboard default 32 MiB). The worker writes a
deterministic pattern to `SD:/EFTEST.BIN`, closes it, reads and verifies it, then removes the file. An existing file
of that name is never overwritten or removed. The volume must already be mounted for firmware.
Free-space checks reserve the requested size plus one 32 KiB chunk. The benchmark uses a 32 KiB internal-RAM buffer.

### Admission, progress and timing

The command acknowledges **queued admission**, not successful filesystem preflight or a passing benchmark.
File-existence and free-space checks run asynchronously on the storage worker. Read Report `11` for phases,
progress, measured throughput and terminal result; the [USB contract](usb-hid.md#report-id-11-storage-benchmark-status)
defines its wire layout. Firmware storage samples are invalid during the run; settings, reboot and MSC handoff are
blocked. Management remains responsive while the worker performs storage I/O.

Write timing includes pattern generation, file allocation, writes and final close/sync. Read timing includes reads,
verification and final close. Throughput is completed bytes divided by elapsed time, not a bus-frequency estimate.
A zero elapsed time produces unavailable throughput (`--`), not a fabricated rate.

### Cancellation and cleanup

Cancel requests stop at the next cooperative checkpoint and attempt cleanup. Short/failed I/O is never replayed.
Observed removal stops further access to that media, including close/unlink; replacement media is not used for cleanup.
A failed close or cleanup can leave `EFTEST.BIN` behind. Inspect it on a computer after safely releasing ownership;
remove it manually before starting another run. Unplugging or power loss during writes can still corrupt the volume.
The benchmark causes real card writes and wear; it provides neither power-loss atomicity nor sustained-workload
qualification.

## Safety and ownership

Back up the card and prepare a FAT16, FAT32, or exFAT volume on a computer. Firmware mounts the existing
volume at `/SD:` without formatting it. This includes factory-formatted exFAT cards.

- Firmware and USB have exclusive ownership; they never mount/access the same medium concurrently.
- Reboot and settings-save are Busy while the storage worker is active or USB owns the medium.
- Bluetooth mirrors storage/configuration reports but cannot change MSC ownership.
- Physical removal during reads/writes can interrupt I/O and corrupt data. Detection does not prevent unplugging,
  make writes atomic, or provide power-loss recovery. Safely eject before returning USB ownership.
- Power-cycle the card before changing between SDMMC and SPI images. A board reset can leave the card in SPI mode;
  the board has no card power switch.
- Only an explicitly confirmed optional benchmark creates a temporary file; no automatic write workload or repair runs.
- Downstream filesystem users must participate in ownership serialization, check every return value and close/sync
  files before handoff. Keep blocking storage work out of the management loop and application lock.
- There is no implemented power-loss recovery or sustained-recording guarantee.
