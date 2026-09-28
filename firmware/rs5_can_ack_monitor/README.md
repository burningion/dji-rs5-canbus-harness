# RS5 CAN acknowledgment diagnostic

This is a separate sketch for the Adafruit Feather ESP32-S3, 8 MB flash / no PSRAM, and Waveshare SN65HVD230. It tests whether automatic CAN acknowledgments allow the RS5 to progress beyond repeated frames seen by the [passive monitor](../README.md).

**This is not electrically passive.** It starts in normal CAN mode at 1,000,000 bit/s and automatically acknowledges valid received frames and participates in CAN error signaling. It has no application transmit calls, DJI packet generator, queries, or movement commands. Its application TX queue is disabled. Automatic ACKs can change what the RS5 sends; they do not indicate that this program understands or accepts a DJI command.

## Wiring and use

Keep the verified wiring and termination from the passive test:
Feather 5/GPIO5 to Waveshare CAN TX, 6/GPIO6 to CAN RX, 3V to 3.3V, and shared Feather/Waveshare/RS5 ground. RS5 CAN-H/L go to CANH/CANL. Leave both RS5 VCC leads insulated. Fit or change the adapter with all power removed.

Close any other serial monitor before uploading. Observe the gimbal with its motion area clear; this firmware does not request movement, but the gimbal retains its own startup and controls.

```sh
arduino-cli compile \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --warnings all \
  --build-path "$PWD/tmp/firmware-build-ack" \
  --output-dir "$PWD/tmp/firmware-ack" \
  firmware/rs5_can_ack_monitor

arduino-cli upload \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --port YOUR_PORT \
  --input-dir "$PWD/tmp/firmware-ack" \
  firmware/rs5_can_ack_monitor

arduino-cli monitor --port YOUR_PORT --config baudrate=115200
```

Use `arduino-cli board list` to find the port. The banner says `NORMAL CAN`; status says `ACK_MONITOR RUNNING`.

Commands `h`/`?`, `s`, and `p` show help, status, and toggle frame previews. None change CAN mode. **Closing the serial monitor does not stop acknowledgments.** Restore the original `rs5_can_monitor` sketch to return to electrically passive listening.

The same preview limit of 20 frame lines per second applies; counters include omitted previews. `id426` counts standard data frames on the observed ID, alongside the legacy `id222`/`id223` counters. IDs and eight-byte fragments are not decoded as DJI messages, and no particular meaning is assigned to 0x426. Timestamps are application dequeue times. This is not a lossless capture.

Compare received-frame rate, IDs/payloads and the change in cumulative `bus_errors` over 10–15 seconds. A reduced repetition rate or advancing payloads after enabling ACKs supports the missing-acknowledgment explanation; it does not prove SDK query/control compatibility. The monitor does not initiate bus-off recovery.

## Initial evidence

The user's passive RS5 test reported repeated standard ID 0x426 with payload `55 45 04 DE E5 06 00 00`. Between 700013 and 715013 ms it received 115,383 frames while `bus_errors` stayed at 290 and receive-loss counters stayed at zero. This prompted this diagnostic; the frame's application meaning remains unknown.

[Espressif TWAI operating modes](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/twai.html#operating-modes) describe normal versus listen-only behavior.

## Hardware validation — 2026-09-28

Compiled with Arduino-ESP32 3.3.11 for the exact Feather profile: 359,442 bytes of application flash and 58,696 bytes of static RAM. The core emitted its existing warning about the hyphenated TinyUF2 partition macro. Upload succeeded and the flasher verified all written data.

The connected Feather reported `ACK_MONITOR RUNNING`. In a 28-second interval (30013–58013 ms), `rx` increased from 1360 to 2631: about 45.4 frames/s, compared with about 7692 frames/s in the earlier passive interval. All sampled `bus_errors`, receive-loss counters, REC and TEC were zero. Printed frames remained on 0x426, but the payloads progressed through multiple fragments and changing header bytes. This strongly supports missing acknowledgments as the earlier repetition cause. It does not identify the payload's meaning or validate DJI SDK queries or motion control; `id222` and `id223` remained zero.

The console sample is saved locally in `tmp/rs5-can-ack-20260928.log` (ignored by Git). It includes the 20-frame-lines/s display limit and is not a full bus capture. The serial monitor was closed after verification; the flashed firmware continues acknowledging while powered.
