# Boards and connections

[Documentation](../README.md) / Hardware

Use the image built for your exact board and memory variant. This is the supported board list, not a claim that
every combination has passed physical tests; see [recorded coverage](../validation/README.md#board-coverage).

| Board | Dashboard connection | SD options |
| --- | --- | --- |
| [ESP32-S3-DevKitC-1 N16R8](#esp32-s3-devkitc-1-n16r8) | USB HID; authenticated BLE | Four-bit SDMMC, SPI, or none |
| [ESP32-C6-DevKitC-1 N8](#esp32-c6-devkitc-1-n8) | Authenticated BLE after [commissioning](../guides/esp32c6-commissioning.md) | SPI or none |
| [Pico, Pico 2](#raspberry-pi-pico-targets) | USB HID | Four-bit SDMMC, SPI, or none |
| [Pico W, Pico 2 W](#raspberry-pi-pico-targets) | USB HID | Four-bit SDMMC, SPI, or none |

For storage, follow [SD-card wiring](sd-card-wiring.md), then [prepare and use the card](../guides/sd-card.md).

## ESP32-S3-DevKitC-1 N16R8

Check for the **ESP32-S3-WROOM-1-N16R8** module: 16 MB quad flash and 8 MB octal PSRAM.
The Zephyr target is `esp32s3_devkitc/esp32s3/procpu`; only PROCPU runs an application.
N4/N8 modules and octal-flash N32R16V/N16R16VA modules need different configuration. Do not infer compatibility
from the ESP32-S3 name alone.

### Connectors

| Connector | Pins/interface | Use |
| --- | --- | --- |
| **USB** | Native USB OTG, GPIO19 D- / GPIO20 D+ | USB HID management; MSC when built with storage and USB export |
| **UART** | USB-UART bridge, UART0 GPIO43 TX / GPIO44 RX | ROM flashing and console at 115200 baud, 8N1 |

Connect both when flashing and managing from one computer. There is no native USB CDC console.
Once a Control key is saved over USB, BLE management needs only board power;
see [wireless setup](../guides/wireless.md).

### Memory and reserved pins

The SoC has 512 KiB internal SRAM and no on-chip flash. The module's flash and PSRAM are external; PSRAM is volatile
RAM, not extra flash. Flash runs in DIO at 80 MHz. Octal PSRAM runs at 40 MHz with a boot-time memory test and a
separate 1 MB shared multi-heap. The kernel heap and Wi-Fi/BLE buffers stay in internal RAM.
The [settings reference](../reference/boot-settings.md#flash-reservation) defines the reserved flash region.

GPIO33-37 serve octal PSRAM and are unavailable on N16R8. Leave flash/PSRAM and boot-strapping pins alone; check
the schematic for the exact board revision.

### Onboard RGB LED

The default LED pin is GPIO48. Official DevKitC-1 revisions use GPIO48 (v1.0) or GPIO38 (v1.1); the default is
unverified for the diymore dual-USB-C N16R8 clone. For GPIO38, select `SPIM2_MOSI_GPIO38` instead of
`SPIM2_MOSI_GPIO48` in the hardware overlay and rebuild. Reported board facts follow that selection.

Some YD-ESP32-S3-style clones have a solder link marked **RGB** beside the white LED. An open link disconnects
GPIO48 from the LED input. Check the [board-family pinout](https://github.com/vcc-gnd/YD-ESP32-S3) and
[reference photo](https://github.com/HiraAbbas/Esp32-s3-Onboard-Leds-Rgb-Blinking/blob/main/rgb_solder_jumper.jpeg),
but verify the actual board before changing anything. Disconnect both USB cables and all external power first.
Do not confuse **RGB** with **IN-OUT** or **USB-OTG**, which alter power connections.

## ESP32-C6-DevKitC-1 N8

The software target is `esp32c6_devkitc/esp32c6/hpcore`, configured for the ESP32-C6-WROOM-1U-N8 module:
8 MB flash, no PSRAM, and a maximum CPU clock of 160 MHz. This project does not control the onboard LED.

Use UART for flashing and console output. There is no native USB HID/MSC interface.
Direct BLE management requires a saved Control key; follow [initial commissioning](../guides/esp32c6-commissioning.md).
Fresh images do not advertise before that key exists.

Storage choices are `none` and SD-over-SPI, not SDMMC. The wiring guide lists its pin allocation.

## Raspberry Pi Pico targets

| Board | Zephyr target | MCU | SRAM | Flash |
| --- | --- | --- | --- | --- |
| Pico | `rpi_pico/rp2040` | RP2040 | 264 KiB | 2 MB |
| Pico W | `rpi_pico/rp2040/w` | RP2040 | 264 KiB | 2 MB |
| Pico 2 | `rpi_pico2/rp2350a/m33` | RP2350A, Arm cores | 520 KiB | 4 MB |
| Pico 2 W | `rpi_pico2/rp2350a/m33/w` | RP2350A, Arm cores | 520 KiB | 4 MB |

RP2040 and RP2350 images are not interchangeable. The reported board model comes from the build, not detection.
Flash is the external QSPI chip; there is no PSRAM. Images use a 1 MiB code partition, with the remaining flash
reserved for [settings](../reference/boot-settings.md#flash-reservation).

| Connection | Use |
| --- | --- |
| Micro-USB | HID management, MSC when built in, and UF2 flashing in BOOTSEL mode |
| GP0 TX (pin 1), GP1 RX (pin 2), GND (pin 3) | UART0 console at 115200 baud, 8N1 |
| GP2-GP8, 3V3(OUT) (pin 36), GND (pin 38) | SD wiring in storage-enabled images |

There is no USB-UART bridge. Connect a 3.3 V adapter's RX to GP0, TX to GP1, and ground to GND; leave its supply
output disconnected. Install firmware with the
[BOOTSEL procedure](../guides/flashing.md#raspberry-pi-pico-release-image).

All four boards use USB management. W boards also support
[Wi-Fi scans and temporary joins](../guides/wireless.md), not BLE.
The CYW43439 radio uses PIO0 with GP23 enable, GP24 shared data/host wake, GP25 chip select, and GP29 clock.
Native SD uses PIO1.

Pico images do not control the onboard LED or save settings. They keep the build clock: 125 MHz on RP2040,
150 MHz on RP2350. SD-enabled images reserve GP2-GP8 and disable default I2C0 (GP4/GP5) and I2C1 (GP6/GP7)
assignments. Card detect is active-high with no MCU pull; the breakout must drive or bias it.
