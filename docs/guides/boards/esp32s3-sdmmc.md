# ESP32-S3 N16R8: native SDMMC wiring

Use the `esp32s3-devkitc-sdmmc` release image only with an ESP32-S3-DevKitC-1 N16R8:
16 MB flash, 8 MB octal PSRAM, PROCPU. Check the module marking before
[flashing the release image](../flashing.md#flash-a-release-image).

## Connections

GPIO numbers are MCU signals, not header positions. Verify your exact board and breakout labels.
Wire with all power disconnected; never apply 5 V to MCU GPIOs or a bare SD card.
Use short wires and adequate regulated 3.3 V power.

| SD-card signal | ESP32-S3 connection | Direction relative to MCU |
| --- | --- | --- |
| CLK | GPIO12 | Output |
| CMD | GPIO11 | Bidirectional |
| DAT0 | GPIO13 | Bidirectional |
| DAT1 | GPIO14 | Bidirectional |
| DAT2 | GPIO15 | Bidirectional |
| DAT3 | GPIO10 | Bidirectional, not a chip-select output |
| Card detect | GPIO8 | Input, active-low with internal pull-up |
| Supply | 3V3 | 3.3 V power |
| Ground | GND | Common ground |

Use a 3.3 V socket exposing CLK, CMD and **all four data lines**. SPI-only breakouts and directional
SPI level shifters are unsuitable without bidirectional access to these signals.
CMD and DAT0-DAT3 require external pull-ups to 3.3 V, typically 10 kOhm; verify the socket/module
requirements and existing resistors. Do not pull up CLK. Internal command/data pull-ups do not
replace suitable external resistors or electrical qualification.

Connect the socket's insertion switch between GPIO8 and GND, closing when inserted.
Low means inserted; 3.3 V means absent. An unwired GPIO8 reads absent and prevents mounting.
A switch that drives 3.3 V on insertion needs polarity adaptation. Shorting GPIO8 to GND reports
inserted even without a card, so firmware retries initialization. There is no card power-control GPIO.

The image uses four-bit SDMMC slot 0 with a requested 40 MHz ceiling, not a measured frequency or
throughput guarantee. CLK, CMD and DAT0-DAT3 use about 10 mA pad drive; the 20 mA reset default
caused write CRC errors on tested hand wiring. Wiring changes may need a different drive strength
(5, 10, 20 or 40 mA) and renewed write qualification.
Leave flash/PSRAM and boot-strapping pins alone; GPIO33-37 serve octal PSRAM. SPI3 is disabled.

## Card safety and qualification

Back up the card and prepare an existing FAT16, FAT32 or exFAT volume on a computer.
Firmware mounts at `/SD:` without automatic formatting. Never remove the card during I/O.
USB drive access requires explicit handoff; safely eject before returning ownership to firmware.
Disconnect card power before changing between native SDMMC and SPI images: a board reset alone
may leave the card in SPI mode. There is no runtime bus fallback or card power switching.

These are not electrically qualified wiring instructions. Release workflows do not flash hardware;
software checks do not qualify physical USB, radio or SD behavior. See
[qualification limits](../../validation/qualification-gaps.md) before use.
