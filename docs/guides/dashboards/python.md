# Run the Python dashboard

[Documentation](../../README.md) / [Dashboards](README.md) / Python

The Python host connects to the board over USB HID and serves a local dashboard. It does not connect through
the USB-UART bridge or Bluetooth. For browser-native USB/BLE, use [Direct](direct.md).

## Install

Use Python 3.10 or newer in a virtual environment:

```sh
python -m venv .venv
```

Activate it with `source .venv/bin/activate` on Linux/macOS or `.venv\Scripts\Activate.ps1` in Windows PowerShell, then:

```sh
python -m pip install zephyrforge
zephyrforge web
```

Open **http://127.0.0.1:8001/**. The wheel includes the dashboard assets; Node.js and .NET are not required.
Runtime dependencies are Flask and HIDAPI, pinned in [pyproject.toml](../../../host/python/pyproject.toml).
Native HID access still requires the operating system's usual libraries, drivers, and permissions.
The dashboard displays the installed distribution version, matching `pip show zephyrforge`, for wheels and editable
installs. Uninstalled source checkouts report `0.0.0-debug`.

You can also install a wheel downloaded from
[GitHub Releases](https://github.com/djwinter29-oss/ZephyrForge/releases/latest).
Then [connect a board](README.md#connect-over-usb). On Linux, set up
[HID permissions](README.md#linux-usb-permissions) if needed.

### Run from a source checkout

From the repository root:

```sh
bash tools/host/run-python.sh web
```

The wrapper reuses its virtual environment, refreshes the editable installation and version metadata, and checks
HID permissions. It defaults to `web` when no command is given and does not launch with stale metadata if
installation fails. Stop an already-running host before relaunching.

## Commands

```sh
zephyrforge web
zephyrforge web --port 8011
zephyrforge status
zephyrforge status --serial SERIAL
zephyrforge reboot --serial SERIAL --confirm
python -m zephyrforge --help
```

`status` prints JSON diagnostics from the reports received within a five-second collection window; unavailable
fields remain `null`. It includes self-test status when the board advertises that capability, without extending
the collection window. `reboot` requires explicit confirmation and uses the board's saved settings.
All three commands accept either `--serial SERIAL` or `--device-path PATH` to select a board.
The server always binds to `127.0.0.1`.

For board selection, controls, and uncertain command outcomes, use the
[dashboard guide](README.md).

## Development

See the [HTTP API](../../reference/http-api.md) for client integration and
[host development](../../development/host-tools.md#python-host-checks-and-packaging) for editable installs,
tests, versions, and packaging.
