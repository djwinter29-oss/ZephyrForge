# Host development

Python and .NET share a dashboard and HTTP API; each host owns the
board's USB HID interface and decodes the [USB HID reports](../reference/usb-hid.md).
The [Direct dashboard](../../host/direct/README.md) is independent: see
[Direct dashboard independence](#direct-dashboard-independence).

## Run software checks

Install test dependencies in a Python virtual environment. Commands run from the repository root:

```sh
python3 -m pip install --editable host/python -r host/python/tests/requirements.txt
python3 -m unittest discover -s host/python/tests -v
(cd host/python && pyright)
node host/web/tests/run-tests.mjs
dotnet test host/dotnet/ZephyrForge.Tests
python3 tools/check_docs.py
```

Python and .NET tests use fake HID devices and loopback HTTP; they do not open real hardware and are not firmware
or hardware tests. [Host Validation](../../.github/workflows/host-validation.yml) runs Python 3.10 and 3.12 on
Ubuntu, macOS and Windows. Node 22 and .NET 10 checks run once per OS, alongside Python 3.12. Ceedling and
documentation links are independent jobs in that reusable workflow.
On Ubuntu with Python 3.12 it also type-checks the Python package and verifies an installed wheel.
[Release packages](releases.md) additionally smoke-test the extracted Direct dashboard,
the installed Python wheel and the self-contained .NET dashboards without connected hardware.
There is no demo mode: without a board, the hosts show an empty board list and never invent values.

Keep report decoding, field names and error semantics aligned across the three clients. Adding telemetry requires
updates to the firmware report, all three decoders, the references and tests. Do not expose the hosts beyond
loopback, retry uncertain commands, or introduce silent timeouts/fallback data.

On Linux, run [hid-permissions-setup.sh](../../tools/host/hid-permissions-setup.sh) once to install the udev rule
for `CAFE:4021`. [run-python.sh](../../tools/host/run-python.sh) and [run-dotnet.sh](../../tools/host/run-dotnet.sh)
call [hid-permissions-check.sh](../../tools/host/hid-permissions-check.sh) before starting.

## Python host design

The Python host is an installable package with pinned dependencies and separate test tooling.
Run `zephyrforge`, `python -m zephyrforge`, or the checkout launch script.

| Layer | Responsibility |
|---|---|
| Package identity | Version from the installed distribution, with a development fallback |
| CLI | `status`, `web` and `reboot` commands with `--serial` / `--device-path` selection |
| Reports (`zephyrforge.reports`) | Pure USB HID report codecs and ZephyrForge-specific validation; see the [USB HID contract](../reference/usb-hid.md) |
| HID transport (`zephyrforge.hid`) | Device discovery, Feature/Input reads and Control writes |
| Dashboard (`zephyrforge.dashboard`) | Loopback Flask app, board discovery, one serialized monitor per board and the HTTP API |
| Tests | Domain-grouped suites and shared fixtures live under `tests/zephyrforge_tests/` |

Public packages import independently without opening a device. `zephyrforge.cli` provides both CLI entry points.
Package builds require an explicit copy of the shared dashboard assets; there is no custom build hook.
The staged copy is ignored by Git and included in both wheels and source distributions.
Source-checkout launches and editable installs need no staging and use the shared dashboard directly.
Python type-checking runs in strict mode across the complete runtime package and compile-only API contracts.
Runtime dependency pins live in `pyproject.toml`; pip handles installation
without a checked-in lockfile. Hatchling builds the package, and hatch-vcs derives its version from Git tags.
Editable installs use the same distribution metadata as wheels; reinstall after a commit or tag change to refresh it.
To stage the current assets and check a wheel locally on Linux/macOS, from the repository root:

```sh
rm -rf host/python/src/zephyrforge/web
mkdir -p host/python/src/zephyrforge/web
cp -R host/web/src/. host/python/src/zephyrforge/web/
python3 -m pip wheel --no-deps host/python --wheel-dir build/wheels
python3 -m venv build/wheel-check
build/wheel-check/bin/python -m pip install build/wheels/*.whl
build/wheel-check/bin/python -I tools/check_python_wheel.py
```

Repeat staging before every checkout package build so deleted or changed assets do not leave stale copies.
After packaging, remove `host/python/src/zephyrforge/web` to resume serving the shared checkout assets directly.
A source distribution built after staging already contains the assets and needs no external copy when building its wheel.

## .NET builds and versions

The .NET host uses MinVer with `vMAJOR.MINOR.PATCH` Git tags, patch increments and `dev` prerelease identifiers.
Builds need Git history and tags to calculate development versions. The dashboard displays the assembly's
informational version without an SDK-appended commit hash. After a commit, tag or version override change, use
`dotnet build host/dotnet/ZephyrForge/ZephyrForge.csproj --no-incremental` to refresh both the assembly version and
cached dependency metadata.
For an explicit package version, pass `-p:MinVerVersionOverride=1.2.3` to `dotnet publish`.

## Shared dashboard

Both hosts serve the same static assets: Python uses the checkout or installed package,
and .NET includes the assets during build and publication.

| Layer | Responsibility |
|---|---|
| Static assets | Page, icon and styles; no inline code |
| Composition | Wires modules to page events; owns no device workflow |
| Board session | Discovery, selection and stale-status fencing |
| Command recovery | Per-board acknowledgements, uncertain outcomes and explicit Re-sync |
| Settings workflow | Boot-settings draft, confirmation and plain or combined wireless save |
| Device actions | Reboot, USB drive handoff, benchmark and LED commands |
| API client | Board, status, settings, reboot, re-sync and wireless routes |
| Presentation | Rendering without device or network I/O |
| Operation policy | Control eligibility shared by rendering and command handlers |
| Wireless forms | Wi-Fi scan/test requests and dialogs, and the Control key dialog |

Unit tests cover individual responsibilities; integration tests exercise the assembled page and shared status
contract. Browser checks drive Chromium against both hosts with fake boards.
See the [shared dashboard HTTP API](../../host/web/README.md) for routes and request rules.

```sh
node host/web/tests/run-tests.mjs
npx --yes --package typescript@5.9.3 tsc --project host/web/tsconfig.json
python3 -m pip install -r host/web/tests/browser/requirements.txt
python3 -m playwright install chromium
python3 host/web/tests/browser/run.py
```

The browser runner also requires the editable Python host and .NET SDK installed above.
Chromium needs its system libraries and at least one font; `python3 -m playwright install-deps chromium`
installs them on Debian and Ubuntu.

## Direct dashboard independence

Direct owns its HTML, CSS, favicon, browser modules, and loopback static server. It does not use Web Serial.

- Direct must not import, link or copy at runtime from `host/web/`; `host/web/` must not reference Direct.
- Browser integration belongs at the application boundary. Protocol and state do not access the DOM or device APIs;
  lower layers receive browser dependencies by injection.
- Board modules update domain state through declared events; the connection module owns connection phases.
- Commands are journalled before writing. The single-flight queue rejects overlap rather than retaining stale
  intents. Opcode policy determines allowed transports.
- UI gating and use cases share eligibility decisions. Rendering performs no device I/O. Domain errors use reason
  codes; presentation supplies the wording. Form drafts and phases stay local to their controller.
- All browser modules are type-checked; core utilities and protocol codecs use full TypeScript strict mode.
  Board lifecycle, recovery, polling, transport and reducer modules also use full strict mode.
- Unit and architectural contract tests enforce the layer boundaries.

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

The runner owns an ephemeral loopback server and a fake WebHID board driving the production page. It fails on
incomplete checks, assertion/browser errors, or failed asset loads. Checks cover dialogs, focus/Escape, narrow
viewports, wireless cancellation, save/reconnect ownership, history navigation, and retained recovery journals.
Vendored axe-core 4.10.3 checks WCAG 2.1 A/AA and best-practice rules on the dashboard and dialogs; its MPL-2.0 and
third-party notices are in `host/direct/tests/browser/vendor/`. These checks do not exercise physical USB or BLE.

Host Validation runs both dashboards' Node tests and the Direct server tests on every OS, dashboard
TypeScript checks on Ubuntu, and both Chromium browser checks in a separate job.
