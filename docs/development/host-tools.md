# Host development

[Documentation](../README.md) / Development / Hosts and dashboards

Python and .NET connect to USB HID and serve the same dashboard and HTTP API. Direct is a separate browser
application using WebHID or Web Bluetooth.

## Choose your checks

Run the checks for the component you changed:

- [Python host](#python-host-checks-and-packaging) - unit tests, strict type checks, and package checks.
- [.NET host](#net-builds-and-versions) - protocol, monitor, and HTTP API tests.
- [Shared dashboard](#shared-dashboard) - Node tests, type checks, and browser checks against both hosts.
- [Direct](#direct-tests-and-type-checks) - Node/server tests and type checks, plus
  [browser checks](#direct-browser-checks).

Commands below run from the repository root. Use Python 3.10+ in a virtual environment, Node 22 for JavaScript
checks, and the .NET 10 SDK for .NET and shared-dashboard browser checks.

Tests use simulated devices and loopback HTTP, not real hardware. CI runs them through
[Host Validation](../../.github/workflows/host-validation.yml); [release checks](releases.md) also exercise extracted
packages.

Keep report decoding, field names, and error semantics aligned across all three clients and the
[USB HID contract](../reference/usb-hid.md). A protocol change needs matching firmware, host, specification, and
test changes. Keep hosts on loopback, report failures clearly, and never replay an uncertain command.
Keep device access and protocol validation separate from rendering; rendering performs no I/O, and action
eligibility must agree between controls and command handlers.

## Python host checks and packaging

Install the package and test dependencies, then run unit and type checks:

```sh
python3 -m pip install --editable host/python -r host/python/tests/requirements.txt
python3 -m unittest discover -s host/python/tests -v
(cd host/python && pyright)
```

For running against a board, use the [Python host guide](../guides/dashboards/python.md).

The installed package can be imported without opening a device. Source-checkout launches and editable installs use
the shared dashboard directly. Wheel and source-distribution builds need an explicit copy of those assets; there is
no custom build hook. Python type checks run in strict mode across the runtime package and compile-only API
contracts. Dependencies are declared in `pyproject.toml`, and hatch-vcs derives package versions from Git tags.
Reinstall a manual editable installation after changing commits or tags to refresh its installed version metadata;
the [checkout launch wrapper](../guides/dashboards/python.md#run-from-a-source-checkout) does this on every launch.

For release wheels, use the [same build and check helpers as CI](releases.md#reproduce-the-python-release-wheel).
For a source distribution, copy the contents of `host/web/src/` into
`host/python/src/zephyrforge/web/` before building, starting from a fresh checkout to avoid stale assets.
The source distribution includes those assets for downstream wheel builds.

## .NET builds and versions

```sh
dotnet test host/dotnet/ZephyrForge.Tests
```

For running against a board, use the [.NET host guide](../guides/dashboards/dotnet.md).

The .NET host uses MinVer with `vMAJOR.MINOR.PATCH` Git tags, patch increments and `dev` prerelease identifiers.
Builds need Git history and tags to calculate development versions. The dashboard displays the assembly's
informational version without an SDK-appended commit hash. After a commit, tag or version override change, use
`dotnet build host/dotnet/ZephyrForge/ZephyrForge.csproj --no-incremental` to refresh both the assembly version and
cached dependency metadata.
For an explicit package version, pass `-p:MinVerVersionOverride=1.2.3` to `dotnet publish`.
Run the host with `ASPNETCORE_ENVIRONMENT=Development` to expose `/openapi/v1.json`.

## Shared dashboard

Both hosts serve the same static assets: Python uses the checkout or installed package,
and .NET includes the assets during build and publication.

The frontend uses ES modules without a bundle step or inline code. Unit tests cover individual workflows;
integration tests exercise the assembled page and shared status contract. Browser checks drive Chromium against
both hosts with fake boards.
See the [shared dashboard HTTP API](../reference/http-api.md) for routes and request rules.

```sh
node host/web/tests/run-tests.mjs
npx --yes --package typescript@5.9.3 tsc --project host/web/tsconfig.json
python3 -m pip install -r host/web/tests/browser/requirements.txt
python3 -m playwright install chromium
python3 host/web/tests/browser/run.py
```

The browser runner also requires the editable Python host installation above and the .NET SDK.
Chromium needs its system libraries and at least one font; `python3 -m playwright install-deps chromium`
installs them on Debian and Ubuntu.

## Direct integration

Direct is a separate browser application; it does not load or share runtime assets with `host/web/`. It connects to
devices through WebHID or Web Bluetooth. Its tests check browser behavior and those architectural boundaries;
type checks cover all browser modules.

### Direct versions and local serving

The loopback server sends same-origin-only WebHID/Web Bluetooth permissions and isolates top-level browsing
contexts. These headers do not encrypt traffic or change device permissions. Startup does not require reverse DNS.

Checkout launches serve a temporary, versioned asset snapshot. Tags use `vMAJOR.MINOR.PATCH`; the next commit after
`v1.2.3` displays `1.2.4-dev.1`, with a short commit ID and `.dirty` when applicable. Without a release tag the
version is `0.0.0-dev.COUNT`; unavailable Git identity logs a warning and uses `0.0.0-debug`.
Restart after source or Git changes.

Release ZIPs and Pages retain their stamped version regardless of the surrounding checkout. `build.json` records
version, commit, and dirty state. Inconsistent page/build identity is rejected, not restamped. A custom
`--directory` without metadata is served unchanged.

### Direct protocol behavior

Direct validates the [USB HID](../reference/usb-hid.md) and
[BLE](../reference/bluetooth-le.md) contracts before enabling controls. After reconnecting, it verifies the same
board and reads outstanding results without resending commands.

For Service Changed compatibility, Direct attempts subscription before discovery. If the browser reports
`NotAllowedError`, `NotFoundError`, `NotSupportedError`, or `SecurityError`, it warns and continues with browser
cache behavior; other subscription errors abort setup. It still rediscovers the service and does not reuse cached
handles across connections.

BLE reads and writes are serialized, including each proof with its command. Direct waits 250 ms between status-read
rounds, in addition to read latency. Telemetry becomes stale after 3 seconds without a valid receipt; storage and
benchmark status after 6 seconds. Wireless status expires after three polling intervals: 1.5 seconds during a
test/scan and 6 seconds otherwise. Invalid reports do not refresh receipt times; disconnect clears snapshots. The
browser chooses the negotiated MTU. Commands whose queued write deadline expires are canceled rather than sent
later.

### Direct tests and type checks

```sh
node host/direct/tests/run-tests.mjs
python3 -m unittest host.direct.tests.test_server -v
npx --yes --package typescript@5.9.3 tsc --project host/direct/tests/types/tsconfig.json
npx --yes --package typescript@5.9.3 tsc --project host/direct/tests/types/tsconfig.core-strict.json
npx --yes --package typescript@5.9.3 tsc --project host/direct/tests/types/tsconfig.protocol-strict.json
npx --yes --package typescript@5.9.3 tsc --project host/direct/tests/types/tsconfig.board-runtime-strict.json
```

For command-lifecycle failures, run `node --test host/direct/tests/board/commands.test.mjs`.
Static-only negative type contracts belong under `tests/types/` and are not Node tests. Other checks cover unit
behavior, architecture boundaries, composition, and browser interaction, using Direct-only fixtures.

The JavaScript runner requires aggregate coverage of 90% lines, 80% branches, and 85% functions, with per-file
floors of 80% lines, 55% branches, and 50% functions. LCOV output supplies per-file metrics.

### Direct browser checks

```sh
python3 -m pip install -r host/direct/tests/browser/requirements.txt
python3 -m playwright install chromium
python3 host/direct/tests/browser/run.py
```

The runner starts an ephemeral loopback server and a simulated WebHID board for the production page. It fails on
incomplete checks, assertion/browser errors, or failed asset loads. Checks cover dialogs, focus/Escape, narrow
viewports, wireless cancellation, save/reconnect ownership, history navigation, and retained recovery journals.
Vendored axe-core 4.10.3 checks WCAG 2.1 A/AA and best-practice rules on the dashboard and dialogs; its MPL-2.0 and
third-party notices are in `host/direct/tests/browser/vendor/`. These checks do not exercise physical USB or BLE.
