# Run the .NET dashboard

[Documentation](../../README.md) / [Dashboards](README.md) / .NET

The .NET 10 host monitors the ZephyrForge vendor HID interface (`0xCAFE:0x4021`) and serves the shared local dashboard
at `http://127.0.0.1:8002/` by default. Override the port with `--port PORT`; only one dashboard server can bind to a
port at a time.

## Run the host

For a packaged host, extract the archive for your OS from
[GitHub Releases](https://github.com/djwinter29-oss/ZephyrForge/releases/latest) and run its executable. These
packages include the runtime. Windows/macOS packages are not signed or notarized and may require OS approval.

For a source checkout, install the .NET 10 SDK and run from the repository root:

```sh
dotnet run --project host/dotnet/ZephyrForge/ZephyrForge.csproj
```

Alternatively, run `bash tools/host/run-dotnet.sh` from the repository root; the wrapper also checks HID permissions.
Open **http://127.0.0.1:8002/**, then [connect a board](../dashboard-use.md#connect-over-usb).
On Linux, set up [HID permissions](../dashboard-use.md#linux-usb-permissions) if needed.

## Select a board

Select a default board at launch with `--serial SERIAL` (the 16 hex-digit Report 1 UID) or `--device-path HID_PATH`.
The options are mutually exclusive; without either, the first discovered board is selected.
When using `dotnet run`, put application options after `--`:

```sh
dotnet run --project host/dotnet/ZephyrForge/ZephyrForge.csproj -- --serial SERIAL
```

For board switching, controls, and uncertain command outcomes, use the [dashboard guide](../dashboard-use.md).

## Development

See the [HTTP API](../../reference/http-api.md) for client integration and
[host development](../../development/host-tools.md#net-builds-and-versions) for tests, versions, and OpenAPI.
