# Use Direct

[Documentation](../../README.md) / [Dashboards](README.md) / Direct

Direct connects to a ZephyrForge board from desktop Chrome or Edge using USB (WebHID) or Bluetooth LE
(Web Bluetooth). It does not need the Python or .NET host application.

## Choose how to run it

| Option | Start | Wi-Fi shell link |
| --- | --- | --- |
| Hosted page | [Open Direct](https://djwinter29-oss.github.io/ZephyrForge/) | Not available |
| Repository checkout | Run `bash tools/host/run-direct.sh --port 8000` from the repository root | Included |
| Release ZIP | Extract `zephyrforge-direct-vX.Y.Z.zip`, then run `python3 server.py --port 8000` | Included |

The hosted page follows a published release. For firmware built from a checkout, or a board/image variant the hosted
page does not support, run Direct from the firmware's matching checkout.

For either local option, open **http://127.0.0.1:8000/**. The local server uses Python's standard library, not the
ZephyrForge Python host package. The browser talks directly to HID/BLE; the server does not bridge those connections.

WebHID and Web Bluetooth need a supported browser/OS and a secure context: HTTPS or localhost, not a `file://`
page. Bluetooth support on Linux varies by browser and adapter. The dashboard has no JavaScript bundle or runtime
package installation step.

Local checkout launches serve a temporary versioned snapshot; restart the server after editing or committing.
The version tooltip shows the source commit. For version rules and development checks, see
[host development](../../development/host-tools.md#direct-integration).

## Connect a board

Follow [USB connection](../dashboard-use.md#connect-over-usb) or [Bluetooth connection](../wireless.md#bluetooth-le).
The shared [dashboard guide](../dashboard-use.md) covers status, settings, and recovery.
For the optional terminal, follow [in-browser Wi-Fi shell](../console.md#in-browser-terminal).
