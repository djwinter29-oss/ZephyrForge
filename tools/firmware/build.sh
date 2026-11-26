#!/usr/bin/env bash
set -euo pipefail
board=esp32s3
storage=sdmmc
storage_selected=false
conf=prj.conf
disabled=()
while (($#)); do
	case "$1" in
		--board)
			(($# >= 2)) || { printf 'Error: --board requires esp32s3, esp32c6, pico, pico_w, pico2 or pico2_w.\n' >&2; exit 1; }
			board="$2"; shift 2 ;;
		--storage)
			(($# >= 2)) || { printf 'Error: --storage requires sdmmc, sd-spi or none.\n' >&2; exit 1; }
			storage="$2"; storage_selected=true; shift 2 ;;
		--without)
			(($# >= 2)) || { printf 'Error: --without requires a module name.\n' >&2; exit 1; }
			case "$2" in
				wifi|bluetooth|tcp-shell|usb-msc|benchmark) disabled+=("$2") ;;
				*) printf 'Error: unknown optional module: %s\n' "$2" >&2; exit 1 ;;
			esac
			shift 2 ;;
		-h|--help)
			printf '%s\n' \
				'Usage: bash tools/firmware/build.sh [--board esp32s3|esp32c6|pico|pico_w|pico2|pico2_w] [--storage sdmmc|sd-spi|none]' \
				'       [--without wifi|bluetooth|tcp-shell|usb-msc|benchmark] (repeatable)'
			exit 0 ;;
		*) printf 'Error: unknown argument: %s\n' "$1" >&2; exit 1 ;;
	esac
done
if [[ "$board" == esp32c6 && "$storage_selected" == false ]]; then
	storage=none
fi
case "$storage" in
	sdmmc) build=firmware; mode=sdmmc ;;
	sd-spi) build=firmware-sd-spi; mode=sd_spi ;;
	none) build=firmware-no-storage ;;
	*) printf 'Error: --storage must be sdmmc, sd-spi or none.\n' >&2; exit 1 ;;
esac
case "$board" in
	esp32s3) target=esp32s3_devkitc/esp32s3/procpu; family=esp32s3
		conf+=";boards/esp32s3/board.conf"
		toolchain=xtensa-espressif_esp32s3_zephyr-elf ;;
	esp32c6) family=esp32c6
		[[ "$storage" != sdmmc ]] || {
			printf 'Error: ESP32-C6 supports SD over SPI or no storage, not native SDMMC.\n' >&2
			exit 1
		}
		target=esp32c6_devkitc/esp32c6/hpcore
		build="${build/firmware/firmware-esp32c6}"
		toolchain=riscv64-zephyr-elf ;;
	pico) target=rpi_pico/rp2040; soc=rp2040 ;;
	pico_w) target=rpi_pico/rp2040/w; soc=rp2040 ;;
	pico2) target=rpi_pico2/rp2350a/m33; soc=rp2350 ;;
	pico2_w) target=rpi_pico2/rp2350a/m33/w; soc=rp2350 ;;
	*) printf 'Error: --board must be esp32s3, esp32c6, pico, pico_w, pico2 or pico2_w.\n' >&2; exit 1 ;;
esac
[[ "$board" != pico* ]] || family=rpi_pico
# Overlays describe hardware: family board, Pico SoC flash layout, then SD card wiring. Kconfig enables each module
# whose hardware the devicetree describes.
overlay="boards/$family/board.overlay"
[[ -z "${soc:-}" ]] || overlay+=";boards/$family/soc/$soc.overlay"
[[ "$storage" == none ]] || overlay+=";boards/$family/storage/$mode.overlay"
if [[ "$board" == pico* ]]; then
	build="${build/firmware/firmware-$board}"
	toolchain=arm-zephyr-eabi
fi
if [[ " ${disabled[*]} " == *" wifi "* ]]; then
	disabled+=(tcp-shell)
fi
if [[ " ${disabled[*]} " == *" bluetooth "* ]]; then
	[[ "$board" != esp32c6 ]] || {
		printf 'Error: ESP32-C6 requires Bluetooth for management; it has no native USB HID.\n' >&2
		exit 1
	}
fi
kconfig=()
# Keep customized images separate from the default directories used by flashing and release wrappers.
for feature in wifi bluetooth tcp-shell usb-msc benchmark; do
	if [[ " ${disabled[*]} " == *" $feature "* ]]; then
		case "$feature" in
			wifi) symbol=WIFI ;;
			bluetooth) symbol=BLUETOOTH ;;
			tcp-shell) symbol=TCP_SHELL ;;
			usb-msc) symbol=USB_MSC ;;
			benchmark) symbol=STORAGE_BENCHMARK ;;
		esac
		kconfig+=("-DCONFIG_ZF_$symbol=n")
		build+="-no-$feature"
	fi
done
export ZEPHYRFORGE_VALIDATE_BUILD_CONFIG=1
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset ZEPHYRFORGE_VALIDATE_BUILD_CONFIG
if [[ "$board" == pico* ]]; then
	bash "$root/tools/firmware/check-pico-pio.sh" "$workspace/modules/hal/rpi_pico"
fi
cache="$root/build/$build/CMakeCache.txt"
if [[ -f "$cache" ]]; then
	cached_build="$(sed -n 's/^APPLICATION_BINARY_DIR:PATH=//p' "$cache")"
	cached_sdk="$(sed -n 's/^ZEPHYR_SDK_INSTALL_DIR:[^=]*=//p' "$cache")"
	cached_sdk_cmake="$(sed -n 's/^Zephyr-sdk_DIR:[^=]*=//p' "$cache")"
	cached_zephyr_base="$(sed -n 's/^ZEPHYR_BASE:[^=]*=//p' "$cache")"
	cached_zephyr_dir="$(sed -n 's/^Zephyr_DIR:[^=]*=//p' "$cache")"
	cached_home="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")"
	cached_toolchain="$(sed -n 's/^ZEPHYR_TOOLCHAIN_VARIANT:[^=]*=//p' "$cache")"
	cached_compiler="$(sed -n 's/^CMAKE_C_COMPILER:[^=]*=//p' "$cache")"
	expected_compiler="$ZEPHYR_SDK_INSTALL_DIR/$toolchain/bin/$toolchain-gcc"
	expected_zephyr_dir="$ZEPHYR_BASE/share/zephyr-package/cmake"
	needs_pristine=false
	if [[ -n "$cached_build" && "$cached_build" != "$root/build/$build" ]]; then
		printf 'Build directory moved; cleaning %s before rebuilding.\n' "$root/build/$build"
		needs_pristine=true
	fi
	if [[ -n "$cached_home" && "$cached_home" != "$root/firmware" ]]; then
		printf 'Build system changed; cleaning %s before rebuilding.\n' "$root/build/$build"
		needs_pristine=true
	fi
	if [[ -n "$cached_zephyr_base" && "$cached_zephyr_base" != "$ZEPHYR_BASE" ]] ||
		[[ -n "$cached_zephyr_dir" && "$cached_zephyr_dir" != "$expected_zephyr_dir" ]]; then
		printf 'Zephyr installation changed; cleaning %s before rebuilding.\n' "$root/build/$build"
		needs_pristine=true
	fi
	if [[ -n "$cached_sdk" && "$cached_sdk" != "$ZEPHYR_SDK_INSTALL_DIR" ]] ||
		[[ -n "$cached_sdk_cmake" && "$cached_sdk_cmake" != "$ZEPHYR_SDK_INSTALL_DIR/cmake" ]] ||
		[[ -n "$cached_toolchain" && "$cached_toolchain" != zephyr ]] ||
		[[ -n "$cached_compiler" && "$cached_compiler" != "$expected_compiler" ]]; then
		printf 'Build toolchain changed; cleaning %s before rebuilding.\n' "$root/build/$build"
		needs_pristine=true
	fi
	if "$needs_pristine"; then
		cmake "-DBINARY_DIR=$root/build/$build" "-DSOURCE_DIR=$root/firmware" \
			-P "$ZEPHYR_BASE/cmake/pristine.cmake"
	fi
fi
source_version="$(sed -n 's/^#define ZEPHYRFORGE_VERSION "\(.*\)"/\1/p' "$root/firmware/src/app/zephyrforge.h")"
[[ -n "$source_version" ]] || {
	printf 'Error: source firmware version is missing from zephyrforge.h.\n' >&2
	exit 1
}
firmware_version="${ZEPHYRFORGE_VERSION:-$source_version}"
source_commit="$(git -C "$root" rev-parse HEAD)"
if [[ -z "$(git -C "$root" status --porcelain)" ]]; then
	source_dirty=false
else
	source_dirty=true
fi
radio_firmware_hash=""
radio_clm_hash=""
if [[ "$board" == *_w ]]; then
	radio_firmware_hash="$(sha256sum "$workspace/modules/hal/infineon/zephyr/blobs/img/whd/resources/firmware/COMPONENT_43439/43439A0.bin" |
		awk '{print $1}')"
	radio_clm_hash="$(sha256sum "$workspace/modules/hal/infineon/zephyr/blobs/img/whd/resources/clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0.clm_blob" |
		awk '{print $1}')"
fi
build_dir="$root/build/$build"
build_identity="$build_dir/zephyrforge-build-info.txt"
image_file=zephyr.bin
[[ "$board" != pico* ]] || image_file=zephyr.uf2
mkdir -p "$build_dir"
rm -f "$build_identity" "$build_identity.tmp"
# Select configuration explicitly because Zephyr does not discover nested board folders.
python -m west build --pristine auto --no-sysbuild -b "$target" "$root/firmware" \
  -d "$root/build/$build" -- \
  -DCONF_FILE="$conf" \
  -DDTC_OVERLAY_FILE="$overlay" \
  -DZEPHYR_SDK_INSTALL_DIR="$ZEPHYR_SDK_INSTALL_DIR" \
  -DZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -DZEPHYRFORGE_VERSION="$firmware_version" \
  "${kconfig[@]}"
image_hash="$(sha256sum "$build_dir/zephyr/$image_file" | awk '{print $1}')"
elf_hash="$(sha256sum "$build_dir/zephyr/zephyr.elf" | awk '{print $1}')"
{
	printf 'FIRMWARE_VERSION=%s\nSOURCE_COMMIT=%s\nSOURCE_DIRTY=%s\nBOARD=%s\nTARGET=%s\nSTORAGE=%s\nIMAGE_SHA256=%s\n' \
		"$firmware_version" "$source_commit" "$source_dirty" "$board" "$target" "$storage" "$image_hash"
	printf 'ELF_SHA256=%s\n' "$elf_hash"
	if [[ -n "$radio_firmware_hash" ]]; then
		printf 'RADIO_FIRMWARE_SHA256=%s\nRADIO_CLM_SHA256=%s\n' "$radio_firmware_hash" "$radio_clm_hash"
	fi
} > "$build_identity.tmp"
mv "$build_identity.tmp" "$build_identity"
