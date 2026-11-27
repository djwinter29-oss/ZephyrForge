#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
module="${1:?Usage: check-pico-pio.sh RPI_PICO_HAL_MODULE}"
pio_dir="$module/tools/pioasm"
source="$root/firmware/src/drivers/sdio_pio.pio"
checked_in="$root/firmware/src/drivers/sdio_pio.pio.h"
temporary="$(mktemp -d)"
trap 'rm -rf "$temporary"' EXIT

version="$(python3 - "$module/pico_sdk_version.cmake" <<'PY'
import re
import sys

text = open(sys.argv[1], encoding="utf-8").read()
parts = []
for name in ("MAJOR", "MINOR", "REVISION"):
    match = re.search(rf"^\s*set\(PICO_SDK_VERSION_{name}\s+(\d+)\s*\)", text, re.MULTILINE)
    if not match:
        raise SystemExit(f"Missing PICO_SDK_VERSION_{name} in {sys.argv[1]}")
    parts.append(match.group(1))
print(".".join(parts))
PY
)"

cmake -S "$pio_dir" -B "$temporary/build" -DPIOASM_VERSION_STRING="$version" -DCMAKE_BUILD_TYPE=Release
cmake --build "$temporary/build" --parallel 2
"$temporary/build/pioasm" -o c-sdk "$source" "$temporary/generated.h"
sed -E 's/pioasm version [^;]+;/pioasm version (module version);/' "$checked_in" > "$temporary/checked-in.normalized"
sed -E 's/pioasm version [^;]+;/pioasm version (module version);/' "$temporary/generated.h" > "$temporary/generated.normalized"
if ! cmp -s "$temporary/checked-in.normalized" "$temporary/generated.normalized"; then
	diff -u "$temporary/checked-in.normalized" "$temporary/generated.normalized"
	exit 1
fi
printf 'Pico SD PIO source matches its generated header (pioasm %s).\n' "$version"
