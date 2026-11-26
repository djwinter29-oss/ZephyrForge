# Wireless setup

[Documentation](../README.md) / Guides / Wireless

Wireless features depend on the board:

| Board | Wi-Fi setup | Bluetooth LE |
| --- | --- | --- |
| ESP32-S3 | Scan, test, and save over USB | Provision a Control key over USB, then connect with Direct |
| ESP32-C6 | No dashboard Wi-Fi provisioning path | [Commission the first key physically](esp32c6-commissioning.md), then connect with Direct |
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
The C6 uses the [physical commissioning procedure](esp32c6-commissioning.md) for its first BLE key.

## Wi-Fi

Use a **2.4 GHz WPA2-Personal or open network with DHCP**. WPA3-SAE and enterprise networks are not supported.

1. Select a scanned network or choose **Other network…** and enter the hidden SSID.
2. Enter a password if required, then choose **Test connection**.
3. On ESP32-S3, use **Save&Reboot** after a successful test to persist the credentials. On Pico W, the join remains
   temporary.

Testing does not write flash. Staged credentials expire after ten minutes; canceling discards them and returns
to the saved network. Save a successful pair before expiry or test it again. The dashboard requires a successful
test before saving.

The Wi-Fi boot switch controls automatic joining, not manual tests or scans. Scanning pauses an in-progress
saved-network join, which resumes afterward. A test needs an IPv4 address; association alone is not enough.
Joins time out after 30 seconds; saved-network retries start after 5 seconds and double up to 60 seconds. Scans
time out after 10 seconds on ESP32-S3 and 15 seconds on Pico W. After canceling a join, wait for disconnection
before starting another. If a scan times out without a completion result, reboot before starting another scan.

## Bluetooth LE

On ESP32-S3, choose **Generate key** over USB, copy the Control key to a password manager, then choose
**Save&Reboot**. The board never returns the key. On ESP32-C6, follow
[first-key commissioning](esp32c6-commissioning.md).

Once the board is running with a saved key:

1. Open Direct in desktop Chrome or Edge, served from localhost or HTTPS.
2. Choose **Bluetooth** and enter the saved key.
3. Select `ZephyrForge-<serial>`.

Python and .NET can provision a key over USB; use Direct for a BLE connection.
See the [BLE specification](../reference/bluetooth-le.md) for authentication and the
[security boundaries](../reference/security.md#management-links) before connecting.

## Save settings

**Save&Reboot** commits boot settings and staged credentials, then reboots after a verified save.
Safely eject and [return an exported SD card](sd-card.md#export-or-return-a-card) before saving.
Radio switches and saved credentials take effect at the next boot. If a radio has saved credentials, disabling it
in a successful dashboard save clears those credentials; enabling it again requires provisioning them again.
USB saves can change radio switches; BLE saves preserve the saved switches.

The [boot settings reference](../reference/boot-settings.md) explains persistence and failure semantics.

## Troubleshooting and recovery

The driver reports a generic connection failure for both a wrong password and a missing access point. Check the
network band, SSID, password, DHCP service, and [UART log](console.md#uart).

For a timed-out command or dropped connection, follow
[dashboard recovery](dashboard-use.md#recover-after-a-timeout).

On ESP32-S3, the [raw TCP diagnostic shell](console.md#raw-tcp) is reachable on a usable IPv4 connection, including
a temporary test join.

### Failed provisioning in Direct

A failed replacement test invalidates the previous tested pair. Save stays blocked until another test succeeds
or cancellation clears the temporary credentials. If cancellation fails, the dialog stays open until clearing is
confirmed. A failed replacement-key save wipes submitted key text; regenerate it or disable Bluetooth to abandon
the replacement, subject to the [credential-deletion policy](#save-settings).

The form shows why Save is blocked. The same eligibility checks run again when confirming an action.
