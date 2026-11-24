#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -euo pipefail

version_component='(0|[1-9]|[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])'
if (($# != 1)) || [[ ! "$1" =~ ^v${version_component}\.${version_component}\.${version_component}$ ]]; then
	printf 'Error: release tag must be vMAJOR.MINOR.PATCH with components from 0 to 255 and no leading zeros.\n' >&2
	exit 1
fi
printf '%s\n' "${1#v}"
