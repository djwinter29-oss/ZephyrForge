# Dashboards

[Documentation](../../README.md) / Dashboards

## Choose a dashboard

| Dashboard | Connection to the board | What runs on your computer |
| --- | --- | --- |
| [Direct](direct.md) | Browser-native USB HID or Bluetooth LE | Desktop Chrome or Edge; hosted page or optional local server |
| [Python](python.md) | USB HID | Python 3.10+ host serving a local web page |
| [.NET](dotnet.md) | USB HID | Packaged host with runtime, or .NET 10 SDK for a source checkout |

Use Direct for Bluetooth or a no-install hosted page. Use Python or .NET for a local USB bridge; they serve the
same dashboard. Direct is independent. Check [board connections](../../hardware/README.md) before choosing a
transport; the ESP32-C6 uses BLE and needs [first-key commissioning](../../hardware/esp32c6-commissioning.md).

Follow the selected guide to launch it. The controls and recovery rules below apply to all dashboards unless noted.

## Connect over USB

1. Connect the board's native management USB port, not its USB-UART bridge. Use a data-capable cable.
2. Close other management clients. Only one HID client should own a board; separate host processes can conflict.
3. In Direct, choose **USB**, then **Select board**, and grant access to ZephyrForge. Python/.NET discover attached
   boards; use **Rescan** and the board selector when needed.
4. Confirm the model, serial, firmware version, and fresh telemetry before using controls.

For Bluetooth, follow [wireless setup](../wireless.md#bluetooth-le). If Linux refuses HID access, use the permission
procedure below. For startup failures, check the [UART log](../console.md#uart).

### Linux USB permissions

From a repository checkout, run the permission helper once:

```sh
bash tools/host/hid-permissions-setup.sh
```

Without a checkout, download [the helper](../../../tools/host/hid-permissions-setup.sh) and run it with Bash.
It may prompt for sudo to install `/etc/udev/rules.d/99-zephyrforge-hid.rules` for `CAFE:4021`.
Reconnect the board and restart the client afterward. Run the browser and host as your regular user.
The Python/.NET checkout wrappers check permissions before starting.

## Read the dashboard

| Area | What it shows |
| --- | --- |
| Device information | Product, serial, firmware, system clock, and module memory |
| Board configuration in Direct; Boot configuration in Python/.NET | Next-boot clock and radio switches, plus reset cause |
| Pin configuration | Management UART, storage, and card-detect allocation |
| Live diagnostics | Uptime, core load, temperature, storage clock, and wireless state |
| Storage | Card identity, capacity, filesystem, ownership, and available actions |

Hover or focus Product for board/MCU details, or Module memory for SRAM/PSRAM reservations and flash capacity.
Click/tap toggles the details; Escape closes them. RAM reservation is boot-stable linked accounting, not live heap
usage. Flash capacity is physical capacity, not free space.

Unavailable values display `--`, never a fabricated zero. Stale data is not current data, and unknown card presence
is not confirmed absence. Per-core load shows Core 0 / Core 1; an unused core remains unavailable. Storage appears
once detection or initialization is known and can remain visible during a mount failure or USB ownership.
Python/.NET show malformed diagnostic values with a warning and preserve last-known storage visibility while busy
or stale; disconnect clears detection. Failed status polls remain visible in open wireless dialogs, and the page
shows the age of the last successful status fetch.

The storage clock is the value reported by firmware, not a throughput measurement. The SD profile stays Standard;
select SDMMC, SPI, or no storage when [building the image](../../development/firmware-build.md).

## Make changes

Controls follow the connected board's capabilities, transport, and fresh reports. An unavailable control does not
become available merely because the physical board has that peripheral.

- **Save settings:** **Save&Reboot** confirms changes, saves, and schedules a reboot. See
  [clock settings](../../reference/boot-settings.md#system-clock) or
  [radio settings and credential deletion](../wireless.md#save-settings).
- **Reboot:** **Reboot** / **Reboot Board** uses already saved settings, not unsaved edits. Cancel or Escape sends nothing.
- **Provision wireless:** USB only. Follow [Wi-Fi and Control-key provisioning](../wireless.md);
  BLE cannot stage credentials or change radio switches.
- **Export an SD card:** enable the USB drive, then safely eject in the OS before confirming return.
  Follow [SD handoff](../../reference/sd-storage.md#usb-drive-handoff).
- **Run a benchmark:** confirm the size and card-write warning after reading
  [benchmark safety and cleanup](../../reference/sd-storage.md#optional-storage-benchmark). BLE reads progress only.
- **Set RGB color:** the Device information dropdown sends a volatile, low-intensity color request on supported
  boards. It needs a working reported driver and does not save a setting.
- **Open the Wi-Fi shell:** follow [diagnostic shells](../console.md#in-browser-terminal).
  Direct needs its local server; Python/.NET include a relay.

Save confirmation identifies the board and summarizes boot/radio changes and whether credentials are unchanged,
replaced, or erased. It may name the tested network but never displays passwords or Control keys. Return an
exported card to firmware before saving or rebooting.

Unsaved settings and wireless edits trigger a warning before being discarded. A board change invalidates pending
action confirmations. Successful save reporting requires a matching completed result; after reconnecting, reread
configuration rather than assuming the requested state was applied.

## Recover after a timeout

A timeout or disconnect can leave a command's outcome uncertain. Further writes stay blocked:

1. Do not submit the command again.
2. Reconnect to the same board and choose **Re-sync**.
3. Inspect the reported command result and current configuration or storage state. For a wireless save, verify
   clock and radio switches; credentials are never returned.
4. If the state still cannot be confirmed, leave recovery unresolved.

**Python/.NET:** an unresolved command survives a page reload. After a confirmed reboot, writes remain blocked until
the host observes restart evidence: disconnect/reconnect, reboot-pending followed by clear, or fresh lower uptime.
If controls remain blocked after reconnection, reload the page to reconcile with the host.

**Direct:** keep the originating tab open until recovery is resolved; its pending recovery does not carry to a new
tab. **Abandon unresolved recovery…** clears only the local record after confirmation, not the board's state.
Navigating away releases the board; returning through browser history starts disconnected with drafts cleared.
