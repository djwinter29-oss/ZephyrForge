# First BLE Control key on ESP32-C6

Use this procedure once to commission an ESP32-C6-DevKitC-1 N8 for Direct BLE management. A fresh image does not
advertise until a Control key is saved. The C6 has no native USB HID, so this procedure writes the first key through
its UART flashing port.

This procedure writes settings to an unencrypted board through its flashing port. Use it only with physical access
to the board and no other application using that port. The procedure has software checks, not hardware qualification.

## Prepare the board and build

1. Check the module marking. In a trusted, initialized Zephyr workspace,
   [build the selected C6 image](../../development/firmware-build.md#esp32-c6-family) and
   [flash it](../flashing.md#from-a-source-build).
2. Retain the matching build directory, including compiled configuration and devicetree. The utility derives the
   settings address from that build; it does not prove the matching image is installed.
3. Close all other programs using the port. The `--allow-settings-write` flag in the next step enables the
   settings-sector write.

## Validate without writing

Activate the workspace's Python venv, which includes esptool. From a checkout directly inside the workspace:

```sh
source ../.venv/bin/activate
python tools/firmware/provision-c6.py --build-dir build/firmware-esp32c6-no-storage --port PORT --dry-run
```

Replace `PORT` with the actual UART flashing port. For an SD-over-SPI image, use
`build/firmware-esp32c6-sd-spi`. The dry run validates build metadata only; it does not read a key, access the device,
or check the board's settings.

## Write and verify the key

Generate a unique, cryptographically random 32-byte Control key and retain it in a password manager. Run:

```sh
python tools/firmware/provision-c6.py --build-dir build/firmware-esp32c6-no-storage --port PORT --allow-settings-write
```

Enter the key's 64 hexadecimal digits at the hidden terminal prompt. Do not put the key in command arguments,
environment variables, logs, or result records. Piped or echoed key entry is refused.

The utility checks the C6 chip and detected 8 MB flash, then requires the entire first settings sector to be erased.
It refuses existing settings, makes one write attempt, and compares the full sector afterward. The record contains
the key, default clock, and enabled radio switches, with no Wi-Fi credentials.

Only after verified success, reset the board manually and connect through Direct's **Bluetooth** option with the
same key. Device operations leave the board in download mode rather than automatically starting the application.

## If the operation fails

After a failure, timeout, or interruption, do not rerun provisioning or erase settings. To inspect the sector,
rerun the dry run to get its address, then read the sector without writing:

```sh
python tools/firmware/provision-c6.py --build-dir build/firmware-esp32c6-no-storage --port PORT --dry-run
readback=$(mktemp)
chmod 600 "$readback"
python -m esptool --chip esp32c6 --port PORT --after no-reset read-flash OFFSET 4096 "$readback"
python - "$readback" <<'PY'
from pathlib import Path
import sys

print("erased" if Path(sys.argv[1]).read_bytes() == b"\xff" * 4096 else "not erased")
PY
rm -f -- "$readback"
```

Replace `OFFSET` with the address printed by the dry run. The temporary readback may contain settings; keep it
private and remove it after inspection. If the sector is not erased, leave the board unchanged. Only retry
commissioning after readback confirms the entire sector is erased.
The utility never requests a full-chip erase or retries an uncertain write.

The utility removes its private temporary files on normal success or failure. Abrupt process termination can leave
private files to clean up manually.

Keys and settings remain unencrypted in flash; Control-key authentication does not encrypt BLE traffic.
See [boot settings](../../reference/settings.md) for the record format and power-loss limits, and
[wireless setup](../wireless.md#bluetooth-le) for connecting afterward.
