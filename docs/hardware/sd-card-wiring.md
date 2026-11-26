# Wire an SD card

[Documentation](../README.md) / [Hardware](README.md) / SD-card wiring

Choose the pinout for both the board and storage image. GPIO/GP numbers are MCU signals, not header positions.
Verify the board and breakout labels before wiring.

Disconnect all power while wiring. Use a 3.3 V-compatible socket, short wires and adequate regulated 3.3 V power.
Never apply 5 V to MCU GPIOs or a bare SD card. CMD and DAT0-DAT3 require external pull-ups to 3.3 V for SDMMC,
typically 10 kOhm; SPI modules require suitable SD pull-ups, including CS. A module's internal pull-ups do not replace
the required external resistors. Check the socket/module resistors meet the bus requirements; MCU internal
pull-ups are not a substitute. Do not pull up CLK.

Jump to [ESP32-S3](#esp32-s3-devkitc-1-n16r8), [ESP32-C6](#esp32-c6-devkitc-1-n8), or
[Pico](#raspberry-pi-pico-family), then [prepare and use the card](../guides/sd-card.md).

Requested bus ceilings below are configuration values, not measured runtime clocks or throughput.
The [SD storage reference](../reference/sd-storage.md#driver-and-filesystem-configuration) explains controller limits;
see [qualification coverage](../validation/README.md) for tested board/card combinations.

## ESP32-S3-DevKitC-1 N16R8

Use the configured N16R8 board with either SDMMC or SPI storage.

| SD signal / function | Native four-bit SDMMC | SD-over-SPI |
| --- | --- | --- |
| CLK / SCK | GPIO12 | GPIO12 |
| CMD / MOSI | GPIO11 | GPIO11 |
| DAT0 / MISO | GPIO13 | GPIO13 |
| DAT1 | GPIO14 | Unused |
| DAT2 | GPIO15 | Unused |
| DAT3 / CS | GPIO10 (data, not CS) | GPIO10 (active-low CS) |
| Card detect | GPIO8, active-low, internal pull-up | GPIO8, active-low, internal pull-up |
| Supply / ground | 3V3 / GND | 3V3 / GND |

For SDMMC, use a socket with bidirectional access to all four data lines; an SPI-only breakout or directional SPI
level shifter is unsuitable. Connect the insertion switch from GPIO8 to GND, closing when inserted. A switch that
drives 3.3 V on insertion needs polarity adaptation. There is no card power-control GPIO.

The SDMMC image requests a 40 MHz ceiling and uses about 10 mA pad drive on CLK, CMD and DAT0-DAT3. The SPI image
requests 25 MHz on SPI3; SPI2 remains reserved for the onboard RGB LED. GPIO33-37 serve octal PSRAM. Leave flash,
PSRAM and boot-strapping pins unchanged.

## ESP32-C6-DevKitC-1 N8

Only SD-over-SPI is supported. The image uses SPI2.

| SD signal / function | Connection |
| --- | --- |
| SCK | GPIO6 |
| MOSI | GPIO7 |
| MISO | GPIO2 |
| CS, active-low | GPIO10 |
| Card detect | GPIO3, active-low with internal pull-up |
| Supply / ground | 3V3 / GND |

Connect the insertion switch from GPIO3 to GND. An unwired or incompatible card-detect input prevents mounting.
GPIO6 and GPIO7 are the board's default I2C0 pins, so I2C0 is disabled in this storage variant. The image requests
a 25 MHz ceiling. There is no card power-control GPIO.

## Raspberry Pi Pico family

The following pinouts apply to Pico, Pico W, Pico 2 and Pico 2 W. For header positions, GP numbers refer to the
shared 40-pin Pico header. Native SDMMC uses PIO1; PIO0 remains available for W-board radio.

| SD signal / function | Native four-bit SDMMC | SD-over-SPI |
| --- | --- | --- |
| CLK / SCK | GP2 (pin 4) | GP2 (pin 4) |
| CMD / MOSI | GP3 (pin 5) | GP3 (pin 5) |
| DAT0 / MISO | GP4 (pin 6) | GP4 (pin 6) |
| DAT1 | GP5 (pin 7) | Unused |
| DAT2 | GP6 (pin 9) | Unused |
| DAT3 / CS | GP7 (pin 10), data not CS | GP7 (pin 10), active-low CS |
| Card detect | GP8 (pin 11), active-high, no MCU pull | GP8 (pin 11), active-high, no MCU pull |
| Supply / ground | 3V3(OUT) pin 36 / GND pin 38 | 3V3(OUT) pin 36 / GND pin 38 |

The native bus needs a 3.3 V socket with bidirectional access to all four data lines. GP5 and GP6 do not carry SPI
data and may remain connected to DAT1 and DAT2. Card detect must be driven or biased: 3.3 V means inserted and low
means absent. The Adafruit microSD SPI/SDIO breakout (P4682) grounds DET without a card and pulls it high when
inserted. An unconnected GP8 can float and read as inserted; a switch that closes to GND on insertion needs
polarity adaptation.

The native image's PIO host requests up to 50 MHz, limited by the system clock divider; SPI0 requests 25 MHz.
Both default I2C0 and I2C1 pin assignments are disabled because their default GPIOs overlap the storage allocation.
For custom native wiring, DAT0-DAT3 must be four ascending consecutive GPIOs below GPIO32, CLK must be two GPIOs
below DAT0 (modulo 32), and CMD must be another GPIO below GPIO32. Custom SPI wiring must use the selected
controller's SCK, MOSI and MISO functions; CS is an ordinary GPIO.
