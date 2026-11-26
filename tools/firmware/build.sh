#!/usr/bin/env bash
set -euo pipefail
board=esp32s3
storage=sdmmc
storage_selected=false
conf=prj.conf
disabled=()
enabled=()
benchmark_chunk_size=""
module_names=(wifi bluetooth tcp-shell uart-shell cpu-load temperature settings-save usb-hid usb-msc benchmark led)
module_symbols=(
	WIFI BLUETOOTH TCP_SHELL UART_SHELL CPU_LOAD TEMPERATURE SETTINGS_SAVE
	USB_HID USB_MSC STORAGE_BENCHMARK LED
)

module_symbol()
{
	local i

	for i in "${!module_names[@]}"; do
		if [[ "${module_names[$i]}" == "$1" ]]; then
			printf '%s' "${module_symbols[$i]}"
			return 0
		fi
	done
	return 1
}

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
			module_symbol "$2" >/dev/null || {
				printf 'Error: unknown optional module: %s\n' "$2" >&2
				exit 1
			}
			disabled+=("$2")
			shift 2 ;;
		--with)
			(($# >= 2)) || { printf 'Error: --with requires a module name.\n' >&2; exit 1; }
			module_symbol "$2" >/dev/null || {
				printf 'Error: unknown optional module: %s\n' "$2" >&2
				exit 1
			}
			enabled+=("$2")
			shift 2 ;;
		--benchmark-chunk-size)
			(($# >= 2)) || { printf 'Error: --benchmark-chunk-size requires a byte count.\n' >&2; exit 1; }
			if [[ ! "$2" =~ ^[0-9]+$ ]]; then
				printf 'Error: benchmark chunk size must be a multiple of 4 from 4096 to 32768.\n' >&2
				exit 1
			fi
			benchmark_chunk_size=$((10#$2))
			((benchmark_chunk_size >= 4096 && benchmark_chunk_size <= 32768 && benchmark_chunk_size % 4 == 0)) || {
				printf 'Error: benchmark chunk size must be a multiple of 4 from 4096 to 32768.\n' >&2
				exit 1
			}
			shift 2 ;;
		-h|--help)
			printf '%s\n' \
				'Usage: bash tools/firmware/build.sh [--board BOARD] [--storage PROFILE]' \
				'       [--with MODULE | --without MODULE] (repeatable)' \
				'       [--benchmark-chunk-size BYTES]' \
				'       Modules: wifi bluetooth tcp-shell uart-shell cpu-load temperature' \
				'               settings-save usb-hid usb-msc benchmark led'
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
seen_modules=" "
for feature in "${enabled[@]}" "${disabled[@]}"; do
	if [[ "$seen_modules" == *" $feature "* ]]; then
		printf 'Error: module %s was specified more than once.\n' "$feature" >&2
		exit 1
	fi
	seen_modules+="$feature "
done
for feature in "${enabled[@]}"; do
	if [[ " ${disabled[*]} " == *" $feature "* ]]; then
		printf 'Error: module %s cannot be both enabled and disabled.\n' "$feature" >&2
		exit 1
	fi
done
kconfig=()
# Keep customized images separate from the default directories used by flashing and release wrappers.
for i in "${!module_names[@]}"; do
	feature=${module_names[$i]}
	symbol=${module_symbols[$i]}
	if [[ " ${disabled[*]} " == *" $feature "* ]]; then
		kconfig+=("-DCONFIG_ZF_$symbol=n")
		build+="-no-$feature"
	elif [[ " ${enabled[*]} " == *" $feature "* ]]; then
		kconfig+=("-DCONFIG_ZF_$symbol=y")
		build+="-with-$feature"
	fi
done
if [[ -n "$benchmark_chunk_size" ]]; then
	kconfig+=("-DCONFIG_ZF_STORAGE_BENCHMARK_CHUNK_SIZE=$benchmark_chunk_size")
	build+="-benchmark-$benchmark_chunk_size"
fi
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
python -m west build --cmake-only --pristine auto --no-sysbuild -b "$target" "$root/firmware" \
  -d "$root/build/$build" -- \
  -DCONF_FILE="$conf" \
  -DDTC_OVERLAY_FILE="$overlay" \
  -DZEPHYR_SDK_INSTALL_DIR="$ZEPHYR_SDK_INSTALL_DIR" \
  -DZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -DZEPHYRFORGE_VERSION="${ZEPHYRFORGE_VERSION:-}" \
  "${kconfig[@]}"
# Kconfig owns module dependencies and hardware availability; it drops a requested value it cannot honor with
# only a warning, so fail before compiling when the resolved configuration differs from the request.
config="$build_dir/zephyr/.config"
for i in "${!module_names[@]}"; do
	feature=${module_names[$i]}
	symbol=CONFIG_ZF_${module_symbols[$i]}
	if [[ " ${enabled[*]} " == *" $feature "* ]] && ! grep -Fxq "$symbol=y" "$config"; then
		printf 'Error: %s is unavailable for this board, storage profile or module selection.\n' "$feature" >&2
		exit 1
	fi
	if [[ " ${disabled[*]} " == *" $feature "* ]] && grep -Fxq "$symbol=y" "$config"; then
		printf 'Error: %s could not be disabled.\n' "$feature" >&2
		exit 1
	fi
done
if [[ -n "$benchmark_chunk_size" ]] &&
	! grep -Fxq "CONFIG_ZF_STORAGE_BENCHMARK_CHUNK_SIZE=$benchmark_chunk_size" "$config"; then
	printf 'Error: --benchmark-chunk-size requires the benchmark module.\n' >&2
	exit 1
fi
python -m west build -d "$root/build/$build"
# Record the version CMake generated, which is the default when ZEPHYRFORGE_VERSION is unset.
firmware_version="$(sed -n 's/^#define ZEPHYRFORGE_VERSION "\(.*\)"/\1/p' "$build_dir/zephyrforge/app/version.h")"
[[ -n "$firmware_version" ]] || {
	printf 'Error: the build did not generate a firmware version.\n' >&2
	exit 1
}
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
