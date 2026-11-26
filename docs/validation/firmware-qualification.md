# Firmware qualification procedure

[Documentation](../README.md) / [Validation](README.md) / Firmware qualification

- **Check installed firmware:** start with the [USB runner](#runnable-usb-target-checks) for read-only diagnostics.
- **Qualify an ESP32-S3-DevKitC-1 N16R8 image:** follow the [test stages](#test-stages), including the software gate
  and manual matrices. Add results to the dated [validation records](README.md#validation-records).

C6 and Pico do not yet have complete board-specific procedures. Their images need separate test plans; see
[adapting checks](#adapting-checks-to-other-boards).

## Test stages

| Stage | What it covers | Record |
| --- | --- | --- |
| Software gate | Portable tests, tooling checks, and S3 storage builds | Command output and generated configuration |
| Runner safety tests | Validate the target runner without connecting to a board | Test output |
| Read-only USB checks | Reports, telemetry, and HID handle reopens | `firmware-hid-check.json` |
| Wi-Fi scans | Repeated scans and management responsiveness | `firmware-wifi-check.json` |
| Storage benchmark | Card writes, verification, cleanup, and USB responsiveness | A distinct JSON file for each size |
| Manual matrices | SD card, MSC, reconnects, and wireless lifecycle | New dated validation record |

Time depends on the selected checks and repetitions. Record the actual duration of each run.

## Agree on the test actions

For a shared bench, agree with the board owner on each state-changing or physical action before starting. Confirm
the board, ports, card, network, and test interval; agreement on one action does not cover the others.

| Action | Before you start |
| --- | --- |
| Board access | Reserve the exact board, USB and UART ports, and test interval; stop other clients and automated jobs |
| Flashing, rebooting, or saving settings | Confirm before replacing firmware, restarting the board, or changing persistent settings |
| Credential or network changes | Confirm before scanning, using the network, or changing credentials, AP state, or DHCP lease behavior |
| Card writes or MSC handoff | Use the dedicated expendable card; confirm before writing or transferring ownership to the host |
| Fault injection or power interruption | Agree separately before removing or power-cycling the card or board during a test |
| Physical USB reconnect | Agree before disconnecting and reconnecting the board's USB data cable |

## Ownership and safety prerequisites

- Use only the board and ports selected for this run. Never select an arbitrary connected board.
- Use the N16R8 image and [wiring](../hardware/sd-card-wiring.md#esp32-s3-devkitc-1-n16r8) matching the selected
  storage transport.
  Preserve 16 MB flash, 8 MB octal PSRAM, native USB pins and the native image's 10 mA SD pad drive.
- Use a backed-up, dedicated card containing only expendable test data. Prepare FAT16, FAT32 or exFAT on a computer;
  no check may format or repair a card from firmware. Before changing buses, follow the
  [card power-cycle instruction](../reference/sd-storage.md#safety-and-ownership).
- Provision any required Wi-Fi network and BLE Control key through the existing dashboards. Keep credentials out
  of logs, screenshots, command lines, and result records. Control-key authentication is not BLE link encryption.

## Prepare the run record

Record enough information to reproduce and attribute the result:

- **Image:** source revision and local changes, firmware SHA-256, frozen west manifest, SDK, generated `.config`
  and DTS.
- **Hardware:** board and module revision, wiring, power arrangement, card make and model, CID, capacity, and
  filesystem.
- **Host:** OS, USB topology, browser and client versions, UART logs, and host USB logs.
- **Run:** selected checks, thresholds, counts, and duration.

Retain each image's identity with its results. A configured SD clock ceiling is not a measured bus clock or throughput.

Record attempted, passed, failed, and inconclusive counts, the first failure, recovery steps, and whether a file
remains.

## Stop conditions

Stop the current test and preserve logs if the board resets unexpectedly, the image or card differs from the record,
a command's outcome is uncertain, the board locks up, or data does not match. Do not retry an uncertain
state-changing command or conceal a failed run with retries. Reconcile the board and card before any recovery.

## Software gate

For qualification, complete these checks before testing the board:

1. Run the [portable C suite](../development/firmware.md#host-native-unit-tests) and the
   [SPI and no-storage report and control tests](../development/firmware.md#storage-feature-gates).
2. Build ESP32-S3 SDMMC and SPI images using the
   [build wrapper](../development/firmware-build.md#build-with-the-wrapper).
3. Run the [firmware tooling tests](../development/firmware.md#build-and-tooling-checks).

Inspect generated configuration: correct PROCPU target, 16 MB flash, 8 MB octal PSRAM, selected storage driver,
four-bit native bus, and both FAT mkfs options disabled. Builds and portable mocks are not physical qualification.

### Runner unit tests

Before an on-board qualification run, check the runner itself from the repository root:

```sh
python3 -m unittest discover -s firmware/tests/hardware -v
```

## Runnable USB target checks

The runner starts with [read-only HID checks](#read-only-checks). [Wi-Fi scans](#wi-fi-scans) and the
[card-writing benchmark](#storage-benchmark) are opt-in. Use [the report guide](#reading-the-runner-report)
to interpret the JSON and exit status.

For an installed-image read-only check, use a running board with USB HID management and install the
[Python host package](../guides/dashboards/python.md#install) in an active venv. Find the exact board serial in the
dashboard, then close it and any other management clients. On Linux, check
[HID permissions](../guides/dashboards/README.md#linux-usb-permissions) if needed. The read-only check works with
the installed firmware and can run with the card slot empty.

### Read-only checks

From the repository root, run the default check. It reads reports and reopens the HID handle without
sending state-changing commands.

```sh
mkdir -p build
python3 firmware/tests/hardware/run.py --serial BOARD_SERIAL \
  --cycles 100 --reopens 10 > build/firmware-hid-check.json
```

It requires fresh telemetry and storage reports within 5 seconds, validates reports through the existing codecs,
rejects unexpected uptime resets, and fails sampled configuration/wireless Feature reads or Input polls exceeding
1000 ms. HID handle reopens are **not** physical USB disconnect and reconnects or firmware USB-stack reset tests.

### Wi-Fi scans

On a shared bench, [agree on the scan](#agree-on-the-test-actions). Repeat scans from the repository root
while checking management responsiveness.

```sh
mkdir -p build
python3 firmware/tests/hardware/run.py --serial BOARD_SERIAL --cycles 100 --reopens 10 \
  --wifi-scans 20 --allow-state-changes > build/firmware-wifi-check.json
```

Scans must complete with the expected generation, count, unique SSIDs and strongest-first ordering.
Results exclude SSIDs, passwords, Control keys and full wireless reports.

### Storage benchmark

Use a backed-up, expendable card mounted by firmware, with enough free space for the selected size.
On a shared bench, [agree on card writes](#agree-on-the-test-actions). Start with 32 MiB from the repository root.

```sh
mkdir -p build
python3 firmware/tests/hardware/run.py --serial BOARD_SERIAL --cycles 100 --reopens 10 \
  --benchmark-mib 32 --allow-state-changes --allow-card-writes > build/firmware-benchmark-32-check.json
```

Then use 64, 256, and 1024 MiB on cards with enough free space. Change `--benchmark-mib` and use a distinct output
filename for each run. The 1024 MiB run checks USB responsiveness during a long benchmark.
The runner never formats the card, saves settings, reboots the board, or transfers the card to USB. If an operation
fails or its outcome is unclear, it stops instead of canceling or retrying it.

A passing run requires observed benchmark activity before accepting its terminal result, exact completed byte counts,
zero mismatches or errors, successful cleanup, and restoration of the original identified card's firmware mount.
If a run finishes before activity can be observed, treat it as inconclusive rather than attributing an old result.
The benchmark uses only the reserved file and safety rules in the [storage contract](../reference/sd-storage.md).

### Reading the runner report

Exit status 0 and JSON `result: passed` mean the selected runner checks passed. Use the default thresholds
first: 1000 ms for sampled reads and 900 s for benchmark completion. If you adjust them with
`--max-latency-ms` or `--timeout-seconds`, record the values.

The JSON records measured maxima and selected check counts. `checks.samples` counts baseline cycles and handle
reopens; report counters also include benchmark monitoring. Input latency includes the requested polling wait.
Identity and command-acknowledgement Feature calls are not individually timed. The runner measures completed calls;
it cannot interrupt a HID operation blocked in the operating system. Supervise the process externally and
record a runner hang as a failure.

A nonzero exit means the run failed. Stop, preserve the output, and reconcile the board before recovery.

## SD card and USB MSC matrix

Cover native SDMMC and SPI separately, at least three representative cards across FAT32 and exFAT where supported,
two wiring and socket assemblies, and Linux, Windows, and macOS for USB MSC. Keep these proposed minimums in the record;
smaller matrices establish only the combinations actually exercised.

| Check | Procedure and proposed minimum | Acceptance |
| --- | --- | --- |
| Boot and card changes | 20 cold boots with a card and 20 without, then 20 insert, remove, and reinsert cycles at idle per combination | Correct detection and identity; stable insertion mounts within 5 s in the normal case; replacement never auto-exports |
| Sustained I/O | 10 passing runs each at 32, 64, and 256 MiB, 3 at 1024 MiB, then a 2-hour supervised workload | Exact write and read verification; no leftover file, unexpected reset, host USB reset loop or UART CRC/I/O errors |
| MSC handoff | 50 cycles of Enable USB drive, OS eject, and confirmed Return per OS, card, and transport | Admission distinguished from completed ownership; firmware unmounts before export, return remounts the same card, HID recovers within 15 s |
| MSC data integrity | While exported, use OS-native file copy and hash checks on expendable files; safely eject before return | Hashes preserved; no concurrent firmware filesystem access; successful remount after return |
| Busy and no-medium regression | Monitor host USB logs during the 1024 MiB benchmark while the unexported LUN is probed | Prompt no-medium responses; no repeated ~33 s USB reset loop; management reads meet the recorded latency threshold |
| Detected removal during work | Remove the dedicated card during a write, read, or ownership transition; repeat 5 times per phase | Run fails visibly, access is revoked, uncertain commands are not replayed; replacement is not used for cleanup or auto-export |
| Fault recovery | Preserve logs and inspect or recover the volume on a computer; reinsert or power-cycle the card if reinitialization fails | Record recovery steps; firmware does not format or repair the card, and the report does not claim an unverified benchmark pass |

Removal or power loss can corrupt FAT; passing a fault-injection test does not mean writes are atomic or the volume
will remain undamaged.
Record a retained `EFTEST.BIN` as cleanup evidence; only the operator may remove it from the original card after
safe ownership release. A failed cleanup is not a successful benchmark. Power-loss tests need separate approval.
The single-copy settings save is not power-loss atomic.

## USB, BLE, and Wi-Fi lifecycle stress

| Check | Procedure and proposed minimum | Acceptance |
| --- | --- | --- |
| Settings-save responsiveness | Save known recoverable settings while measuring save response and concurrent diagnostic-read delay; repeat over each supported management transport | Record timings and compare them with a separately approved product limit; no numeric response-time limit is established here |
| Physical USB reconnect | 50 USB data-cable disconnect and reconnect cycles at idle, retaining UART logs | Same identity; no unexpected board reset; HID reports return within 15 s and remain responsive |
| BLE reconnect and authentication | With Direct, 50 connect, unlock, read, and disconnect cycles; test MTU 23 and a larger negotiated MTU where the central allows it | Advertising resumes; each new connection is locked; old session proofs are unusable; long reads have consistent report layouts |
| BLE and USB coexistence | Read diagnostics through both transports for 30 minutes; designate only one state-changing client | No mixed or stale long-read snapshots, unexpected resets or ownership changes; recorded management latencies remain within the selected limits |
| Wi-Fi recovery | 20 saved-network reconnects after disabling and re-enabling the AP; exercise unavailable-network and wrong-password tests, then cancel explicitly | Failure visible; scan/test generation preserved; saved-network retry resumes; no credential disclosure |
| DHCP renewal | Use an AP configured with a short lease; observe at least 3 lease renewals | Connectivity and management recover without reboot; address/state changes match observations |
| RF coexistence and soak | Run Wi-Fi traffic, BLE reads and USB monitoring for 2 hours, including 20 scans and a long storage run | No reset, stuck test or scan, exhausted connection resources, or persistent transport outage |

Settings saves erase, program, and verify flash synchronously in a transport callback while holding the application
mutex. The 20 ms management-loop period does not guarantee callback latency. Capture the
[flash-operation timing entry](../reference/boot-settings.md#saving) with the host measurements; it excludes mutex wait
and full transport-response time.

Use UART logs and Direct for manual BLE observations. The USB runner is not a BLE central; it cannot qualify MTU
handling, RF, physical USB re-enumeration, or DHCP renewal. Longer soaks, temperature and power variation, flash
endurance, and RF, EMC, and regulatory qualification remain separate product work.

## Adapting checks to other boards

These are prerequisites for a future board-specific procedure, not evidence that S3 results carry over.
Select that board's image, pin allocation, and available commands before adapting the matrices.

**ESP32-C6:** run [initial physical commissioning](../hardware/esp32c6-commissioning.md) separately and record
its verified outcome before management testing. The USB runner cannot manage C6; BLE checks need a separate
procedure. Keep keys out of logs and records, and follow the commissioning utility's no-replay rules.

**Pico:** qualify the selected UF2 image and storage bus separately. The PIO SD host retries failed data transfers.
Include a controlled transfer-error case where a write may have partially reached the card, then verify the affected
data and filesystem on a computer. Do not infer safety from a successful retry or replay a higher-level command
whose outcome is uncertain.
