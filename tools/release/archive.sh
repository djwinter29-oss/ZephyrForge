#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -euo pipefail

for board in esp32s3 pico pico_w pico2 pico2_w; do
  for storage in sdmmc sd-spi none; do
    if [[ "$board" == esp32s3 ]]; then
      artifact_board=esp32s3
      archive_board=esp32s3-devkitc
    else
      artifact_board="$board"
      archive_board="$board"
    fi
    (cd "build/firmware-packages/zephyrforge-$artifact_board-$storage-software-build" &&
      zip -r "../../packages/zephyrforge-firmware-v$VERSION-$archive_board-$storage.zip" .)
  done
done
for storage in sd-spi none; do
  (cd "build/firmware-packages/zephyrforge-esp32c6-$storage-software-build" &&
    zip -r "../../packages/zephyrforge-firmware-v$VERSION-esp32c6-devkitc-$storage.zip" .)
done
cd build/packages
sha256sum *.zip *.tar.gz *.whl > SHA256SUMS.txt
