#!/usr/bin/env bash
set -euo pipefail
board=esp32s3
storage=sdmmc
conf=prj.conf
while (($#)); do
	case "$1" in
		--board)
			(($# >= 2)) || { printf 'Error: --board requires esp32s3, pico, pico_w, pico2 or pico2_w.\n' >&2; exit 1; }
			board="$2"; shift 2 ;;
		--storage)
			(($# >= 2)) || { printf 'Error: --storage requires sdmmc or sd-spi.\n' >&2; exit 1; }
			storage="$2"; shift 2 ;;
		-h|--help)
			printf 'Usage: bash tools/firmware/build.sh [--board esp32s3|pico|pico_w|pico2|pico2_w] [--storage sdmmc|sd-spi]\n'
			exit 0 ;;
		*) printf 'Error: unknown argument: %s\n' "$1" >&2; exit 1 ;;
	esac
done
case "$storage" in
	sdmmc) build=firmware; mode=sdmmc ;;
	sd-spi) build=firmware-sd-spi; mode=sd_spi ;;
	*) printf 'Error: --storage must be sdmmc or sd-spi.\n' >&2; exit 1 ;;
esac
case "$board" in
	esp32s3) target=esp32s3_devkitc/esp32s3/procpu
		overlay="boards/espressif/esp32s3_devkitc_n16r8.overlay;boards/espressif/$mode.overlay"
		conf+=";wifi.conf;bluetooth.conf;boards/espressif/esp32s3_devkitc_n16r8.conf"
		toolchain=xtensa-espressif_esp32s3_zephyr-elf ;;
	pico) target=rpi_pico/rp2040; soc=rp2040 ;;
	pico_w) target=rpi_pico/rp2040/w; soc=rp2040 ;;
	pico2) target=rpi_pico2/rp2350a/m33; soc=rp2350 ;;
	pico2_w) target=rpi_pico2/rp2350a/m33/w; soc=rp2350 ;;
	*) printf 'Error: --board must be esp32s3, pico, pico_w, pico2 or pico2_w.\n' >&2; exit 1 ;;
esac
if [[ "$board" != esp32s3 ]]; then
	build="${build/firmware/firmware-$board}"
	overlay="boards/rpi_pico/$mode.overlay;boards/rpi_pico/$soc.overlay"
	if [[ "$board" == *_w ]]; then
		overlay+=";boards/rpi_pico/w.overlay"
		conf+=";wifi.conf;boards/rpi_pico/w.conf"
	fi
	toolchain=arm-zephyr-eabi
fi
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
  -DZF_ENABLE_STORAGE_BENCHMARK=ON
