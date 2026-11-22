#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -euo pipefail

for storage in sdmmc sd-spi; do
  (cd "build/firmware-packages/zephyrforge-esp32s3-$storage-software-build" &&
    zip -r "../../packages/zephyrforge-firmware-v$VERSION-esp32s3-devkitc-$storage.zip" .)
  for board in pico pico_w pico2 pico2_w; do
    (cd "build/firmware-packages/zephyrforge-$board-$storage-software-build" &&
      zip -r "../../packages/zephyrforge-firmware-v$VERSION-$board-$storage.zip" .)
  done
done
cd build/packages
sha256sum *.zip *.tar.gz *.whl > SHA256SUMS.txt
