# Makerfabs AoA → Feather → RS5 pan tracking

This sketch adds a first pan-tracking controller for the **Adafruit Feather ESP32-S3, 8 MB flash / no PSRAM**, the **original Makerfabs MaUWB STM32 AoA Development Kit**, and the existing Waveshare CAN interface. It is a prototype: compiled and tested on the host, but **not yet tested with the physical kit or RS5**. RS5 connector verification in the [main project](../../README.md#termination-and-first-electrical-checks) remains necessary.

The default build is **preview only**: UWB input and proposed steering are displayed, and CAN stays listen-only. A separate, explicitly enabled build adds joint-angle queries and manually armed motion. Flash this sketch or the [CAN monitor](../README.md) separately; they are alternative Feather applications.

## Which board goes where

- **Anchor:** the board with the UWB-X3-AOA two-antenna module. Mount it on the moving camera platform so it pans with the lens, close to the lens centerline. It measures the direction to the tag.
- **Tag:** the board with the UWB-X3-MAX module. The subject carries it, with the antenna unobstructed.
- **Feather:** reads the anchor's measurements and computes yaw speed, then uses the Waveshare transceiver to communicate with the RS5.

Both Makerfabs boards use STM32F103C8T6 controllers. Keep their factory anchor/tag firmware initially: it already performs UWB ranging and AoA processing and sends results over UART. **No STM32 reflashing or ST-Link is needed for this version.** The new tracking program runs on the Feather.

The anchor must rotate with the camera for this controller's bearing feedback to work. A fixed handle-mounted anchor needs a different coordinate transform and camera-orientation feedback. Align the antenna measurement plane horizontally and keep the camera approximately level for the initial test. Avoid placing the antennas directly against the camera's metal body. Rebalance the gimbal after mounting the anchor, and route power/signal cables with clearance throughout the intended motion.

This kit measures **one angular dimension**, not both azimuth and elevation. The controller changes yaw only; it does not follow vertical movement, center a face in the image, or search through a full revolution. Makerfabs specifies approximately ±60° coverage and ±5° angular error; these are vendor specifications, not measurements here. Start with a wide lens and broad framing. A tag outside the valid field causes disarming, requiring manual reacquisition and re-arming.

## Wiring

Keep the existing CAN wiring:

| Feather pad label | Connect to |
| --- | --- |
| **5 / GPIO5** | Waveshare CAN TX |
| **6 / GPIO6** | Waveshare CAN RX |
| **3V** | Waveshare 3.3V |
| **GND** | Waveshare GND and verified RS5 GND |

Add only these two wires for the anchor:

| Makerfabs anchor | Feather |
| --- | --- |
| **TXD1**, STM32 PA9, schematic J2 pin 4 | **RX**, GPIO38 |
| **GND**, schematic J2 pin 2 | **GND** |

Use the actual board silkscreen and the schematic to identify the header: the pin numbers above are schematic identities, not a left-to-right count from an unspecified viewing angle. They come from the vendor's **V1.1** schematic; confirm the revision of the kit that arrives.

Leave the Feather **TX / GPIO39 unconnected**. Do not connect to anchor RXD1/PA10: its onboard CH340 USB-to-UART chip already drives that input. The program receives at **115200 baud, 8 data bits, no parity, 1 stop bit**, with no flow control, and never sends anchor commands on this wire. TX-to-RX is correct here because this is UART; the CAN transceiver's TX/RX wiring has different signal roles.

Power the Feather and anchor through their own USB connectors, with common ground. Power the tag independently by its USB-TTL connector or supported battery. Do not join their 3.3 V/5 V supply rails, and keep RS5 accessory VCC disconnected. The Feather's 3V output powers only the CAN breakout in this setup.

Before joining TXD1 to the Feather, disconnect power and use continuity mode to verify the selected ground/header pads against the schematic. Then power the anchor alone and use DC-voltage mode to check TXD1 relative to GND: this is a **3.3 V logic** signal, not RS-232 or a 5 V output. A meter may show an average while data is flowing; it cannot decode UART or prove its voltage peaks. Do not use resistance mode on powered boards. Remove power before making the final wiring connection.

## Prepare the Makerfabs kit

First verify ranging using the factory setup, independently of the RS5:

1. Connect the anchor's **USB-NATIVE** port to the computer; power the tag through USB-TTL or a supported battery.
2. In Makerfabs' **AOA System** application, discover and bind the actual tag. Keep **one anchor paired to one tag**; the vendor specifically warns that multiple bindings prevent normal operation.
3. Verify that displayed distance and horizontal bearing change as the tag moves. Use the factory calibration initially. Do not substitute raw phase (`P`) for bearing.
4. Ensure the anchor emits **JSON mode (`USER_CMD 0`)**. This receiver does not implement the alternative binary output format.

The stock anchor also accepts these text commands through its USB-NATIVE serial port, useful on systems without the vendor GUI. Use 115200 and **CR+LF** line endings. Send commands one at a time and read their replies:

```text
GETDLIST
GETKLIST
USER_CMD 0
SAVE
```

`GETDLIST` reports discovered 64-bit tag addresses and clears that discovery list; `GETKLIST` reports saved bindings including `a16`, the short address used by the Feather. If there is no binding, the stock command syntax is:

```text
ADDTAG YOUR_16_HEX_DIGIT_TAG_ADDRESS 1234 1 1 0
GETKLIST
SAVE
```

Replace the long-address placeholder with the tag actually discovered. `1234` is an example chosen short address, followed by fast/slow multipliers of 1 and mode 0. Do not add another binding when one already exists. Configure the anchor while the tracker is disarmed; configuration replies can interrupt the measurement stream.

Verify regular reports while the tag is stationary as well as moving, preferably every 100 ms or faster. The tracker requires three valid reports and stops accepting a fix after 300 ms without a new report. If updates are too slow, investigate the actual firmware/configuration before increasing that timeout. Factory revisions can differ; this implementation follows the source revision listed below.

## Build and test the preview first

Open [rs5_uwb_tracker.ino](rs5_uwb_tracker.ino) in Arduino IDE with **Adafruit Feather ESP32-S3 No PSRAM** selected, Arduino-ESP32 **3.3.11**, 8 MB flash, TinyUF2 8 MB partition, TinyUSB and USB CDC enabled. No additional Arduino libraries are required. Keep `RS5_ENABLE_MOTION` at **0**. Board selection/upload details are in the [CAN monitor instructions](../README.md#flash-with-arduino-ide).

From the repository root, the equivalent CLI build is:

```sh
arduino-cli compile \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --warnings all \
  --build-path "$PWD/tmp/tracker-preview-build" \
  --output-dir "$PWD/tmp/tracker-preview" \
  firmware/rs5_uwb_tracker

arduino-cli board list

arduino-cli upload \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --port YOUR_FEATHER_PORT \
  --input-dir "$PWD/tmp/tracker-preview" \
  firmware/rs5_uwb_tracker
```

Upload with the gimbal disconnected. Replace `YOUR_FEATHER_PORT` with the detected Feather port, not the anchor's port. Open the Feather's USB serial monitor at 115200, with newline or CR+LF endings. The program announces each observed tag until you explicitly select one:

```text
tag 1234
status
```

Use the real `a16` displayed by the anchor or the Feather. Selection is deliberately not automatic and is not saved across resets. Status repeats every 500 ms and includes bearing, range, proposed yaw speed, and tracking state. Missing joint telemetry is expected in preview mode.

With CANH/CANL still disconnected from the RS5:

1. Place the tag about 2–5 m ahead, at roughly the anchor's height. Check that the selected tag reaches `state=tracking` and bearing is near zero at the center of the lens view.
2. Move the tag gently left and right. Check that bearing crosses zero and the proposed speed changes sign. Rotation of the anchor toward a stationary tag should reduce the bearing magnitude.
3. Turn off the tag. The reported state should become stale and proposed speed return to zero. Bring it back and verify three new reports are required. Repeat with tag movement near the edge of the valid field.
4. Confirm that `probe` and `arm +` are refused by this build. No command enables CAN transmission in preview firmware.

If center alignment has a consistent offset, measure it over several seconds and set `zeroDegrees` in [TrackingCore.h](TrackingCore.h), then rebuild. That compensates mounting offset, not varying radio multipath or parallax. Validate the installed mount with the camera powered, since its body and nearby electronics can affect measurements.

## Optional motion build, after electrical and preview checks

The preview cannot move the gimbal. To build the separate motion-capable version:

```sh
arduino-cli compile \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --warnings all \
  --build-property compiler.cpp.extra_flags=-DRS5_ENABLE_MOTION=1 \
  --build-path "$PWD/tmp/tracker-motion-build" \
  --output-dir "$PWD/tmp/tracker-motion" \
  firmware/rs5_uwb_tracker
```

Upload using the preview command with `--input-dir "$PWD/tmp/tracker-motion"`. In Arduino IDE, the equivalent is changing the sketch's default `#define RS5_ENABLE_MOTION 0` to `1`. Both builds boot **listen-only and disarmed**.

Start with a balanced gimbal on a stable stand, unlocked for normal operation, with a clear motion area and accessible RS5 power control. Confirm the connector, termination, CAN bitrate and ground using the [electrical procedure](../../README.md#termination-and-first-electrical-checks). Select the tag, then send:

```text
probe
status
```

`probe` changes to normal CAN mode and sends **joint-angle queries only**, every 200 ms. It acknowledges CAN traffic but does not request movement. Require `joint=fresh` and plausible yaw values that change correctly with gimbal orientation. Replies must match the outstanding query sequence, both DJI CRCs, command, successful return code, and joint-angle data type. A quiet passive bus can be inconclusive; failed queries are not grounds to bypass these checks.

For the first movement, put the tag only about 5–10° off center, with the gimbal near its center. Send `arm +`, observe briefly, then `stop`. The camera should move toward the tag and reduce bearing magnitude. If it moves away, stop immediately and use `arm -` for the next small test. This explicitly calibrates the relationship between the installed sensor orientation and DJI's yaw direction; it is not established by the source code. Confirm both sides and verify `stop`, tag power loss, and subsequent manual re-arming before trying continuous tracking.

| Command | Effect |
| --- | --- |
| `tag HHHH` | Disarm, select that short tag address, reacquire three reports |
| `status`, `help` | Show state or commands |
| `probe` | Motion build only: enter normal CAN and query joint angles; remain disarmed |
| `arm +`, `arm -` | Motion build only: arm with selected direction after fresh tag/joint checks |
| `stop` | Disarm, request zero speed, then release speed control |
| `listen` | Disarm and return to listen-only CAN |

No autonomous startup or re-arming is implemented. Closing the USB monitor **does not stop an already armed tracker** if the Feather remains powered. Use `stop` before disconnecting the console; keep the physical RS5 power control available during tests.

## Control behavior and limitations

The controller calculates `atan2(Xcm, Ycm)` from the anchor's processed coordinates, applies a 0.25 s smoothing time constant and 3° deadband, then requests yaw speed proportional to the remaining angular error. Defaults cap speed at **15°/s**, acceleration at **45°/s²**, valid bearing at **±55°**, and distance at **0.75–20 m**. Arming starts speed from zero. Commands repeat at 20 Hz with roll/pitch speed zero. Tuning constants are in `TrackingCore.h`.

Stale tag data (>300 ms), invalid selected-tag range/bearing, large angular jumps, malformed/partial UART reports, serial overflow, stale joint telemetry (>500 ms), loop stalls, and CAN faults disarm. Duplicate and recent out-of-order ranging sequences cannot refresh the fix. Unselected tags do not refresh it either. Disarming normally sends zero-speed and release packets. A CAN fault stops the driver and discards its queued commands; it may make sending a stop impossible.

The legacy DJI SDK states that each speed command lasts at most **0.5 s**. This fallback behavior, stop/release behavior, yaw direction, and RS5 compatibility all require physical verification. Source review and host tests do not prove that a real gimbal will stop on a broken connection. The ±85° joint-yaw envelope, slowing within 10° of its edge, is a software bound relative to reported gimbal joints; it is not a guaranteed mechanical/cable limit. Adjust it to the actual mounting clearance after measurement. The program does not modify the RS5's stored modes or limits.

The Makerfabs JSON link has length framing but **no checksum**. The parser validates structure, required fields, bounds and sequence progression; plausible corrupted readings or multipath errors can still pass. This is supervised prototype tracking, not a guarantee that the subject will always remain framed. RF occlusion, limited field of view, vertical movement and a rapidly moving subject can all break tracking.

## Validation and source references

Host tests exercise vendor-format input, truncated/malformed packets, integer bounds, noise, sequence and timer wraparound, tag selection, stale-data stopping, rate limits, and a simulated yaw plant. DJI packet generation is checked against the independent SDK v2.5 section 3.3 example; every single-byte corruption of a sample reply is rejected. The simulator checks controller behavior, not RF performance or motor dynamics.

```sh
mkdir -p tmp
c++ -std=c++17 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  firmware/tests/tracker_test.cpp -o tmp/tracker_test
tmp/tracker_test
```

Both preview and motion builds compile for the exact Feather profile with Arduino-ESP32 3.3.11. The core emits its existing command-line warning about the default hyphenated TinyUF2 partition macro. No hardware was flashed during development.

- [Makerfabs product](https://www.makerfabs.com/mauwb-stm32-aoa-development-kit.html) and [official repository](https://github.com/Makerfabs/UWB-AOA-with-Display-STM32F103C8T6): original STM32 kit, not Gen2 or the unrelated AT-command ranging modules.
- Source reviewed at commit `34b9705edcb7feca83f652280047847d4bc03c34`: `Firmware/stm32cube/CubeIde/projectN/DW/PDoA/json_2pc.c` defines `JS` + four ASCII hex length digits + JSON + CR/LF. `D`, `Xcm`, `Ycm` are centimeters; `P` is phase data, and `X/Y/Z` are acceleration fields. `DW/Examples/ds_twr_sts_sdc_responder.c` supplies processed coordinates. `APP/Generic_cmd.c` mirrors output to USB/UART; `Core/Src/usart.c` configures USART1 at 115200. The stock Keil `Anchor.zip` contains the same UART/JSON output path.
- Vendor `Hardware/UWB AOA with Display STM32F103C8T6 V1.1.sch`: J2 pin 4/TXD1 connects to STM32 PA9 and the CH340 receive input; J2 pin 3/RXD1 connects to PA10 and CH340 transmit; J2 pin 2 is GND.
- [Adafruit Feather pinout](https://learn.adafruit.com/adafruit-esp32-s3-feather/pinouts) and Arduino-ESP32's `adafruit_feather_esp32s3_nopsram` variant: RX=38, TX=39, GPIO5/6 chosen for CAN.
- [Local DJI SDK v2.5](../../references/dji-r-sdk-v2.5.pdf), sections 2.3.4.2–3 and 3: speed control, joint queries, CAN fragmentation and CRCs. The official SDK archive's CRC headers initialize their reflected implementation with `0x3aa3` (CRC16) and `0x00003aa3` (CRC32), the reflected forms of the PDF's XorIn values. The known-answer test checks this distinction.
