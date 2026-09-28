# Finite RS5 motion bench test

Adafruit Feather ESP32-S3 8 MB/no PSRAM; the same verified Waveshare wiring as the CAN monitors: GPIO5 to CAN TX, GPIO6 to CAN RX, 3V to 3.3V, shared ground, and RS5 H/L to CANH/CANL. No UWB kit is required.

The sketch boots in normal CAN mode, acknowledging frames but sending no application messages. `probe` enables documented joint-angle queries on CAN ID 0x223. Replies on 0x222 must match the query sequence, both CRCs, command, successful return code, joint-angle type, and angle bounds. It must receive three consecutive valid replies before allowing `run`.

`run` performs exactly three cycles: positive yaw speed 5 degrees/s for one second, zero speed for two seconds, negative yaw speed 5 degrees/s for one second, zero speed for two seconds. Left/right sign depends on the installation and viewing direction. Roll and pitch requested speeds are zero. Speed requests repeat every 50 ms; the final action sends zero speed and releases control. It does not change stored gimbal modes or limits.

The unit must be balanced, supported, unlocked for normal operation, and clear of cable/physical obstructions, with the physical power control accessible. These are supervised first-motion tests. The 0.5-second speed-command expiration is specified in the older DJI SDK; software cannot guarantee a stop if the link or device fails.

Motion aborts on telemetry older than 400 ms, host heartbeat loss for 750 ms, USB disconnect, a loop stall over 100 ms, invalid console input, or a joint-angle excursion over 8 degrees from initial yaw / 5 degrees from initial roll or pitch. Initial yaw must be inside +/-70 degrees and active yaw inside +/-80 degrees. Faults never automatically restart motion. A usable bus receives zero/release on abort; a CAN fault stops the driver and clears TX, and may prevent a stop packet from reaching the gimbal.

## Build and flash

```sh
arduino-cli compile --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram --warnings all --build-path "$PWD/tmp/firmware-build-motion-test" --output-dir "$PWD/tmp/firmware-motion-test" firmware/rs5_can_motion_test
arduino-cli upload --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram --port YOUR_PORT --input-dir "$PWD/tmp/firmware-motion-test" firmware/rs5_can_motion_test
```

## Run

Close other serial monitors. The Python helper requires pyserial and logs all console output. Without `--run`, it verifies joint queries only. Add `--run` only when ready for the fixed motion test. It maintains the heartbeat, never retries `run`, and sends `stop` on completion, timeout, or interruption.

```sh
python3 firmware/rs5_can_motion_test/run_test.py --port YOUR_PORT --log tmp/rs5-joint-check.log
python3 firmware/rs5_can_motion_test/run_test.py --port YOUR_PORT --run --log tmp/rs5-motion-test.log
```

Console commands, each followed by newline: `help`, `status`, `probe`, `ping`, `run`, `stop`. Send `ping` at least every 200 ms while running; otherwise the firmware aborts within 750 ms. `stop` ends motion and queries. The host helper sends it before closing the port. Closing USB during motion also triggers abort. The board remains in normal CAN mode while idle; reflash the passive monitor to restore listen-only operation.

## Checks

```sh
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer firmware/tests/motion_sequence_test.cpp -o tmp/motion_sequence_test
tmp/motion_sequence_test
```

The sequence checks cover all three cycles and pauses, timer wrap, operator stop, stale telemetry, heartbeat loss, loop stalls, nonfinite angles, and the excursion limits. Packet encoding/parser code is copied from the existing tracker and checked against the independent DJI SDK vector in `firmware/tests/tracker_test.cpp`.

## Hardware result — 2026-09-28

Compiled for the exact Feather profile with Arduino-ESP32 3.3.11: 363,458 bytes of application flash and 58,896 bytes of static RAM. Upload and flash verification passed. The motion-sequence and existing tracker/protocol host tests passed with address/undefined-behavior sanitizers. The Arduino core emitted its existing TinyUF2 partition macro warning.

After the operator confirmed the balanced, stabilized gimbal was ready, the host helper ran all three requested cycles. Joint yaw started at 1.9 degrees, ranged from -3.2 to +2.2 degrees in the sampled telemetry, and ended at 2.2 degrees. Pauses showed steady joint angles, and the run ended with `DONE reason=completed zero_and_release_sent=yes`. All sampled bus-error, receive-loss, REC and TEC counters were zero. There were 196 validated joint replies before the host stopped queries and closed the port. The flashed sketch remains idle, with no automatic restart.

**On this unit, positive requested yaw speed decreased the reported joint yaw; negative speed increased it.** Do not assume speed-command sign matches joint-angle sign when building a closed-loop controller. Camera left/right still depends on viewing orientation.

Local logs (ignored by Git): `tmp/rs5-query-check-20260928.log` records the initial read-only tracker probe, and `tmp/rs5-motion-test-20260928.log` records this finite test. This validates the tested joint queries, small yaw motions, pauses, and normal completion. The fault-abort cases were host-tested, not physically injected; a broken-link stop and the SDK's 0.5-second fallback remain unverified on hardware.
