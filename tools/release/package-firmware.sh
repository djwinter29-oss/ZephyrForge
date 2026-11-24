#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -euo pipefail

case "$STORAGE" in
  sdmmc|sd-spi|none) ;;
  *) printf 'Error: STORAGE must be sdmmc, sd-spi or none.\n' >&2; exit 1 ;;
esac
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
        printf 'Error: release flashing requires runner option %s.\n' "$expected" >&2
        exit 1
      fi
    done
    image=zephyr.bin
    components=(zephyr modules/hal/espressif modules/crypto/mbedtls) ;;
  esp32c6)
    if [[ "$STORAGE" == sdmmc ]]; then
      printf 'Error: ESP32-C6 supports SD over SPI or no storage, not native SDMMC.\n' >&2
      exit 1
    fi
    target=esp32c6_devkitc/esp32c6/hpcore
    if ! grep -Fxq "CONFIG_BOARD_TARGET=\"$target\"" "$FIRMWARE_BUILD/zephyr/.config"; then
      printf 'Error: ESP32-C6 packaging requires board target %s.\n' "$target" >&2
      exit 1
    fi
    image=zephyr.bin
    components=(zephyr modules/hal/espressif modules/crypto/mbedtls) ;;
  pico) target=rpi_pico/rp2040 ;;
  pico_w) target=rpi_pico/rp2040/w ;;
  pico2) target=rpi_pico2/rp2350a/m33 ;;
  pico2_w) target=rpi_pico2/rp2350a/m33/w ;;
  *) printf 'Error: BOARD must be esp32s3, esp32c6, pico, pico_w, pico2 or pico2_w.\n' >&2; exit 1 ;;
esac
if [[ "$board" == pico* ]]; then
  if ! grep -Fxq "CONFIG_BOARD_TARGET=\"$target\"" "$FIRMWARE_BUILD/zephyr/.config"; then
    printf 'Error: %s packaging requires board target %s.\n' "$board" "$target" >&2
    exit 1
  fi
  image=zephyr.uf2
  components=(zephyr modules/hal/rpi_pico modules/hal/cmsis_6)
  if [[ "$board" == pico_w || "$board" == pico2_w ]]; then
    components+=(modules/hal/infineon)
  fi
fi
package=zephyrforge/build/firmware-package
mkdir -p "$package/licenses"
cp zephyrforge/LICENSE "$package/LICENSE"
cp "$FIRMWARE_BUILD/zephyr/$image" "$package/"
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
  elif [[ "$board" == esp32c6 ]]; then
    printf 'Board: esp32c6_devkitc/esp32c6/hpcore\nModule: ESP32-C6-WROOM-1U-N8\nZephyr SDK: 0.17.4\n'
    printf 'Management: authenticated BLE; USB Serial/JTAG is console/flashing only.\n'
    printf 'Provisioning: BLE requires a Control key already present in saved settings.\n'
  else
    printf 'Board: %s\nZephyr SDK: 0.17.4\n' "$target"
    if [[ "$board" == pico_w || "$board" == pico2_w ]]; then
      printf 'Radio: CYW43439 Wi-Fi only; Bluetooth transport is unsupported.\n'
      sha256sum modules/hal/infineon/zephyr/blobs/img/whd/resources/firmware/COMPONENT_43439/43439A0.bin \
        modules/hal/infineon/zephyr/blobs/img/whd/resources/clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0.clm_blob
    fi
  fi
  printf 'Software build only; not flashed or hardware-qualified. SD testing is deferred.\n'
} > "$package/BUILD-INFO.txt"
