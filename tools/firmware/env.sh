#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
toolchain="${toolchain:-xtensa-espressif_esp32s3_zephyr-elf}"
base="${PYTHON:-python3}"
packages=()
for pair in python3:python3 git:git cmake:cmake ninja:ninja-build gperf:gperf dtc:device-tree-compiler \
	make:make gcc:gcc g++:g++ xz:xz-utils wget:wget; do
	command -v "${pair%%:*}" >/dev/null 2>&1 || packages+=("${pair#*:}")
done
if command -v "$base" >/dev/null 2>&1; then
	"$base" -c 'import venv, ensurepip' >/dev/null 2>&1 || packages+=(python3-venv)
else
	[[ "$base" == python3 ]] || {
		printf 'Error: requested Python interpreter not found: %s\n' "$base" >&2
		exit 1
	}
	packages+=(python3-venv)
fi
if ((${#packages[@]})); then
	if ! command -v apt-get >/dev/null 2>&1; then
		printf 'Error: install missing host prerequisites with your package manager: %s\n' "${packages[*]}" >&2
		exit 1
	fi
	printf 'Installing missing host prerequisites: %s (may require sudo).\n' "${packages[*]}"
	privilege=()
	if ((EUID != 0)); then
		command -v sudo >/dev/null 2>&1 || {
			printf 'Error: sudo is required to install host prerequisites.\n' >&2
			exit 1
		}
		privilege=(sudo)
	fi
	"${privilege[@]}" apt-get update
	"${privilege[@]}" apt-get install -y --no-install-recommends "${packages[@]}"
fi

workspace="$root"
while [[ ! -f "$workspace/.west/config" ]]; do
	if [[ "$workspace" == / ]]; then
		workspace="$(dirname "$root")"
		break
	fi
	workspace="$(dirname "$workspace")"
done
west_config="${WEST_CONFIG_LOCAL:-$workspace/.west/config}"
[[ "$west_config" == /* ]] || west_config="$workspace/$west_config"

west_manifest_setting() {
	awk -v key="$1" '
		/^\[manifest\]$/ { in_manifest=1; next }
		/^\[/ { in_manifest=0 }
		in_manifest && $0 ~ "^[[:space:]]*" key "[[:space:]]*=" {
			sub(/^[^=]*=[[:space:]]*/, "")
			sub(/[[:space:]]+$/, "")
			print
			exit
		}' "$2"
}

if [[ -f "$west_config" ]]; then
configured_manifest="$(west_manifest_setting path "$west_config")"
configured_file="$(west_manifest_setting file "$west_config")"
	[[ -n "$configured_file" ]] || configured_file=west.yml
	manifest_root="$configured_manifest"
	[[ "$manifest_root" == /* ]] || manifest_root="$workspace/$manifest_root"
	if [[ -d "$manifest_root" ]]; then
		if [[ "$(realpath "$manifest_root")" != "$(realpath "$root")" ]]; then
			printf 'Error: west workspace manifest must point to this checkout (%s), not %s.\n' \
				"$root" "$configured_manifest" >&2
			exit 1
		fi
		manifest_file="$configured_file"
		[[ "$manifest_file" == /* ]] || manifest_file="$manifest_root/$manifest_file"
		if [[ ! -f "$manifest_file" || "$(realpath "$manifest_file")" != "$(realpath "$root/west.yml")" ]]; then
			printf "Error: west workspace must select this checkout's west.yml, not %s.\n" \
				"$configured_file" >&2
			exit 1
		fi
	fi
fi

venv="${VENV_DIR:-$workspace/.venv}"
[[ "$venv" == /* ]] || venv="$PWD/$venv"
bin="$venv/bin"
[[ -d "$venv/Scripts" ]] && bin="$venv/Scripts"
if [[ ! -f "$venv/pyvenv.cfg" || ! -f "$bin/activate" ]]; then
	if [[ -e "$venv" ]]; then
		printf 'Error: %s exists but is not a Python venv; select another VENV_DIR.\n' "$venv" >&2
		exit 1
	fi
	if ! command -v "$base" >/dev/null 2>&1; then
		printf 'Error: Python interpreter not found: %s\n' "$base" >&2
		exit 1
	fi
	"$base" -m venv "$venv"
	[[ -d "$venv/Scripts" ]] && bin="$venv/Scripts"
fi

source "$bin/activate"
cd "$workspace"
unset ZEPHYR_BASE
python -m pip install --disable-pip-version-check --no-input west
if [[ ! -f "$workspace/.west/config" ]]; then
	printf 'Initializing Zephyr workspace at %s.\n' "$workspace"
	python -m west init -l "$root"
fi
manifest_path="$(python -m west config --local manifest.path)"
if [[ ! -e "$workspace/$manifest_path" &&
	"$root" == "$workspace/"* && -f "$root/west.yml" ]]; then
	for cache in "$root"/build/firmware*/CMakeCache.txt; do
		if [[ -f "$cache" ]] &&
			grep -Fxq "APPLICATION_SOURCE_DIR:PATH=$workspace/$manifest_path/firmware" "$cache"; then
			checkout="${root#"$workspace"/}"
			printf 'Updating moved manifest checkout from %s to %s.\n' "$manifest_path" "$checkout"
			python -m west config --local manifest.path "$checkout"
			manifest_path="$checkout"
			break
		fi
	done
fi
manifest_root="$manifest_path"
[[ "$manifest_root" == /* ]] || manifest_root="$workspace/$manifest_root"
if [[ ! -d "$manifest_root" || "$(realpath "$manifest_root")" != "$(realpath "$root")" ]]; then
	printf 'Error: west workspace manifest must point to this checkout (%s), not %s.\n' \
		"$root" "$manifest_path" >&2
	exit 1
fi
manifest_file="$(west_manifest_setting file "$west_config")"
[[ -n "$manifest_file" ]] || manifest_file=west.yml
[[ "$manifest_file" == /* ]] || manifest_file="$manifest_root/$manifest_file"
if [[ ! -f "$manifest_file" || "$(realpath "$manifest_file")" != "$(realpath "$root/west.yml")" ]]; then
	printf "Error: west workspace must select this checkout's west.yml, not %s.\n" \
		"$manifest_file" >&2
	exit 1
fi
# Keep dependencies aligned with the pinned manifest; west preserves local working-tree changes.
python -m west update --narrow -o=--depth=1
zephyr="$(python -m west list zephyr -f '{abspath}')"
espressif="$(python -m west list hal_espressif -f '{abspath}')"
for requirements in "$zephyr/scripts/requirements-base.txt" "$espressif/zephyr/requirements.txt"; do
	if [[ ! -f "$requirements" ]]; then
		printf 'Error: missing requirements after west update: %s\n' "$requirements" >&2
		exit 1
	fi
done
python -m pip install --disable-pip-version-check --no-input \
	-r "$zephyr/scripts/requirements-base.txt" -r "$espressif/zephyr/requirements.txt"
export ZEPHYR_BASE="$zephyr"
bash "$root/tools/firmware/apply-zephyr-patches.sh" "$zephyr"
if [[ "$toolchain" == xtensa-* || "${board:-}" == esp32c6 ]]; then
	python -m west blobs fetch hal_espressif
elif [[ "${board:-}" == *_w ]]; then
	python -m west blobs fetch hal_infineon \
		--allow-regex 'img/whd/resources/(firmware/COMPONENT_43439/43439A0\.bin|clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0\.clm_blob)$'
fi

sdk=""
for candidate in "${ZEPHYR_SDK_INSTALL_DIR:-}" "$HOME/zephyr-sdk-0.17.4" /opt/zephyr-sdk-0.17.4; do
	if [[ -f "$candidate/sdk_version" ]] && [[ "$(<"$candidate/sdk_version")" == 0.17.4 ]] &&
		[[ -x "$candidate/$toolchain/bin/$toolchain-gcc" ]]; then
		sdk="$candidate"
		break
	fi
done
if [[ -n "$sdk" ]]; then
	export ZEPHYR_SDK_INSTALL_DIR="$sdk"
else
	unset ZEPHYR_SDK_INSTALL_DIR
	sdk_args=(--version 0.17.4 --toolchains "$toolchain")
	[[ -z "${GH_TOKEN:-}" ]] || sdk_args+=(--personal-access-token "$GH_TOKEN")
	python -m west sdk install "${sdk_args[@]}"
	sdk="$(python -m west sdk list | awk '
		$0 == "0.17.4:" { selected=1; next }
		selected && /^[^[:space:]]/ { exit }
		selected && /^[[:space:]]+path:/ {
			sub(/^[[:space:]]*path:[[:space:]]*/, "")
			print
			exit
		}')"
	if [[ ! -f "$sdk/sdk_version" || "$(<"$sdk/sdk_version")" != 0.17.4 ||
		! -x "$sdk/$toolchain/bin/$toolchain-gcc" ]]; then
		printf 'Error: west did not register Zephyr SDK 0.17.4 with %s at a usable path.\n' "$toolchain" >&2
		exit 1
	fi
	export ZEPHYR_SDK_INSTALL_DIR="$sdk"
fi
