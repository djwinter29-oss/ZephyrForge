# Raspberry Pi Pico family: SD-over-SPI wiring

This wiring applies to Pico, Pico W, Pico 2 and Pico 2 W images ending in `sd-spi`.
Choose the release archive for the **exact board**; RP2040 and RP2350 images are not interchangeable.
Follow [release-image flashing](../flashing.md#flash-a-release-image) for BOOTSEL installation.
Pico storage images have no hardware test records; see [qualification gaps](../../validation/qualification-gaps.md).

## Connections

GP numbers are MCU signals; header positions refer to the shared 40-pin Pico header.
Verify your exact board and breakout labels. Wire with all power disconnected.
Never apply 5 V to MCU GPIOs or a bare SD card. Use short wires and adequate regulated 3.3 V power.

| SPI function | Pico connection | Header pin | SD-card signal |
| --- | --- | ---: | --- |
| SCK | GP2 | 4 | CLK |
| MOSI | GP3 | 5 | CMD / DI |
| MISO | GP4 | 6 | DAT0 / DO |
| CS, active-low | GP7 | 10 | DAT3 / CS |
| Card detect | GP8 | 11 | Inserted-high detection signal |
| Supply | 3V3(OUT) | 36 | 3.3 V supply |
| Ground | GND | 38 | Common ground |

GP5 and GP6 (header pins 7 and 9) carry no SPI data and may stay connected to DAT1/DAT2.
Use a 3.3 V-compatible SPI socket/module with appropriate external SD pull-ups, including CS.
Internal MOSI/MISO pull-ups do not replace external resistors. Do not pull up CLK.
This is full-duplex SPI with one MOSI and one MISO line, not native one-bit SDMMC.

GP8 is active-high: 3.3 V means inserted; low means absent. The MCU applies **no pull**.
The breakout must drive or bias it. The Adafruit microSD SPI/SDIO breakout (P4682) grounds DET
without a card and pulls it up when inserted. An unconnected GP8 floats and can read inserted,
so firmware retries initialization. A switch that closes to GND on insertion needs polarity adaptation.

The image uses SPI0 with a requested 25 MHz ceiling, not a measured frequency or throughput guarantee.
The board's default I2C0 and I2C1 pin assignments are disabled because they overlap the reserved SD pins.
For custom wiring, SCK, MOSI and MISO must be functions of the selected SPI controller;
CS is an ordinary GPIO.

## Card safety

Back up the card and prepare an existing FAT16, FAT32 or exFAT volume on a computer.
Firmware mounts at `/SD:` without automatic formatting. Never remove the card during I/O.
USB drive access requires explicit handoff; safely eject before returning ownership to firmware.
Before changing buses, follow the
[card power-cycle instruction](../../reference/sdcard.md#safety-and-ownership).
