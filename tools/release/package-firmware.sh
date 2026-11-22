#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -euo pipefail

board="${BOARD:-esp32s3}"
case "$board" in
  esp32s3)
    for expected in 'CONFIG_ESP_SIMPLE_BOOT=y' 'CONFIG_FLASH_SIZE=16777216' \
      'CONFIG_BOARD_TARGET="esp32s3_devkitc/esp32s3/procpu"'; do
      if ! grep -Fxq "$expected" "$FIRMWARE_BUILD/zephyr/.config"; then
        printf 'Error: N16R8 simple-boot packaging requires %s.\n' "$expected" >&2
        exit 1
      fi
    done
    for expected in --esp-app-address=0x0 --esp-flash-mode=dio \
      --esp-flash-freq=80m --esp-flash-size=16MB; do
      if ! grep -Fq -- "$expected" "$FIRMWARE_BUILD/zephyr/runners.yaml"; then
        printf 'Error: packaged flashing instructions require runner option %s.\n' "$expected" >&2
        exit 1
      fi
    done
    image=zephyr.bin
    components=(zephyr modules/hal/espressif modules/crypto/mbedtls) ;;
  pico) target=rpi_pico/rp2040; title="Raspberry Pi Pico / RP2040" ;;
  pico_w) target=rpi_pico/rp2040/w; title="Raspberry Pi Pico W / RP2040" ;;
  pico2) target=rpi_pico2/rp2350a/m33; title="Raspberry Pi Pico 2 / RP2350A Cortex-M33" ;;
  pico2_w) target=rpi_pico2/rp2350a/m33/w; title="Raspberry Pi Pico 2 W / RP2350A Cortex-M33" ;;
  *) printf 'Error: BOARD must be esp32s3, pico, pico_w, pico2 or pico2_w.\n' >&2; exit 1 ;;
esac
if [[ "$board" != esp32s3 ]]; then
  if ! grep -Fxq "CONFIG_BOARD_TARGET=\"$target\"" "$FIRMWARE_BUILD/zephyr/.config"; then
    printf 'Error: %s packaging requires board target %s.\n' "$board" "$target" >&2
    exit 1
  fi
  image=zephyr.uf2
  components=(zephyr modules/hal/rpi_pico modules/hal/cmsis_6)
fi
package=zephyrforge/build/firmware-package
mkdir -p "$package/licenses"
cp zephyrforge/LICENSE "$package/LICENSE"
cp "$FIRMWARE_BUILD"/zephyr/{"$image",zephyr.elf,.config,zephyr.dts,runners.yaml} "$package/"
cp "$FIRMWARE_BUILD/west-manifest.yml" "$package/"
cp -R zephyrforge/docs "$package/docs"
for component in "${components[@]}"; do
  find "$component" -name .git -prune -o -type f \
    \( -iname 'license*' -o -iname 'copying*' -o -iname 'notice*' \
       -o -iname 'copyright*' -o -iname '*third*notice*' \) -print0 |
  while IFS= read -r -d '' source; do
    destination="$package/licenses/$source"
    mkdir -p "$(dirname "$destination")"
    cp "$source" "$destination"
  done
done
sed -n '1,/\*\//p' modules/fs/fatfs/ff.c > "$package/licenses/fatfs-LICENSE"
source_version=$(sed -n 's/^#define ZEPHYRFORGE_VERSION "\(.*\)"/\1/p' zephyrforge/firmware/src/app/zephyrforge.h)
{
  printf 'Firmware version: %s\n' "${ZEPHYRFORGE_VERSION:-$source_version}"
  printf 'Source commit: %s\n' "$(git -C zephyrforge rev-parse HEAD)"
  printf 'Storage image: %s\n' "$STORAGE"
  if [[ "$board" == esp32s3 ]]; then
    printf 'Board: esp32s3_devkitc/esp32s3/procpu\nModule: ESP32-S3-WROOM-1-N16R8\nZephyr SDK: 0.17.4\n'
  else
    printf 'Board: %s\nZephyr SDK: 0.17.4\n' "$target"
  fi
  printf 'Software build only; not flashed or hardware-qualified. SD testing is deferred.\n'
} > "$package/BUILD-INFO.txt"
if [[ "$board" != esp32s3 ]]; then
  cat > "$package/FLASHING.txt" <<EOF
Target: $title, UF2 image.
This is a software build, not a hardware-qualified release.
Check docs/guides/hardware.md and docs/validation/qualification-gaps.md first.
BUILD-INFO.txt identifies the storage variant. Use only its matching SD wiring.
Disconnect card power before changing between native SDMMC and SD-over-SPI images.

Hold BOOTSEL while connecting the micro-USB cable; the board appears as a USB drive.
Copy zephyr.uf2 to that drive. The board restarts into ZephyrForge, and the same USB
connector then carries the ZephyrForge HID management interface.
Console logs use UART0: GP0 TX and GP1 RX at 115200 baud.
Copy success does not verify application boot or SD behavior.
Do not use this image on a different board.
EOF
  exit 0
fi
cat > "$package/FLASHING.txt" <<'EOF'
Target: ESP32-S3-DevKitC-1 N16R8 / 16 MB flash, 8 MB octal PSRAM, PROCPU, simple-boot image.
This is a software build, not a hardware-qualified release.
Check docs/guides/hardware.md and docs/validation/qualification-gaps.md first.
BUILD-INFO.txt identifies the storage variant. Use only its matching SD wiring.
Disconnect card power before changing between native SDMMC and SD-over-SPI images.

Close all serial clients. Flash through the UART (USB-to-UART) connector;
the USB connector carries the ZephyrForge HID management interface after boot.
Create/activate a Python virtual environment, then install esptool 5:
  python -m pip install "esptool>=5,<6"
Replace PORT below with the actual serial port:
  python -m esptool --chip esp32s3 --port PORT --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 zephyr.bin
This overwrites image sectors; it does not request a full-chip erase.
Do not use this image on a different flash/module configuration.
If reset fails, hold BOOT, press/release RESET, release BOOT and retry.
Uploader success does not verify application boot, Wi-Fi, BLE or SD behavior.

runners.yaml is build provenance, not a relocatable west build directory.
For the source-build west flash wrapper, follow docs/guides/getting-started.md.
EOF
