# Diagnostic shells

ZephyrForge enables Zephyr's native diagnostic shell on the UART connector and, on the ESP32-S3, as a raw TCP stream
on port **2323**.
Neither shell carries dashboard commands or changes saved settings, wireless credentials or SD ownership.
USB HID and authenticated BLE remain the management interfaces.

## UART

Open the board's USB-to-UART bridge port in a serial terminal at **115200 baud, 8N1**, with flow control and local
echo disabled. The Raspberry Pi Pico boards have no bridge; use a 3.3 V USB-UART adapter on UART0, wired as in the
[hardware guide](hardware.md#raspberry-pi-pico-targets). The prompt is `ZephyrForge> `. Enter submits a command;
Tab lists or completes native commands.
Boot messages and logs share this UART and may interrupt the prompt. Close the terminal before flashing.
The native USB connector has HID and MSC, not a CDC serial shell.

## Raw TCP

The TCP shell needs Wi-Fi, so only the ESP32-S3 image includes it.
First provision Wi-Fi through a dashboard and wait for an IPv4 address in its live wireless diagnostics.
Connect to that address:

```sh
nc <board-ip> 2323
```

In PuTTY select **Raw**, port `2323`, and enable local echo and local line editing. Do not use Telnet negotiation.
Enter ordinary ASCII command lines. TCP echo is disabled on connection, so a normal line-buffered terminal provides
local editing.
Press Enter once if no prompt appears after connecting. Close and reconnect the client after network loss.

One TCP client is accepted at a time. UART and TCP have independent shell contexts and may be used concurrently.
The listener is available whenever the board has a usable IPv4 connection, including during a temporary Wi-Fi test.
Disabling automatic Wi-Fi joining does not disable it.

**TCP is unauthenticated and unencrypted.** Any reachable network peer can use the diagnostic shell; the BLE
Control key does not protect it. Use only on a trusted, isolated network, never port-forward it, and do not enter
secrets. Logs are not forwarded to TCP, and command history is disabled on both shells.

### In-browser terminal

With fresh connected Wi-Fi status, open the shell link in the dashboard. Python and .NET provide a relay.
Direct needs its [local server](../../host/direct/README.md#wi-fi-shell). The terminal uses the board's reported
IPv4 address and shows an error if the shell is unreachable.

**The board's TCP shell is unauthenticated plaintext.** Use it on a trusted, isolated network and keep
secrets out of the terminal.

Click the terminal to type. It displays plain text and supports native editing, including arrows, Tab and
Control keys. Use Shift+Tab to leave the terminal and confirm multiline paste before sending. Opening a
session clears unfinished input from the previous client and enables echo for the session.

Close the tab to disconnect. The relay closes the session after 15 seconds without requests. If a send
fails or its outcome is uncertain, close the session and check the board. Avoid replaying uncertain input.
For the bridge endpoints and session rules, see the [HTTP contract](../../host/web/README.md#browser-shell).

## Commands and limits

Start with `help` to list the compiled native commands, or `device --help` for device inspection:

```text
help
device list
```

Native shell utilities manage the current terminal and report shell statistics. Use the dashboard for board,
settings, wireless, and storage management.

The kernel shell is disabled because its reboot and thread-control commands bypass management and storage safety
gates. The shell also excludes memory, flash, filesystem, network-configuration, and Bluetooth commands.
