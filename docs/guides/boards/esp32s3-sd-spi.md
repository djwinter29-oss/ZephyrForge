# ESP32-S3 N16R8: SD-over-SPI wiring

Use the `esp32s3-devkitc-sd-spi` release image only with an ESP32-S3-DevKitC-1 N16R8:
16 MB flash, 8 MB octal PSRAM, PROCPU. Check the module marking before
[flashing the release image](../flashing.md#flash-a-release-image).
Hardware reports cover selected setups only; see [qualification gaps](../../validation/qualification-gaps.md).

## Connections

GPIO numbers are MCU signals, not header positions. Verify your exact board and breakout labels.
Wire with all power disconnected; never apply 5 V to MCU GPIOs or a bare SD card.
Use short wires and adequate regulated 3.3 V power.

| SPI function | ESP32-S3 connection | SD-card signal |
| --- | --- | --- |
| SCK | GPIO12 | CLK |
| MOSI | GPIO11 | CMD / DI |
| MISO | GPIO13 | DAT0 / DO |
| CS, active-low | GPIO10 | DAT3 / CS |
| Card detect | GPIO8 | Inserted-low detection switch to GND |
| Supply | 3V3 | 3.3 V supply |
| Ground | GND | Common ground |

GPIO14 and GPIO15 are unused; DAT1/DAT2 do not carry SPI data.
Use a 3.3 V-compatible SPI socket/module with appropriate external SD pull-ups, including CS.
Internal data pull-ups do not replace external resistors. Do not pull up CLK.
This is full-duplex SPI with one MOSI and one MISO line, not native one-bit SDMMC.

GPIO8 is active-low with an internal pull-up. Connect the insertion switch between GPIO8 and GND,
closing when inserted. Low means inserted; 3.3 V means absent. An unwired GPIO8 prevents mounting.
A switch that drives 3.3 V on insertion needs polarity adaptation. Shorting GPIO8 to GND reports
inserted without a card, so firmware retries initialization. There is no card power-control GPIO.

The image disables native SDMMC and uses SPI3 with a requested 25 MHz ceiling, not a measured
frequency or throughput guarantee. Leave flash/PSRAM and boot-strapping pins alone;
GPIO33-37 serve octal PSRAM. SPI2 remains reserved for the onboard RGB LED.

## Card safety

Back up the card and prepare an existing FAT16, FAT32 or exFAT volume on a computer.
Firmware mounts at `/SD:` without automatic formatting. Never remove the card during I/O.
USB drive access requires explicit handoff; safely eject before returning ownership to firmware.
Before changing buses, follow the
[card power-cycle instruction](../../reference/sdcard.md#safety-and-ownership).
