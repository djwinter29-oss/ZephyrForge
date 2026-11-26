# Shared dashboard HTTP API

The [Python](../python/README.md) and [.NET](../dotnet/ZephyrForge/README.md) hosts serve this frontend and implement
the same HTTP contract. The browser assets use ES modules without a build step. Direct does not share them.
For development checks and package asset handling, see [host development](../../docs/development/host-tools.md).

## HTTP API

Both hosts must serve the same ZephyrForge routes and JSON field names.
Board-scoped routes take `?board=ID` from `/api/boards`.

`/api/status` separates internal SRAM, external PSRAM, and module SPI flash. It adds
`hardware.psram_total_bytes`, `psram_reserved_bytes` and `flash_total_bytes`, in bytes, when the firmware advertises
[memory inventory](../../docs/reference/usb-hid.md#report-id-13-module-memory-inventory).
Unsupported or unavailable values are `null` or absent and display `--`. RAM reservation is linked accounting,
including heap pools, not live usage; flash capacity is not free space.

| Route | Purpose |
| --- | --- |
| `GET /`, `/favicon.svg`, `/css/<path>`, `/js/<path>` | Static dashboard assets |
| `GET /console?board=ID` | Browser terminal for the selected board's raw TCP shell |
| `POST /api/console?board=ID` | Open a console using fresh reported Wi-Fi status |
| `GET`, `DELETE /api/console` | Read and close a console session |
| `POST /api/console/input` | Send one non-idempotent shell input packet |
| `GET /api/boards` | Discover boards and obtain IDs for board-scoped requests |
| `GET /api/status` | Identity, telemetry, storage, boot configuration and wireless status |
| `POST /api/settings` | Save clock and radio switches, then reboot; `system_clock_mhz` is `0`, `80`, `160` or `240`, `sdcard_profile` is `0`, `radios` is `0`-`3` |
| `POST /api/reboot` | Confirmed reboot, with `{"confirm":true}` |
| `POST /api/led` | Volatile onboard RGB color |
| `POST /api/msc` | Explicitly enable USB storage or return it after confirmed OS eject |
| `POST /api/benchmark` | Explicitly start or cancel the temporary-file storage benchmark |
| `POST /api/control/resync` | Explicitly reconcile an uncertain command outcome |
| `POST /api/wireless` | Save Wi-Fi credentials, Control key and radio switches |
| `POST /api/wireless/scan` | Scan for Wi-Fi networks |
| `POST /api/wireless/test` | Test candidate credentials |
| `POST /api/wireless/cancel` | Cancel a test and clear temporary credentials |

### Settings and wireless

Settings requests with `confirm_data_loss:true` are refused with HTTP 409 and the same error body on both hosts.
Successful settings saves return the confirmed values as
`{"settings":{"system_clock_mhz":240,"sdcard_profile":0,"radios":0}}` on both hosts.
The `tcp_cli_enabled` compatibility field may be omitted or set to `false`; `true` is rejected. Omitted credentials
are not restaged. Opaque SSIDs use `ssid_hex` alongside an escaped display label; test/save requests may use the hex
form to preserve network bytes.

### USB storage

MSC requests use `{"enabled":true}` to export or `{"enabled":false,"confirm_ejected":true}` to return.
`enabled` is required and boolean; optional `confirm_ejected` is boolean and defaults to false. Extra fields,
nonboolean values and unconfirmed return are HTTP 400. Unavailable/stale/busy ownership is HTTP 409; transport
failure is HTTP 503 and may leave an uncertain outcome. HTTP 200 acknowledges admission, not completed handoff:
both hosts return `{"accepted":true}`. Poll `/api/status` for `memory.msc.transition_pending`, `enabled` and
`request_rejected`; do not automatically replay. See the
[SD handoff contract](../../docs/reference/sdcard.md#usb-drive-handoff) for OS eject, reconnect and safety limitations.

### Storage benchmark

`POST /api/benchmark` accepts `{"start":true,"size_mib":32,"confirm":true}` or
`{"start":false,"size_mib":0}`. Start requires 1–1024 MiB and explicit write confirmation.
Fields are strictly typed, extras are rejected (HTTP 400), and cancel requires zero size.
Unavailable/stale/busy admission is HTTP 409; transport errors are HTTP 503 and may require explicit resync.
HTTP 200 acknowledges admission, not a passing run. Both hosts return `{"accepted":true}`; this does not indicate
benchmark completion.
Poll `self_test` and `self_test_stale` for
[Report 11](../../docs/reference/usb-hid.md#report-id-11-storage-benchmark-status) progress/result.
`self_test` is null when absent or stale; zero-duration throughput is null.
Controls remain hidden without capability bit 5 or detected storage; BLE is read-only.
Never automatically replay a start/cancel request.

### Status fields

Unavailable per-core load, temperature, card presence, bus clock and capacity diagnostics use JSON `null`.
The dashboard displays unavailable measurements as `--` without a footer warning; malformed diagnostic values
still produce a warning. Unknown card presence is not treated as a confirmed missing card. Storage becomes visible
after a fresh report detects a card or confirms successful initialization, and remains visible if initialization
fails or USB owns the card. Busy and stale reports retain the last known visibility; disconnect clears detection.
Service load shows **Core 0 / Core 1**, for example `3.1% / --` when Core 1 is unavailable.

## RGB LED control

`POST /api/led` accepts only `{"colour":0..7}` with an integer additive RGB mask: red=1, green=2, blue=4,
zero=Off. Capability and fresh available-driver telemetry are required; storage activity does not block it.
Success returns `{"accepted":true}` and means queued transmission, not physical verification or persistence. The
shared dropdown beside Device information displays the reported color and uses Re-sync after an uncertain outcome.

## Browser shell

For the operator workflow and network limits, see [Diagnostic Shells](../../docs/guides/console.md#in-browser-terminal).

`POST /api/console?board=ID` accepts exactly `{}`. The host obtains the address from that board's monitor, sends
two Ctrl-C bytes followed by `shell echo on\r` in one send, and returns `{"session":"TOKEN","ipv4":"ADDRESS"}`.
The controls discard a previous client's unfinished input before requesting native per-session echo.
This setup is not authentication or a product setting. Session tokens are sent only in the `X-Console-Session`
header, never URLs or persistent storage. Subsequent requests are rejected if the board changes, disconnects,
loses fresh wireless status or changes address.

`GET /api/console` long-polls up to two seconds and returns `{"data":"BASE64","closed":false}`, at most 4096 decoded
bytes; `closed:true` means TCP EOF. `POST /api/console/input` accepts exactly `{"data":"BASE64"}`, containing
1–1024 decoded bytes,
and returns `{"written":N}`. A short count or failed request is uncertain: close and never resend the input.
Input deliberately uses POST rather than an idempotent HTTP method that a client could automatically replay.
`DELETE` closes the session and returns `{}`; HTTP 409 during cleanup means it is already unavailable.
Only one console is admitted per host. Requests have bounded waits; sessions expire after 15 seconds without
requests and close during host shutdown. Invalid bodies are HTTP 400, conflicts HTTP 409, TCP failures HTTP 503;
the existing overall body limit still returns HTTP 413.

## Dashboard behavior

### Recovery and confirmations

Commands are serialized per board. A timeout or disconnect can leave the outcome uncertain; further writes
remain blocked until explicit reconciliation. Clients must not replay a state-changing request. Both hosts remain
loopback-only and require local request origins.

After a reload, the dashboard retains an unresolved command and its recovery action. It warns before discarding
unsaved settings or wireless edits, including untested Wi-Fi credentials. Save confirmation identifies the board
and summarizes boot/radio changes and whether credentials are unchanged, replaced, or erased; credentials are
never displayed. A Wi-Fi replacement names the tested network. Failed status polls remain visible in open wireless
dialogs, and the dashboard shows the age of the last successful status fetch.

Reboot confirmation expires on disconnect and identifies the selected board. After a confirmed reboot, writes
remain blocked until the host observes a disconnect/reconnect or a reboot-pending transition followed by clear.
A fresh lower uptime also confirms a restart if the tab missed the disconnect. If controls remain blocked after
reconnection, reload the dashboard to reconcile with the host.
