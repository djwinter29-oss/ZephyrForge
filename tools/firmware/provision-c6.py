#!/usr/bin/env python3
"""Initially provision a C6 Control key through an explicitly selected physical flashing port."""

import argparse
import getpass
import importlib.util
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import warnings
import zlib


SECTOR_SIZE = 4096
PAGE_SIZE = 256
# One CLI invocation otherwise still retries data blocks and reconnects to reflash the entire image.
ESPTOOL_ENTRY = """
import esptool
from esptool import loader
if not hasattr(loader, "WRITE_BLOCK_ATTEMPTS") or not hasattr(loader.ESPLoader, "WRITE_FLASH_ATTEMPTS"):
    raise RuntimeError("Esptool retry controls are unavailable")
loader.WRITE_BLOCK_ATTEMPTS = 1
loader.ESPLoader.WRITE_FLASH_ATTEMPTS = 1
esptool.main()
"""


def settings_page(key: bytes) -> bytes:
    """Encode a key-only version-1 settings record with default clock and radio settings."""
    if len(key) != 32 or not any(key):
        raise ValueError("Control key must be 32 bytes and not all zero; use a cryptographically random key")
    record = bytearray(148)
    struct.pack_into("<4sHH", record, 0, b"EFCF", 1, len(record))
    record[109:141] = key
    record[141] = 1
    struct.pack_into("<I", record, 144, zlib.crc32(record[:144]))
    return bytes(record).ljust(PAGE_SIZE, b"\xff")


def settings_offset(build: Path) -> int:
    """Read the settings sector from the selected C6 build's compiled devicetree."""
    config = (build / "zephyr/.config").read_text(encoding="utf-8").splitlines()
    required = {
        'CONFIG_BOARD_TARGET="esp32c6_devkitc/esp32c6/hpcore"',
        "CONFIG_SOC_ESP32C6=y",
        "CONFIG_FLASH_SIZE=8388608",
        "CONFIG_ESP_SIMPLE_BOOT=y",
        "CONFIG_FLASH_LOAD_OFFSET=0x0",
    }
    if not required.issubset(config):
        raise ValueError("Build must target ESP32-C6-DevKitC-1 N8")
    with (build / "zephyr/zephyr.bin").open("rb") as image:
        if image.read(1) != b"\xe9":
            raise ValueError("Build has no valid ESP application image")
        image_size = image.seek(0, os.SEEK_END)
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    base = re.search(r"^ZEPHYR_BASE:[^=]+=(.+)$", cache, re.MULTILINE)
    if base is None:
        raise ValueError("Build has no configured Zephyr dependency root")
    parser_path = Path(base[1]) / "scripts/dts/python-devicetree/src"
    if not (parser_path / "devicetree/dtlib.py").is_file():
        raise ValueError("Build's Zephyr devicetree parser is unavailable")
    sys.path.insert(0, str(parser_path))
    try:
        from devicetree.dtlib import DT, DTError

        try:
            tree = DT(str(build / "zephyr/zephyr.dts"))
        except DTError as error:
            raise ValueError("Compiled devicetree is invalid") from error
    finally:
        sys.path.pop(0)
    flash = tree.label2node.get("flash0")
    partition = tree.label2node.get("storage_partition")
    if flash is None or partition is None or partition.parent is None or partition.parent.parent is not flash:
        raise ValueError("Build has no settings partition on flash0")
    try:
        flash_reg = flash.props["reg"].to_nums() if "reg" in flash.props else []
        partition_reg = partition.props["reg"].to_nums() if "reg" in partition.props else []
    except DTError as error:
        raise ValueError("Compiled flash addresses must be numeric") from error
    if flash_reg != [0, 8388608]:
        raise ValueError("Build flash layout does not match the C6 N8 target")
    if len(partition_reg) != 2:
        raise ValueError("Settings partition has no valid address and size")
    offset, size = partition_reg
    if offset < SECTOR_SIZE or offset % SECTOR_SIZE or size < SECTOR_SIZE or offset + size > 8388608:
        raise ValueError("Settings partition is not a safe aligned sector within C6 flash")
    if image_size > offset:
        raise ValueError("Firmware image overlaps the settings sector; refusing to provision")
    return offset


def provision(port: str, offset: int, page: bytes) -> None:
    """Write once to an erased settings sector and verify it; never retry an uncertain write."""
    command = [
        sys.executable, "-c", ESPTOOL_ENTRY, "--chip", "esp32c6", "--port", port,
        "--connect-attempts", "1", "--after", "no-reset",
    ]

    def run(*args: str) -> bytes:
        try:
            result = subprocess.run(
                command + list(args), capture_output=True, timeout=60, check=False,
                env={**os.environ, "ESPTOOL_OPEN_PORT_ATTEMPTS": "1"})
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"esptool {args[0]} timed out; outcome may be uncertain; do not retry") from error
        if result.returncode:
            raise RuntimeError(
                f"esptool {args[0]} failed (exit {result.returncode}); outcome may be uncertain; do not retry")
        return result.stdout

    if not port.strip():
        raise ValueError("An explicit physical flashing port is required")
    if len(page) != PAGE_SIZE:
        raise ValueError("Provisioning page must be exactly 256 bytes")
    if re.search(rb"^Detected flash size: 8MB\r?$", run("flash-id"), re.MULTILINE) is None:
        raise ValueError("Connected flash must be detected as 8 MB; refusing to write an unknown or different size")
    with tempfile.TemporaryDirectory(prefix="zephyrforge-c6-provision-") as directory:
        image = Path(directory) / "settings.bin"
        readback = Path(directory) / "readback.bin"
        for path in (image, readback):
            descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            os.close(descriptor)
        image.write_bytes(page)
        run("read-flash", "--no-progress", hex(offset), str(SECTOR_SIZE), str(readback))
        if readback.read_bytes() != b"\xff" * SECTOR_SIZE:
            raise ValueError("Settings sector is not erased; refusing to overwrite existing settings")
        run("write-flash", "--no-progress", hex(offset), str(image))
        run("read-flash", "--no-progress", hex(offset), str(SECTOR_SIZE), str(readback))
        if readback.read_bytes() != page.ljust(SECTOR_SIZE, b"\xff"):
            raise RuntimeError("Settings readback did not match; do not retry or assume provisioning succeeded")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True, help="Previously built and flashed C6 N8 image")
    parser.add_argument("--port", required=True, help="Explicit physical flashing port; never auto-detected")
    parser.add_argument("--allow-settings-write", action="store_true", help="Authorize a settings-sector write")
    parser.add_argument("--dry-run", action="store_true", help="Validate and report the sector; no key or device access")
    args = parser.parse_args()
    try:
        if importlib.util.find_spec("esptool") is None:
            raise ValueError(
                f"esptool is unavailable to {sys.executable}; run with the initialized workspace's Python venv")
        offset = settings_offset(args.build_dir)
        if args.dry_run:
            print(f"Dry run: C6 settings sector {offset:#x} on {args.port}; no device access")
            return
        if not args.allow_settings_write:
            raise ValueError("Physical provisioning requires --allow-settings-write and exclusive board ownership")
        if not sys.stdin.isatty():
            raise ValueError("Control key entry requires a terminal; refusing an echoed input fallback")
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            text = getpass.getpass("Control key (64 hexadecimal digits, hidden): ").strip()
        if re.fullmatch(r"[0-9a-fA-F]{64}", text) is None:
            raise ValueError("Control key must contain exactly 64 hexadecimal digits")
        page = settings_page(bytes.fromhex(text))
        provision(args.port, offset, page)
        print("Control key verified. Settings and flash are unencrypted. Reset the board manually to start BLE.")
    except getpass.GetPassWarning:
        parser.exit(1, "Error: Cannot hide Control key entry; refusing an echoed input fallback\n")
    except (OSError, ValueError, RuntimeError, ImportError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
