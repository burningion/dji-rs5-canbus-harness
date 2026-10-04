# Body-side wireless UWB controller

Adafruit Feather ESP32-S3 8 MB / no PSRAM on the fixed RS5 body, acting as **master**. Builds the same [manual/Control Desk controller](../rs5_manual_control/README.md) with paired ESP-NOW UWB input instead of the anchor UART wires. It polls the optional camera node and owns tracking and all CAN commands. Manual control works without that node; its arrival offers UWB follow without taking control. Existing CAN wiring and operator leases remain in effect; boots disarmed.

Flash [rs5_wireless_control.ino](rs5_wireless_control.ino) onto the **RS5-body** ESP32 using the [main README's flashing and pairing steps](../../README.md#flash-both-esp32s). First upload discovers its STA MAC (`help` over USB serial at 115200); after generating the shared pairing file, rebuild/upload both ESP32s. Without configuration, `radio_ready=0` is expected.

See [wireless wiring and power](../WIRELESS_UWB.md). This build needs local pair configuration and the existing USB/Control Desk session. The moving camera has its own Feather and shared anchor battery; its supply is independent of this board. The user's wired Waveshare/Mill-Max tracking path is validated; the added wireless hop still needs a physical test.
