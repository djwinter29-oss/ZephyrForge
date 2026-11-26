# Use Direct

[Documentation](../../README.md) / [Dashboards](README.md) / Direct

Direct connects to a ZephyrForge board from desktop Chrome or Edge using USB (WebHID) or Bluetooth LE
(Web Bluetooth). It does not need the Python or .NET host application.

## Choose how to run it

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
For version rules and development checks, see
[host development](../../development/host-tools.md#direct-integration).

## Connect a board

Follow [USB connection](README.md#connect-over-usb) or [Bluetooth connection](../wireless.md#bluetooth-le).
The dashboard guide also covers [controls](README.md#make-changes),
[Linux HID permissions](README.md#linux-usb-permissions), and [recovery](README.md#recover-after-a-timeout).

## Wireless provisioning

Follow [Wireless Setup](../wireless.md#open-radio-settings) to provision either radio over USB.

A failed replacement test invalidates the previous tested pair. Save stays blocked until another test succeeds
or cancellation clears the temporary credentials. If cancellation fails, the dialog stays open until clearing is
confirmed. A failed replacement-key save wipes submitted key text; regenerate it or disable Bluetooth to abandon
the replacement. Before disabling a configured radio, read the
[credential-deletion policy](../wireless.md#save-settings).

The form shows why Save is blocked. The same eligibility checks run again when confirming an action.

## Wi-Fi shell

The local ZephyrForge server enables the shell link when the board reports a fresh Wi-Fi connection.
The release ZIP includes this server. GitHub Pages and other static servers show Wi-Fi status without the link.

Keep the originating dashboard tab open and connected. The relay checks the board identity, address, and
status freshness before each operation. It accepts private LAN IPv4 targets on port `2323` through
same-origin loopback requests.

Review the [TCP shell limits](../console.md#raw-tcp) before connecting.

For Direct's browser compatibility, polling, freshness, and command-queue behavior, see
[Direct protocol behavior](../../development/host-tools.md#direct-protocol-behavior). If a command outcome is
uncertain, follow [dashboard recovery](README.md#recover-after-a-timeout).
