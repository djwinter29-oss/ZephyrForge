# Wireless setup

Wireless features depend on the board:

| Board | Wi-Fi setup | Bluetooth LE |
| --- | --- | --- |
| ESP32-S3 | Scan, test, and save over USB | Provision a Control key over USB, then connect with Direct |
| ESP32-C6 | No dashboard Wi-Fi provisioning path | [Commission the first key physically](boards/esp32c6-commissioning.md), then connect with Direct |
| Pico W / Pico 2 W | USB-controlled scans and temporary joins | Not supported |
| Pico / Pico 2 | No radio | Not supported |

Radio switches default to enabled where supported. Wi-Fi needs saved credentials for an automatic boot-time join;
BLE needs a saved Control key before it advertises. An enabled switch is not evidence of a working connection.

Pico W settings are read at boot but cannot be saved. Dashboard credentials stay in RAM and disappear on
cancellation, expiry, or reset. Saving is disabled and Bluetooth provisioning is hidden on these boards.
Their shared-SPI CYW43439 connection is not supported by the pinned Zephyr Bluetooth transport.
The W-board PRNG is used for noncryptographic network identifiers, not authentication nonces.
Before deploying W-board RF, set `CONFIG_WHD_WIFI_COUNTRY` to the appropriate `WHD_COUNTRY_*` value; the pinned
driver defaults to `WHD_COUNTRY_AUSTRALIA`. A build is not regulatory qualification.

## Configuration and operation

On ESP32-S3 or Pico W, connect over USB and open **Boot configuration** in Python/.NET or **Board configuration**
in Direct. Choose **Configure…** beside the radio. This opens provisioning without changing the next-boot switch.
The C6 cannot use this USB setup flow.

### Wi-Fi

Use a **2.4 GHz WPA2-Personal or open network with DHCP**. WPA3-SAE and enterprise networks are not supported.

1. Select a scanned network or enter a hidden SSID.
2. Enter a password if required, then choose **Test connection**.
3. On ESP32-S3, use **Save&Reboot** after a successful test to persist the credentials. On Pico W, the join remains
   temporary.

Testing does not write flash. Staged credentials expire after ten minutes; canceling discards them and returns
to the saved network. Save a successful pair before expiry or test it again. The dashboard requires a successful
test before saving; the control API also permits saving an untested staged pair.

The Wi-Fi boot switch controls automatic joining, not explicit tests or scans. Scanning pauses an in-progress
saved-network join, which resumes afterward. A test succeeds only with an IPv4 address; association alone is
insufficient. Joins time out after 30 seconds. Lost saved networks retry after 5 seconds, doubling up to 60 seconds.
Scans time out after 15 seconds on W boards and 10 seconds on ESP32-S3.

After canceling a join, a replacement waits for confirmed driver disconnection. A timed-out scan remains
quarantined until its driver reports completion; if completion never arrives, reboot to recover rather than
starting another request against a possibly active driver.

### Bluetooth LE

On ESP32-S3, create a Control key over USB and copy it to a password manager before saving. The board never returns
the key. On ESP32-C6, follow [first-key commissioning](boards/esp32c6-commissioning.md) instead.

After saving and rebooting:

1. Open Direct in desktop Chrome or Edge, served from localhost or HTTPS.
2. Choose **Bluetooth** and enter the saved key.
3. Select `ZephyrForge-<serial>`.

Python and .NET can provision a key over USB but cannot connect through BLE.
The Control key authenticates commands; it is not a pairing password and does not encrypt the link.
See the [BLE specification](../reference/bluetooth.md) for client and authentication details.

### Save and dashboard differences

**Save&Reboot** commits boot settings and staged credentials, then reboots after a verified save.
There is no forced save: safely eject and [return an exported SD card](../reference/sdcard.md#usb-drive-handoff)
before saving. Radio switches and saved credentials take effect at the next boot.

Direct supports USB and a restricted set of BLE actions. Python and .NET manage the selected USB-attached board.
BLE cannot provision credentials, test/scan Wi-Fi, or change the saved radio switches.
See [boot settings](../reference/settings.md) for persistence and failure semantics.

## Troubleshooting and recovery

The driver does not reliably distinguish a wrong password from a missing access point. Check the network band,
SSID, password, DHCP service, and [UART log](console.md#uart) rather than inferring the cause from a generic failure.

If delivery or a test result is lost, choose **Re-sync**, not another test or save. The Python dashboard can recover
an interrupted RAM-only Wi-Fi operation after a reset by checking the reset control slot, wireless generations,
and absence of staged credentials. It reports the operation as discarded, never successful.

An uncertain Save or Reboot needs separate reconciliation of the saved state. Power-cycling alone does not clear
the dashboard's recovery state.

On ESP32-S3, the [raw TCP diagnostic shell](console.md#raw-tcp) is reachable on a usable IPv4 connection, including
a temporary test join. Pico and C6 images do not include that shell.

## Security boundaries

- BLE telemetry, storage, and wireless reads require an unlocked session. The Control key authenticates commands,
  but BLE traffic is not encrypted.
- Wi-Fi credentials and the Control key are unencrypted in flash. Physical flash, UART-download, or JTAG access can
  expose them. Reports do not return secrets, and the clients do not keep them in browser storage or on the SD card.
- Python and .NET bind to loopback and reject foreign Host/Origin headers. Do not expose or reverse-proxy them to a
  LAN without a separate authentication design.
- The diagnostic TCP shell is unauthenticated plaintext; the BLE key does not protect it. Use a trusted, isolated
  network without port forwarding.

Current physical evidence and remaining RF, coexistence, and recovery tests are listed in
[qualification gaps](../validation/qualification-gaps.md).
