# Choose a dashboard

[Documentation](../../README.md) / Dashboards

| Dashboard | Connection to the board | What runs on your computer |
| --- | --- | --- |
| [Direct](direct.md) | Browser-native USB HID or Bluetooth LE | Desktop Chrome or Edge; hosted page or optional local server |
| [Python](python.md) | USB HID | Python 3.10+ host serving a local web page |
| [.NET](dotnet.md) | USB HID | Packaged host with runtime, or .NET 10 SDK for a source checkout |

Use Direct for Bluetooth or a no-install hosted page. Use Python or .NET for a local USB bridge; they serve the
same dashboard. Direct is independent. Check [board connections](../../hardware/README.md) before choosing a
transport; the ESP32-C6 uses BLE and needs [first-key commissioning](../esp32c6-commissioning.md).

Launch instructions are in each dashboard's guide. Once it is running, use the shared
[connection, controls, and recovery guide](../dashboard-use.md).
