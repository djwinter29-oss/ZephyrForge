# Raspberry Pi Pico family: native SDMMC wiring

This wiring applies to Pico, Pico W, Pico 2 and Pico 2 W images ending in `sdmmc`.
Choose the release archive for the **exact board**; RP2040 and RP2350 images are not interchangeable.
Follow [release-image flashing](../flashing.md#flash-a-release-image) for BOOTSEL installation.

## Connections

GP numbers are MCU signals; header positions refer to the shared 40-pin Pico header.
Verify your exact board and breakout labels. Wire with all power disconnected.
Never apply 5 V to MCU GPIOs or a bare SD card. Use short wires and adequate regulated 3.3 V power.

| SD-card signal | Pico connection | Header pin | Direction relative to MCU |
| --- | --- | ---: | --- |
| CLK | GP2 | 4 | Output |
| CMD | GP3 | 5 | Bidirectional |
| DAT0 | GP4 | 6 | Bidirectional |
| DAT1 | GP5 | 7 | Bidirectional |
| DAT2 | GP6 | 9 | Bidirectional |
| DAT3 | GP7 | 10 | Bidirectional, not a chip-select output |
| Card detect | GP8 | 11 | Input, active-high, no MCU pull |
| Supply | 3V3(OUT) | 36 | 3.3 V power |
| Ground | GND | 38 | Common ground |

Use a 3.3 V socket exposing CLK, CMD and **all four data lines**. SPI-only breakouts and directional
SPI level shifters are unsuitable without bidirectional access to these signals.
CMD and DAT0-DAT3 require external pull-ups to 3.3 V, typically 10 kOhm; verify the socket/module
requirements and existing resistors. Do not pull up CLK. Internal command/data pull-ups do not
replace suitable external resistors or electrical qualification.

GP8 is active-high: 3.3 V means inserted; low means absent. The MCU applies **no pull**.
The breakout must drive or bias it. The Adafruit microSD SPI/SDIO breakout (P4682) grounds DET
without a card and pulls it up when inserted. An unconnected GP8 floats and can read inserted,
so firmware retries initialization. A switch that closes to GND on insertion needs polarity adaptation.

The native bus uses PIO1; PIO0 remains available for the W boards' radio.
The board's default I2C0 and I2C1 pin assignments are disabled because they overlap the SD signals.
For custom native wiring, DAT0-DAT3 must be four ascending consecutive GPIOs below GPIO32,
CLK must be two GPIOs below DAT0 (modulo 32), and CMD must be another GPIO below GPIO32.
The build rejects other native allocations.

## Card safety and qualification

Back up the card and prepare an existing FAT16, FAT32 or exFAT volume on a computer.
Firmware mounts at `/SD:` without automatic formatting. Never remove the card during I/O.
USB drive access requires explicit handoff; safely eject before returning ownership to firmware.
Disconnect card power before changing between native SDMMC and SPI images: a board reset alone
may leave the card in SPI mode. There is no runtime bus fallback or card power switching.

These are not electrically qualified wiring instructions. Release workflows do not flash hardware;
software checks do not qualify physical USB, radio or SD behavior. See
[qualification limits](../../validation/qualification-gaps.md) before use.
