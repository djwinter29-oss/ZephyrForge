#!/usr/bin/env bash
set -euo pipefail
board=esp32s3
storage=sdmmc
storage_selected=false
conf=prj.conf
usb_config=";profiles/usb.conf"
storage_config=""
while (($#)); do
	case "$1" in
		--board)
			(($# >= 2)) || { printf 'Error: --board requires esp32s3, esp32c6, pico, pico_w, pico2 or pico2_w.\n' >&2; exit 1; }
			board="$2"; shift 2 ;;
		--storage)
			(($# >= 2)) || { printf 'Error: --storage requires sdmmc, sd-spi or none.\n' >&2; exit 1; }
			storage="$2"; storage_selected=true; shift 2 ;;
		-h|--help)
			printf 'Usage: bash tools/firmware/build.sh [--board esp32s3|esp32c6|pico|pico_w|pico2|pico2_w] [--storage sdmmc|sd-spi|none]\n'
			exit 0 ;;
		*) printf 'Error: unknown argument: %s\n' "$1" >&2; exit 1 ;;
	esac
done
if [[ "$board" == esp32c6 && "$storage_selected" == false ]]; then
	storage=none
fi
case "$storage" in
	sdmmc) build=firmware; mode=sdmmc; storage_enabled=ON; benchmark=ON
		storage_config=";profiles/storage.conf" ;;
	sd-spi) build=firmware-sd-spi; mode=sd_spi; storage_enabled=ON; benchmark=ON
		storage_config=";profiles/storage.conf" ;;
	none) build=firmware-no-storage; storage_enabled=OFF; benchmark=OFF ;;
	*) printf 'Error: --storage must be sdmmc, sd-spi or none.\n' >&2; exit 1 ;;
esac
case "$board" in
	esp32s3) target=esp32s3_devkitc/esp32s3/procpu; family=esp32s3
		conf+=";profiles/wifi.conf;profiles/bluetooth.conf;profiles/tcp-shell.conf;boards/esp32s3/board.conf"
		toolchain=xtensa-espressif_esp32s3_zephyr-elf ;;
	esp32c6) family=esp32c6
		usb_config=""
		benchmark=OFF
		[[ "$storage" != sdmmc ]] || {
			printf 'Error: ESP32-C6 supports SD over SPI or no storage, not native SDMMC.\n' >&2
			exit 1
		}
		target=esp32c6_devkitc/esp32c6/hpcore
		build="${build/firmware/firmware-esp32c6}"
		conf+=";profiles/wifi.conf;profiles/bluetooth.conf"
		toolchain=riscv64-zephyr-elf ;;
	pico) target=rpi_pico/rp2040; soc=rp2040 ;;
	pico_w) target=rpi_pico/rp2040/w; soc=rp2040 ;;
	pico2) target=rpi_pico2/rp2350a/m33; soc=rp2350 ;;
	pico2_w) target=rpi_pico2/rp2350a/m33/w; soc=rp2350 ;;
	*) printf 'Error: --board must be esp32s3, esp32c6, pico, pico_w, pico2 or pico2_w.\n' >&2; exit 1 ;;
esac
[[ "$board" != pico* ]] || family=rpi_pico
# Overlay order: family board, Pico SoC flash layout, storage wiring, then wireless tweaks.
overlay="boards/$family/board.overlay"
[[ -z "${soc:-}" ]] || overlay+=";boards/$family/soc/$soc.overlay"
[[ "$storage" == none ]] || overlay+=";boards/$family/storage/$mode.overlay"
if [[ "$board" == pico* ]]; then
	build="${build/firmware/firmware-$board}"
	if [[ "$board" == *_w ]]; then
		overlay+=";boards/rpi_pico/wireless/w.overlay"
		conf+=";profiles/wifi.conf;boards/rpi_pico/wireless/w.conf"
	fi
	toolchain=arm-zephyr-eabi
fi
conf+="$usb_config$storage_config"
[[ -z "$usb_config" || -z "$storage_config" ]] || conf+=";profiles/usb-msc.conf"
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cache="$root/build/$build/CMakeCache.txt"
if [[ -f "$cache" ]]; then
	cached_build="$(sed -n 's/^APPLICATION_BINARY_DIR:PATH=//p' "$cache")"
	if [[ -n "$cached_build" && "$cached_build" != "$root/build/$build" ]]; then
		printf 'Build directory moved; cleaning %s before rebuilding.\n' "$root/build/$build"
		cmake "-DBINARY_DIR=$root/build/$build" "-DSOURCE_DIR=$root/firmware" \
			-P "$ZEPHYR_BASE/cmake/pristine.cmake"
	fi
fi
# Select configuration explicitly because Zephyr does not discover nested board folders.
python -m west build --pristine auto -b "$target" "$root/firmware" \
  -d "$root/build/$build" -- \
  -DCONF_FILE="$conf" \
  -DDTC_OVERLAY_FILE="$overlay" \
  -DZF_ENABLE_STORAGE="$storage_enabled" \
  -DZF_ENABLE_STORAGE_BENCHMARK="$benchmark"
