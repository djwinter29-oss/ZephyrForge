# Use a dashboard

[Documentation](../README.md) / [Dashboards](dashboards/README.md) / Use a dashboard

Launch your [chosen dashboard](dashboards/README.md) first. Connection, controls, and recovery work as described
below unless a dashboard-specific difference is noted.

- [Connect over USB](#connect-over-usb), including [Linux permissions](#linux-usb-permissions)
- [Read status](#read-the-dashboard) and [make changes](#make-changes)
- [Recover after a timeout](#recover-after-a-timeout)

## Connect over USB

1. Connect the board's native management USB port, not its USB-UART bridge. Use a data-capable cable.
2. Close other management clients. Only one HID client should own a board; separate host processes can conflict.
3. In Direct, choose **USB**, then **Select board**, and grant access to ZephyrForge. Python/.NET discover attached
   boards; use **Rescan** and the board selector when needed.
4. Confirm the model, serial, firmware version, and fresh telemetry before using controls.

For Bluetooth, follow [wireless setup](wireless.md#bluetooth-le). For startup failures, check the
[UART log](console.md#uart).

### Linux USB permissions

From a repository checkout, run the permission helper once:

```sh
bash tools/host/hid-permissions-setup.sh
```

Without a checkout, download [the helper](../../tools/host/hid-permissions-setup.sh) and run it with Bash.
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
select SDMMC, SPI, or no storage when [building the image](../development/firmware-build.md).

## Make changes

Controls follow the connected board's capabilities, transport, and fresh reports. An unavailable control does not
become available merely because the physical board has that peripheral.

- **Save settings:** **Save&Reboot** confirms changes, saves, and schedules a reboot. See
  [clock settings](../reference/boot-settings.md#system-clock) or
  [radio settings and credential deletion](wireless.md#save-settings).
- **Reboot:** **Reboot** / **Reboot Board** uses already saved settings, not unsaved edits. Cancel or Escape sends nothing.
- **Provision wireless:** USB only. Follow [Wi-Fi and Control-key provisioning](wireless.md);
  BLE cannot stage credentials or change radio switches.
- **Use an SD card:** follow the [card export and benchmark procedures](sd-card.md), including OS eject before return.
- **Set RGB color:** the Device information dropdown sends a volatile, low-intensity color request on supported
  boards. It needs a working reported driver and does not save a setting.
- **Open the Wi-Fi shell:** follow [diagnostic shells](console.md#in-browser-terminal).

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
