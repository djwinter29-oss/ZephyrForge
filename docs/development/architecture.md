# Firmware architecture

[Documentation](../README.md) / Development / Architecture

USB HID and BLE feed the same control dispatcher. Slow radio and storage operations run outside the management
loop's application lock, including settings flash writes. This page explains the service boundaries
and concurrency rules. For source layout and tests, use [firmware development](firmware.md).

- [Service responsibilities](#service-responsibilities)
- [Platform integration and build-time composition](#platform-integration)
- [Startup, locking, and failure handling](#lifecycle-and-failure-handling)

```mermaid
flowchart LR
    subgraph Browsers["Browser clients"]
        Direct["Direct browser"]
        PythonBrowser["Python-hosted browser"]
        DotNetBrowser[".NET-hosted browser"]
    end
    subgraph Hosts["Loopback USB hosts"]
        PythonHost["Python host<br/>(hidapi)"]
        DotNetHost["ASP.NET host<br/>(HidSharp)"]
    end
    subgraph Device["ESP32-S3 / Zephyr"]
        USB["USB HID<br/>(native USB)"]
        BLE["BLE GATT"]
        App["Zephyr application<br/>Control dispatcher + boot settings<br/>Wi-Fi station + DHCP<br/>4-bit SDMMC / FatFs"]
        Console["Zephyr diagnostic shells / log"]
        ROM["ROM bootloader<br/>(flashing)"]
    end
    UART["UART connector"]

    Direct -->|WebHID| USB
    PythonBrowser --> PythonHost --> USB
    DotNetBrowser --> DotNetHost --> USB
    Direct -->|Web Bluetooth| BLE
    USB --> App
    BLE --> App
    UART --> Console
    UART --> ROM
    TCP["Raw TCP diagnostic client"] -->|Wi-Fi| Console
```

USB HID and BLE share ZephyrForge report payloads and Control semantics, defined by the
[USB HID](../reference/usb-hid.md) and [Bluetooth LE](../reference/bluetooth-le.md) specifications.

The diagram shows the full ESP32-S3 configuration. Board and feature selections can remove links and services;
the [board guide](../hardware/README.md) lists available connections, and the platform table below identifies
their driver integration.

## Service responsibilities

| Layer | Responsibility |
| --- | --- |
| Application lifecycle | Boot order and periodic servicing of control, Wi-Fi, telemetry, and USB |
| Board integration | Shared state locking, UID/serial, reset cause, board facts and delayed reboot |
| Control dispatcher | Command validation and execution shared by USB and BLE, with separate result slots |
| Report encoding | Shared identity, configuration, telemetry, storage, and wireless payloads |
| Wireless staging and authentication | Expiring credential candidates, BLE session unlock and per-command HMAC proofs |
| USB HID and BLE | Transport framing, read snapshots, publication/polling, and connection lifecycle |
| Wi-Fi | Saved-network joins, candidate tests and scans |
| Diagnostic shells | Native UART and raw TCP inspection without application mutations ([console](../guides/console.md)) |
| Telemetry and storage | CPU load, temperature, and clock; runtime SD detection, FAT mount, and exclusive USB handoff |
| Boot settings | One CRC-checked next-boot record for CPU clock, radio switches and credentials ([settings](../reference/boot-settings.md)) |

Python and .NET own USB HID connections and serve the same dashboard through a loopback HTTP API. Direct owns
its browser frontend and talks through WebHID/Web Bluetooth. It does not import the shared dashboard.
See [host development](host-tools.md) for these boundaries.

## Platform integration

| Integration | ESP32-S3 | ESP32-C6 | Pico family |
| --- | --- | --- | --- |
| Application core | PROCPU only | High-performance core | Core 0 only |
| USB device stack | Zephyr `device_next`, native OTG | No native HID/MSC | Zephyr `device_next`, native device controller |
| Wi-Fi | Espressif network management and DHCP | Espressif network management and DHCP | AIROC/WHD and DHCP on W boards; none on non-W boards |
| Bluetooth | Zephyr host and SoC controller | Zephyr host and SoC controller | Not included |

Storage uses Zephyr disk access and FatFs over the selected native or SPI host. The Pico native host implements
the Zephyr SDHC API on PIO. The [SD reference](../reference/sd-storage.md) covers driver configuration and limits.
Settings use a fixed CRC-checked record rather than Zephyr settings/NVS;
the [settings reference](../reference/boot-settings.md) defines its lifecycle and durability.

### Build-time composition

Board selection defines hardware identity and wiring; feature selection defines which services are linked.
Shared report encoding and control policy are independent of USB and BLE transport implementations.
Wi-Fi and BLE each have a board-independent disabled implementation. Advertised radio support follows the built
services rather than the physical board's possible radios. Runtime radio switches cannot restore omitted modules.
Storage policy remains separate from the Zephyr media worker, and USB export can be omitted without removing FAT
mounting. Use [firmware build options](firmware-build.md#optional-modules) for selection and dependencies.

## Lifecycle and failure handling

### Boot

Startup loads the [settings record](../reference/boot-settings.md) before consumers start, then applies the clock
and initializes storage and radios before USB on supported boards.

A mount failure is logged but does not stop USB or the radios. Report 10 can show an initialized card without a
mounted filesystem; it does not carry the mount error code. Boot logs include settings-load source and elapsed
time, and initial SD setup/mount duration. No card self-test or file write runs at boot.

### Threads and locking

USB callbacks run in the USB stack thread and BLE callbacks in the Bluetooth thread. Both call the same control
dispatcher. After boot initialization, every access to settings, staging, and wireless state holds one application
mutex.

- **Settings saves** encode the record under the mutex, then erase, write, and verify flash on a system work-queue
  item without it. USB reports Pending until the item finishes; the BLE write handler waits, unlocked, so its
  result stays terminal. The nominal 20 ms main-loop period is not a response-time guarantee. The
  [flash-operation timing log](../reference/boot-settings.md#saving) excludes lock wait and full callback latency;
  save responsiveness and contention have not been hardware-qualified.
- **Wi-Fi events** are queued and applied by the main loop. Connect, disconnect, and scan requests run outside
  the lock.
- **Storage** runs in a worker outside the application lock. Native MSC block I/O runs in its own USB class thread.

### Command completion and recovery

Reboots are scheduled about one second after the reply so the host can read the result. A running settings write, then
an accepted reboot or save, blocks further reboot/save and storage-control requests until reset; diagnostic reads
remain available.
Failed reset scheduling leaves normal admission available. Busy or USB-owned storage blocks reboot/settings-save.
An accepted storage request is not proof that the ownership transition has finished; see
[SD ownership](../reference/sd-storage.md#usb-drive-handoff) for completion and recovery rules.

Wi-Fi joins, scans and recovery have separate completion states. Their user-visible timing and recovery behavior
belong to the [wireless guide](../guides/wireless.md) and
[USB control contract](../reference/usb-hid.md#wireless-command-rules).

Hosts reject reports outside the wire contracts and never retry a command whose outcome is uncertain. They block
further writes until the user confirms a re-sync, so the board can be checked first.
