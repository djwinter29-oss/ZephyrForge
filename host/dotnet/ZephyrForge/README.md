# .NET host

The .NET 10 host monitors the ZephyrForge vendor HID interface (`0xCAFE:0x4021`) and serves the shared local dashboard
at `http://127.0.0.1:8002/` by default. Override the port with `--port PORT`; only one dashboard server can bind to a
port at a time.

Python and .NET serve the same dashboard. Direct is a separate browser application.

## Run the host

For a packaged host, extract the archive for your OS from
[GitHub Releases](https://github.com/djwinter29-oss/ZephyrForge/releases/latest) and run its executable. These
packages include the runtime. Windows/macOS packages are not signed or notarized and may require OS approval.

For a source checkout, install the .NET 10 SDK and run from the repository root:

```sh
dotnet run --project host/dotnet/ZephyrForge/ZephyrForge.csproj
```

Select a default board at launch with `--serial SERIAL` (the 16 hex-digit Report 1 UID) or `--device-path HID_PATH`.
The options are mutually exclusive; without either, the first discovered board is selected. Use **Rescan** to refresh
the board list. On Linux, grant HID access once with
[hid-permissions-setup.sh](../../../tools/host/hid-permissions-setup.sh), then reconnect the board.
[run-dotnet.sh](../../../tools/host/run-dotnet.sh) checks those permissions and runs this project.

The host follows the [USB HID specification](../../../docs/reference/usb-hid.md).
Identity, configuration or storage reports outside that contract are rejected as invalid.

## Connection and recovery

Serial-identified boards retain their monitor for at least 30 seconds after disappearing from a successful rescan,
allowing brief USB reconnects without losing selection. Active commands postpone retirement; uncertain outcomes keep
the monitor until confirmed re-sync or shutdown. Boards without serial numbers use their HID device path as identity.
If a HID write times out or its result cannot be matched, the dashboard marks the command outcome uncertain and blocks
further writes rather than replaying or overwriting the result. Persistent errors, invalid reports, and permission
failures remain errors.

Saved radio switches refresh after a settings save or reconnect. Tested saves commit the staged pair without
restaging it. See [wireless setup](../../../docs/guides/wireless.md) for provisioning.

Board monitoring starts asynchronously on first access, so the first status response can briefly show that it is
waiting for HID. The monitor polls every 100 ms.

## HTTP API

Python and .NET implement the [shared dashboard HTTP API](../../web/README.md#http-api).
Use the board ID returned by discovery when selecting a board for API requests.
Run with `ASPNETCORE_ENVIRONMENT=Development` to expose `/openapi/v1.json`.

## Development

Use [host development](../../../docs/development/host-tools.md) for tests, versions, and shared assets.
The host tests cover protocol and transport behavior; they do not validate physical USB detach/reconnect.
