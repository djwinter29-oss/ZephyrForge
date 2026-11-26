# Dashboard HTTP API

[Documentation](../README.md) / Reference / HTTP API

The Python and .NET hosts implement this contract. Direct connects to devices without this management API.
For dashboard operation, use the [dashboard guide](../guides/dashboards/README.md); for checks and asset packaging,
use [host development](../development/host-tools.md).

- [Routes](#routes) and [status fields](#status-fields)
- [Settings](#settings) and [wireless](#wireless)
- [Errors and recovery](#errors-and-recovery)
- [USB storage](#usb-storage), [benchmark](#storage-benchmark), and [RGB LED](#rgb-led-control)
- [Browser shell](#browser-shell)

## Routes

Both hosts serve the same routes and JSON field names, bind to loopback, and require local request origins.
Board-scoped routes take `?board=ID` from `/api/boards`. Commands are serialized per board; an uncertain result
blocks further writes until [explicit reconciliation](#errors-and-recovery).

| Route | Purpose |
| --- | --- |
| `GET /`, `/favicon.svg`, `/css/<path>`, `/js/<path>` | Static dashboard assets |
| `GET /console?board=ID` | Browser terminal for the selected board's raw TCP shell |
| `POST /api/console?board=ID` | Open a console using fresh reported Wi-Fi status |
| `GET`, `DELETE /api/console` | Read and close a console session |
| `POST /api/console/input` | Send one non-idempotent shell input packet |
| `GET /api/boards` | Discover boards and obtain IDs for board-scoped requests |
| `GET /api/status` | Identity, telemetry, storage, boot configuration and wireless status |
| `POST /api/settings` | Save clock and radio switches, then reboot |
| `POST /api/reboot` | Confirmed reboot, with `{"confirm":true}` |
| `POST /api/led` | Volatile onboard RGB color |
| `POST /api/msc` | Explicitly enable USB storage or return it after confirmed OS eject |
| `POST /api/benchmark` | Explicitly start or cancel the temporary-file storage benchmark |
| `POST /api/control/resync` | Explicitly reconcile an uncertain command outcome |
| `POST /api/wireless` | Save Wi-Fi credentials, Control key and radio switches |
| `POST /api/wireless/scan` | Scan for Wi-Fi networks |
| `POST /api/wireless/test` | Test candidate credentials |
| `POST /api/wireless/cancel` | Cancel a test and clear temporary credentials |

## Discover boards and read status

`GET /api/boards` returns `{"boards":[...],"default_board_id":...}`. Each board has an opaque `id`,
`product_name`, nullable `serial_number`, and host-specific `device_path`. `default_board_id` is the preferred
board ID or `null` when no default is available. The response may include `warnings` when duplicate identities
prevent safe selection. Use a listed ID as the `board` query parameter; do not construct one from the serial or path.

```json
{
  "boards":[
    {
      "id":"<opaque-board-id>",
      "product_name":"ZephyrForge",
      "serial_number":"<board-serial>",
      "device_path":"<host-device-path>"
    }
  ],
  "default_board_id":"<opaque-board-id>"
}
```

`GET /api/status?board=ID` returns a status snapshot. Without `board`, it returns a disconnected snapshot. The
top-level fields are always present:

| Fields | Meaning |
| --- | --- |
| `connected`, `reconnecting`, `error` | Board connection state and its current error, if any |
| `hardware`, `board_configuration` | Device identity/capabilities and reported board configuration |
| `runtime`, `memory`, `self_test` | Telemetry, storage status, and benchmark status |
| `settings_supported`, `settings_message` | Whether boot settings were read and any read issue |
| `control_result`, `command_outcome_uncertain`, `command_resync_supported`, `reboot_pending` | Last command result and recovery state |
| `telemetry_stale`, `storage_stale`, `self_test_stale` | Freshness of the corresponding report group |
| `updated_at` | Time of the last accepted input report, or `null` |
| `wireless_phase`, `wireless`, `wireless_error`, `wireless_cleanup_required` | Wireless operation and status, where supported |

Select a listed board, then poll its status:

```http
GET /api/status?board=<listed-board-id>
```

For example, this excerpt shows fresh telemetry while storage remains unavailable. Other top-level fields are omitted:

```json
{
  "connected":true,
  "runtime":{"uptime_ms":12345},
  "telemetry_stale":false,
  "memory":null,
  "storage_stale":true
}
```

Unavailable report objects and values are `null`; nullable fields such as `error` and `settings_message` may also be
`null`. Stale report groups are withheld as `null` and indicated by their matching stale flag. Disconnected snapshots
set connection state to false, report groups to `null`, and report groups stale. A non-null wireless object may
include `stale:true` when its data is old. These rules distinguish unavailable, stale, and measured zero values.

The HTTP response uses its own JSON names; it does not expose the USB report field names. Commonly consumed fields
and their JSON types are:

| Object | Field | Type and meaning |
| --- | --- | --- |
| `hardware` | `product_name`, `mcu`, `board_model` | string |
| `hardware` | `serial_number` | string or `null` |
| `hardware` | `capabilities`, `ram_total_bytes`, `ram_reserved_bytes`, `psram_total_bytes`, `psram_reserved_bytes`, `flash_total_bytes` | integer; optional memory inventory values may be `null` |
| `board_configuration` | `system_clock_mhz`, `sdcard_profile`, `radios`, `monitor_uart_index`, `monitor_uart_tx_gpio`, `monitor_uart_rx_gpio`, `led_gpio`, `card_detect_gpio` | integer |
| `board_configuration` | `storage_gpios` | array of integers |
| `board_configuration` | `tcp_cli_enabled` | boolean |
| `runtime` | `uptime_ms`, `system_clock_hz`, `ram_used_bytes`, `ram_total_bytes` | integer; RAM values may be `null` |
| `runtime` | `temperature_centidegrees` | integer or `null`; hundredths of a degree Celsius |
| `runtime` | `cpu_usage_core0_basis_points`, `cpu_usage_core1_basis_points` | integer or `null`; 100 basis points equal 1% |
| `runtime` | `temperature_valid`, `usb_mounted`, `cdc_open`, `led_available`, `led_on` | boolean |
| `memory` | `present` | boolean or `null` when card presence is unknown |
| `memory` | `capacity_bytes`, `block_count`, `bus_clock_hz` | integer or `null` when unavailable |
| `memory` | `mode`, `speed_mode`, `filesystem_format` | string; `mode` and `speed_mode` may be `null` |
| `memory` | `msc` | object with boolean `enabled`, `transition_pending`, and `request_rejected` fields |
| `self_test` | `state`, `active`, `progress_percent`, `target_bytes`, `written_bytes`, `read_bytes` | string, boolean, number, and integers respectively |
| `self_test` | `write_bytes_per_second`, `read_bytes_per_second` | number or `null` when elapsed time is zero |
| `wireless` | `wifi_state`, `ipv4`, `ssid`, `ble_state`, `phase` | string |
| `wireless` | `wifi_enabled`, `ble_enabled`, `stale`, `network_configured` | boolean |

Each object in this table is itself nullable when unavailable or stale. Other fields, including identity,
configuration, diagnostic counters, and operation details, are also returned as described by their names and the
focused [USB HID contract](usb-hid.md) for wire interpretation. HTTP-specific decoded names and derived values above
are authoritative for this API: for example, the wire's `core0_service_load_basis_points` becomes
`runtime.cpu_usage_core0_basis_points`, and storage capacity is exposed as `memory.capacity_bytes`.

## Status fields

`/api/status` separates internal SRAM, external PSRAM, and module SPI flash. It adds
`hardware.psram_total_bytes`, `psram_reserved_bytes` and `flash_total_bytes`, in bytes, when firmware advertises
[memory inventory](usb-hid.md#report-id-13-module-memory-inventory). Unsupported or unavailable values are `null` or
absent. RAM reservation is linked accounting, including heap pools, not live usage; flash capacity is not free space.

Unavailable load, temperature, card presence, bus clock, or capacity remains `null`, not zero. Diagnostic values
must preserve unknown and invalid states rather than claim a successful measurement. The
[dashboard guide](../guides/dashboards/README.md#read-the-dashboard) defines their presentation.

## Settings

`POST /api/settings` requires integer `system_clock_mhz` (`0`, `80`, `160`, or `240`; C6 accepts at most `160`),
`sdcard_profile` (`0`), and `radios` (`0`-`3`; bit 0 Wi-Fi, bit 1 BLE). `tcp_cli_enabled:false` is accepted for
compatibility; `true` is rejected. `confirm_data_loss` is optional and defaults to `false`; `true` is refused with
HTTP 409 because forced saves are unavailable.

```json
{"system_clock_mhz":240,"sdcard_profile":0,"radios":3}
```

Success returns `{"settings":{"system_clock_mhz":240,"sdcard_profile":0,"radios":3}}`; the compatibility field
`tcp_cli_enabled` may also be present. The firmware schedules a reboot after verifying the save.

## Wireless

`POST /api/wireless` combines settings and credential staging into one save. It requires `confirm_reboot:true`,
boolean `wifi_enabled` and `ble_enabled`, and a `board` object with the three required [settings fields](#settings). The
booleans must match the corresponding bits in `board.radios`. Credentials are optional: send `ssid` or `ssid_hex`,
`password`, and/or `ble_key` only for values to replace or clear. Omitted credentials are preserved by the endpoint;
send both `ssid:""` and `password:""` to clear saved Wi-Fi credentials, or `ble_key:""` to clear the Control key.
The dashboard applies a separate [credential-deletion policy](../guides/wireless.md#save-settings).
`tcp_cli_enabled:false` is optional.

An SSID is at most 32 UTF-8 bytes; `ssid_hex` carries up to 32 bytes as hexadecimal. A password is empty or 8-63
printable ASCII characters. A Control key is empty or 64 hexadecimal characters.

```json
{
  "confirm_reboot":true,
  "wifi_enabled":true,
  "ble_enabled":true,
  "board":{"system_clock_mhz":240,"sdcard_profile":0,"radios":3},
  "ssid":"Example network",
  "password":"<passphrase>",
  "ble_key":"<64 hexadecimal digits>"
}
```

To save a successfully tested Wi-Fi pair, send `use_tested_wifi:true`, its 32-character lowercase hexadecimal
`test_id` and `test_generation` (1-255) from the test response, and do not send `password`. The test must still be
current; its optional `ssid`/`ssid_hex` must match the tested network. A successful response contains `settings`,
`reboot_pending:true`, and current `wireless` status; saved passwords and Control keys are never returned.

`POST /api/wireless/test` accepts `{"ssid":"Example network","password":"<passphrase>"}` or an opaque SSID using
`ssid_hex` instead of `ssid`. The password may be omitted for an open network. Success returns a `test` object with
`ipv4`, `detail`, `phase`, `test_generation`, `remaining_seconds`, and `test_id`.
`POST /api/wireless/scan` needs no body and returns `{"networks":[...]}`; each network includes `ssid`, `ssid_hex`,
`rssi`, `secured`, and `security`. `POST /api/wireless/cancel` needs no body and returns
`{"cancelled":true}`.

## Errors and recovery

Errors use a JSON object with an `error` message. Python may also include an `uncertain` boolean. Invalid JSON or
fields return 400; a foreign Origin on an API write returns 403; an unknown board returns 404; board-state conflicts
and firmware refusals return 409; request bodies over 4096 bytes return 413; device communication failures return
503. Server failures return 500. A failed or timed-out state-changing request can have an uncertain outcome even
when the response does not include that flag: reconcile with `/api/control/resync` and do not replay the write.

`POST /api/control/resync` requires `{"confirm":true}`. It returns `uncertain`, the reconciled `control_result`
when available, and current `wireless` status when available. Re-sync inspects device state; it does not replay the
command.

## USB storage

| Item | Contract |
| --- | --- |
| Export | `POST /api/msc` with `{"enabled":true}` |
| Return | `POST /api/msc` with `{"enabled":false,"confirm_ejected":true}` |
| Validation | `enabled` is required and boolean; optional `confirm_ejected` is boolean and defaults to `false`. Extra fields, invalid types, or an unconfirmed return are HTTP 400. |
| Response | HTTP 200 with `{"accepted":true}` means admitted, not completed. Poll `/api/status` fields `memory.msc.transition_pending`, `enabled`, and `request_rejected`. |
| Errors and recovery | Unavailable, stale, or busy ownership is HTTP 409; transport failure is HTTP 503 and may leave an uncertain outcome. Do not replay. |

See the [SD handoff contract](sd-storage.md#usb-drive-handoff) for OS eject, reconnect and safety limits.

## Storage benchmark

| Item | Contract |
| --- | --- |
| Start | `POST /api/benchmark` with `{"start":true,"size_mib":32,"confirm":true}`; size is 1–1024 MiB and write confirmation is required. |
| Cancel | `{"start":false,"size_mib":0}`; cancellation requires zero size. |
| Validation | Fields are strictly typed; extra fields or invalid values are HTTP 400. |
| Response | HTTP 200 with `{"accepted":true}` means admitted, not completed. Poll `self_test` and `self_test_stale` for [Report 11](usb-hid.md#report-id-11-storage-benchmark-status). |
| Errors and limits | Unavailable, stale, or busy admission is HTTP 409; transport errors are HTTP 503 and may need explicit resync. `self_test` is null when absent or stale; zero-duration throughput is null. Controls require capability bit 5 and detected storage; BLE is read-only. |

Do not replay an uncertain start or cancel request.

## RGB LED control

| Item | Contract |
| --- | --- |
| Request | `POST /api/led` accepts only `{"colour":0..7}`; the integer mask is red=1, green=2, blue=4, zero=Off. |
| Admission | Requires LED capability and fresh telemetry showing an available driver; storage activity does not block it. |
| Response | `{"accepted":true}` means queued transmission, not physical verification or persistence. |

## Browser shell

For the operator workflow, see [Diagnostic Shells](../guides/console.md#in-browser-terminal). The relay
does not add authentication; see [security boundaries](security.md#management-links).

| Route | Request / response | Limits and behavior |
| --- | --- | --- |
| `POST /api/console?board=ID` | Accepts `{}`; returns `{"session":"TOKEN","ipv4":"ADDRESS"}` | Host uses the selected board's current address, clears unfinished input from a previous client, then enables shell echo. Token is sent only in `X-Console-Session`, never in a URL or persistent storage. |
| `GET /api/console` | Returns `{"data":"BASE64","closed":false}` or `{"data":"BASE64","closed":true}` | Long-polls for up to 2 seconds and returns at most 4096 decoded bytes. `closed:true` means TCP EOF. |
| `POST /api/console/input` | Accepts `{"data":"BASE64"}`; returns `{"written":N}` | Decoded input is 1–1024 bytes. Short or failed writes are uncertain; close the session and do not resend. |
| `DELETE /api/console` | Returns `{}` | Closes the session; HTTP 409 means it is already unavailable. |

Only one console is admitted per host. Sessions expire after 15 seconds without requests and close on host shutdown.
Subsequent requests require `X-Console-Session` and fail if the board changes, disconnects, loses fresh Wi-Fi status,
or changes address.
Invalid bodies return HTTP 400, conflicts 409, TCP failures 503, and bodies beyond the existing limit 413.
