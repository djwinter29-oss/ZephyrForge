# Security boundaries

[Documentation](../README.md) / Reference / Security

ZephyrForge is a local development foundation, not a hardened product security design. This page summarizes the
security boundaries; each linked contract defines its operational details.

## Management links

USB HID uses physical access. BLE authenticates commands with the Control key, but the default BLE link is not
encrypted. Unlocked BLE status can expose the saved SSID and IP address to a nearby radio observer. See the
[Bluetooth security properties](bluetooth-le.md#security-properties).

The ESP32-S3 raw TCP diagnostic shell is unauthenticated plaintext on port 2323. Use it only on a trusted, isolated
network; never port-forward it or enter secrets. See [diagnostic shells](../guides/console.md#raw-tcp).

Python and .NET bridge hosts bind to loopback. Do not expose or reverse-proxy them to a network without a separate
authentication design. See the [host API](http-api.md#routes).

## Stored credentials

Wi-Fi credentials and the Control key are stored unencrypted in flash. Physical flash, UART-download, or JTAG access
can recover them. Reports and pending-operation journals do not contain credentials. See
[settings and flash safety](boot-settings.md#record-format).

## Storage safety

Firmware mounts existing cards without formatting. Physical removal during writes and power loss can corrupt the
filesystem; safely eject before returning USB ownership. See [card handling and handoff](../guides/sd-card.md).
