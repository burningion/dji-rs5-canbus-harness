# RS5 manual pan/tilt firmware

Companion to the [local mouse / PS4 UI](../../control_ui/README.md). Exact board: **Adafruit Feather ESP32-S3 8 MB / No PSRAM**. Existing verified wiring remains GPIO5 → Waveshare TX, GPIO6 → RX, 3.3 V supply, shared ground, and CAN H/L to the RS5. Accessory VCC stays insulated.

```sh
arduino-cli compile --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram --warnings all --build-path "$PWD/tmp/firmware-build-manual" --output-dir "$PWD/tmp/firmware-manual" firmware/rs5_manual_control
arduino-cli upload --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram --port /dev/cu.usbmodem2101 --input-dir "$PWD/tmp/firmware-manual" firmware/rs5_manual_control
```

Boots **disarmed**, in normal 1 Mbit/s Classical CAN mode. No application transmission until `probe`. Speed requests use the documented SDK command 0x0E/0x01 on CAN 0x223; matching joint-angle queries reply on 0x222. Roll speed is always zero. The copied protocol implementation adds pitch encoding at SDK payload bytes 4–5 and retains the existing CRC/parser code.

The firmware caps each controlled axis at **60°/s**. It expires input after 300 ms, telemetry after 400 ms, and stops on USB loss, main-loop stalls, or invalid input. It imposes no starting-angle, absolute-angle, or per-enable travel restrictions. Pan wraparound does not stop movement; joint feedback accepts the SDK’s signed 16-bit range without the old ±180° rejection. See the UI README for physical travel and broken-link limitations. It never automatically resumes after a stop/fault.

## Serial protocol v2

115200 baud, ASCII newline-delimited commands. JSON status is emitted every 50 ms when USB is open, including `protocol:2`, `max_speed_dps:60`, `bounded_retry:true`, device `clock`, session `token`, `armed`, `ready`, live angles, requested speeds, counters, and stop reason. CAN fault telemetry preserves `tx_failed`, `arbitration_lost`, and `fault_alerts` plus the actual reason/counters after driver shutdown.

- `probe`: disable movement and begin read-only joint queries.
- `arm TOKEN CLOCK`: require current token, a device timestamp no more than 300 ms old, three consecutive valid joint replies, fresh telemetry, USB connection, and finite joint angles. Starts at zero speed.
- `drive TOKEN CLOCK SEQ PAN TILT`: request signed speeds in **tenths of a degree/second**; each must be within ±600. SEQ must strictly increase within the enable session. Requires an unexpired input lease. No roll input is exposed.
- `stop`: zero/release if active, invalidate the enable session; retain telemetry queries.
- `idle`: stop and end queries, used when disconnecting.
- `status`, `help`: diagnostic output only.

Each stop rotates the session token. Delayed, malformed, replayed, or out-of-range input stops active motion. Expiry is checked before buffered serial input can refresh the lease. Packet transmission uses normal CAN arbitration retries within a 20 ms deadline; no application backlog is replayed. CAN faults stop/uninstall the driver and clear pending frames; physical disconnection can prevent any stop request from reaching the gimbal.

## Validation

The current 60°/s build uses Arduino-ESP32 3.3.11 for the exact Feather profile: **363,786 bytes flash** and **59,008 bytes static RAM**. Only the existing Arduino/TinyUF2 macro-name warnings appeared. Device-side speed/timeout tests pass under address and undefined-behavior sanitizers, including timer wrap, malformed/replayed input, full-speed signed encoding, three simulated uninterrupted pan revolutions, reverse wraparound, and telemetry beyond ±180°. The independent DJI SDK framing vector and pitch-byte ordering are checked. USB read-only verification is recorded separately; 60°/s and full-turn movement require the operator’s hands-on check.

Flashed and hash-verified on 2026-09-28. The UI bridge received 15 valid joint replies while disarmed, with zero requested speeds and zero sampled bus errors / receive loss / REC / TEC. See the [UI hardware result](../../control_ui/README.md#hardware-check--2026-09-28). No new motion was sent during this verification. The UI now opens this internal control session automatically for a fresh held mouse control or L1 gesture and closes it on release; no manual enable/stop buttons are needed.

After the 60°/s update, upload/hash verification and the read-only check passed again. The board reported `protocol:2`, `max_speed_dps:60`, 15 valid replies, and all error counters zero while disarmed. Log: `tmp/rs5-ui-60dps-check.jsonl`.

The former single-shot flag could turn ordinary arbitration loss into TX failure. Tests of the actual sketch using a fake TWAI driver verify arbitration can complete within the packet deadline, persistent TX is cleared after 20 ms, and fault details survive shutdown and subsequent stop commands. USB disconnection also ends telemetry queries. These changes preserve the existing input/telemetry watchdogs.

The current retry build was flashed and hash-verified. A 90-second read-only check returned 900 joint replies with zero errors/loss/TX failures/REC/TEC while disarmed. The board advertised `bounded_retry:true`. No arbitration-loss events occurred in the observed window, and motion was not commanded. See the UI README for the log and exact test scope.
