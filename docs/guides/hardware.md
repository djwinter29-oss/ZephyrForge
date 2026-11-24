# Hardware setup

Use the image built for your exact board and memory variant. ZephyrForge supports ESP32-S3-DevKitC-1 N16R8,
ESP32-C6-DevKitC-1 N8, and the four Raspberry Pi Pico boards below.

- [ESP32-S3 N16R8](#esp32-s3-devkitc-1-n16r8)
- [ESP32-C6 N8](#esp32-c6-devkitc-1-n8)
- [Pico family](#raspberry-pi-pico-targets)
- [SD-card wiring](#sd-card-wiring)

## ESP32-S3-DevKitC-1 N16R8

Check for the **ESP32-S3-WROOM-1-N16R8** module: 16 MB quad flash and 8 MB octal PSRAM.
The Zephyr target is `esp32s3_devkitc/esp32s3/procpu`; only PROCPU runs an application.
N4/N8 modules and octal-flash N32R16V/N16R16VA modules need different configuration. Do not infer compatibility
from the ESP32-S3 name alone.

### Connectors

| Connector | Pins/interface | Use |
| --- | --- | --- |
| **USB** | Native USB OTG, GPIO19 D- / GPIO20 D+ | USB HID management, VID:PID `CAFE:4021`; MSC in storage-enabled images |
| **UART** | USB-UART bridge, UART0 GPIO43 TX / GPIO44 RX | ROM flashing and console at 115200 baud, 8N1 |

Connect both when flashing and managing from one computer. There is no native USB CDC console.
Once a Control key is saved over USB, BLE management needs only board power; see [wireless setup](wireless.md).

### Memory and reserved pins

The SoC has 512 KiB internal SRAM and no on-chip flash. The module's flash and PSRAM are external; PSRAM is volatile
RAM, not extra flash. Flash runs in DIO at 80 MHz. Octal PSRAM runs at 40 MHz with a boot-time memory test and a
separate 1 MB shared multi-heap. The kernel heap and Wi-Fi/BLE buffers stay in internal RAM.
The [settings reference](../reference/settings.md#flash-reservation) defines the reserved flash region.

GPIO33-37 serve octal PSRAM and are unavailable on N16R8. Leave flash/PSRAM and boot-strapping pins alone; check
the schematic for the exact board revision.

The dashboard distinguishes SRAM reservation, PSRAM reservation/capacity, and module-flash capacity. Reservations
include linked regions and permanent heap pools, not live usage. Unknown inventory stays `--`; it is not inferred
from the board name. No additional flash chip or interface is configured.

### Onboard RGB LED

SPI2 drives the WS2812-compatible LED through MOSI only; no clock, MISO, or chip select is assigned.
SPI3 is disabled in the native SDMMC image and used by the card in the SPI image.

The default LED pin is GPIO48. Official DevKitC-1 revisions use GPIO48 (v1.0) or GPIO38 (v1.1); the default is
unverified for the diymore dual-USB-C N16R8 clone. For GPIO38, select `SPIM2_MOSI_GPIO38` instead of
`SPIM2_MOSI_GPIO48` in the hardware overlay and rebuild. Reported board facts follow that selection.

The driver uses low fixed intensity, boots Off, and sends Off twice with a reset/latch interval between transfers.
MOSI has a pull-down. The waveform uses eight-bit SPI at up to 6.4 MHz and a 350-microsecond low reset interval.
SPI2 needs `line-idle-low`; the ESP32 driver's default high idle level prevents the reset interval even if
transmission succeeds. A logged transmission failure disables LED control until reboot.

Some YD-ESP32-S3-style clones have a solder link marked **RGB** beside the white LED. An open link disconnects
GPIO48 from the LED input. Check the [board-family pinout](https://github.com/vcc-gnd/YD-ESP32-S3) and
[reference photo](https://github.com/HiraAbbas/Esp32-s3-Onboard-Leds-Rgb-Blinking/blob/main/rgb_solder_jumper.jpeg),
but verify the actual board before changing anything. Disconnect both USB cables and all external power first.
Do not confuse **RGB** with **IN-OUT** or **USB-OTG**, which alter power connections.
Pin choice, waveform, and optical colour still need qualification on each board revision.

## ESP32-C6-DevKitC-1 N8

The software target is `esp32c6_devkitc/esp32c6/hpcore`, configured for the ESP32-C6-WROOM-1U-N8 module:
8 MB flash, no PSRAM, and a maximum CPU clock of 160 MHz. This project does not control the onboard LED.

Use UART for flashing and console output. There is no native USB HID/MSC interface.
Direct BLE management requires a saved Control key; follow [initial commissioning](boards/esp32c6-commissioning.md).
Fresh images do not advertise before that key exists.

Storage choices are `none` and SD-over-SPI, not SDMMC. The SPI2 image uses GPIO2 MISO, GPIO6 SCK, GPIO7 MOSI,
GPIO10 active-low CS, and GPIO3 active-low card detect. It disables I2C0 because the default GPIO6/7 allocation
conflicts. See [C6 SPI wiring](boards/esp32c6-sd-spi.md).

C6 boot, radios, commissioning, and card wiring have not been physically qualified.

## Raspberry Pi Pico targets

| Board | Zephyr target | MCU | SRAM | Flash |
| --- | --- | --- | --- | --- |
| Pico | `rpi_pico/rp2040` | RP2040 | 264 KiB | 2 MB |
| Pico W | `rpi_pico/rp2040/w` | RP2040 | 264 KiB | 2 MB |
| Pico 2 | `rpi_pico2/rp2350a/m33` | RP2350A, Arm cores | 520 KiB | 4 MB |
| Pico 2 W | `rpi_pico2/rp2350a/m33/w` | RP2350A, Arm cores | 520 KiB | 4 MB |

RP2040 and RP2350 images are not interchangeable. The reported board model comes from the build, not detection.
Flash is the external QSPI chip; there is no PSRAM. Images use a 1 MiB code partition, with the remaining flash
reserved for [settings](../reference/settings.md#flash-reservation).

| Connection | Use |
| --- | --- |
| Micro-USB | HID management (`CAFE:4021`), MSC when built in, and UF2 flashing in BOOTSEL mode |
| GP0 TX (pin 1), GP1 RX (pin 2), GND (pin 3) | UART0 console at 115200 baud, 8N1 |
| GP2-GP8, 3V3(OUT) (pin 36), GND (pin 38) | SD wiring in storage-enabled images |

There is no USB-UART bridge. Connect a 3.3 V adapter's RX to GP0, TX to GP1, and ground to GND; leave its supply
output disconnected. Install firmware with the [BOOTSEL procedure](flashing.md#raspberry-pi-pico-release-image).

All four boards use USB management. W boards also support [Wi-Fi scans and temporary joins](wireless.md), not BLE.
The CYW43439 radio uses PIO0 with GP23 enable, GP24 shared data/host wake, GP25 chip select, and GP29 clock.
Native SD uses PIO1.

Pico images do not control the onboard LED or save settings. They keep the build clock: 125 MHz on RP2040,
150 MHz on RP2350. SD-enabled images reserve GP2-GP8 and disable default I2C0 (GP4/GP5) and I2C1 (GP6/GP7)
assignments. Card detect is active-high with no MCU pull; the breakout must drive or bias it.
These targets have software builds only.

## SD-card wiring

Choose the guide for both the board and storage image:

| Board | Four-bit SDMMC | SD-over-SPI |
| --- | --- | --- |
| ESP32-S3 N16R8 | [Connections and pull-ups](boards/esp32s3-sdmmc.md) | [Connections and pull-ups](boards/esp32s3-sd-spi.md) |
| ESP32-C6 N8 | Not supported | [Connections and pull-ups](boards/esp32c6-sd-spi.md) |
| Pico family | [Connections and pull-ups](boards/pico-sdmmc.md) | [Connections and pull-ups](boards/pico-sd-spi.md) |

Wire with all power disconnected. Use a 3.3 V-compatible socket, suitable external pull-ups, short wires, and
adequate regulated power. Never apply 5 V to MCU GPIOs or a bare card. Four-bit SDMMC needs CLK, CMD, and DAT0-DAT3;
a SPI-only breakout is insufficient.

Back up the card and prepare FAT16, FAT32, or exFAT on a computer. Firmware does not automatically format it.
Insertion mounts the card for firmware, not automatically for USB. Safely eject in the OS before confirming
return from USB ownership; removal during I/O can corrupt data.

There is no card power switching. Disconnect card power when changing between SDMMC and SPI images; resetting
only the board may leave the card in SPI mode. See [SD storage](../reference/sdcard.md) for mount and ownership
behavior, and [qualification gaps](../validation/qualification-gaps.md) for current hardware evidence.
