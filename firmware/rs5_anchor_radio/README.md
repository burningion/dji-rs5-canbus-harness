# Camera-side UWB radio

Battery-powered Adafruit Feather ESP32-S3 8 MB / no PSRAM beside the stock STM32 anchor. Acts as an **optional UWB peripheral** to the RS5-body master: receives TXD1/PA9 on RX/GPIO38 at 115200 and answers the master's requests with fresh measurements over paired ESP-NOW. No CAN interface or USB-host requirement. Once paired, powering it on makes its reports available automatically; the master decides when to follow.

Flash [rs5_anchor_radio.ino](rs5_anchor_radio.ino) onto the **camera** ESP32 using the [main README's flashing and pairing steps](../../README.md#flash-both-esp32s). First upload discovers its STA MAC; after generating the shared pairing file, rebuild/upload both ESP32s. Without configuration, `radio_ready=0` is expected. USB serial at 115200 prints the camera MAC once per second.

See [wireless wiring, shared battery and charging](../WIRELESS_UWB.md). The [revision G enclosure](../../design/mauwb-anchor/README.md) includes this board; physical fit and wireless operation remain unverified.
