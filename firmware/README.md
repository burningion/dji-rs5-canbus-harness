# RS5 firmware

**For the camera-to-RS5 wireless setup, flash both ESP32s using the [main README's complete flashing and pairing steps](../README.md#flash-both-esp32s).** Both boards are Adafruit Feather ESP32-S3, 8 MB / No PSRAM, using Arduino-ESP32 3.3.11.

| Use | Firmware |
| --- | --- |
| Camera ESP32: anchor UART → ESP-NOW | [rs5_anchor_radio](rs5_anchor_radio/README.md) |
| RS5-body ESP32: ESP-NOW → tracking → Waveshare CAN → Mill-Max harness | [rs5_wireless_control](rs5_wireless_control/README.md) |
| Original wired UART → CAN setup, with Control Desk | [rs5_manual_control](rs5_manual_control/README.md) |
| Passive CAN diagnostics | `rs5_can_monitor`, documented below |

The user confirmed the Mill-Max interface and successful tag following through the direct wired path on 2026-10-04. The wireless firmware is implemented and software-tested; physical pairing and wireless following still need validation. The anchor and tag retain their factory firmware. See [wireless wiring/power](WIRELESS_UWB.md) and [Control Desk setup](../control_ui/UWB_SETUP.md).

## RS5 CAN monitor for Adafruit Feather ESP32-S3

Open [rs5_can_monitor/rs5_can_monitor.ino](rs5_can_monitor/rs5_can_monitor.ino) in Arduino IDE. The target is the **Adafruit ESP32-S3 Feather with STEMMA QT / Qwiic, 8 MB flash, no PSRAM (product 5323)**, using its built-in TWAI controller and the Waveshare SN65HVD230 transceiver. No additional Arduino libraries are required.

For Makerfabs STM32 AoA tracking through the existing UI and PS4 X button, use the [combined manual + UWB sketch](rs5_manual_control/README.md) and [connection/setup guide](../control_ui/UWB_SETUP.md). The separate [console UWB prototype](rs5_uwb_tracker/README.md) is also retained. It keeps CAN on GPIO5/6 and adds anchor UART input on RX/GPIO38. This page describes the standalone passive CAN monitor.

The firmware listens at **1,000,000 bit/s, Classical CAN**. It accepts all IDs and prints a sample of received frames, with separate counters for standard data frames on the inherited DJI IDs `0x222` and `0x223`. The bitrate worked in the 2026-09-28 RS5 passive receive test, which showed ID `0x426`. Subsequent tests with the separate [motion bench sketch](rs5_can_motion_test/README.md#hardware-result--2026-09-28) verified SDK joint queries on 0x223, replies on 0x222, and three small yaw cycles; this passive sketch never sends commands.

For the next diagnostic after successful passive reception, the separate [CAN acknowledgment monitor](rs5_can_ack_monitor/README.md) runs in normal CAN mode with automatic ACK/error signaling but no application messages or DJI commands. This original sketch remains permanently listen-only.

After successful SDK joint-angle queries, the separate [finite motion bench test](rs5_can_motion_test/README.md) provides three supervised small pan cycles with telemetry, heartbeat, and angle bounds. It needs no UWB kit and never starts movement automatically.

For interactive mouse or PS4 control, use the [Control Desk UI](../control_ui/README.md) with the [manual-control sketch](rs5_manual_control/README.md). It controls pan and tilt up to 60°/s without added software travel limits, leaves roll fixed, and requires a fresh held mouse/L1 gesture plus live input and telemetry.

**This version always uses listen-only mode.** It sends no CAN messages, acknowledgments, or error frames, has no transmit command, and never switches to an active mode. A quiet bus is inconclusive: an RS5 may wait for a query, and a solitary transmitter cannot get an acknowledgment from this listener. The firmware cannot discover ground or protect against connecting a supply pad to CANH/CANL.

## Wiring

Use the Feather header pads labelled **5** and **6**: they are GPIO5 and GPIO6. These are not physical header positions or the pads labelled TX/RX. GPIO4 is SCL on this Feather and is shared with the onboard I2C devices and STEMMA QT; this sketch leaves it alone. The two CAN pin constants are at the top of the sketch.

| Feather header label | Waveshare board |
| --- | --- |
| 5 (GPIO5) | CAN TX |
| 6 (GPIO6) | CAN RX |
| 3V (regulated 3.3 V) | 3.3V |
| GND | GND |

TX connects to TX and RX to RX on this board: CAN TX is the transceiver's input from the controller, and CAN RX is its output to the controller. They are not UART signals.

After verifying the RS5 contacts:

| RS5 signal | Connection |
| --- | --- |
| CAN-H | Waveshare CANH |
| CAN-L | Waveshare CANL |
| Verified GND | Common Waveshare/ESP32 GND, even though the Waveshare GND pin is on the other side |
| Both VCC contacts | Individually insulated, unconnected |
| SBUS_RX | Insulated, unconnected |
| AD_COM | The project's 47 kOhm pull-down, only after identifying this contact; never a direct short |

Use USB to power the Feather and its **3V** output to power the Waveshare. The RS5 accessory output must not connect to this supply. Twist CANH/CANL together. Fit or remove the gimbal adapter with all power disconnected. See the [main electrical checks](../README.md#termination-and-first-electrical-checks) for ground, pad orientation, and bus termination.

Check the unpowered, disconnected Waveshare's resistance between CANH/CANL before adding a resistor: it may already have termination. Account for that resistance when checking the complete bus. The older SDK ties accessory detection to port power; whether AD_COM is required for RS5 CAN alone remains unverified.

The sketch holds GPIO5 HIGH (recessive) from the beginning of setup. An optional 10 kOhm pull-up from Waveshare CAN TX to its 3.3V keeps that input high while the ESP32 GPIO is floating during reset. This does not replace pinout verification.

## Flash with Arduino IDE

1. Install **esp32 by Espressif Systems**, version **3.3.11** (the build used here), through Boards Manager.
2. Open the sketch and select **Adafruit Feather ESP32-S3 No PSRAM**. Keep **Flash Size: 8MB**, **Flash Mode: QIO 80MHz**, and **Partition Scheme: TinyUF2 8MB (2MB APP/3.7MB FATFS)**. This profile already disables PSRAM.
3. Use the Feather's USB-C connector. Keep **USB Mode: USB-OTG (TinyUSB)**, **USB CDC On Boot: Enabled**, and **Upload Mode: USB-OTG CDC (TinyUSB)**, the defaults for this board.
4. Select the connected port and upload with the RS5 disconnected. If the board does not enter download mode automatically, hold BOOT, tap RESET, release BOOT, then select the new port and upload. Reset after the first upload if needed.
5. Open Serial Monitor at **115200 baud**. If the startup banner has already passed, send `h`; status repeats once per second.

The Feather uses native USB for serial output. Its header TX/RX pins are unused by this sketch. Uploading installs this Arduino application in place of any existing application such as CircuitPython; copy any files you need from CIRCUITPY first.

## Flash with Arduino CLI

Run these from the repository root. The board profile selects 8 MB flash, no PSRAM, TinyUSB serial, and the TinyUF2 partition layout.

```sh
arduino-cli compile \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --warnings all \
  --build-path "$PWD/tmp/firmware-build-feather" \
  --output-dir "$PWD/tmp/firmware-feather" \
  firmware/rs5_can_monitor

arduino-cli board list
```

Replace `YOUR_PORT` below with the actual port, such as `/dev/cu.usbmodem...` on macOS or `COM5` on Windows. Close any existing serial monitor before uploading.

```sh
arduino-cli upload \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --port YOUR_PORT \
  --input-dir "$PWD/tmp/firmware-feather" \
  firmware/rs5_can_monitor

arduino-cli monitor --port YOUR_PORT --config baudrate=115200
```

Native USB can re-enumerate after flashing; check `board list` again if the port changes. A first upload from the ROM bootloader may require an explicit `--board-options UploadMode=default` on the upload command to skip the TinyUSB 1200-baud reset sequence; the sketch still uses TinyUSB serial after it starts.

## First test and output

Start with the Feather and Waveshare connected to each other and **CANH/CANL disconnected from the RS5**. With USB and any LiPo battery disconnected, check the Waveshare's CANH-to-CANL resistance and record it. Then power the Feather by USB and use DC-voltage mode (black lead in COM, red in V/ohms) to check approximately **3.3 V between Waveshare 3.3V and GND**. Keep the probe tips from bridging adjacent pins; resistance/continuity mode is only for unpowered checks.

Seeing `LISTEN_ONLY RUNNING` and repeating status proves the firmware/USB/TWAI driver started. `rx=0` is expected. This does not test the transceiver's CAN path or confirm the gimbal contacts.

After confirming the gimbal wiring and termination, turn all power off, connect the RS5 CANH/CANL and common ground, then power the ESP32 and RS5. Keep the gimbal's motion area clear for its own startup. Open the serial monitor and observe.

An illustrative frame line (the payload below is only an example):

```text
[4200 ms] STD ID=0x222 DATA DLC=8 01 02 03 04 05 06 07 08
```

The timestamp is time since ESP32 boot when the application reads the frame, not a bus capture timestamp. `STD` means an 11-bit ID, `EXT` a 29-bit ID, and `RTR` a remote request with no payload. The monitor does not reassemble DJI packets or validate their SDK CRCs.

| Output | Meaning |
| --- | --- |
| `rx` | Total frames read from the TWAI receive queue since boot |
| `id222`, `id223` | Standard data frames matching the inherited DJI IDs; ID matching alone does not validate an SDK reply |
| `bus_errors` | Driver's cumulative bus-error count; inspect bitrate, termination, continuity, and wiring if it increases |
| `rx_missed`, `fifo_overrun` | Driver-reported receive losses; this is not a lossless bus recorder |
| `REC`, `TEC` | Driver error counters; zero in listen-only mode does not prove electrical correctness |
| `queued` | Frames currently waiting in the driver's receive queue |
| `alerts` | OR of driver alert flags since the previous status report |
| `omitted` | Frame previews omitted by the 20-per-second limit or by pausing previews; these frames still count toward `rx` |
| `console_skipped` | Whole output lines skipped because serial was unavailable or the 4 KB console queue was full; queued lines drain in USB-sized chunks |

Send `h` or `?` for help, `s` for immediate status, and `p` to toggle frame previews. Any serial line ending works. These commands only change local output. Reception continues without an open serial monitor.

Valid frames are evidence that the receive path and bitrate work. Repeated frames may be retries caused by missing acknowledgments. **No frames does not mean the wiring is wrong, and zero errors does not mean it is right.** After confirming the wiring, the separate [tracker's optional query mode](rs5_uwb_tracker/README.md#optional-motion-build-after-electrical-and-preview-checks) is available as the next step if passive listening stays quiet; this monitor remains permanently passive.

## Validation

Compiled for `esp32:esp32:adafruit_feather_esp32s3_nopsram` with Arduino-ESP32 **3.3.11**: 359,366 bytes of application flash and 58,688 bytes of static RAM. On 2026-09-28 it was flashed and verified running on the Feather. The user's connected RS5 test received repeated standard ID `0x426` frames, including a 15-second interval with 115,383 received frames, no additional bus errors, and no reported receive losses. These results validate passive reception in that setup, not SDK query/control compatibility. See the separate [acknowledgment diagnostic results](rs5_can_ack_monitor/README.md#hardware-validation--2026-09-28) for the follow-up test.

That core emits a command-line macro warning for its default hyphenated partition name (`ARDUINO_PARTITION_tinyuf2-partitions-8MB`). The build succeeds; the sketch does not use that macro.

The console queue test passes with address/undefined-behavior sanitizers, covering TinyUSB's 64-byte output chunks, wraparound, partial writes, a stalled sink, and whole-line rejection on overflow. To repeat from the repository root after the firmware build has created `tmp/`:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  firmware/tests/console_queue_test.cpp -o tmp/console_queue_test
./tmp/console_queue_test
```

## References

- [Adafruit Feather ESP32-S3 pin descriptions](https://learn.adafruit.com/adafruit-esp32-s3-feather/pinouts)
- [Adafruit Feather ESP32-S3 Arduino setup](https://learn.adafruit.com/adafruit-esp32-s3-feather/using-with-arduino-ide)
- [Espressif TWAI driver used by this sketch](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/twai.html)
- [Arduino-ESP32 USB flashing and serial settings](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/cdc_dfu_flash.html)
- [Waveshare SN65HVD230 CAN Board](https://www.waveshare.com/product/sn65hvd230-can-board.htm)
