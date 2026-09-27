# DIY RS5 side-port CAN connector - prewired Mill-Max prototype

Build a removable spring-contact adapter on the RS5's electrical RSA/NATO port, with the battery grip attached. The design uses the **prewired Mill-Max 889-22-008-70-501010**, held between two printed parts. The RS5 adaptation retains this part; no different contact block is indicated by the compatibility evidence below. Use an ESP32 with a 3.3 V CAN transceiver, powered from USB for the first version.

**Status: RS5 prototype, not physically validated.** Mill-Max dimensions come from its family drawing. The nominal mounting geometry is inherited from the RS2 community design, supported by DJI's cross-model Focus Wheel compatibility. Pinout, voltage, detect resistor and CAN settings below come from DJI R SDK v2.5, whose connector illustration is for RS2. DJI now lists RS5 SDK support but still links that older download. Applying its connector details to RS5 is an inference, not a separately published RS5 pinout. Confirm physical fit, ground/pad orientation and communication on your unit before use.

Geometry validation: base and cover each export as one closed solid; the combined print plate contains two closed solids. OpenSCAD's solid intersection between the assembled holder and the nominal Mill-Max connector envelope is empty (no solid interference). See [mesh checks](design/mesh-checks.json). This check covers the nominal connector geometry, not the RS5 or manufacturing tolerances.

## Changes from RS2

- **Use the one electrical RSA/NATO port.** The RS5 manual identifies it as item 17, next to the joystick-mode switch (item 25). The other side's NATO rail, item 12 near the power button, has no charging or communication. Identify the exposed contacts rather than relying on an ambiguous left/right description.
- **Retain Mill-Max 889-22-008-70-501010 and the ESP32/CAN-transceiver circuit.** DJI lists the same Focus Wheel, supplied with two M4 screws, for RS2 and RS5. That is strong evidence of a compatible legacy RSA interface; it is not a dimensional drawing for this DIY part.
- **No evidenced geometry change.** The RS5 base and cover deliberately retain revision-B dimensions. The new RS5 files, fit gauge and port instructions make that carry-over explicit. Confirm 19.85 mm nominal screw spacing, eight-pad alignment, pad recess, screw depth and cable-ear clearance on your RS5; those values were not measured on one here.
- **Keep USB power for the controller.** Leave both accessory VCC leads insulated. The older SDK's 8 V accessory output is not an RS5-specific voltage guarantee; the RS5 battery's 11.2-17.8 V specification is a different circuit.
- **The electrical port is occupied by this adapter.** The RS5 Electronic Briefcase Handle cannot use that same mounting point simultaneously. DJI's special warning about fitting that new handle to older gimbals does not change the legacy Focus Wheel compatibility evidence.

## Files

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
- [Makerfabs UWB camera-tracking firmware](firmware/rs5_uwb_tracker/README.md): camera-mounted AoA anchor, subject-carried tag, UART on Feather RX/GPIO38; preview by default, optional manually armed RS5 pan control. Compiled and host-tested; physical operation remains unverified.
- [Makerfabs tag pocket enclosure](design/mauwb-tag/README.md): parameterized OpenSCAD case, fit gauge and print files for a removable 500 mAh battery; external charging, no soldered headers. Vendor PCB dimensions with provisional component heights.
- [RS5 port illustrations](references/dji-rs5-ports.png) and [overview](references/dji-rs5-overview.png), from pages 17 and 6 of the [RS5 user manual](references/dji-rs5-user-manual.pdf).
- [DJI's illustrated pinout](references/dji-sdk-pinout.png), from PDF page 21 / printed page 19 of the [SDK](references/dji-r-sdk-v2.5.pdf).

## Electrical design

Treat the following as the proposed wiring pending RS5 pad-orientation confirmation. A fit gauge proves mechanical alignment only; it cannot identify ground, power or CAN signals.

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

For the **Adafruit Feather ESP32-S3, 8 MB flash / no PSRAM**, the [test firmware](firmware/README.md) selects **pin 5 (GPIO5) to Waveshare CAN TX** and **pin 6 (GPIO6) from Waveshare CAN RX**, with the Feather's 3V output and common GND. GPIO4 is the Feather's I2C SCL line and is left available for its onboard devices and STEMMA QT. The firmware always listens passively at 1 Mbit/s and sends no CAN messages, acknowledgments, or error frames.

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

The inherited SDK settings are **Classical CAN, 1,000,000 bit/s, standard 11-bit IDs**; these have not been bench-tested here on RS5. Your controller transmits on **0x223** and receives gimbal traffic on **0x222**. DJI SDK messages span multiple eight-byte CAN frames and use DJI's packet framing and CRCs; arbitrary CAN payloads will not control the gimbal.

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
