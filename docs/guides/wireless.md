# Wireless Setup and Operation

Wi-Fi and Bluetooth LE are compiled into ESP32-S3 and ESP32-C6 images, and both radio switches **default to enabled**.
Pico W and Pico 2 W include Wi-Fi, while the non-W Pico boards have no radios. Wi-Fi joins its saved network
at boot, while a dashboard can temporarily test a candidate network. BLE advertises only when it is enabled and a
Control key is saved. Radio switches and saved credentials are boot settings; see the
[settings design](../reference/settings.md) for persistence and save behaviour. Espressif software coexistence is
enabled on ESP32-S3.
ESP32-C6 has no USB HID path for first-use setup. Its BLE service remains off until a Control key is saved, and the
[physical C6 commissioning procedure](#initial-esp32-c6-commissioning) below supplies that key without adding
unauthenticated BLE access. This procedure has software checks only, not physical qualification.

Pico W and Pico 2 W support USB-controlled scans and temporary candidate joins in both SD images. Their settings
record is read at boot, but settings save remains unsupported: credentials tested through the dashboard stay in RAM
and disappear on cancellation, expiration or reset. The dashboards keep Wi-Fi configuration available, disable
saving and hide Bluetooth provisioning. Radio switches cannot be changed persistently on these boards.

Bluetooth LE is not supported on either W target: pinned Zephyr supports the CYW43439 UART HCI transport, not the
shared-SPI transport wired on these boards. The images neither advertise BLE support nor stage Control keys.
Their SDK PRNG supplies noncryptographic network identifiers only, not authentication nonces.
Before using W-board RF, set `CONFIG_WHD_WIFI_COUNTRY` to the appropriate `WHD_COUNTRY_*` value for deployment;
the pinned driver defaults to `WHD_COUNTRY_AUSTRALIA`. A successful build is not regulatory qualification.

## Configuration and operation

On ESP32-S3 and Pico W boards, use USB to provision wireless settings. In Python and .NET, open the selected board's
**Boot configuration**. In the Direct dashboard, open **Board configuration**. Use **Configure…** to scan and test
Wi-Fi or to create a Control key. The radio switches are separate next-boot controls.
ESP32-C6 has no USB HID and cannot use this setup flow; its BLE service requires a Control key already present in
saved settings.

### Initial ESP32-C6 commissioning

This is an initial Control-key procedure for ESP32-C6-DevKitC-1 N8, not a Wi-Fi provisioning, key-rotation or
settings-reset workflow. It requires authorized physical access to an unencrypted board's UART flashing port.

1. Check the module marking and use a trusted, initialized Zephyr workspace to
   [build and flash the selected C6 image](../development/firmware-build.md#esp32-c6-family).
   Retain the matching build directory, including compiled configuration and devicetree, not just the release binary.
   The utility derives the settings address from that build; it does not prove that its image is installed.
2. Close other programs using the board's flashing port. Obtain exclusive board ownership and explicit authorization
   to write settings. Activate the workspace's Python venv, which includes esptool. From the checkout, when it is a
   direct child of the workspace, validate the selected build without reading a key or accessing a device:

   ```sh
   source ../.venv/bin/activate
   python tools/firmware/provision-c6.py --build-dir build/firmware-esp32c6-no-storage --port PORT --dry-run
   ```

   Replace `PORT` with the actual UART flashing port. For an SD-over-SPI image, select
   `build/firmware-esp32c6-sd-spi` instead. A dry run validates build metadata only, not the board or its settings.
3. Generate a unique, cryptographically random 32-byte Control key and retain it securely in a password manager.
   Authorize the write and enter its 64 hexadecimal digits at the hidden terminal prompt:

   ```sh
   python tools/firmware/provision-c6.py --build-dir build/firmware-esp32c6-no-storage --port PORT --allow-settings-write
   ```

   Do not put the key in command arguments, environment variables, logs or result records. Piped or echoed key entry
   is refused. The utility checks the C6 chip and detected 8 MB flash, then requires the entire first settings sector
   to be erased. It refuses existing settings, makes one write attempt, and compares the full sector after writing.
   The record contains the key, default clock and enabled radio switches, with no Wi-Fi credentials.
4. Only after the utility reports verified success, reset the board manually and connect through Direct's
   **Bluetooth** option with the same key. Device operations leave the board in download mode rather than automatically
   running the application.

Keys and settings remain unencrypted in flash; Control-key authentication does not encrypt BLE traffic. The utility
uses private temporary files and removes them on normal success or failure; abrupt process termination can leave
private files that require authorized cleanup. It never requests a full-chip erase or retries an uncertain write.
After a failure, timeout or interruption, inspect and reconcile the board with read-only operations before any
separately authorized recovery. Do not blindly rerun provisioning, erase existing settings or infer success.
The [settings specification](../reference/settings.md) defines the record and its power-loss limits.

### Wi-Fi

Select a scanned network or enter a hidden SSID, enter its password if needed, and test the connection. Testing
stages a temporary candidate; dashboard test-and-save flows require a successful test before saving it.
**Save&Reboot** is the only dashboard action that writes credentials to flash. Staged credentials expire after ten
minutes. Canceling discards them and returns to the saved network. If a test succeeds, save before it expires or
test again. The control API also allows an untested staged pair to be saved.

Scanning pauses an in-progress saved-network join, which resumes afterward. Scans and tests can run with the
Wi-Fi boot switch off; that switch controls only the automatic saved-network join at boot.
After a cancelled join, replacement admission waits for the driver to confirm disconnect or for an interface-status
check to confirm it is down. If a timed-out scan never reports completion, scan admission remains blocked rather than
reusing a possibly active driver; reboot the board to recover.

Use a 2.4 GHz WPA2-Personal or open network with DHCP; WPA3-SAE and enterprise networks are not supported. A test
succeeds when the board has an IPv4 address. A join that does not finish within 30 seconds fails. While a saved
network is lost, the board retries every 5 seconds, doubling the delay up to 60 seconds. The Zephyr ESP32 driver does
not report why a join failed, so a wrong password and a missing network look the same. Check the network band,
SSID, password, DHCP availability and UART logs rather than assuming a specific cause.
On W boards, association alone is not success: DHCP must bind before a test is marked joined. Blocking driver
requests run separately from USB management. Scans time out after 15 seconds on W boards and 10 seconds on ESP32-S3.

If USB delivery or the test result is lost, use **Re-sync** rather than resending the operation.
The Python dashboard can also recover a pending RAM-only Wi-Fi operation after a board reset: it checks that the
control result slot and both wireless generations are reset and no staged credentials remain. It then explicitly
reports that the temporary operation was discarded, not that the test succeeded, and allows a new test.
An uncertain Save or Reboot is not cleared this way; inspect saved settings and resolve its outcome separately.
Power-cycling the board does not by itself clear the dashboard's recovery state.

### Bluetooth LE

Create a Control key and copy it to a password manager before saving: the board never returns the key. Bluetooth
stays off until a key is saved. To connect, choose the Direct dashboard's **Bluetooth** option, enter the key, and
select `ZephyrForge-<serial>`. Use desktop Chrome or Edge with Web Bluetooth, served from localhost or HTTPS. Python and
.NET can provision Bluetooth settings over USB but do not connect over BLE.

The Control key authenticates commands. It is not a Bluetooth pairing password and does not encrypt the link.
The [Bluetooth LE specification](../reference/bluetooth.md) defines authentication and client behavior.

### Save and dashboard differences

**Save&Reboot** commits the radio switches and staged credentials with the other boot settings, then reboots after a
verified save. There is no forced save: safely eject and
[return an exported SD card](../reference/sdcard.md#usb-drive-handoff) before saving.

Python and .NET manage the selected USB-attached board. Direct supports USB (WebHID) and BLE, with a restricted set of
actions over BLE.

Live diagnostics report the wireless state; an enabled switch does not mean the radio is connected.
On ESP32-S3, the [raw TCP diagnostic shell](console.md) is reachable on a usable IPv4 connection, including a
temporary Wi-Fi test. The Pico images do not include it. C6 Wi-Fi configuration is unavailable without USB HID.

## Security boundaries

- The Control key authenticates BLE commands but does not encrypt BLE traffic. BLE reads of telemetry, storage and
  wireless state require an unlocked session.
- Saved Wi-Fi credentials and the Control key are **unencrypted in flash**. Anyone with physical flash, UART-download
  or JTAG access can read them. They are never returned in reports, kept in browser storage or written to the SD card.
- Python and .NET bind to loopback and reject foreign Host/Origin headers. Do not expose or reverse-proxy them onto a
  LAN without a separate authentication design.
- The board's diagnostic TCP shell is unauthenticated plaintext and is not protected by the BLE Control key.
  Follow the [console network precautions](console.md#raw-tcp).

## Qualification

Host checks and target builds do not qualify physical radio behaviour. No coexistence, RF performance, power
consumption or range has been measured. Review the [qualification gaps](../validation/qualification-gaps.md) before
relying on a hardware configuration.
