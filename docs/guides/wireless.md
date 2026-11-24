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

Pico W reads settings at boot but cannot save them. Wi-Fi credentials entered in the dashboard stay in RAM and are
discarded on cancellation, expiry, or reset. The release image uses the driver's Australia country default.
Use a locally built image configured for your regulatory region before enabling Pico W Wi-Fi; see the
[Pico build instructions](../development/firmware-build.md#raspberry-pi-pico-family).

## Open radio settings

On ESP32-S3 or Pico W, connect over USB and open **Boot configuration** in Python/.NET or **Board configuration**
in Direct. Choose **Configure…** beside the radio. This opens provisioning without changing the next-boot switch.
The C6 uses the [physical commissioning procedure](boards/esp32c6-commissioning.md) for its first BLE key.

### Wi-Fi

Use a **2.4 GHz WPA2-Personal or open network with DHCP**. WPA3-SAE and enterprise networks are not supported.

1. Select a scanned network or enter a hidden SSID.
2. Enter a password if required, then choose **Test connection**.
3. On ESP32-S3, use **Save&Reboot** after a successful test to persist the credentials. On Pico W, the join remains
   temporary.

Testing does not write flash. Staged credentials expire after ten minutes; canceling discards them and returns
to the saved network. Save a successful pair before expiry or test it again. The dashboard requires a successful
test before saving; the control API also permits saving an untested staged pair.

The Wi-Fi boot switch controls automatic joining, not manual tests or scans. Scanning pauses an in-progress
saved-network join, which resumes afterward. A test needs an IPv4 address; association alone is not enough. See the
[wireless lifecycle notes](../reference/architecture.md#wi-fi-lifecycle) for retry and timeout behavior.

After canceling a join, wait for the existing request to finish before starting another. If a scan times out and
never completes, reboot the board before trying again.

### Bluetooth LE

On ESP32-S3, create a Control key over USB and copy it to a password manager before saving. The board never returns
the key. On ESP32-C6, follow [first-key commissioning](boards/esp32c6-commissioning.md) instead.

After saving and rebooting:

1. Open Direct in desktop Chrome or Edge, served from localhost or HTTPS.
2. Choose **Bluetooth** and enter the saved key.
3. Select `ZephyrForge-<serial>`.

Python and .NET can provision a key over USB; use Direct for a BLE connection.
The Control key authenticates commands but does not encrypt the link.
See the [BLE specification](../reference/bluetooth.md) for client and authentication details.

### Save settings

**Save&Reboot** commits boot settings and staged credentials, then reboots after a verified save.
Safely eject and [return an exported SD card](../reference/sdcard.md#usb-drive-handoff) before saving.
Radio switches and saved credentials take effect at the next boot.

See [boot settings](../reference/settings.md) for persistence and failure semantics.

## Troubleshooting and recovery

The driver reports a generic connection failure for both a wrong password and a missing access point. Check the
network band, SSID, password, DHCP service, and [UART log](console.md#uart).

If a command times out or the connection drops, reconnect to the same board and choose **Re-sync**. Check the
reported command result and reread the board configuration before starting another operation. For **Save&Reboot**,
confirm the saved clock and radio switches in the configuration view; credentials are not returned by the board.
If the result remains uncertain, leave the operation unresolved rather than submitting it again.

For a timed-out scan that remains active, reboot the board before trying another scan.

On ESP32-S3, the [raw TCP diagnostic shell](console.md#raw-tcp) is reachable on a usable IPv4 connection, including
a temporary test join. Pico and C6 images do not include that shell.

## Security boundaries

- BLE telemetry, storage, and wireless reads require an unlocked session. The Control key authenticates commands,
  but the radio link is not encrypted.
- Wi-Fi credentials and the Control key are unencrypted in flash. Physical flash, UART-download, or JTAG access can
  expose them. Reports do not return secrets, and the clients do not keep them in browser storage or on the SD card.
- Python and .NET bind to loopback and reject foreign Host/Origin headers. Do not expose or reverse-proxy them to a
  LAN without a separate authentication design.
- The diagnostic TCP shell is unauthenticated plaintext; the BLE key does not protect it. Use a trusted, isolated
  network without port forwarding.
