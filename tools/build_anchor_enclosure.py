"""Export the camera-top anchor prototype, check meshes/interference, render preview.

Uses installed OpenSCAD, NumPy and Pillow. Run from any directory.
Only documented simplified component envelopes are checked, not a hardware scan.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import json
import re
import shutil
import subprocess

import numpy as np
from PIL import Image, ImageDraw

from verify_and_preview import read_stl, verify_mesh, render_panel, font

ROOT = Path(__file__).resolve().parents[1]
DESIGN = ROOT / "design/mauwb-anchor"
TEMP = ROOT / "tmp/anchor-cad"
SOURCE = DESIGN / "enclosure.scad"
PRINTS = ["base", "cover", "hood", "shield", "fit_gauge", "mount_gauge", "insert_coupon", "print_plate"]
CONTACTS = ["collision", "case_collision", "fastener_collision", "fastener_electronics_collision",
            "driver_collision", "lid_lift_collision", "radio_sweep_collision", "wire_exit_collision"]


def value(name):
    match = re.search(rf"^{name}\s*=\s*([^;]+);", SOURCE.read_text(), re.M)
    if not match:
        raise ValueError(name)
    return json.loads(match[1])


def export(executable, part, path):
    if path.exists():
        path.unlink()  # No stale collision mesh may survive an empty export.
    result = subprocess.run([executable, "-D", f'part="{part}"', "-o", str(path), str(SOURCE)],
                            capture_output=True, text=True, cwd=ROOT)
    log = result.stdout + result.stderr
    (TEMP / f"{part}.log").write_text(log)
    contact_check = part in CONTACTS
    empty_expected = contact_check or (part == "wire" and not value("cable_notches"))
    empty = empty_expected and "empty" in log.lower()
    # OpenSCAD 2015 may warn when an intersection contains only mating faces.
    # Actual positive volume is still rejected by contact_volume below.
    checked_log = log.replace("WARNING: Exported object may not be a valid 2-manifold and may need repair", "") if contact_check else log
    if "ERROR:" in log or "WARNING:" in checked_log or (result.returncode and not empty):
        raise RuntimeError(f"Export {part}: {log}")


def contact_volume(path):
    if not path.exists():
        return 0.0
    t = read_stl(path)
    if not len(t):
        return 0.0
    v = abs(float(np.einsum("ij,ij->i", t[:, 0], np.cross(t[:, 1], t[:, 2])).sum() / 6))
    if v > 0.001:
        raise AssertionError(f"Interference: {path}: {v:.5f} mm^3")
    return v


def translated(mesh, vector):
    return mesh + np.array(vector)


def floor_at(mesh, xy):
    triangles = mesh[np.all(np.abs(mesh[:, :, 2]) < 1e-5, axis=1), :, :2]
    a = triangles[:, 0]
    u, v, w = triangles[:, 1]-a, triangles[:, 2]-a, np.array(xy)-a
    determinant = u[:, 0]*v[:, 1]-u[:, 1]*v[:, 0]
    valid = np.abs(determinant) > 1e-10
    u, v, w, determinant = u[valid], v[valid], w[valid], determinant[valid]
    s = (w[:, 0]*v[:, 1]-w[:, 1]*v[:, 0])/determinant
    t = (u[:, 0]*w[:, 1]-u[:, 1]*w[:, 0])/determinant
    return bool(np.any((s >= -1e-6) & (t >= -1e-6) & (s+t <= 1+1e-6)))


def check_mount(meshes):
    """Probe exported floor faces, independently of the SCAD fastener references."""
    centers = [[value("mount_x"), round(value("mount_y") + sign*value("mount_pitch")/2, 6)]
               for sign in [-1, 1]]

    offset = np.array([value("case_x_min"), value("case_y_min")])
    for name in ["base", "mount_gauge"]:
        for center in centers:
            c = np.array(center)-offset
            assert not floor_at(meshes[name], c), f"Blocked cage hole in {name}"
            # Confirm clearance on both sides of each hole and material outside it.
            for axis in np.eye(2):
                for sign in [-1, 1]:
                    assert not floor_at(meshes[name], c+sign*axis*(value("mount_hole")/2-0.1))
                    assert floor_at(meshes[name], c+sign*axis*(value("mount_hole")/2+0.1))
        assert floor_at(meshes[name], np.array([value("mount_x"), value("mount_y")])-offset), \
            f"Old center mounting hole remains in {name}"
    return centers


def check_closure(meshes):
    xmin, ymin, ymax = [value(x) for x in ["case_x_min", "case_y_min", "case_y_max"]]
    for x, y in value("case_screws"):
        # Cover prints roof-down. Holes must be open at the outside TOP face.
        c = np.array([x-xmin, ymax-y])
        assert not floor_at(meshes["cover"], c), "Lid screw cannot enter from above"
        for axis in np.eye(2):
            for sign in [-1, 1]:
                assert not floor_at(meshes["cover"], c+sign*axis*(value("head_diameter")/2-0.15))
                assert floor_at(meshes["cover"], c+sign*axis*(value("head_diameter")/2+0.15))
        assert floor_at(meshes["base"], [x-xmin, y-ymin]), "Insert pocket breaks through base"
    for x, y in [[8,-7],[47,-7],[8,39],[47,39]]:
        assert floor_at(meshes["base"], [x-xmin, y-ymin]), "Old underside lid hole remains"


def check_no_lid_locators(meshes, body_h):
    """Reject any lid surface in the four former PCB pillar/pin volumes."""
    lo = meshes["cover"].min(axis=1)
    hi = meshes["cover"].max(axis=1)
    pcb_z = body_h-value("top_clearance")-value("roof")-value("pcb_thickness")
    radius = value("support_diameter")/2
    for x,y in value("pcb_holes"):
        cx,cy = x-value("case_x_min"), value("case_y_max")-y
        # Cover prints roof-down: ignore the roof itself, but inspect all the
        # space occupied by the old pillars, down past the former pin tips.
        zone_lo = np.array([cx-radius,cy-radius,value("roof")+0.05])
        zone_hi = np.array([cx+radius,cy+radius,body_h-pcb_z+1])
        assert not np.any(np.all((hi > zone_lo) & (lo < zone_hi), axis=1)), \
            f"Lid feature remains over PCB mounting hole {(x,y)}"


def check_wire_exit(meshes, body_h):
    if not value("cable_notches"):
        return
    xmin,ymin,ymax = [value(x) for x in ["case_x_min","case_y_min","case_y_max"]]
    cover = meshes["cover"]*[1,-1,-1]+[xmin,ymax,body_h]
    zmid = value("base_thickness")+value("cable_exit_height")/2
    # Re-map planar walls onto floor_at's XY plane to inspect exported apertures.
    rear = cover[:,:,[1,2,0]]-[0,0,xmin]
    yc = value("cable_exit_y")
    for y in [yc, yc-value("cable_exit_width")/2+0.2, yc+value("cable_exit_width")/2-0.2]:
        assert not floor_at(rear,[y,zmid]), "Rear wire exit is blocked"
    assert floor_at(rear,[yc,value("base_thickness")+value("cable_exit_height")+0.3])
    for y in [ymin,ymax]:
        side = cover[:,:,[0,2,1]]-[0,0,y]
        assert floor_at(side,[55.5,zmid]), "Former side wire exit remains open"
    for x in [50.5,60.5]:
        for y in [ymin+3.5,ymax-3.5]:
            assert floor_at(meshes["base"],[x-xmin,y-ymin]), "Old tie slot remains open"
    for y in [yc-value("cable_tie_span")/2,yc+value("cable_tie_span")/2]:
        assert not floor_at(meshes["base"],[value("cable_tie_x")-xmin,y-ymin])
    # The band recess must remain blind, with plastic above it.
    assert floor_at(meshes["base"]-[0,0,value("cable_tie_recess")],
                    [value("cable_tie_x")-xmin,yc-ymin])


def plate_offsets():
    row = value("case_y_max")-value("case_y_min")+8
    return {"base":[0,0,0],"cover":[0,row,0],"shield":[0,2*row,0],"hood":[75,2*row,0]}


def check_plate_matches_parts(meshes):
    """Check every plate part against its current standalone export."""
    plate = meshes["print_plate"]
    used = np.zeros(len(plate),dtype=bool)
    for name,offset in plate_offsets().items():
        expected = meshes[name]+offset
        lo,hi = expected.min((0,1)),expected.max((0,1))
        select = np.all((plate >= lo-0.002) & (plate <= hi+0.002),axis=(1,2))
        assert select.any() and not np.any(used & select), f"Missing/overlapping {name} on plate"
        used |= select
        actual_points = np.unique(plate[select].reshape(-1,3),axis=0)
        expected_points = np.unique(expected.reshape(-1,3),axis=0)
        # ASCII OpenSCAD exports round translated coordinates differently. Match
        # vertices within 0.002 mm rather than requiring identical decimal text.
        for candidates,reference in [(actual_points,expected_points),(expected_points,actual_points)]:
            for start in range(0,len(candidates),128):
                delta = candidates[start:start+128,None,:]-reference[None,:,:]
                assert np.all(np.min(np.sum(delta*delta,axis=2),axis=1) < 0.002**2), \
                    f"Plate contains an outdated or misplaced {name}"
    assert used.all(), "Unexpected geometry on print plate"


def preview(meshes, body_h, total_h):
    xmin, ymin, ymax = [value(x) for x in ["case_x_min", "case_y_min", "case_y_max"]]
    base = translated(meshes["base"], [xmin, ymin, 0])
    cover = meshes["cover"] * [1, -1, -1] + [xmin, ymax, body_h]
    hx0 = value("radio_x")-value("antenna_air_gap")-value("hood_wall")
    hood = translated(meshes["hood"], [hx0, ymin, body_h])
    shield_z = value("base_thickness")+max(value("battery_height")+value("battery_vertical_clearance"),value("battery_deck_height"))
    cradle_margin = value("battery_side_clearance")+value("cradle_wall")
    shield_y_min = min(value("battery_y")-cradle_margin,
                       value("mount_y")-value("mount_pitch")/2-value("mount_boss_diameter")/2)
    shield = translated(meshes["shield"], [value("battery_x")-cradle_margin, shield_y_min, shield_z])
    electronics = read_stl(TEMP / "electronics.stl")
    battery = read_stl(TEMP / "battery.stl")
    radio = read_stl(TEMP / "radio.stl")
    pcb_z = shield_z+value("shield_thickness")+value("bottom_clearance")
    centers = electronics.mean(1)
    colors = np.tile([153, 44, 50], (len(electronics), 1))
    colors[centers[:, 2] < pcb_z-0.1] = [184, 187, 190]
    colors[(centers[:, 2] > pcb_z+1.61) & (centers[:, 0] < 45)] = [34, 39, 46]
    colors[(centers[:, 0] > value("header_x")-0.01) & (centers[:, 2] > pcb_z+1.61)] = [65, 112, 75]
    colors[(centers[:, 0] < 8) & (centers[:, 2] > pcb_z+2)] = [184, 187, 190]
    keep = centers[:, 2] >= pcb_z-2.01
    blue, pale, gold = [86, 133, 151], [138, 173, 183], [198, 166, 100]
    image = Image.new("RGB", (1900, 1250), "#f4f6f7")
    draw = ImageDraw.Draw(image)

    def text(x, y, message, size=24, bold=False, fill="#283d48"):
        draw.text((x, y), message, font=font(size, bold), fill=fill)

    text(44, 28, "UWB CAMERA ANCHOR", 40, True)
    text(44, 82, "Prototype E  /  rear wire exit  /  no lid PCB locating pillars  /  top-access screws", 27)
    length = value("case_x_max")-value("case_x_min")
    width = value("case_y_max")-value("case_y_min")
    text(44, 123, f"Body {length:g} x {width:g} x {body_h:.1f} mm  |  Overall height {total_h:.2f} mm", 25)
    for x, title, subtitle in [
        (44, "ASSEMBLED FROM REAR", "Wire exit opposite the antenna, below USB end"),
        (668, "BASE + MOUNT", "Four tall posts hold the lid's threaded inserts"),
        (1292, "EXPLODED", "Hood / cover / board / shield / tray")]:
        text(x, 196, title, 26, True)
        text(x, 235, subtitle, 21)

    assembled = [(base, blue), (cover, blue), (hood, pale), (electronics, colors)]
    if value("cable_notches"):
        assembled.append((read_stl(TEMP/"wire.stl"),[212,141,57]))
    render_panel(image, assembled, [329, 545], [555, 520], azimuth=220, elevation=32)
    render_panel(image, [(base, blue), (battery, [188, 190, 195]),
                         (translated(shield, [0, 0,16]), gold)],
                 [949, 545], [550, 480], azimuth=-60, elevation=43)
    render_panel(image, [(base, blue), (battery, [188, 190, 195]),
                         (translated(shield, [0,0,13]), gold),
                         (translated(electronics[keep], [0,0,28]), colors[keep]),
                         (translated(cover, [0,0,70]), blue),
                         (translated(hood, [0,0,89]), pale)],
                 [1566, 560], [550, 560], azimuth=-55, elevation=25)
    draw.rounded_rectangle((44, 881, 1856, 1170), radius=18, fill="#e3ebef")
    text(68, 902, "ALIGNMENT", 24, True)
    text(68, 947, "The two antenna elements stay side by side and level; their broad face looks forward.", 26)
    text(68, 987, "Mount on the moving camera or cage. Verify zero bearing and left/right response before tracking.", 24)
    text(68, 1039, "Measured cell: 35.5 x 29 x 4.65 mm. Antenna top: 42.11 mm above carrier underside.", 24, True)
    text(68, 1083, "Antenna position follows user's flush-front description; header and printed clearance need checking.", 23)
    text(68, 1125, "Print the gauge first. RF performance and physical fit are not validated. External battery charging.", 23)
    text(44, 1200, f"Cage holes: {value('mount_pitch'):g} mm center-to-center across camera, {value('mount_hole'):g} mm clearance. Verify screws with mount-gauge.stl.", 22)
    image.save(DESIGN / "preview.png")


def mount_preview(meshes, centers):
    image = Image.new("RGB", (1500, 1040), "#f4f6f7")
    draw = ImageDraw.Draw(image)
    draw.text((45, 28), "TWO-SCREW CAMERA CAGE MOUNT", font=font(35, True), fill="#283d48")
    draw.text((45, 80), "Top view of open base  /  +X toward lens  /  screws enter from inside", font=font(24), fill="#283d48")
    heights = meshes["base"][:, :, 2].mean(1)
    colors = np.tile([86, 133, 151], (len(heights), 1))
    colors[heights > value("base_thickness")+0.1] = [146, 181, 192]
    render_panel(image, [(meshes["base"], colors)],
                 [505, 525], [830, 700], azimuth=180, elevation=90)
    # From above, +X is up and +Y is left at this azimuth.
    length = value("case_x_max")-value("case_x_min")
    width = value("case_y_max")-value("case_y_min")
    scale = min(830/width, 700/length)
    middle = [(value("case_x_max")+value("case_x_min"))/2,
              (value("case_y_max")+value("case_y_min"))/2]
    points = sorted((505-(p[1]-middle[1])*scale, 525-(p[0]-middle[0])*scale) for p in centers)
    dimension_y = points[0][1]-102
    for x, y in points:
        draw.line([(x, y-20), (x, dimension_y-10)], fill="#283d48", width=2)
        draw.line([(x-5, dimension_y-7), (x+5, dimension_y+7)], fill="#283d48", width=2)
    draw.line([(points[0][0], dimension_y), (points[1][0], dimension_y)], fill="#283d48", width=2)
    draw.text(((points[0][0]+points[1][0])/2, dimension_y-28),
              f"{value('mount_pitch'):g} mm centers", anchor="mm", font=font(24, True), fill="#283d48")
    for i, line in enumerate([
        "MOUNT DETAILS",
        f"2 x {value('mount_hole'):g} mm clearance holes",
        f"{value('mount_head_pocket'):g} mm head recesses",
        f"{value('mount_seat_thickness'):g} mm screw seating thickness",
        "Pair centered on antenna baseline",
        "Cage provides the screw threads",
        "",
        "REPRINT",
        "Base + cover (rear wire exit)",
        "Reuse revision-D hood and shield",
        "",
        "CHECK WITH MOUNT GAUGE",
        "Screw fit and cage thread depth",
        "Lens alignment and cage seating",
        "Lid screws are now on TOP",
    ]):
        draw.text((940, 230+i*37), line, font=font(22, i in [0, 7, 11]), fill="#283d48")
    draw.text((45, 960), f"Measured screw: shaft {value('mount_screw_diameter'):g} mm; head {value('mount_head_diameter'):g} mm wide x {value('mount_head_height'):g} mm tall. Check fit with the gauge.",
              font=font(23), fill="#283d48")
    image.save(DESIGN / "mount-preview.png")


def closure_preview(meshes, body_h):
    image = Image.new("RGB", (1500, 1060), "#f4f6f7")
    draw = ImageDraw.Draw(image)
    ink = "#283d48"
    draw.text((45, 28), "OPEN THE LID WHILE THE BASE STAYS ON THE CAGE", font=font(33, True), fill=ink)
    draw.text((45, 85), "Prototype E  /  four M3 x 8 countersunk screws from above  /  rear wire exit", font=font(25), fill=ink)
    xmin, ymin, ymax = [value(x) for x in ["case_x_min", "case_y_min", "case_y_max"]]
    base = translated(meshes["base"], [xmin, ymin, 0])
    cover = meshes["cover"] * [1,-1,-1] + [xmin, ymax, body_h]
    hood = translated(meshes["hood"], [value("radio_x")-value("antenna_air_gap")-value("hood_wall"), ymin, body_h])
    heights = base[:, :, 2].mean(1)
    colors = np.tile([86,133,151], (len(heights), 1))
    colors[heights > value("base_thickness")+0.1] = [146,181,192]
    colors[heights > body_h-value("case_cap_thickness")-value("case_axial_gap")-0.1] = [198,166,100]
    length, width = value("case_x_max")-xmin, ymax-ymin
    scale = min(600/width, 630/length)
    midx, midy = (value("case_x_max")+xmin)/2, (ymax+ymin)/2
    for cx, title, objects in [
        (390, "CLOSED: FOUR SCREWS ON TOP", [(cover,[86,133,151]),(hood,[146,181,192])]),
        (1110, "OPEN: INSERTS IN BASE POSTS", [(base,colors)]),
    ]:
        draw.text((cx-290, 155), title, font=font(26, True), fill=ink)
        render_panel(image, objects, [cx,530], [600,630], azimuth=180, elevation=90)
        for i, (x,y) in enumerate(value("case_screws"), 1):
            px, py = cx-(y-midy)*scale, 530-(x-midx)*scale
            radius = value("head_diameter")/2*scale+5
            draw.ellipse((px-radius,py-radius,px+radius,py+radius), outline="#dd9443", width=3)
            draw.text((px+radius+9,py), str(i), anchor="lm", font=font(23, True), fill=ink)
    draw.rounded_rectangle((45,895,1455,1015), radius=14, fill="#e3ebef")
    draw.text((65,910), "Undo the four top screws, then lift the cover and attached hood straight up.", font=font(26, True), fill=ink)
    draw.text((65,955), "To remove the base: detach antenna, release PCB screws, then lift PCB and shield to reach the cage screws.", font=font(23), fill=ink)
    image.save(DESIGN / "closure-preview.png")


def antenna_preview(meshes):
    image = Image.new("RGB", (1500, 1050), "#f4f6f7")
    d = ImageDraw.Draw(image)
    ink, orange = "#283d48", "#cc7929"
    d.text((45,28), "LID CLEARANCE FOR THE INSTALLED ANTENNA", font=font(35,True), fill=ink)
    d.text((45,85), "Prototype E: PCB locating pillars remain removed; the wire exit is at the opposite, rear end.", font=font(24), fill=ink)
    d.text((75,160), "LID UNDERSIDE", font=font(27,True), fill=ink)
    render_panel(image, [(meshes["cover"],[113,163,178])], [380,525], [580,630], azimuth=180, elevation=90)
    length = value("case_x_max")-value("case_x_min")
    width = value("case_y_max")-value("case_y_min")
    scale = min(580/width,630/length)
    for x,y in value("pcb_holes"):
        px = 380+(y-(value("case_y_min")+value("case_y_max"))/2)*scale
        py = 525-(x-(value("case_x_min")+value("case_x_max"))/2)*scale
        r = (value("support_diameter")/2+0.6)*scale
        for angle in range(0,360,45):
            d.arc((px-r,py-r,px+r,py+r),angle,angle+28,fill=orange,width=3)
    d.text((75,865), "Orange rings: former pillars, now clear", font=font(24,True), fill=orange)
    d.text((840,160), "BOARD + ANTENNA, FROM ABOVE", font=font(27,True), fill=ink)
    pcb_scale = 8
    cx, front_y = 1080,250
    pcb_l, pcb_w = value("pcb_length"),value("pcb_width")
    def point(x,y):
        return cx+(y-pcb_w/2)*pcb_scale, front_y+(pcb_l-x)*pcb_scale
    left, top = point(pcb_l,0)
    right,bottom = point(0,pcb_w)
    d.rectangle((left,top,right,bottom),fill="#cfddd4",outline="#4c715d",width=3)
    for x,y in value("pcb_holes"):
        px,py = point(x,y)
        r = 1.5*pcb_scale
        d.ellipse((px-r,py-r,px+r,py+r),fill="#f4f6f7",outline="#4c715d",width=2)
    rear, front = value("radio_x"),value("radio_x")+value("radio_depth")
    x0,y0 = point(front,value("radio_y"))
    x1,y1 = point(rear,value("radio_y")+value("radio_width"))
    d.rectangle((x0,y0,x1,y1),fill="#47785d",outline=ink,width=2)
    for x,y in value("pcb_holes"):
        if rear <= x <= front:
            px,py = point(x,y)
            r = 1.5*pcb_scale
            for angle in range(0,360,60):
                d.arc((px-r,py-r,px+r,py+r),angle,angle+38,fill="white",width=2)
    d.text((cx,210), "Antenna front flush with PCB edge", anchor="mm",font=font(22,True),fill=ink)
    d.line((cx,y1+5,cx,y1+54),fill=orange,width=2)
    d.text((cx,y1+77), "Antenna covers the front holes",anchor="mm",font=font(22,True),fill=orange)
    d.text((cx,y1+112), "Fasten board BEFORE attaching antenna",anchor="mm",font=font(21),fill=ink)
    d.text((cx,bottom+33), "USB / display end",anchor="mm",font=font(23),fill=ink)
    d.rounded_rectangle((45,930,1455,1020),radius=12,fill="#e3ebef")
    d.text((65,946), "One plate: base, cover, hood + shield. The rear-exit update changes the base and cover.",font=font(25,True),fill=ink)
    d.text((65,984), "Antenna x=68...72 mm is inferred from the 72 mm PCB edge and approximately 4 mm module depth.",font=font(22),fill=ink)
    image.save(DESIGN / "antenna-preview.png")


def plate_preview(meshes, body_h):
    image = Image.new("RGB",(1600,1160),"#f4f6f7")
    d = ImageDraw.Draw(image)
    ink = "#283d48"
    d.text((45,28),"REAR WIRE EXIT + COMPLETE PRINT PLATE",font=font(36,True),fill=ink)
    bounds = meshes["print_plate"].max((0,1))-meshes["print_plate"].min((0,1))
    d.text((45,85),f"Prototype E  /  all four parts  /  {bounds[0]:.1f} x {bounds[1]:g} mm footprint  /  millimeters, 100% scale",font=font(25),fill=ink)
    colors={"base":[86,133,151],"cover":[138,173,183],"hood":[116,155,166],"shield":[198,166,100]}
    d.text((75,155),"PRINT PLATE — TOP VIEW",font=font(27,True),fill=ink)
    render_panel(image,[(meshes[p]+offset,colors[p]) for p,offset in plate_offsets().items()],
                 [400,635],[580,840],azimuth=-90,elevation=90)
    d.text((855,155),"WIRES LEAVE AT THE REAR",font=font(27,True),fill=ink)
    xmin,ymin,ymax=[value(p) for p in ["case_x_min","case_y_min","case_y_max"]]
    assembly=[(meshes["base"]+[xmin,ymin,0],colors["base"]),
              (meshes["cover"]*[1,-1,-1]+[xmin,ymax,body_h],colors["cover"]),
              (meshes["hood"]+[value("radio_x")-value("antenna_air_gap")-value("hood_wall"),ymin,body_h],colors["hood"])]
    if value("cable_notches"):
        assembly.append((read_stl(TEMP/"wire.stl"),[224,135,45]))
    render_panel(image,assembly,[1180,480],[650,500],azimuth=220,elevation=24)
    for i,line in enumerate([
        "ONE 7 x 4 mm rear opening for both wires",
        "Opposite antenna, at the base / lid seam",
        "Old side openings closed",
        "Tie slots beside the rear exit",
        "Base flat; cover roof-down; hood open-end-down",
        "Shield flat; all four parts match individual STLs",
    ]):
        d.text((835,785+i*43),line,font=font(23,i==0),fill=ink)
    d.text((45,1110),"Use print-plate.stl for the whole set. Fit gauges and the insert coupon are separate files.",font=font(24),fill=ink)
    image.save(DESIGN/"print-plate-preview.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openscad", default=shutil.which("openscad") or "/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD")
    parser.add_argument("--skip-export", action="store_true")
    args = parser.parse_args()
    TEMP.mkdir(parents=True, exist_ok=True)
    outputs = {p: DESIGN / f"{p.replace('_', '-')}.stl" for p in PRINTS}
    outputs.update({p: TEMP/f"{p}.stl" for p in ["electronics", "battery", "radio", "wire", *CONTACTS]})
    if not args.skip_export:
        with ThreadPoolExecutor(max_workers=3) as pool:
            futures = [pool.submit(export, args.openscad, p, path) for p, path in outputs.items()]
            for future in futures:
                future.result()
    meshes, checks = {}, {}
    for p in PRINTS:
        meshes[p], checks[p] = verify_mesh(outputs[p], 4 if p=="print_plate" else 1)
    contacts = {p: contact_volume(outputs[p]) for p in CONTACTS}
    centers = check_mount(meshes)
    check_closure(meshes)
    check_plate_matches_parts(meshes)
    # The remaining base/hood pair is separated along Z by construction.
    assert meshes["base"][:, :, 2].max() < value("base_thickness")+meshes["cover"][:, :, 2].max()
    log = (TEMP/"base.log").read_text()
    dimensions = {}
    for label in ["BODY", "OVERALL"]:
        m = re.search(rf'"{label} L/W/H", ([\d.]+), ([\d.]+), ([\d.]+)', log)
        if not m:
            raise RuntimeError("Missing dimensions in OpenSCAD output")
        dimensions[label.lower()] = list(map(float, m.groups()))
    check_no_lid_locators(meshes, dimensions["body"][2])
    check_wire_exit(meshes, dimensions["body"][2])
    report = {
        "prototype": "E", "physical_fit_verified": False, "rf_tested": False,
        "dimensions_mm": dimensions,
        "installed_radio_position": "Front face aligned to 72 mm PCB edge per user; rear face 68 mm inferred from approximate 4 mm depth; header envelope still provisional",
        "packed_cell_envelope_mm": [value(x) for x in ["battery_length","battery_width","battery_height"]],
        "mount": {
            "interface": "Two through screws into threaded camera cage holes; no printed threads or captive nut",
            "centers_xy_mm": centers,
            "pitch_mm": value("mount_pitch"),
            "direction": "Y / camera left-right",
            "clearance_diameter_mm": value("mount_hole"),
            "head_pocket_diameter_mm": value("mount_head_pocket"),
            "seat_thickness_mm": value("mount_seat_thickness"),
            "head_to_shield_clearance_mm": round(value("base_thickness")+max(value("battery_height")+value("battery_vertical_clearance"), value("battery_deck_height"))-value("mount_seat_thickness")-value("mount_head_height"), 3),
            "checked_screw_envelope_diameter_head_diameter_head_height_mm": [value(x) for x in ["mount_screw_diameter", "mount_head_diameter", "mount_head_height"]],
            "screw_dimensions_source": "User approximate measurements, 2026-09-30",
            "physical_screw_fit_verified": False,
            "floor_holes_and_closed_old_center_verified": True,
        },
        "contacts_mm3": contacts,
        "print_plate": {
            "parts":list(plate_offsets()),
            "current_individual_meshes_match_verified":True,
            "xy_footprint_mm":checks["print_plate"]["bounds_mm"][:2],
        },
        "wire_exit": {
            "enabled":value("cable_notches"),
            "face":"Rear / -X / USB end, opposite antenna",
            "width_height_mm":[value("cable_exit_width"),value("cable_exit_height")],
            "center_y_mm":value("cable_exit_y"),
            "old_side_openings_closed_verified":value("cable_notches"),
            "tie_band_recess_depth_mm":value("cable_tie_recess"),
            "trial_bundle_diameter_mm":value("wire_check_diameter"),
        },
        "closure": {
            "screw_access": "From above; four blind insert pockets in base posts",
            "screw_centers_xy_mm": value("case_screws"),
            "screw_length_mm_including_head": value("case_screw_length"),
            "top_openings_and_closed_base_verified": True,
            "driver_shaft_diameter_mm": value("driver_diameter"),
            "sampled_lid_lift_offsets_mm": value("lid_lift_checks"),
            "physical_access_verified": False,
            "lid_pcb_locating_pillars": 0,
            "former_pillar_volumes_clear_verified": True,
            "pcb_mounting_complete": False,
            "pcb_retention": "Incomplete: 5 mm-diameter supports have 3.1 mm-diameter, 1.2 mm-deep legacy locating sockets, not screw mounts. Lid does not retain PCB. Base and print plate require a PCB mounting revision.",
            "continuous_radio_sweep_travel_mm": max(value("lid_lift_checks")),
        },
        "collision_scope": "Simplified components, four printed parts, six M3 screws and two measured cage screw envelopes; top driver clearance, sampled lid removal, continuous 60 mm radio/header sweep and trial wire bundle at rear exit; heat-set knurls and cage excluded; PCB fasteners absent from design and checks, PCB retention incomplete",
        "meshes": list(checks.values()),
    }
    (DESIGN/"mesh-checks.json").write_text(json.dumps(report, indent=2)+"\n")
    preview(meshes, dimensions["body"][2], dimensions["overall"][2])
    mount_preview(meshes, centers)
    closure_preview(meshes, dimensions["body"][2])
    antenna_preview(meshes)
    plate_preview(meshes,dimensions["body"][2])
    print("Anchor prototype export, checks and preview complete.")


if __name__ == "__main__":
    main()
