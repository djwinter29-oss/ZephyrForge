# Hardware setup

ZephyrForge builds for ESP32-S3-DevKitC-1 N16R8, ESP32-C6-DevKitC-1 N8, and Raspberry Pi Pico, Pico W, Pico 2 and Pico 2 W.
The Espressif sections come first; the Pico boards are covered in
[Raspberry Pi Pico targets](#raspberry-pi-pico-targets).

## ESP32-C6-DevKitC-1 N8

The software target is `esp32c6_devkitc/esp32c6/hpcore`, using the ESP32-C6-WROOM-1U-N8 module's 8 MB flash and no
PSRAM. The C6 has a 160 MHz maximum CPU clock, no onboard LED support in this project, and no native USB HID
device. Use the board's UART connection for flashing and diagnostic output; Direct's authenticated BLE service is
available only when a Control key is already present in settings. The project currently has no secure key-provisioning
path for a fresh C6 board, so fresh images do not advertise BLE.

ESP32-C6 supports `none` or SD-over-SPI, not native four-bit SDMMC. The SPI image uses SPI2 with GPIO2 MISO, GPIO6
SCK, GPIO7 MOSI, GPIO10 active-low CS and GPIO3 active-low card detect. GPIO6/7 are also the default I2C0 pins, so
I2C0 is disabled in this storage variant. See the [C6 SPI wiring guide](boards/esp32c6-sd-spi.md) for connections.
These assignments are software configuration only; the board, radio and card wiring have not been physically
qualified.

## ESP32-S3-DevKitC-1 N16R8

The ESP32-S3 image targets `esp32s3_devkitc/esp32s3/procpu` in Zephyr 4.3.2.
ZephyrForge builds for the ESP32-S3-DevKitC-1 **N16R8** (ESP32-S3-WROOM-1-N16R8 module): 16 MB quad flash and
8 MB octal PSRAM. The project configuration selects the N16R8 memory sizes and partition layout
(settings in `storage_partition` at `0xFB0000`), with octal PSRAM at 40 MHz and a boot-time memory test.
PSRAM provides a separate 1 MB shared multi-heap; the kernel heap, Wi-Fi and BLE buffers stay in internal RAM.
Flash runs in DIO at 80 MHz.

Check the module marking. An image for N16R8 does not suit N4/N8 modules (smaller flash, no octal PSRAM)
or N32R16V/N16R16VA modules (octal flash). Other boards need their own DTS/flash configuration and validation;
do not assume compatibility from the ESP32-S3 chip name alone.

The DevKitC has two USB-C connectors, and ZephyrForge uses both:

| Connector | ESP32-S3 interface | Used for |
| --- | --- | --- |
| **USB** | Native USB OTG (GPIO19 D- / GPIO20 D+) | Management: USB HID, VID:PID `CAFE:4021`, read by the Python and .NET dashboards and by Direct over WebHID |
| **UART** | USB-to-UART bridge to UART0 (GPIO43 TX / GPIO44 RX) | ROM flashing and the Zephyr diagnostic shell/log at 115200 baud, 8N1 |

Connect both when you flash and manage the board from one computer. Native USB exposes HID management and a
removable MSC drive with explicit SD ownership handoff; there is no CDC ACM console. After a Control key is saved over USB,
Bluetooth LE management needs only power; see the [wireless guide](wireless.md).
For UART or raw TCP diagnostics, see the [console guide](console.md).

## SD-card SDMMC wiring

Use a 3.3 V-compatible SD socket exposing CLK, CMD and DAT0-DAT3, with suitable external command/data pull-ups,
short wires and adequate regulated power. A SPI-only breakout is insufficient for four-bit operation.
Wire with power disconnected. Do not apply 5 V to MCU GPIOs or a bare SD card.
This is proposed wiring, not electrically qualified hardware.

The alternate SD-over-SPI image supports a 3.3 V SPI socket/module without DAT1/DAT2 wiring.
Follow the [SD pin allocation](../reference/sdcard.md#pin-allocation) for the selected board and image, including
its card-detect polarity, supply and ground. On the ESP32-S3, GPIO8 detection is active-low and an unwired input
reads absent and prevents mounting.

On the ESP32-S3, GPIO assignments use explicit hardware pinctrl for native SDMMC slot 0 or SPI3.
The ESP32-S3 has 512 KiB of internal SRAM and no on-chip flash. N16R8 specifies the module's 16 MiB SPI flash
and 8 MiB octal PSRAM; both are external to the SoC. PSRAM is volatile RAM, not additional flash.
The Module memory details separate SRAM reservation, PSRAM reservation/capacity and detected module-flash
capacity. Reservations include linked regions and permanent heap pools, not live usage. Unknown or unavailable
inventory remains `--`; it is not inferred from the board name. No additional flash chip or interface is configured.

SPI2 is dedicated to the onboard WS2812-compatible RGB LED, with only MOSI allocated.
SPI3 is disabled in the native image and dedicated to the SD card in the SPI image.
The MOSI pin has a pull-down configured. Startup sends Off twice, separated by the reset/latch interval, so the
first SPI transfer's output configuration cannot leave a startup colour latched.
The default LED data pin is GPIO48. This is unverified for the diymore dual-USB-C N16R8 clone; official DevKitC-1
revisions use GPIO48 (v1.0) or GPIO38 (v1.1). For GPIO38, change the LED pinctrl selection from
`SPIM2_MOSI_GPIO48` to `SPIM2_MOSI_GPIO38` in the hardware overlay and rebuild. Reported board facts derive from that
selection. No SPI clock, MISO or chip-select pin is assigned. The driver uses a low fixed intensity, boots Off and
transmits an eight-bit SPI waveform at up to 6.4 MHz with a 350-microsecond low reset/latch interval. SPI2 must use
`line-idle-low`: the ESP32 SPI driver's default high idle level prevents that interval even when transmission succeeds.
GPIO choice, waveform and optical colour still require physical qualification. A logged transmission failure disables
LED control until reboot.

Some YD-ESP32-S3-style clones have a solder link marked **RGB**, immediately beside the white RGB LED, between
GPIO48 and the LED data input. An open link leaves the LED dark even when SPI transmissions succeed; inspect
this before trying another GPIO. See the [board-family pinout](https://github.com/vcc-gnd/YD-ESP32-S3) and
[RGB-link reference photo](https://github.com/HiraAbbas/Esp32-s3-Onboard-Leds-Rgb-Blinking/blob/main/rgb_solder_jumper.jpeg).
Disconnect both USB cables and any external power before inspecting or modifying a verified RGB link.
Do not confuse it with **IN-OUT** or **USB-OTG**, which change power connections. A matching product photo does
not establish the link's condition on the actual board.

Octal PSRAM occupies GPIO33-37, so those pins are not available on N16R8.
Leave boot-strapping and onboard flash/PSRAM pins alone. Consult your exact board schematic.
The ESP32-S3 requested SD bus ceiling is 40 MHz for native SDMMC and 25 MHz for SPI; this is not a measured bus
frequency or throughput claim. Native speed is negotiated with the card; higher-speed wiring still needs write
qualification.

Prepare an existing FAT16, FAT32 or exFAT volume on your computer and back up its data.
Cards may be inserted after startup or replaced; detection mounts them for firmware, not automatically for USB.
Power switching is not implemented. Power-cycle the card when switching between native SDMMC and SPI images;
a board-only reset may leave the card in SPI mode.
Physical unplugging during I/O may corrupt data. Safely eject the USB drive before confirming return to firmware.
See the [SD contract](../reference/sdcard.md) for data-safety behavior.

No board or SD card was attached during project setup. Wiring and radio operation await hardware.

## Raspberry Pi Pico targets

| Board | Zephyr target | MCU | SRAM | Flash |
| --- | --- | --- | --- | --- |
| Raspberry Pi Pico | `rpi_pico/rp2040` | RP2040 | 264 KiB | 2 MB |
| Raspberry Pi Pico W | `rpi_pico/rp2040/w` | RP2040 | 264 KiB | 2 MB |
| Raspberry Pi Pico 2 | `rpi_pico2/rp2350a/m33` | RP2350A, Arm cores | 520 KiB | 4 MB |
| Raspberry Pi Pico 2 W | `rpi_pico2/rp2350a/m33/w` | RP2350A, Arm cores | 520 KiB | 4 MB |

Flash the image built for the exact board: the reported board model comes from the build, not from detection, and
RP2040 and RP2350 images are not interchangeable. Flash is the board's external QSPI chip; there is no PSRAM.
Each image links into a 1 MiB code partition; the rest of the flash is the
[settings reservation](../reference/settings.md#flash-reservation).

| Connector or pins | Used for |
| --- | --- |
| Micro-USB | Native USB management HID (VID:PID `CAFE:4021`) and the removable MSC drive; UF2 flashing in BOOTSEL mode |
| GP0 TX (pin 1), GP1 RX (pin 2), GND (pin 3) | UART0 Zephyr diagnostic shell/log at 115200 baud, 8N1 |
| GP2-GP8, 3V3(OUT) (pin 36), GND (pin 38) | SD card; see the [Pico SD pin allocation](../reference/sdcard.md#raspberry-pi-pico-boards) |

The boards have no USB-UART bridge. For diagnostics, connect a 3.3 V USB-UART adapter's RX to GP0, TX to GP1 and
ground to GND only; do not connect its supply output. Flash by holding BOOTSEL while connecting micro-USB and
copying the UF2 image, as described in [getting started](getting-started.md#2-build-now-flash-later).

The Pico images are managed over USB only. Pico W and Pico 2 W use the CYW43439 radio for
[Wi-Fi scans and temporary joins](wireless.md); Bluetooth LE remains unsupported. The radio uses PIO0 with GP23
enable, GP24 shared data/host wake, GP25 chip select and GP29 clock; native SD uses PIO1.
The images do not control the onboard LED, cannot save settings
and keep the default system clock: 125 MHz on RP2040 and 150 MHz on RP2350.

GP2-GP8 are reserved for the SD card in both images; the board's default I2C0 (GP4/GP5) and I2C1 (GP6/GP7)
assignments are disabled because they share those pins. The SD wiring precautions, card preparation, power cycling
when changing images and safe ejection described above apply unchanged, but card detection is active-high with no
MCU pull. The Pico images are software builds only; see the [qualification gaps](../validation/qualification-gaps.md).
