#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat <<'EOF'
Usage: bash tools/firmware/flash.sh [--board esp32s3|esp32c6] [--storage sdmmc|sd-spi|none]
                                    [--port PORT] [--baud BAUD] [--dry-run]

Flash ZephyrForge through the USB-to-UART connector; build first if artifacts are missing.
Missing workspace, Python dependencies and build tools are prepared automatically.
  --storage     Image profile: sdmmc (default), sd-spi or none
  --board       ESP32 target (default: esp32s3; ESP32-C6 defaults to no storage)
  --port PORT   Serial port, e.g. /dev/ttyUSB0 or COM4 (auto-detected if omitted)
  --baud BAUD   Positive upload baud rate (default: 460800)
  --dry-run     Validate prerequisites and print the command; do not flash
  -h, --help    Show this help

Close serial clients before flashing. Existing complete ESP images are reused; incomplete images are rebuilt.
Environment: PYTHON (default python3), VENV_DIR (default workspace/.venv).
The venv is created/reused and dependencies installed, including for --dry-run.
Flashing replaces existing firmware; this script does not request a full-chip erase.
EOF
}

fail() {
	printf 'Error: %s\n' "$*" >&2
	exit 1
}

port=""
baud=460800
dry_run=false
board=esp32s3
storage=""
while (($#)); do
	case "$1" in
		--board|--port|--baud|--storage)
			(($# >= 2)) && [[ -n "$2" && "$2" != -* ]] ||
				fail "$1 requires a value"
			if [[ "$1" == --board ]]; then
				board="$2"
			elif [[ "$1" == --port ]]; then
				port="$2"
			elif [[ "$1" == --baud ]]; then
				baud="$2"
			else
				storage="$2"
			fi
			shift 2
			;;
		--dry-run) dry_run=true; shift ;;
		-h|--help) usage; exit 0 ;;
		*) fail "Unknown argument: $1 (use --help)" ;;
	esac
done

[[ "$baud" =~ ^[1-9][0-9]*$ ]] || fail "--baud must be a positive integer"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
case "$board" in
	esp32s3) target=esp32s3_devkitc/esp32s3/procpu; build_name=firmware
		toolchain=xtensa-espressif_esp32s3_zephyr-elf ;;
	esp32c6) target=esp32c6_devkitc/esp32c6/hpcore; build_name=firmware-esp32c6
		toolchain=riscv64-zephyr-elf ;;
	*) fail "--board must be esp32s3 or esp32c6" ;;
esac
if [[ -z "$storage" ]]; then
	storage=sdmmc
	[[ "$board" != esp32c6 ]] || storage=none
fi
case "$storage" in
	sdmmc)
		[[ "$board" == esp32s3 ]] || fail "ESP32-C6 does not support native SDMMC"
		driver=SDHC_ESP32 ;;
	sd-spi) build_name+="-sd-spi"; driver=SPI_SDHC ;;
	none) build_name+="-no-storage"; driver=none ;;
	*) fail "--storage must be sdmmc, sd-spi or none" ;;
esac
build="$root/build/$build_name"
is_esp_image() {
	[[ -s "$1" ]] && [[ "$(od -An -tx1 -N1 "$1" | tr -d '[:space:]')" == "e9" ]]
}
needs_build=false
for artifact in zephyr.bin zephyr.elf .config runners.yaml; do
	[[ -s "$build/zephyr/$artifact" ]] || needs_build=true
done
is_esp_image "$build/zephyr/zephyr.bin" || needs_build=true
source "$root/tools/firmware/env.sh"
if [[ -z "$port" ]]; then
	port="$(python - <<'PY'
from serial.tools import list_ports

ports = []
for info in list_ports.comports():
    description = f"{info.description or ''} {info.hwid or ''}".lower()
    if info.vid == 0x10c4:
        score = 3
    elif info.vid in (0x1a86, 0x0403, 0x067b):
        score = 2
    elif info.vid is not None and "usb" in description and (
        "serial" in description or "uart" in description
    ):
        score = 1
    else:
        continue
    ports.append((score, info.device, info.description or "USB serial device"))

if not ports:
    raise SystemExit("Error: no likely USB serial port found; connect the board or specify --port.")

best_score = max(score for score, _, _ in ports)
matches = [(device, description) for score, device, description in ports if score == best_score]
if len(matches) != 1:
    choices = ", ".join(f"{device} ({description})" for device, description in matches)
    raise SystemExit(f"Error: multiple likely serial ports found: {choices}; specify --port.")

print(matches[0][0])
PY
)"
	[[ -n "$port" ]] || fail "Could not detect a serial port; specify --port."
	printf 'Auto-detected serial port: %s\n' "$port"
fi
if "$needs_build"; then
	printf 'Build artifacts are missing or incomplete; building %s firmware before upload.\n' "$storage"
	bash "$root/tools/firmware/build.sh" --board "$board" --storage "$storage"
fi
for artifact in zephyr.bin zephyr.elf .config runners.yaml; do
	[[ -s "$build/zephyr/$artifact" ]] || fail "Build did not produce $build/zephyr/$artifact"
done
is_esp_image "$build/zephyr/zephyr.bin" ||
	fail "Build did not produce a valid ESP image at $build/zephyr/zephyr.bin"
grep -Fxq "CONFIG_BOARD_TARGET=\"$target\"" "$build/zephyr/.config" ||
	fail "Build is not for $target; rebuild for the selected target"
if [[ "$storage" == none ]]; then
	for feature in USBD_MSC_CLASS DISK_ACCESS FILE_SYSTEM; do
		if grep -Eq "^CONFIG_${feature}=(y|m)$" "$build/zephyr/.config"; then
			fail "Build does not match --storage none; rebuild the selected image"
		fi
	done
else
	grep -Fxq "CONFIG_$driver=y" "$build/zephyr/.config" ||
		fail "Build does not match --storage $storage; rebuild the selected image"
fi

command=(python -m west flash -d "$build" --skip-rebuild -r esp32
	--esp-device "$port" --esp-baud-rate "$baud" --no-erase)

if "$dry_run"; then
	printf 'Dry run only (no device access): '
	printf '%q ' "${command[@]}"
	printf '\n'
	exit 0
fi

printf 'Flashing %s %s image on %s at %s baud; existing firmware will be replaced.\n' \
	"$board" "$storage" "$port" "$baud"
exec "${command[@]}"
