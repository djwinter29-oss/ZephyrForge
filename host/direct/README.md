# Direct dashboard

Direct connects to a ZephyrForge board from desktop Chrome or Edge using USB (WebHID) or Bluetooth LE
(Web Bluetooth). It does not need the Python or .NET host application.
Physical test coverage is limited to selected ESP32-S3 setups; see
[qualification gaps](../../docs/validation/qualification-gaps.md).

Controls follow the board's advertised capabilities. Pico boards use USB only; ESP32-C6 uses BLE after
[physical key commissioning](../../docs/guides/boards/esp32c6-commissioning.md).

## Quick start

### Choose how to run it

| Option | Start |
| --- | --- |
| Hosted page | [Open Direct](https://djwinter29-oss.github.io/ZephyrForge/) |
| Repository checkout | Run `bash tools/host/run-direct.sh --port 8000` from the repository root |
| Release ZIP | Extract `zephyrforge-direct-vX.Y.Z.zip`, then run `python3 server.py --port 8000` |

For either local option, open **http://127.0.0.1:8000/**. The local server uses Python's standard library, not the
ZephyrForge Python host package. The browser talks directly to HID/BLE; the server does not bridge those connections.

WebHID and Web Bluetooth need a supported browser/OS and a secure context: HTTPS or localhost, not a `file://`
page. Bluetooth support on Linux varies by browser and adapter. The dashboard has no JavaScript bundle or runtime
package installation step.

Local checkout launches serve a temporary versioned snapshot. Restart the server after editing or committing to
refresh it. Release ZIPs and Pages keep their release version; the version tooltip shows the source commit.
For version rules and development checks, see [host development](../../docs/development/host-tools.md#direct-dashboard-independence).

### Connect a board

1. Close other applications using the board's HID interface, including Python/.NET hosts.
2. Choose **USB**, then **Select board**.
3. Select the ZephyrForge device (`0xCAFE:0x4021`) and grant access to its vendor HID interface.
4. Wait for valid identity, configuration, and fresh status before using controls.

Cancel leaves Direct disconnected. Close other applications using the board's HID interface if it is unavailable.

For BLE, first save a Control key through USB on ESP32-S3 or physical commissioning on C6. Choose **Bluetooth**,
enter the saved key, and select `ZephyrForge-<serial>`. The key authenticates commands; it does not encrypt traffic.
See [wireless setup](../../docs/guides/wireless.md).

## Dashboard layout

| Section | Contents |
| --- | --- |
| Device information | Product, serial, system clock, firmware, and module memory |
| Board configuration | Next-boot clock and radio switches, reset cause |
| Pin configuration | Management UART, storage, and card-detect allocations |
| Live diagnostics | Uptime, Core 0 load, temperature, storage clock, and wireless state |
| Storage | Card state, capacity, filesystem, identity, and available storage actions |

Hover or focus Product for Board/MCU details, or Module memory for SRAM/PSRAM reservations and flash capacity.
Click/tap toggles the details; Escape closes them. Reserved RAM is boot-stable accounting, not live heap usage.
The storage clock is the value reported by firmware, not a dashboard measurement. The saved SD profile is reserved
as Standard (profile 0); storage transport is selected when building the image.

## Controls

Controls depend on the board, transport, and fresh reports. Unsupported actions stay unavailable.

- **Clock and reboot:** supported over USB and authenticated BLE where the board permits them. ESP32-S3 offers
  Default, 80, 160, or 240 MHz; C6 accepts at most 160 MHz. Pico images cannot save settings.
- **Wireless provisioning:** USB only. BLE cannot stage credentials, scan/test Wi-Fi, or change radio switches.
- **USB drive:** export an initialized card, then safely eject in the OS before confirming return to firmware.
  BLE cannot change ownership. See [SD handoff](../../docs/reference/sdcard.md#usb-drive-handoff).
- **RGB LED:** the Device information dropdown sends a volatile color request over USB or authenticated BLE.
  It requires fresh telemetry showing an available driver; it does not save a setting.
- **Benchmark:** when supported, confirm the size and card-write warning before starting. Cancellation and
  progress are shown in Storage; BLE mirrors progress read-only. A board change invalidates an open confirmation.
  See [benchmark safety](../../docs/reference/sdcard.md#optional-storage-benchmark).

### Wireless provisioning

1. Over USB, choose **Configure…** for the radio.
2. For Wi-Fi, select a network or **Other network…**, enter its password if needed, and successfully test it.
3. For Bluetooth, generate a Control key and keep a copy in a password manager.
4. On a board that supports persistence, choose **Save&Reboot** before staging expires, normally ten minutes.

A failed replacement test invalidates the previous tested pair. Save stays blocked until another test succeeds
or cancellation clears the temporary credentials. If cancellation fails, the dialog stays open until clearing is
confirmed. A failed replacement-key save wipes submitted key text; regenerate it or disable Bluetooth to abandon
the replacement. Disabling a configured radio erases its saved credentials on a successful save.

The form shows why Save is blocked. The same eligibility checks run again when confirming an action.

### Saving and rebooting

**Save&Reboot** requires confirmation and a matching completed result before reporting success. Firmware saves
and schedules a reboot; Direct rereads configuration after reconnection. BLE saves keep the saved radio switches.
**Reboot** is a separate confirmed action; Cancel or Escape sends nothing.

Return an exported card to firmware before saving. Disconnecting with unsaved edits asks for
confirmation and identifies the board.

## Wi-Fi shell

When served by the local ZephyrForge server, fresh connected Wi-Fi status links to the
[diagnostic shell](../../docs/guides/console.md). Keep the originating dashboard tab open and connected.
The terminal checks that tab's fresh board identity and address before opening, reading, or sending.
Replacement, stale status, or a closed dashboard disconnects the terminal.

The relay accepts only private LAN IPv4 targets on port 2323 through same-origin loopback requests. Pages and
Other static servers show plain Wi-Fi status instead of a shell link. TCP connection failures are reported because
connected Wi-Fi does not guarantee that the listener is ready.

Keep the originating dashboard tab open while using the terminal. The relay checks the current board and Wi-Fi
status and disconnects when either changes. The release ZIP includes the relay and needs no Python host installation.
The [shared HTTP API](../web/README.md#browser-shell) documents the relay contract.

The terminal discards unfinished input from the previous client and requests `shell echo on` once for that
session. It supports plain-text output and native editing, not full VT100 or color emulation. Tab stays in the
terminal; Shift+Tab leaves it. Multiline paste needs confirmation. Close the tab to disconnect; abandoned sessions
expire after 15 idle seconds.

**The shell is unauthenticated plaintext.** The Control key does not protect it. Use a trusted, isolated network
and never enter secrets or replay uncertain input.

## Page lifecycle

Navigating away releases the board. Returning through browser history reloads into a disconnected state with
drafts cleared; reconnect to continue.

After a timeout or disconnect, use **Re-sync** before sending another command. Keep the tab open until the result is
resolved; pending recovery does not carry over to a new tab.

If the board's state cannot be confirmed, leave recovery unresolved. **Abandon unresolved recovery…** clears only
the local recovery record after confirmation; it does not change the board.

## Protocol and connection

Direct checks the [USB HID](../../docs/reference/usb-hid.md) and
[BLE](../../docs/reference/bluetooth.md) reports before enabling controls. After reconnecting, it verifies the same
board and reads outstanding results without resending commands.

Direct disables controls when reports are stale or invalid. Protocol references define polling and freshness limits.
