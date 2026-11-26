# Use an SD card

[Documentation](../README.md) / Guides / SD card

Use this guide to prepare a card, export it over USB, or run the optional benchmark. For driver configuration and
application integration, see the [SD storage reference](../reference/sd-storage.md).

## Before you start

- Use an SD-enabled image and follow the [wiring for your board and bus](../hardware/sd-card-wiring.md).
  Disconnect all power before changing wiring.
- Back up the card. Prepare a FAT16, FAT32, or exFAT volume on a computer; firmware never formats or repairs it.
- Power-cycle the card before changing between SDMMC and SPI images. A board reset can leave the card in SPI mode,
  and the board has no card power switch.
- Keep the card and power connected during I/O. Physical removal or power loss can corrupt the volume.

Insert the card and wait for fresh Storage status in the dashboard. Firmware mounts an inserted card at boot or
after insertion. If mounting fails, check the [UART log](console.md#uart); card identity and capacity can still be
available. Capacity is the whole volume, not free space.

## Export or return a card

This needs a [USB dashboard connection](dashboard-use.md#connect-over-usb) and firmware with USB mass storage
enabled. An initialized card can be exported even if firmware could not mount its filesystem. BLE cannot change
card ownership.

1. Choose **Enable USB drive**. The native USB device briefly disconnects and reconnects; reconnect the dashboard
   to the same board if needed.
2. Check that Storage status shows USB ownership before using the drive. An accepted command is not a completed
   handoff; if it is rejected, inspect the status and UART log before continuing.
3. When finished, **safely eject the drive in the operating system**.
4. Choose **Return to firmware**, confirm OS eject, and check the settled ownership and filesystem status.
   If the volume cannot mount, check the UART log rather than assuming it is ready.

The eject confirmation is your assertion that the host has finished with the card; firmware cannot verify that
every OS cache was flushed. There is no force-return action. Firmware and USB must never access the card together.
Return the card before saving settings or rebooting; those actions are blocked while USB owns it or storage is busy.

If a command times out or the board disconnects unexpectedly, follow
[dashboard recovery](dashboard-use.md#recover-after-a-timeout) instead of submitting it again.
Reinserting a card never automatically exports it to USB.

## Run a benchmark

The benchmark needs USB control, a visible benchmark action, and a volume mounted for firmware. BLE can read its
progress but cannot start or cancel it. It never runs automatically at boot or insertion.

1. Choose **1-1024 MiB**; the dashboard defaults to **32 MiB**. Leave room for that size plus a 32 KiB chunk.
2. Confirm the card-write warning and start the run.
3. Wait for the terminal result. Admission only queues the work; filesystem preflight can still fail.

The worker writes, reads, and verifies a temporary `SD:/EFTEST.BIN`, then removes it. An existing file with that name
is never overwritten or removed. Settings, reboot, and USB handoff are blocked during the run, but management reads
remain available. This causes real card writes and wear, not a read-only check or sustained-workload qualification.
For throughput accounting, see [benchmark timing](../reference/sd-storage.md#admission-progress-and-timing).

### Cancel or clean up

Cancellation stops at the next cooperative checkpoint and attempts cleanup; it is not immediate.
A failed close, cleanup, removal, or power interruption can leave `EFTEST.BIN` behind. After safely releasing
ownership, inspect the original card on a computer and remove the file manually before another run.
Do not use a replacement card to clean up an interrupted run.
