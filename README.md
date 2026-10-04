# DIY RS5 side-port CAN connector - prewired Mill-Max prototype

Build a removable spring-contact adapter on the RS5's electrical RSA/NATO port, with the battery grip attached. The design uses the **prewired Mill-Max 889-22-008-70-501010**, held between two printed parts. The RS5 adaptation retains this part; no different contact block is indicated by the compatibility evidence below. Use an ESP32 with a 3.3 V CAN transceiver, powered from USB for the first version.

**Status: the Mill-Max interface and wired UWB tracking work on the user's RS5.** Confirmed by the user on 2026-10-04: the direct `UWB anchor → ESP32 → Waveshare → Mill-Max pin harness → RS5` connection successfully tracked the tag. The new two-ESP32 wireless path is implemented and software-tested; pairing and following over that radio link still need a hardware check. Camera-enclosure fit, combined-battery operation and charging remain separate, unverified work.

Mill-Max dimensions come from its family drawing; nominal mounting geometry comes from the RS2 community design. The reference pinout, voltage and detect-resistor details below come from DJI R SDK v2.5's RS2 illustration. The working RS5 setup establishes this build's interface, but is not a measurement of accessory VCC or every mechanical tolerance. Keep the proven harness wiring and termination when adding wireless.

Geometry validation: base and cover each export as one closed solid; the combined print plate contains two closed solids. OpenSCAD's solid intersection between the assembled holder and the nominal Mill-Max connector envelope is empty (no solid interference). See [mesh checks](design/mesh-checks.json). This check covers the nominal connector geometry, not the RS5 or manufacturing tolerances.

## UWB through the camera and RS5 ESP32s

The intended wireless path is implemented, with the **RS5-body ESP32 as master** and the camera ESP32 as an optional UWB peripheral:

```text
Subject tag --UWB--> camera-mounted STM32 anchor
  --short UART TXD1/GND--> camera ESP32 (rs5_anchor_radio)
  --paired ESP-NOW radio--> RS5-body ESP32 (rs5_wireless_control)
  --GPIO5 TX / GPIO6 RX--> Waveshare SN65HVD230
  --CAN-H / CAN-L--> Mill-Max harness --> RS5
```

The camera ESP32 reads the anchor on **RX/GPIO38 at 115200 baud** and relays measurements. The body ESP32 runs the existing tracking controller and sends DJI commands over **1 Mbit/s CAN**. ESP-NOW needs no router. Move the anchor's UART/ground connection to the camera ESP32; keep the working body-side Waveshare/Mill-Max circuit. The camera and body have separate power, with no UART, power or ground cable crossing the moving axes. See [wireless wiring and power](firmware/WIRELESS_UWB.md).

The master polls the paired camera every 50 ms and owns tag selection, tracking, motion limits and all CAN commands. Manual mouse/PS4 control works with the camera off, absent, or not yet paired. After the one-time pairing below, powering the camera and tag automatically makes their reports available to the master; no body reflash or mode change is needed on each connection. Control Desk shows **NO UWB**, **TAG DETECTED**, then **TAG LIVE** once the selected tag has a valid fix. Detection offers follow but never starts it or takes over manual control. Losing UWB stops active follow; manual control remains available, and recovery requires a fresh follow action.

**The body still uses USB to the existing Control Desk.** It boots disarmed; apply your tag settings, wait for TAG LIVE, then press PS4 **X** or **Start UWB follow**. This firmware does not start autonomous follow when powered on. The camera ESP32 can run without a USB host. The Makerfabs anchor and tag keep their existing factory firmware and binding.

## Flash both ESP32s

Both targets are **Adafruit Feather ESP32-S3, 8 MB flash / No PSRAM**, using **esp32 by Espressif Systems 3.3.11**. Keep the full repository layout because the sketches include shared files.

| Physical board | Sketch to flash |
| --- | --- |
| On the moving camera, beside the UWB anchor | [rs5_anchor_radio.ino](firmware/rs5_anchor_radio/rs5_anchor_radio.ino) |
| On the RS5 body: master, connected to Waveshare CAN | [rs5_wireless_control.ino](firmware/rs5_wireless_control/rs5_wireless_control.ino) |
| Original single-ESP32 wired setup, if restoring it | [rs5_manual_control.ino](firmware/rs5_manual_control/rs5_manual_control.ino), on the body ESP32 |

**First-time pairing takes two uploads per ESP32:** first discover their MAC addresses, then generate the shared pairing file and rebuild/upload both. Without `firmware/shared/WirelessConfig.h`, both wireless sketches run for MAC discovery with `radio_ready=0`; UWB radio operation is disabled. If that file already exists for these exact two boards, keep it and go directly to the configured builds/uploads.

### 1. Prepare the tools and identify the ports

Run commands from the repository root. With [Arduino CLI installed](https://arduino.github.io/arduino-cli/latest/installation/), install the tested core if needed:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli board list
```

Connect each Feather through its own USB-C connector, one at a time, and label it **camera** or **body**. Replace `CAMERA_FEATHER_PORT` and `BODY_FEATHER_PORT` below with their actual ports (for example `/dev/cu.usbmodem...` on macOS). Do not select the Makerfabs anchor's port. Close Control Desk's serial connection and any serial monitor before uploading. For flashing, leave the RS5 powered off; neither sketch automatically starts motion.

### 2. Build, flash and read each MAC address

**Camera ESP32:**

```sh
arduino-cli compile \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram --warnings all --clean \
  --build-path "$PWD/tmp/build-rs5_anchor_radio" \
  --output-dir "$PWD/tmp/rs5_anchor_radio" firmware/rs5_anchor_radio
arduino-cli upload \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --port CAMERA_FEATHER_PORT --input-dir "$PWD/tmp/rs5_anchor_radio" \
  firmware/rs5_anchor_radio
arduino-cli monitor --port CAMERA_FEATHER_PORT --config baudrate=115200
```

Record the `camera mac=AA:BB:CC:DD:EE:FF` address printed once per second. Close the monitor with Ctrl+C.

**RS5-body ESP32:**

```sh
arduino-cli compile \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram --warnings all --clean \
  --build-path "$PWD/tmp/build-rs5_wireless_control" \
  --output-dir "$PWD/tmp/rs5_wireless_control" firmware/rs5_wireless_control
arduino-cli upload \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_nopsram \
  --port BODY_FEATHER_PORT --input-dir "$PWD/tmp/rs5_wireless_control" \
  firmware/rs5_wireless_control
arduino-cli monitor --port BODY_FEATHER_PORT --config baudrate=115200
```

Type `help` and press Enter. Record the `body mac=AA:BB:CC:DD:EE:FF` address, then close the monitor. These are the **Wi-Fi STA MAC addresses**, not UWB tag IDs or Bluetooth addresses. USB ports can change after flashing; run `arduino-cli board list` again and update the port if needed.

### 3. Pair, then rebuild and flash both again

Replace the MAC placeholders with the two recorded addresses:

```sh
python3 tools/configure_wireless_pair.py --camera CAMERA_STA_MAC --body BODY_STA_MAC
```

This creates ignored `firmware/shared/WirelessConfig.h` with the peer addresses, random encryption keys and channel 6. It refuses to overwrite an existing pairing. Keep this file for subsequent updates to the same pair; only move it aside deliberately when replacing/re-pairing boards. Both builds must use the same file.

**Repeat both compile/upload blocks from step 2 after generating the file.** The `--clean` builds ensure the new pairing configuration is included. Uploading the earlier discovery binaries again will leave the radios disabled.

### 4. Verify the link and enable follow

With both nodes powered, check camera serial output for `radio_ready=1`. In the body's JSON status, the `uwb` object should contain `"transport":"esp-now"` and `"radio_ready":true`. These flags confirm local radio initialization; live tag reports on the body prove measurements cross the wireless link.

Close the serial monitors, power the RS5, and start the existing UI:

```sh
python3 control_ui/server.py
```

Open **http://127.0.0.1:8765**, connect to the **body** ESP32, wait for LIVE RS5 angles, apply your tag ID/direction/offset and **5°/s** follow speed, then wait for **TAG LIVE**. Press PS4 **X** or **Start UWB follow**. Retain the settings that worked in the wired test; confirm the camera turns toward the tag. Test camera power loss/tag loss: follow should stop and require a fresh enable action after reports return. [Control Desk setup and controls](control_ui/UWB_SETUP.md#controls-and-first-follow-test).

### Arduino IDE and upload recovery

In Arduino IDE, install **esp32 by Espressif Systems 3.3.11**, then open the camera or body `.ino` from the table. Select **Adafruit Feather ESP32-S3 No PSRAM** and its USB port. Keep **8MB**, **QIO 80MHz**, **TinyUF2 8MB (2MB APP/3.7MB FATFS)**, **USB-OTG (TinyUSB)**, **USB CDC On Boot: Enabled**, and **Upload Mode: USB-OTG CDC (TinyUSB)**. Upload each discovery sketch and use Serial Monitor at **115200**, with a newline for the body's `help` command. Generate the pairing file with the Python command above, then compile/upload both again; the CLI `--clean` commands above provide a clean rebuild after adding the file.

If a Feather has no upload port, hold **BOOT**, tap **RESET**, release **BOOT**, then select its new port. For CLI upload from the ROM bootloader, add `--board-options UploadMode=default` to the upload command if the TinyUSB reset step fails. Press RESET after uploading from ROM mode, then rediscover the application port. [Adafruit's native-USB upload/recovery guide](https://learn.adafruit.com/adafruit-esp32-s3-feather/using-with-arduino-ide).

## Changes from RS2

- **Use the one electrical RSA/NATO port.** The RS5 manual identifies it as item 17, next to the joystick-mode switch (item 25). The other side's NATO rail, item 12 near the power button, has no charging or communication. Identify the exposed contacts rather than relying on an ambiguous left/right description.
- **Retain Mill-Max 889-22-008-70-501010 and the ESP32/CAN-transceiver circuit.** DJI lists the same Focus Wheel, supplied with two M4 screws, for RS2 and RS5. That is strong evidence of a compatible legacy RSA interface; it is not a dimensional drawing for this DIY part.
- **No evidenced geometry change.** The RS5 base and cover deliberately retain revision-B dimensions. The new RS5 files, fit gauge and port instructions make that carry-over explicit. Confirm 19.85 mm nominal screw spacing, eight-pad alignment, pad recess, screw depth and cable-ear clearance on your RS5; those values were not measured on one here.
- **Keep USB power for the controller.** Leave both accessory VCC leads insulated. The older SDK's 8 V accessory output is not an RS5-specific voltage guarantee; the RS5 battery's 11.2-17.8 V specification is a different circuit.
- **The electrical port is occupied by this adapter.** The RS5 Electronic Briefcase Handle cannot use that same mounting point simultaneously. DJI's special warning about fitting that new handle to older gimbals does not change the legacy Focus Wheel compatibility evidence.

## Files

- [Wireless camera-to-body electronics](firmware/WIRELESS_UWB.md): camera-side STM32 anchor plus its own Feather ESP32-S3 share one 3.7 V / 500 mAh battery; paired ESP-NOW sends measurements to the body-side Feather/CAN controller. Includes firmware, Feather USB charging, a direct positive-lead battery switch and pairing instructions. Revision G models the stacked Feather/STM32 mounts and Feather charging port; charging with the anchor load and other hardware behavior remain unverified.

- [Print both parts](design/rs5-millmax-print-plate.stl): base and rear cover, already oriented for printing at 100% scale in millimeters.
- [Base STL](design/rs5-millmax-base.stl) and [rear cover STL](design/rs5-millmax-cover.stl), if printing separately.
- [RS5 OpenSCAD entry point](design/rs5-millmax.scad), using [shared adjustable geometry](design/rsa-millmax.scad): select `assembly` or `exploded` for inspection. Requires OpenSCAD 2021.01 or newer.
- [1.2 mm fit gauge](design/rs5-fit-gauge.stl): print first; hold against the unpowered port by hand, without contacts or screws. It checks the footprint and alignment, not pad depth or preload.
- [Prototype preview](design/carrier-preview.png): base, cover, and assembled connector.
- [RS5 carrier alias](design/rs5-carrier.stl) is the same two-part print plate. The original [RS2 entry point](design/rs2-millmax.scad) and RS2 STLs remain available; the default holder geometry is unchanged.
- [Archived PRECI-DIP design](design/archive/rs2-precidip-rev-a.scad): superseded; its dimensions are not for Mill-Max.
- [Original community STL](references/rileyharmon-rs2-connector.stl): provenance for the mounting dimensions only.
- [RS5 wiring diagram](design/wiring.png), with the inherited-pinout limitation marked.
- [Feather ESP32-S3 CAN test firmware](firmware/README.md): Arduino listen-only monitor, GPIO5 TX / GPIO6 RX, serial diagnostics and flashing instructions for the Adafruit 8 MB / no-PSRAM board.
- [Finite RS5 motion bench test](firmware/rs5_can_motion_test/README.md): telemetry-gated three-cycle pan test, verified on the connected RS5 on 2026-09-28; no automatic motion on boot.
- [RS5 Control Desk](control_ui/README.md): local mouse / PS4 controller UI with **X-to-toggle UWB follow** ([connection layout and setup](control_ui/UWB_SETUP.md)) for analog pan and tilt, 1–60°/s speed selection with no added travel limits, live angles, and automatic hold-to-move with release-to-stop; companion [manual-control firmware](firmware/rs5_manual_control/README.md).
- [Standalone Makerfabs UWB camera-tracking prototype](firmware/rs5_uwb_tracker/README.md): camera-mounted AoA anchor, subject-carried tag, UART on Feather RX/GPIO38; preview by default, optional manually armed RS5 pan control. Compiled and host-tested; physical operation remains unverified.
- [Makerfabs tag pocket enclosure](design/mauwb-tag/README.md): parameterized OpenSCAD case, fit gauge and print files for a removable 500 mAh battery; external charging, no soldered headers. Vendor PCB dimensions with provisional component heights.
- [Camera-top UWB anchor enclosure](design/mauwb-anchor/README.md): revision G mounts the Feather below the STM32/antenna, adds Feather USB-C access, and preserves the exact 13.5 × 8.4 mm rocker opening and 26.48 mm cage-hole spacing. Both boards have rear M2 screw mounts; Feather front pins and STM32 front-edge keepers provide support. Body 87.7 × 58 × 41.1 mm, overall height 72.81 mm. The rocker switches battery positive directly. **Physical fit/RF and charging with the anchor load are unverified.**
- [RS5 port illustrations](references/dji-rs5-ports.png) and [overview](references/dji-rs5-overview.png), from pages 17 and 6 of the [RS5 user manual](references/dji-rs5-user-manual.pdf).
- [DJI's illustrated pinout](references/dji-sdk-pinout.png), from PDF page 21 / printed page 19 of the [SDK](references/dji-r-sdk-v2.5.pdf).

## Electrical design

The user's Waveshare/Mill-Max wiring has already worked for RS5 control and wired tag tracking. Preserve that harness. For a new or rebuilt harness, identify every contact before applying power; the reference below is inherited from the older SDK, and a fit gauge cannot identify electrical signals.

The inherited RSA contact layout has **eight pads in two rows of four**; check this against the exposed RS5 port. DJI assigns six signal numbers because power and ground each appear twice. The numbers below are DJI's signal labels, not sequential positions in an eight-pin header.

| DJI label | Signal | Connection in this build |
| --- | --- | --- |
| 1, two pads | VCC, nominal 8 V in the older SDK | Leave disconnected; individually insulate both unused wire ends |
| 2 | CAN-L | CAN-L on the transceiver |
| 3 | SBUS_RX | Leave disconnected |
| 4 | CAN-H | CAN-H on the transceiver |
| 5 | AD_COM | 47 kOhm resistor to ground at the controller's terminal block |
| 6, two pads | GND | Transceiver ground and ESP32 ground; one ground contact suffices for this signal-only build |

DJI specifies a 10-100 kOhm accessory-detect pull-down; 47 kOhm is our choice within that range. The SDK explicitly links detection to enabling port power. It does not establish that the resistor is necessary for CAN alone. Do not substitute a direct short.

The **legacy RS2 SDK illustration**, viewed facing the gimbal contacts in its illustrated orientation, gives this arrangement. This is a reference for tracing signals, not a verified upright view of the RS5:

```text
        GND       AD_COM     SBUS_RX     8 V
        GND       CAN-H      CAN-L       8 V
```

Use the [illustrated DJI reference](references/dji-sdk-pinout.png) to understand its viewing direction. Do not transfer its pictured RS2 body orientation directly to RS5. The older SDK's remark about two rotationally symmetric side ports applies to RS2; RS5 has only one electrical RSA port. Confirm the RS5 ground column against a known ground with the battery and USB disconnected; if orientation cannot be established, stop before energizing and obtain an RS5-specific reference. The adapter's mating face also reverses left/right relative to a face-on view of the gimbal. Mark the ground column and identify each wire by continuity to its contact. The Mill-Max wires are all black; do not infer signal identity from wire color or the order of the loose ends.

Recommended controller interface:

```text
ESP32 TWAI TX ---- transceiver D / TXD
ESP32 TWAI RX <--- transceiver R / RXD
ESP32 3V3 ------- transceiver VCC
ESP32 GND ------- transceiver GND ------- RS5 GND
                  transceiver CANH ------ RS5 CAN-H
                  transceiver CANL ------ RS5 CAN-L

RS5 AD_COM ------- 47 kOhm ------- RS5 GND
```

An SN65HVD230 breakout is suitable: it operates from 3.3 V and supports 1 Mbit/s. Use high-speed mode (RS low, per TI's datasheet), local supply decoupling, and check whether the module already contains a termination resistor. ESP32 GPIOs cannot connect directly to CAN-H/L. ESP32's built-in TWAI controller supplies the controller function; the transceiver supplies the electrical interface. Select GPIOs appropriate to your particular ESP32 board. A classic Arduino Uno instead needs a CAN controller as well as a transceiver.

For the **Adafruit Feather ESP32-S3, 8 MB flash / no PSRAM**, all body-side sketches use **pin 5 (GPIO5) to Waveshare CAN TX** and **pin 6 (GPIO6) from Waveshare CAN RX**, with the Feather's 3V output and common GND. GPIO4 is the Feather's I2C SCL line and is left available for its onboard devices and STEMMA QT. The [CAN monitor](firmware/README.md) is a passive diagnostic; the wired and wireless control sketches use normal 1 Mbit/s CAN and send DJI commands when operated through Control Desk.

The connector has approximately 203 mm (8 inches) of 24 AWG leads. Identify and gently twist the CAN-H/L pair, keeping a short transition at the connector. Run ground alongside the pair. The detect lead goes to a terminal with the 47 kOhm resistor to ground. Bare lead ends can connect to a suitable screw-terminal CAN breakout; the pogo block itself needs no soldering or crimping. Anchor the insulated wires to the rear cover's cable-tie ear, allowing a gentle bend after they leave the holes. Individually insulate unused leads.

## Parts

| Quantity | Part | Selection notes |
| --- | --- | --- |
| 1 each | Printed base and rear cover | PLA for fit checks; PETG or ASA for the working parts |
| 1 | Mill-Max 889-22-008-70-501010 | Prewired, 8 contacts, 2x4, 2.54 mm grid; Mouser stock number 575-8892200870501010 |
| 2 | M4 screws and small washers | Both layers total 11.85 mm nominal, plus washers; choose length after measuring RS5 thread depth |
| 1 | 47 kOhm resistor | Accessory detect to ground |
| 1 | SN65HVD230 breakout | 3.3 V supply, high-speed mode, accessible termination |
| 1 | ESP32 development board | USB powered initially |
| 1 | 120 Ohm resistor and jumper | Fit only if required by the measured bus termination |
| As needed | Terminal connections, insulating sleeves/heat-shrink, small cable tie | No glue is required to retain the pogo block |

Order link: [Mill-Max 889-22-008-70-501010 at Mouser](https://www.mouser.com/ProductDetail/Mill-Max/889-22-008-70-501010?qs=Rp5uXu7WBW9JMC9qfc9GZw%3D%3D). The 889-22 version has spring contacts; the 889-10 version is a different target-contact product.

The rear cover captures the plastic housing and encloses each exposed rear metal section in its own tunnel. All eight wires pass straight through the cover. The housing is retained by the two M4 mounting screws through both printed layers. The locating pins align the cover; they do not carry the spring load.

## Mill-Max dimensions used

See the [manufacturer's family drawing](references/millmax-889-family.pdf) and [enlarged detail](references/millmax-889-drawing-detail.png). The PDF title names the 14-position part; the drawing is explicitly for **889-22-0XX-70-501010** and gives housing length as `number of pins x 0.100 inch / 2`. Applying that formula to 8 contacts gives 10.16 mm. The drawing's inch dimensions are used in CAD to avoid accumulating rounding differences.

| Feature | Drawing dimension | CAD value |
| --- | --- | --- |
| Contact pitch and row pitch | 0.100 in | 2.540 mm |
| Housing length, eight contacts | 8 x 0.100 in / 2 | 10.160 mm |
| Housing width | 0.200 in | 5.080 mm |
| Housing thickness | 0.110 in | 2.794 mm |
| Tip to rear shoulder | 0.283 in | 7.1882 mm |
| Rear shoulder to start of wire insulation | 0.158 in | 4.0132 mm |
| Housing rear to start of wire insulation | 0.204 in, +/-0.010 | 5.1816 mm, +/-0.254 |
| Tip diameter / front fixed barrel | 0.042 / 0.059 in | 1.067 / 1.499 mm |
| Rear shoulder / crimp barrel diameter | 0.067 / 0.062 in | 1.702 / 1.575 mm |
| Maximum spring stroke | 0.055 in | 1.397 mm |

**The 7.19 mm dimension is not measured from the plastic housing's back face.** The correct tip-to-housing-rear distance is `7.1882 + 4.0132 - 5.1816 = 6.0198 mm`. This distinction matters when setting spring compression. The fixed front barrels also need larger holes than the earlier PRECI-DIP tip apertures.

## Mechanical design and fit

The original STL measures approximately 19.80 x 28.85 x 8.00 model units, with 4.3 mm mounting bores about 19.85 mm apart. STL does not encode units; use millimeters as a starting assumption. These are measurements of a community mesh, **not a DJI dimensioned drawing**. The original mesh passed an edge-sharing check for a closed surface.

The RS5 prototype keeps the revision-B mounting footprint but uses a 5.420 mm base plus a 6.432 mm rear cover, approximately **11.85 mm total**. The optional cable-tie ear adds 9 mm to one side of the cover; maximum width is 28.80 mm with the ear enabled. Check its clearance on the RS5; `strain_relief_ear=false` produces a compact 19.80 mm-wide cover.

When mounted and the contacts are loaded, the rear of the connector's plastic housing rests against the cover's inner face. That face sets the spring preload. The front pocket has 0.20 mm axial clearance so the cover does not squeeze a slightly oversize housing. The block can move forward slightly while unmounted; this is intentional. Tighten the M4 screws only enough to close the printed layers and seat the holder, not to compress the plungers to their limit.

Important parameters in `design/rsa-millmax.scad` (or override using OpenSCAD `-D`):

| Parameter | Default | What to verify |
| --- | --- | --- |
| `mount_pitch` | 19.85 mm | Actual M4 hole center spacing |
| `contact_offset_x/y` | 0.02 / 0.22 mm | Contact-array center relative to mounting-hole midpoint |
| `pad_recess` | 0 mm placeholder | Distance from carrier support surface down to contact-pad surface |
| `target_compression` | 0.60 mm | Engineering starting target, subject to actual contact travel and tolerances |
| `pocket_clearance` | 0.40 mm total | Actual pocket fit around the housing, allowing its +/-0.13 mm dimensional tolerance |
| `front_aperture_diameter` | 1.95 mm | Printed holes clear the fixed 1.50 mm front barrels |
| `wire_tunnel_diameter` | 2.10 mm | Printed holes clear rear shoulders and actual wire insulation |
| `axial_clearance` | 0.20 mm | Forward housing play when unmounted |
| `rear_insulation_overlap` | 1.25 mm | Cover extends beyond bare crimp sections onto insulated leads |

The model uses `base_thickness = tip_to_housing_rear - pad_recess - target_compression`. At the placeholder zero recess and 0.60 mm compression, the base is 5.4198 mm thick and the front web is 2.4258 mm. A positive measured pad recess makes the required base thinner. With the housing against the rear cover, free tips extend 0.60 mm beyond the mounting plane; forward play can increase that protrusion by 0.20 mm when unmounted. Measure compression in the mounted condition. The 0.60 mm target leaves nominal room within the 1.397 mm stroke, but component, print, and gimbal tolerances must still be checked together.

Both provided STLs are already oriented for printing: base mating face down; rear cover outer face down, blind locating sockets up. Start with 0.15-0.20 mm layers, four or five perimeters, and 40-60% infill. These are suggested settings, not printer-validated settings. No support is intended. Inspect the slicer preview for the 0.44 mm webs between cover tunnels. Deburr holes and remove first-layer lips. Use plain insulating filament, without carbon or metal additives.

Assembly:

1. Remove the battery grip and disconnect other power. Identify electrical port 17 next to the joystick-mode switch and remove its contact cover. Hold the thin fit gauge against it by hand; check both mounting centers and all eight aperture centers. Do not install screws through the thin gauge. Then fit the empty full-thickness base and cover. Their flat mating faces must close without force. The asymmetric locating pins permit only one cover orientation.
2. Check M4 hole spacing, contact aperture alignment, pad recess, and cable-ear clearance. Adjust SCAD parameters if needed. Do not scale the whole STL: that changes contact pitch. Select M4 screw length using the 11.85 mm assembled thickness, washers, and measured safe thread engagement.
3. Before connecting the wire ends to anything, label each black lead by continuity to its spring contact. Mark the intended gimbal orientation. Thread all eight free wire ends through the corresponding cover holes, entering its INNER face (the face with the two blind locating sockets).
4. Slide the cover along the leads toward the connector, keeping their order. Each bare metal crimp enters its own tunnel. Check smooth passage without forcing, scraping insulation, or bending contacts. Correct any undersize printed holes with the parts off the connector.
5. Place the pogo block in the base pocket, spring tips facing out through the eight apertures. Close the cover onto the locating pins. The connector is captured between the parts without adhesive. Hold the layers together while fitting the M4 screws to the unpowered RS5.
6. Verify that all tips land centrally and have spring travel remaining when the holder is seated. Tighten lightly; do not draw misaligned parts together with the screws. If compression is wrong, adjust `pad_recess` or the base geometry and reprint the base.
7. Gently route the insulated wires to the cover ear and secure with a small cable tie. Avoid a sharp bend or pull at the crimp-to-insulation transition. Twist H/L and connect H, L, ground, and the detect-resistor circuit at the controller. Individually insulate all unused wire ends.

## Termination and first electrical checks

Provide selectable termination, not an unconditional extra resistor. Conventional high-speed CAN uses 120 Ohms at each end, giving roughly 60 Ohms between H and L with all power removed. The DJI documents checked here do not specify the effective termination at your assembled accessory port.

With the battery removed and USB/other supplies disconnected, measure H-to-L resistance through the finished wiring with your extra termination disabled:

- Around 60 Ohms: two conventional terminators are already present; leave the extra one off.
- Around 120 Ohms: typically one terminator is present. For a two-end setup, add 120 Ohms at the otherwise unterminated controller end and recheck.
- Open, unusually low, unstable, or substantially different: investigate contact continuity, module resistors, and actual bus topology before choosing termination. Do not just add resistors until the meter reads 60.

Also verify the 47 kOhm detect path, absence of unintended shorts to the VCC pads, and H/L/ground continuity. Fit and remove the adapter with power off so contacts cannot sweep across adjacent power pads.

The inherited SDK settings are **Classical CAN, 1,000,000 bit/s, standard 11-bit IDs**. The 2026-09-28 [RS5 bench test](firmware/rs5_can_motion_test/README.md#hardware-result--2026-09-28) verified joint queries with controller TX **0x223** and SDK replies on **0x222**, followed by three small yaw cycles. Unsolicited traffic on **0x426** was also observed; it is not decoded by these sketches. DJI SDK messages span multiple CAN frames and use DJI's packet framing and CRCs; arbitrary CAN payloads will not control the gimbal. This test does not validate the accessory supply voltage or all mechanical dimensions.

Start by observing traffic, then use a documented read-only SDK query. Listen-only mode does not acknowledge frames, and a quiet bus alone does not prove the connector failed. An otherwise solitary transmitter may retry without an acknowledging node. Once wiring and bitrate are confirmed, normal CAN mode can acknowledge while your application sends only queries. Clear the gimbal's motion area before any motion-control test.

## Sources and attribution

- [DJI RS SDK download page](https://www.dji.com/rs-sdk): rechecked 2026-09-18; explicitly lists RS5. Still links `SDK documentation_20210729.zip` and the older external-interface PDF. Local SDK v2.5 and diagram were downloaded 2026-09-16. The pinout and electrical parameters above are from those legacy documents, not an RS5-specific electrical drawing.
- [DJI RS5 user manual](https://dl.djicdn.com/downloads/DJI_RS_5/20260115/UM_2/DJI_RS_5_User_Manual_en.pdf): pages 6 and 17 distinguish the electrical RSA/NATO port from the mechanical-only NATO rail.
- [DJI Focus Wheel listing](https://store.dji.com/product/ronin-s-focus-wheel): lists RS5 and RS2 compatibility and two M4 mounting screws; basis for inferring a shared RSA mating interface.
- [DJI RS5 support FAQ](https://www.dji.com/support/product/rs-5): the new Electronic Briefcase Handle cannot be fitted to older models because of its signal-blocking structure. This is a handle-specific change; it does not establish different dimensions for the legacy Focus Wheel interface.
- [Riley Harmon: DJI Ronin RS2/3 Pro Log and Replay](https://github.com/rileyharmon/DJI-Ronin-RS2-Log-and-Replay): original STL and suggested spring-contact part. Credit also to Cornelius von Einem and Casey Basichis as named in its README. Original STL and this CAD derivative are **CC BY-NC 4.0**; the derivative changes are listed in the SCAD header. [Original README](references/rileyharmon-README.md), [license terms](https://creativecommons.org/licenses/by-nc/4.0/).
- [Mill-Max 889 family drawing, hosted by DigiKey](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6448/8892201470501010.pdf): mechanical dimensions and tolerances; local copy linked above.
- [Mill-Max manufacturer announcement](https://www.heilind.eu/page/wp-content/uploads/pr704_-_pre-wired_spring-loaded_target_connectors_ms.pdf): 889-22 prewired family, 24 AWG wire and 8-inch lead length.
- [Mouser exact-part listing](https://www.mouser.com/ProductDetail/Mill-Max/889-22-008-70-501010?qs=Rp5uXu7WBW9JMC9qfc9GZw%3D%3D): exact 8-contact part and order number.
- [Espressif TWAI documentation](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/peripherals/twai.html): external transceiver requirement and CAN controller operation.
- [Texas Instruments SN65HVD230](https://www.ti.com/product/SN65HVD230): transceiver supply, speed, and operating modes.
- [Texas Instruments CAN termination reference](https://www.ti.com/tool/TIDA-01238): termination at the two farthest nodes.
