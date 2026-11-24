# ESP32-C6-DevKitC-1 N8: SD-over-SPI wiring

Use the ESP32-C6 `sd-spi` image only with the ESP32-C6-DevKitC-1 N8 target. The target uses the
ESP32-C6-WROOM-1U-N8 module's 8 MB flash, without PSRAM. Verify the board/module before
[flashing](../flashing.md#esp32-c6-release-image).

## Connections

GPIO numbers are MCU signals, not header positions. Verify your exact board and breakout labels.
Wire with all power disconnected; never apply 5 V to MCU GPIOs or a bare SD card.
Use short wires and adequate regulated 3.3 V power.

| SPI function | ESP32-C6 connection | SD-card signal |
| --- | --- | --- |
| SCK | GPIO6 | CLK |
| MOSI | GPIO7 | CMD / DI |
| MISO | GPIO2 | DAT0 / DO |
| CS, active-low | GPIO10 | DAT3 / CS |
| Card detect | GPIO3 | Inserted-low detection switch to GND |
| Supply | 3V3 | 3.3 V supply |
| Ground | GND | Common ground |

Use a 3.3 V-compatible SPI socket/module with appropriate external SD pull-ups, including CS. Internal data pull-ups
do not replace external resistors. GPIO3 has an internal pull-up; the insertion switch closes to ground when inserted.
An unwired or incompatible card-detect input prevents mounting. There is no card power-control GPIO.

The image uses SPI2 with a requested 25 MHz ceiling, not a measured frequency or throughput guarantee. GPIO6 and
GPIO7 are the board's default I2C0 pins, so I2C0 is disabled in this storage variant. Do not assign those pins to
another peripheral while using SD-SPI.

## Card safety and qualification

Back up the card and prepare an existing FAT16, FAT32 or exFAT volume on a computer. Firmware mounts at `/SD:`
without automatic formatting. The C6 image has no native USB HID or USB MSC; Direct's authenticated BLE connection
can read storage status but cannot export the card to a computer or run the USB-only storage benchmark.
Power-cycle the card before changing between storage variants. There is no runtime bus fallback or card power
switching.

These are not electrically qualified wiring instructions. Software builds do not qualify physical USB, radio or SD
behavior. See [qualification limits](../../validation/qualification-gaps.md) before use.
