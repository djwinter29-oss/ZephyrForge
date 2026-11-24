#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -euo pipefail

case "$STORAGE" in
  sdmmc|sd-spi|none) ;;
  *) printf 'Error: STORAGE must be sdmmc, sd-spi or none.\n' >&2; exit 1 ;;
esac
board="${BOARD:-esp32s3}"
config="$FIRMWARE_BUILD/zephyr/.config"
enabled() {
  grep -Eq "^CONFIG_$1=(y|m)$" "$config"
}
require_enabled() {
  if ! enabled "$1"; then
    printf 'Error: %s %s packaging requires CONFIG_%s.\n' "$board" "$STORAGE" "$1" >&2
    exit 1
  fi
}
require_disabled() {
  if enabled "$1"; then
    printf 'Error: %s %s packaging must not include CONFIG_%s.\n' "$board" "$STORAGE" "$1" >&2
    exit 1
  fi
}
require_runner_option() {
  if ! grep -Eq "^[[:space:]]+- $1$" "$FIRMWARE_BUILD/zephyr/runners.yaml"; then
    printf 'Error: release flashing requires runner option %s.\n' "$1" >&2
    exit 1
  fi
}
case "$board" in
  esp32s3)
    target=esp32s3_devkitc/esp32s3/procpu
    for expected in 'CONFIG_ESP_SIMPLE_BOOT=y' 'CONFIG_FLASH_SIZE=16777216' \
      "CONFIG_BOARD_TARGET=\"$target\""; do
      if ! grep -Fxq "$expected" "$FIRMWARE_BUILD/zephyr/.config"; then
        printf 'Error: N16R8 simple-boot packaging requires %s.\n' "$expected" >&2
        exit 1
      fi
    done
    for expected in --esp-app-address=0x0 --esp-flash-mode=dio \
      --esp-flash-freq=80m --esp-flash-size=16MB; do
      require_runner_option "$expected"
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
    for expected in 'CONFIG_ESP_SIMPLE_BOOT=y' 'CONFIG_FLASH_SIZE=8388608'; do
      if ! grep -Fxq "$expected" "$FIRMWARE_BUILD/zephyr/.config"; then
        printf 'Error: ESP32-C6 release packaging requires %s.\n' "$expected" >&2
        exit 1
      fi
    done
    for expected in --esp-app-address=0x0 --esp-flash-mode=dio \
      --esp-flash-freq=80m --esp-flash-size=8MB; do
      require_runner_option "$expected"
    done
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
case "$STORAGE" in
  none)
    for symbol in USBD_MSC_CLASS DISK_ACCESS DISK_DRIVER_SDMMC SDHC SDHC_ESP32 SPI_SDHC \
      ZF_SDHC_RPI_PICO_PIO FILE_SYSTEM FAT_FILESYSTEM_ELM FS_FATFS_EXFAT; do
      require_disabled "$symbol"
    done ;;
  sdmmc|sd-spi)
    for symbol in DISK_ACCESS DISK_DRIVER_SDMMC SDHC FILE_SYSTEM FAT_FILESYSTEM_ELM FS_FATFS_EXFAT; do
      require_enabled "$symbol"
    done
    if [[ "$STORAGE" == sdmmc ]]; then
      case "$board" in
        esp32s3) expected_driver=SDHC_ESP32 ;;
        pico|pico_w|pico2|pico2_w) expected_driver=ZF_SDHC_RPI_PICO_PIO ;;
        esp32c6)
          printf 'Error: ESP32-C6 supports SD over SPI or no storage, not native SDMMC.\n' >&2
          exit 1 ;;
      esac
      require_enabled "$expected_driver"
      require_disabled SPI_SDHC
    else
      require_enabled SPI_SDHC
      require_disabled SDHC_ESP32
      require_disabled ZF_SDHC_RPI_PICO_PIO
    fi ;;
esac
build_identity="$FIRMWARE_BUILD/zephyrforge-build-info.txt"
if [[ ! -s "$build_identity" ]]; then
  printf 'Error: missing build identity snapshot: %s.\n' "$build_identity" >&2
  exit 1
fi
build_value() {
  local value
  value="$(sed -n "s/^$1=//p" "$build_identity")"
  if [[ -z "$value" ]]; then
    printf 'Error: build identity is missing %s.\n' "$1" >&2
    exit 1
  fi
  printf '%s' "$value"
}
firmware_version="$(build_value FIRMWARE_VERSION)"
source_commit="$(build_value SOURCE_COMMIT)"
source_dirty="$(build_value SOURCE_DIRTY)"
build_board="$(build_value BOARD)"
build_target="$(build_value TARGET)"
build_storage="$(build_value STORAGE)"
image_hash="$(build_value IMAGE_SHA256)"
version_helper="$(cd "$(dirname "${BASH_SOURCE[0]}")/../firmware" && pwd)/release-version.sh"
if ! bash "$version_helper" "v$firmware_version" >/dev/null 2>&1; then
  printf 'Error: unsupported firmware version in build identity snapshot: %s.\n' "$build_identity" >&2
  exit 1
fi
if [[ ! "$source_commit" =~ ^[0-9a-f]{40}$ || "$source_dirty" != true && "$source_dirty" != false ||
  ! "$image_hash" =~ ^[0-9a-f]{64}$ ]]; then
  printf 'Error: malformed build identity snapshot: %s.\n' "$build_identity" >&2
  exit 1
fi
if [[ "$build_board" != "$board" || "$build_target" != "$target" || "$build_storage" != "$STORAGE" ]]; then
  printf 'Error: build identity does not match BOARD=%s STORAGE=%s.\n' "$board" "$STORAGE" >&2
  exit 1
fi
actual_image_hash="$(sha256sum "$FIRMWARE_BUILD/zephyr/$image" | awk '{print $1}')"
if [[ "$actual_image_hash" != "$image_hash" ]]; then
  printf 'Error: firmware image does not match build identity snapshot: %s.\n' "$build_identity" >&2
  exit 1
fi
if [[ "$board" == pico_w || "$board" == pico2_w ]]; then
  radio_firmware_hash="$(build_value RADIO_FIRMWARE_SHA256)"
  radio_clm_hash="$(build_value RADIO_CLM_SHA256)"
  if [[ ! "$radio_firmware_hash" =~ ^[0-9a-f]{64}$ || ! "$radio_clm_hash" =~ ^[0-9a-f]{64}$ ]]; then
    printf 'Error: malformed radio hashes in build identity snapshot: %s.\n' "$build_identity" >&2
    exit 1
  fi
fi
package_root=zephyrforge/build
package="$package_root/firmware-package"
mkdir -p "$package_root"
staging="$(mktemp -d "$package_root/.firmware-package.XXXXXX")"
trap 'rm -rf "$staging"' EXIT
cp zephyrforge/LICENSE "$staging/LICENSE"
cp "$FIRMWARE_BUILD/zephyr/$image" "$staging/"
for component in "${components[@]}"; do
  find "$component" -name .git -prune -o -type f \
    \( -iname 'license*' -o -iname 'copying*' -o -iname 'notice*' \
       -o -iname 'copyright*' -o -iname '*third*notice*' \) -print0 |
  while IFS= read -r -d '' source; do
    destination="$staging/licenses/$source"
    mkdir -p "$(dirname "$destination")"
    cp "$source" "$destination"
  done
done
mkdir -p "$staging/licenses"
sed -n '1,/\*\//p' modules/fs/fatfs/ff.c > "$staging/licenses/fatfs-LICENSE"
{
  printf 'Firmware version: %s\n' "$firmware_version"
  printf 'Source commit: %s\nSource tree dirty: %s\n' "$source_commit" "$source_dirty"
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
      printf 'CYW43439 Wi-Fi firmware SHA-256: %s\nCYW43439 CLM blob SHA-256: %s\n' \
        "$radio_firmware_hash" "$radio_clm_hash"
    fi
  fi
  printf 'Software build only; not flashed or hardware-qualified. SD testing is deferred.\n'
} > "$staging/BUILD-INFO.txt"
previous=""
if [[ -e "$package" ]]; then
  previous="$(mktemp -d "$package_root/.firmware-package.previous.XXXXXX")"
  rmdir "$previous"
  mv "$package" "$previous"
fi
if ! mv "$staging" "$package"; then
  if [[ -n "$previous" && -e "$previous" ]]; then
    mv "$previous" "$package"
  fi
  printf 'Error: could not install staged firmware package at %s.\n' "$package" >&2
  exit 1
fi
trap - EXIT
if [[ -n "$previous" && -e "$previous" ]]; then
  rm -rf "$previous"
fi
