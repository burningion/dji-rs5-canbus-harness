# RS5 Control Desk

A local browser UI for manual **pan and tilt**, using a mouse or a PS4 / DualShock 4 controller, plus **X-to-toggle UWB pan follow** using the Makerfabs STM32 AoA kit. See the [UWB wiring and setup guide](UWB_SETUP.md). Roll speed is always zero. The USB-connected Feather talks to the RS5 through the existing Waveshare CAN wiring. The controller connects to the **Mac**, by USB or Bluetooth.

The **RS5-body ESP32 is the master**. Manual control works with the camera UWB node absent or off. Once paired, the camera ESP32 supplies measurements automatically when available. The UWB panel shows **NO UWB**, **TAG DETECTED**, or **TAG LIVE**; only a fresh selected tag enables follow. Camera detection/loss does not take over a manual gesture, and detection never starts movement. [Flash and pair both ESP32s](../README.md#flash-both-esp32s).

## Start

Requires Python 3.11+ with pyserial and aiohttp. Both were already installed on this Mac. On a new machine, use a virtual environment and `python3 -m pip install -r control_ui/requirements.txt`.

From the repository root:

```sh
python3 control_ui/server.py
```

Open **http://127.0.0.1:8765** in Chrome or Edge. The app binds only to the local machine. Close other serial monitors; one browser tab owns the connection at a time. No internet or external frontend assets are needed.

1. Choose the Feather USB port and click **Connect**. Wait for **LIVE** angles. Release the mouse and L1 once after connecting.
2. **Mouse:** hold and drag the circular pad, or hold a direction button. Movement starts from that gesture; release to stop.
3. **PS4:** hold **L1** and move the **left stick**. Movement starts automatically while L1 is held. Release L1 to stop. The stick remains analog: gentle deflection gives slow movement and full deflection reaches the selected maximum.
4. Manual control has **no Enable button, Stop button, or input-mode selector**. Both devices are available together. The first held control owns the gesture; release both before switching to avoid a jump to an already-held second input.
5. **Space**, **Escape**, or PS4 **Circle** also stops. After a stop, focus loss, disconnect, or fault, release the controls and hold again. Connecting, recovering a connection, or leaving a stick displaced never starts movement by itself.

For UWB, configure the tag in the **UWB FOLLOW** panel and wait for **TAG LIVE**, then press **X** or click **Start UWB follow**. X toggles; Circle/Space/Escape stops. L1 or mouse cancels follow; release and hold again for manual control. Tag loss disarms and requires a fresh X press after reacquisition. Follow defaults to 5°/s with a separate 30°/s cap. The [setup guide](UWB_SETUP.md) includes the camera-mounted anchor wiring and direction calibration.

Keep the balanced gimbal supported, unlocked, and clear of cables/obstructions, with its physical power control accessible.

The maximum-speed slider runs from **1 to 60 degrees/second**, initially **10**. The stick has a 12% radial dead zone and a 1.6-power response for fine movement near center. Diagonal input is normalized. At 50% stick deflection and a 60°/s maximum, speed is approximately 15.6°/s. Mouse-pad displacement is also proportional; the direction buttons use the selected maximum. Changing speed or direction reversal during a gesture stops it; release and hold again to use the new setting.

Use **Reverse pan / Reverse tilt** if the physical direction feels opposite to the labels. The previous RS5 bench test found that positive yaw-speed requests decrease reported joint yaw. The UI reports raw DJI joint angles; it does not assume a command sign matches the reported angle sign or run position correction.

If the app says **CAN connection stopped**, the Feather has latched a bus fault. Ensure the RS5 is on and the connection is sound, click **Disconnect**, press the Feather's **Reset** button, then **Connect** again. A server restart or page refresh alone does not clear this device-side fault. The UI distinguishes a CAN fault from missing firmware, stale USB telemetry, and a gimbal that is not answering queries. Read-only diagnostics are available at `http://127.0.0.1:8765/api/status` without taking control away from the open UI.

## Pair the PS4 controller

Use a USB data cable, or hold **SHARE + PS** until the controller light flashes, then pair **DUALSHOCK 4 Wireless Controller** in macOS Bluetooth settings. Click the browser page and press a controller button so the browser exposes it. The controller is detected automatically; release L1 once after connecting. The first detected DualShock is preferred, otherwise the first connected controller; the app won't switch controllers while enabled.

Only the browser's **standard** gamepad mapping is accepted. An unknown mapping disables controller operation instead of guessing which button is L1. Chrome/Edge are the suggested first browsers to try. If the controller is not shown, confirm macOS pairing/data-cable operation and press a button while this page has focus. A USB cable is the simplest way to isolate a Bluetooth issue.

Sources: [Sony's DualShock 4 pairing instructions](https://www.playstation.com/en-us/support/hardware/ps4-pair-dualshock-4-wireless-with-pc-or-mac/), [MDN Gamepad API and standard mapping](https://developer.mozilla.org/en-US/docs/Web/API/Gamepad_API/Using_the_Gamepad_API). The ESP32-S3 supports Bluetooth LE but not Bluetooth Classic ([Espressif](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-guides/bluetooth.html)); this implementation routes the controller through the Mac.

## Future direct PS4 control

**Not implemented:** the current PS4 connection goes through the Mac/browser and USB. DualShock 4 requires Bluetooth Classic, which our Feather ESP32-S3 lacks. Bluepad32 supports it on the **original ESP32**; installing the library on an S3 does not add that radio capability. [Bluepad32 controller/chip compatibility](https://bluepad32.readthedocs.io/en/latest/FAQ/#why-cant-i-connect-my-dualshock-or-switch-controller-to-my-esp32-s3-or-esp32-c3).

The proposed extension is a small original-ESP32 gamepad receiver connected by UART to the existing body master. It would supply live button/stick input while the master continues to own CAN and optional UWB tracking. Replacing the body S3 with an original ESP32 is also possible in principle, but requires porting the firmware and checking pin assignments, USB behavior and simultaneous Bluetooth/ESP-NOW operation.

Computer-free control needs additional firmware: explicit controller pairing, input-source ownership when a UI is also connected, a controller-report timeout, and a way to select/apply the UWB tag and settings without the browser. Preserve L1-to-move/release-to-stop, Circle stop, X follow toggle, fresh tag/telemetry requirements and no automatic restart. Use fresh controller reports to authorize continued operation instead of requiring the current browser/USB lease. A connected Bluetooth state alone is not proof of fresh input. The present builds still require Control Desk; these notes describe future work.

## Travel and stopping

- Firmware caps pan and tilt at **60°/s per axis**; roll command remains zero. It requires three valid, matching CRC-checked joint replies before starting a held gesture.
- **No added software travel limits or starting-angle restrictions.** Pan can cross the reported ±180° seam and keep turning while input is held. Joint replies use the SDK’s full signed 16-bit angle representation, including readings beyond ±180°. Roll requested speed remains zero, without an extra roll-angle excursion stop.
- DJI specifies **360° continuous pan rotation**, with finite mechanical tilt travel of **−112° to +214°** ([RS5 specifications](https://www.dji.com/rs-5/specs)). Removing the app’s limits does not change the gimbal’s own settings or physical clearance. Arrange external camera/USB cables so rotation cannot wind them around an axis; continuous pan on this particular wired setup has not been automatically motion-tested.
- The browser sends current input at about 20 Hz only while visible and focused. Loss of focus, hiding the tab, controller disconnect, or a paused animation loop stops movement. Returning to the page requires a fresh hold; a still-held control cannot resume movement.
- The bridge never generates a motion heartbeat from cached input. It expires incoming input after **250 ms**, rejects expired server tickets / repeated sequence numbers, and allows one controlling tab. Closing the tab stops and closes USB.
- Firmware independently stops after **300 ms** without valid input, **400 ms** without fresh telemetry, USB disconnection, or a loop delay over **100 ms**. It checks expiry before reading buffered commands; session tokens and device-clock stamps prevent old input from restarting motion. A new gesture requires release followed by a fresh held input.
- Normal stop sends zero speeds and releases SDK control. CAN faults clear TX and stop the driver; resetting the Feather is required after fixing the fault. A broken link can prevent stop packets from reaching the gimbal. The older SDK specifies a 0.5-second speed-command expiry, but that fallback has not been physically fault-tested on this RS5. Keep its physical power control accessible.

These are supervised manual controls, not a hardware emergency-stop system. Release-to-stop and keyboard/controller cancellation depend on a working control link.

## CAN and USB reliability

The initial firmware used CAN single-shot mode. Espressif specifies that this disables retransmission even after losing arbitration, so normal bus contention could become a fatal TX failure. The old log combined several fault types and discarded the counters during shutdown, so the exact earlier event cannot be proven from those logs. The updated sketch uses normal CAN arbitration retries, with a **20 ms packet deadline**. A deadline or actual bus/TX/RX fault still clears TX and stops the driver. It preserves the fault mask, bus/TX failure counters, and arbitration count in telemetry. It never automatically restarts motion after a fault. [Espressif TWAI documentation](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s3/api-reference/peripherals/twai.html).

The bridge also permits up to **20 ms** for a recently written USB setup/stop message to drain. Previously any nonempty USB output buffer was immediately called congestion, which could reject normal back-to-back startup commands. Persistent backlog is still cleared and rejected; old speed requests are never replayed.

## Firmware

Use [rs5_wireless_control](../firmware/rs5_wireless_control/README.md) on the body master and [rs5_anchor_radio](../firmware/rs5_anchor_radio/README.md) on the camera for wireless UWB. The combined [rs5_manual_control](../firmware/rs5_manual_control/README.md) remains the direct-UART option. The UI requires protocol v2 with a reported 60°/s manual capability; UWB follow also requires the `uwb` capability/status object. Older v2 firmware remains manual-only; it will not enable against older console formats or the earlier 10°/s manual firmware. Boot starts in normal CAN mode (acknowledging frames) with no application commands; Connect starts joint queries. Fresh held mouse/L1 input automatically requests a zero-speed control session, waits for the device to acknowledge it, and then forwards new input; release ends the session. The bridge never generates motion from cached input. The app does not change stored RS5 settings or limits.

## Demo and checks

No hardware needed for a preview; Connect selects a simulated Feather:

```sh
python3 control_ui/server.py --demo --port 8766
```

The preview has an always-visible **PREVIEW MODE** banner and never opens a serial device.

```sh
python3 -m unittest discover -s control_ui/tests -v
node control_ui/tests/input_test.mjs
node control_ui/tests/app_test.mjs
node --input-type=module --check < control_ui/static/app.js
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer firmware/tests/manual_control_test.cpp -o tmp/manual_control_test
./tmp/manual_control_test
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Ifirmware/tests/manual_stubs firmware/tests/manual_transport_test.cpp -o tmp/manual_transport_test
./tmp/manual_transport_test
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Ifirmware/tests/manual_stubs firmware/tests/uwb_transport_test.cpp -o tmp/uwb_transport_test
./tmp/uwb_transport_test
```

Tests cover analog shaping, dead zones, L1/Circle mapping, zero on release, the 60°/s speed cap, repeated pan wraparound and angles outside the former travel limits, USB/browser input expiry, replay/late-input rejection, single-tab ownership, same-origin access, and a full simulated WebSocket hold/move/release/disconnect lifecycle. A DOM/event fixture runs the actual frontend through mouse and L1 gestures without a browser or hardware. The actual sketch is also exercised against a fake TWAI driver for arbitration retry, deadline expiry, TX clearing, and preserved fault diagnostics. The C++ protocol check uses DJI's independent SDK vector.

For a real, **read-only** check through the running server (close the UI tab first):

```sh
python3 control_ui/check_connection.py --port /dev/cu.usbmodem2101 --log tmp/rs5-ui-query-check.jsonl
```

This sends Connect/Disconnect only, verifies live angles, and asserts movement remains disabled. Browser visual inspection and a physical PS4 controller test require a browser/controller available to the operator; they are not substitutes for the automated tests. Actual 60°/s movement and full-turn operation remain for a supervised hands-on test; the automatic hardware check only reads telemetry.

### Hardware check — 2026-09-28

Manual-control firmware upload and flash hash verification passed. The local UI bridge received 15 validated joint replies with yaw −24.6°, pitch −2.7°, and roll +1.7° at the end of the read-only check. All sampled CAN errors, missed frames, overruns, REC, and TEC were zero. Every sampled state was disarmed with zero pan/tilt requested speed. The check then disconnected and ended queries. Log: `tmp/rs5-ui-query-check.jsonl` (ignored by Git).

Thirteen Python unit/integration checks, Node input-mapping checks, JavaScript syntax validation, and sanitized C++ control/protocol tests passed. No browser backend was available for visual verification, and no physical PS4 input or new manual movement was exercised by the automated check.

A subsequent connection remained unavailable because the Feather reported `REFUSED reset required after CAN fault`. Resetting the Feather restored communication: 15 valid joint replies, zero errors/loss/REC/TEC, and movement disabled. Recovery log: `tmp/rs5-ui-recovery-check.jsonl`. The underlying transient that triggered the latched fault was not captured. The UI now reports this fault explicitly rather than displaying a generic waiting message; diagnostics and message regression checks passed.

### 60°/s and unrestricted software travel update

The operator requested removal of the app's angle limits and a 60°/s maximum. Firmware, bridge, UI, and simulator were updated together; the slider defaults to 10°/s. Fifteen Python tests, Node input tests, JavaScript syntax checks, and sanitized C++ tests passed, including three simulated pan revolutions, reverse wraparound, and SDK joint replies outside ±180°. The firmware was flashed and hash-verified. A read-only check reported protocol v2 and a 60°/s capability, with 15 valid replies and zero errors/loss/REC/TEC while disarmed. Yaw was −10.0°, roll +1.7°, and pitch +5.3° in the final sample. Log: `tmp/rs5-ui-60dps-check.jsonl`. No automatic full-speed or full-turn movement was performed.

### Automatic input and connection reliability update

Removed the Enable/Stop buttons and mouse/PS4 selector. Fresh held input selects the device automatically, and release stops and relinquishes control. L1 remains mandatory for controller movement. The existing 60°/s cap, analog response, and absence of added travel limits remain. Tests cover held-at-connect, controller replacement, competing devices, focus loss, fault recovery, and release acknowledgements; no still-held gesture can restart after a fault.

The new CAN arbitration policy and persistent fault telemetry were flashed and hash-verified. A normal USB output-drain interval was also fixed after the first hardware connection attempt exposed a startup race. Twenty Python unit/integration tests, Node mapping/router tests, a test of the actual frontend with DOM/event fixtures, JavaScript syntax checks, and the sanitized test of actual firmware transport logic passed.

A **90-second read-only hardware check received 900 validated joint replies**, with zero bus errors, TX failures, receive drops, overruns, REC or TEC. It stayed disarmed with zero requested speeds and disconnected afterward. No arbitration-loss events occurred during that check, so the original intermittent fault cause remains an inference supported by the single-shot configuration and Espressif's documented behavior. The bounded retry/deadline paths were exercised in the fake-TWAI test. Log: `tmp/rs5-ui-can-retry-soak.jsonl`. Actual mouse/PS4 motion and full-speed rotation remain for the operator; no automatic motion was used for this check.

### Combined UWB update — 2026-09-29

UWB follow is integrated into the same UI, bridge, and Feather firmware as mouse/PS4 control. In the wired build, factory Makerfabs UART input goes to RX/GPIO38; CAN wiring stays GPIO5/6. X toggles follow with a fresh tag, while manual input cancels it. The UI includes tag selection, live range/bearing, separate direction/center/speed settings, and an expandable connection layout. See [UWB setup and validation](UWB_SETUP.md). The combined wired build was flashed and hash-verified on 2026-09-29. On 2026-10-04 the user confirmed successful wired tracking through the anchor, ESP32, Waveshare and Mill-Max harness. The newer two-ESP32 wireless connection is implemented and software-tested; physical wireless following remains to be checked.
