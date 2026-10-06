"""Export/check the default Makerfabs tag prototype and render a CAD preview.

Requires OpenSCAD, NumPy and Pillow. No GPU, browser or package installation.
The collision checks cover the documented simplified component envelopes only.
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
DESIGN = ROOT / "design/mauwb-tag"
TEMP = ROOT / "tmp/tag-cad"
SOURCE = DESIGN / "enclosure.scad"
PRINTS = ["front", "rear", "shield", "print_plate", "fit_gauge", "insert_coupon", "switch_coupon"]
CONTACTS = ["collision", "case_collision", "switch_space_collision"]


def export(openscad, part, destination):
    if destination.exists():
        destination.unlink()  # Empty collision exports must not reuse a stale mesh.
    completed = subprocess.run(
        [openscad, "-D", f'part="{part}"', "-o", str(destination), str(SOURCE)],
        capture_output=True, text=True, cwd=ROOT)
    log = completed.stdout + completed.stderr
    (TEMP / f"{part}.log").write_text(log)
    # Old CGAL may export intentional touching faces as a zero-volume STL.
    contact_check = part in CONTACTS
    if completed.returncode and not (contact_check and "empty" in log.lower()):
        raise RuntimeError(f"OpenSCAD {part}: {log}")
    if "ERROR:" in log or ("WARNING:" in log and not contact_check):
        raise RuntimeError(f"OpenSCAD {part}: {log}")


def contact_volume(path):
    if not path.exists():
        return 0.0
    triangles = read_stl(path)
    if not len(triangles):
        return 0.0
    volume = abs(float(np.einsum(
        "ij,ij->i", triangles[:, 0],
        np.cross(triangles[:, 1], triangles[:, 2])).sum() / 6))
    assert volume < 0.001, f"Volumetric interference in {path}: {volume} mm^3"
    return volume


def default_value(name, source=SOURCE):
    match = re.search(rf"^{name}\s*=\s*([^;]+)\s*;", source.read_text(), re.M)
    if not match:
        raise ValueError(f"Expected literal default for {name}")
    return json.loads(match[1])


def default_number(name):
    return float(default_value(name))


def translate(mesh, shift):
    return mesh + np.asarray(shift)


def face_material_at(mesh, axis, plane, point):
    """Probe a point on an exported planar face, not just the source cutout sizes."""
    axes = [i for i in range(3) if i != axis]
    triangles = mesh[np.all(np.abs(mesh[:, :, axis]-plane) < 0.00002, axis=1)][:, :, axes]
    a = triangles[:, 0]
    u, v, w = triangles[:, 1]-a, triangles[:, 2]-a, np.asarray(point)-a
    determinant = u[:, 0]*v[:, 1]-u[:, 1]*v[:, 0]
    valid = np.abs(determinant) > 1e-10
    u, v, w, determinant = u[valid], v[valid], w[valid], determinant[valid]
    s = (w[:, 0]*v[:, 1]-w[:, 1]*v[:, 0])/determinant
    t = (u[:, 0]*w[:, 1]-u[:, 1]*w[:, 0])/determinant
    return bool(np.any((s >= -1e-6) & (t >= -1e-6) & (s+t <= 1+1e-6)))


def check_switch_opening(front, coupon):
    anchor = ROOT / "design/mauwb-anchor/enclosure.scad"
    for name in ["switch_width", "switch_height", "switch_body_width", "switch_body_height"]:
        assert default_value(name) == default_value(name, anchor), f"Tag/anchor switch mismatch: {name}"
    xc, zc, w, h, wall = [default_number(k) for k in
                         ["switch_x", "switch_z", "switch_width", "switch_height", "wall"]]
    assert w == 13.5 and h == 8.4
    for axis, plane, center, mesh in [
        (1, default_number("case_y_max"), [xc, zc], front),
        (1, default_number("case_y_max")-wall, [xc, zc], front),
        (2, 0, [5+w/2, 5+h/2], coupon),
        (2, wall, [5+w/2, 5+h/2], coupon),
    ]:
        assert not face_material_at(mesh, axis, plane, center), "Blocked switch aperture"
        for direction, dimension in zip(np.eye(2), [w, h]):
            for sign in [-1, 1]:
                assert not face_material_at(mesh, axis, plane, center+sign*direction*(dimension/2-0.01))
                assert face_material_at(mesh, axis, plane, center+sign*direction*(dimension/2+0.01))
        axes = [i for i in range(3) if i != axis]
        vertices = mesh.reshape(-1, 3)
        for sx in [-1, 1]:
            for sz in [-1, 1]:
                corner = np.zeros(3)
                corner[axis] = plane
                corner[axes] = np.asarray(center)+[sx*w/2, sz*h/2]
                assert np.any(np.linalg.norm(vertices-corner, axis=1) < 0.002), "Missing exact aperture corner"


def preview(front, rear, shield, electronics, battery, height, length, width):
    image = Image.new("RGB", (1800, 1130), "#f3f6f8")
    draw = ImageDraw.Draw(image)

    def label(x, y, text, size=23, bold=False, fill="#263746"):
        draw.text((x, y), text, font=font(size, bold), fill=fill)

    label(40, 28, "Makerfabs STM32 AoA tag | rev D rocker-switch opening", 36, True)
    label(40, 82, f"{length:.1f} x {width:.1f} x {height:.2f} mm | no header strips | removable 500 mAh cell", 25)
    label(40, 132, "1. Assembled", 27, True)
    label(40, 172, "Display, buttons and side rocker opening", 21)
    label(640, 132, "2. Rear cover + battery", 27, True)
    label(640, 172, "Cradle, PCB supports and cord holes", 21)
    label(1240, 132, "3. Exploded assembly", 27, True)
    label(1240, 172, "Shell / electronics / shield / rear cover", 21)

    centers = electronics.mean(1)
    colors = np.tile([174, 42, 45], (len(electronics), 1))
    pcb_z = default_number("rear_thickness") + default_number("battery_height") + \
        default_number("battery_vertical_clearance") + default_number("shield_thickness") + \
        default_number("bottom_clearance")
    colors[centers[:, 2] < pcb_z] = [48, 53, 60]
    colors[(centers[:, 2] > pcb_z + 1.61) & (centers[:, 0] < 41)] = [32, 39, 51]
    colors[centers[:, 0] > 49] = [49, 113, 76]
    colors[(centers[:, 0] < 7) & (centers[:, 2] > pcb_z)] = [180, 185, 193]
    colors[(centers[:, 0] < 12) & (centers[:, 1] > 28) &
           (centers[:, 2] > pcb_z)] = [217, 215, 205]
    colors[centers[:, 2] < 8] = [183, 185, 188]
    # Avoid displaying the battery twice in exploded view; it remains in its tray.
    electronics_mask = centers[:, 2] > 8
    electronics_upper = electronics[electronics_mask]
    upper_colors = colors[electronics_mask]

    render_panel(image, [(front, [77, 139, 158]), (rear, [83, 147, 162]),
                         (electronics, colors)],
                 [310, 515], [520, 540], azimuth=70, elevation=40)
    render_panel(image, [(rear, [83, 147, 162]), (battery, [183, 185, 188])],
                 [900, 515], [510, 510], azimuth=-70, elevation=55)
    render_panel(image, [(translate(front, [0, 0, 52]), [77, 139, 158]),
                         (translate(electronics_upper, [0, 0, 18]), upper_colors),
                         (translate(shield, [0, 0, 9]), [205, 170, 100]),
                         (rear, [83, 147, 162]), (battery, [183, 185, 188])],
                 [1500, 525], [520, 580], azimuth=-70, elevation=28)

    draw.rounded_rectangle((40, 855, 1760, 1040), 15, fill="#e1ebef")
    label(64, 875, "Battery stays under the display end; no cell or metal clip behind the radio module.", 25, True)
    label(64, 920, "Same 13.5 x 8.4 mm switch opening as the anchor. Battery still charges externally.", 24)
    label(64, 963, "PCB outline and holes come from vendor CAD. Z heights and plugged battery lead remain provisional.", 23)
    label(64, 1004, "Print the fit gauge first. The model is not waterproof and has not been physically fitted or RF-tested.", 22)
    label(40, 1070, "Four M3 x 4 x 5 mm inserts + M3 x 8 mm countersunk screws. Test the insert coupon before the case.", 22)
    image.save(DESIGN / "preview.png")


def switch_preview(front, rear, height):
    image = Image.new("RGB", (1600, 1000), "#f3f6f8")
    draw = ImageDraw.Draw(image)
    ink, orange = "#263746", "#bd6924"
    draw.text((40, 28), "TAG CASE | SAME ROCKER OPENING AS CAMERA ANCHOR", font=font(32, True), fill=ink)
    draw.text((40, 82), "Exact 13.5 x 8.4 mm aperture | BAT-connector long side | outside dimensions unchanged",
              font=font(23), fill=ink)
    render_panel(image, [(front, [77, 139, 158]), (rear, [83, 147, 162])],
                 [390, 410], [640, 490], azimuth=70, elevation=30)
    ymax = default_number("case_y_max")
    face = front[np.all(np.abs(front[:, :, 1]-ymax) < 0.002, axis=1)][:, :, [0, 2]]
    scale = 8
    def point(x, z):
        return (800+(x-default_number("case_x_min"))*scale, 515-z*scale)
    for triangle in face:
        draw.polygon([point(x, z) for x, z in triangle], fill="#668e9a")
    xc, zc, w, h = [default_number(k) for k in ["switch_x", "switch_z", "switch_width", "switch_height"]]
    x0, y0 = point(xc-w/2, zc+h/2)
    x1, y1 = point(xc+w/2, zc-h/2)
    draw.rectangle((x0, y0, x1, y1), outline=orange, width=3)
    draw.text(((x0+x1)/2, y0-26), "13.5 mm", anchor="mm", font=font(23, True), fill=orange)
    draw.text((x1+12, (y0+y1)/2), "8.4 mm", anchor="lm", font=font(23, True), fill=orange)
    draw.text((800, 550), "Actual exported outer wall (+Y), viewed face-on", font=font(23, True), fill=ink)
    draw.text((800, 590), f"Plastic above opening to outer top: {height-zc-h/2:.2f} mm", font=font(23), fill=ink)
    for i, line in enumerate([
        "Sharp rectangle through the tag's 1.6 mm wall; no added fit allowance.",
        "Measured switch body: 13.27 x 8.18 mm. Approximate depth with terminals: 15 mm.",
        "20 mm inward clearance corridor checked against the modeled board, battery, display and radio.",
        "Print switch-coupon.stl first to check clip retention in this thinner wall. Actual flange/wires need fitting.",
        "Rev C rear cover and battery shield still fit. Reprint front.stl, or use the full print-plate.stl.",
    ]):
        draw.text((40, 730+i*44), line, font=font(23, i == 0), fill=ink)
    image.save(DESIGN / "switch-preview.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default_executable = shutil.which("openscad") or "/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD"
    parser.add_argument("--openscad", default=default_executable)
    parser.add_argument("--skip-export", action="store_true")
    args = parser.parse_args()
    TEMP.mkdir(parents=True, exist_ok=True)
    outputs = {name: DESIGN / f"{name.replace('_', '-')}.stl"
               for name in PRINTS}
    outputs.update({name: TEMP / f"{name}.stl"
                    for name in ["electronics", "pcb", "battery", "radio", "switch_space", *CONTACTS]})
    if not args.skip_export:
        with ThreadPoolExecutor(max_workers=3) as executor:
            futures = [executor.submit(export, args.openscad, part, path)
                       for part, path in outputs.items()]
            for future in futures:
                future.result()

    checked = {}
    meshes = {}
    for name in PRINTS:
        meshes[name], checked[name] = verify_mesh(outputs[name], 3 if name == "print_plate" else 1)

    log = (TEMP / "front.log").read_text()
    match = re.search(r'"Outer case L/W/H \(mm\)", ([\d.]+), ([\d.]+), ([\d.]+)', log)
    if not match:
        raise RuntimeError("Missing OpenSCAD dimension report")
    length, width, height = map(float, match.groups())
    xmin, ymin, ymax = [default_number(k) for k in ["case_x_min", "case_y_min", "case_y_max"]]
    front = meshes["front"].copy()
    front[:, :, 0] += xmin
    front[:, :, 1] = ymax - front[:, :, 1]
    front[:, :, 2] = height - front[:, :, 2]
    rear = translate(meshes["rear"], [xmin, ymin, 0])
    cradle_x = default_number("battery_x") - default_number("battery_side_clearance") - default_number("cradle_wall")
    cradle_y = default_number("battery_y") - default_number("battery_side_clearance") - default_number("cradle_wall")
    shield_z = default_number("rear_thickness") + default_number("battery_height") + default_number("battery_vertical_clearance")
    shield = translate(meshes["shield"], [cradle_x, cradle_y, shield_z])
    battery = read_stl(outputs["battery"])
    radio = read_stl(outputs["radio"])
    gap = float(radio[:, :, 0].min() - battery[:, :, 0].max())
    assert gap >= 8.49, "Default battery encroaches on the reserved radio area"
    contacts = {name: contact_volume(outputs[name]) for name in CONTACTS}
    check_switch_opening(front, meshes["switch_coupon"])
    # The first part on the print plate must carry the same through-opening.
    plate = meshes["print_plate"]
    plate_front = plate[np.all((plate[:, :, 0] <= length+0.002) &
                              (plate[:, :, 1] <= width+0.002), axis=1)]
    assert len(plate_front) == len(front), "Print plate front differs from standalone shell"
    plate_front = plate_front*[1, -1, -1]+[xmin, ymax, height]
    check_switch_opening(plate_front, meshes["switch_coupon"])
    np.testing.assert_allclose(checked["front"]["bounds_mm"][:2], [length, width], atol=0.001)

    report = {
        "prototype": True,
        "revision": "D (same rocker aperture as camera anchor)",
        "hardware_fit_verified": False,
        "component_heights": "provisional; measure actual board and mated BAT plug",
        "outer_dimensions_mm": [length, width, height],
        "fasteners": {
            "quantity": 4,
            "thread": "M3 x 0.5",
            "length_mm": 8,
            "head_seat": default_value("case_screw_seat"),
            "heat_set_inserts_enabled": default_value("use_heat_set_inserts"),
            "clearance_diameter_mm": default_number("case_screw_clearance"),
            "pilot_diameter_mm": default_number("case_screw_pilot"),
            "head_recess_diameter_mm": default_number("case_screw_head"),
            "boss_diameter_mm": default_number("case_boss_diameter"),
            "insert_outer_diameter_mm": default_number("insert_outer_diameter"),
            "insert_length_mm": default_number("insert_length"),
            "insert_pocket_diameter_mm": default_number("insert_hole_diameter"),
            "insert_pocket_depth_mm": default_number("insert_length") + default_number("insert_bottom_clearance"),
            "insert_fit_physically_verified": False,
            "coupon_hole_diameters_mm": default_value("insert_coupon_diameters"),
        },
        "battery_to_radio_envelope_x_gap_mm": round(gap, 3),
        "switch": {
            "enabled": default_value("switch_opening"),
            "wall": "BAT-connector long side (+Y), in front shell",
            "opening_width_height_mm": [default_number("switch_width"), default_number("switch_height")],
            "measured_body_width_height_mm": [default_number("switch_body_width"), default_number("switch_body_height")],
            "center_x_z_assembly_mm": [default_number("switch_x"), default_number("switch_z")],
            "wall_thickness_mm": default_number("wall"),
            "installed_depth_including_terminals_approx_mm": default_number("switch_installed_depth"),
            "reserved_depth_from_outer_wall_mm": default_number("switch_space_depth"),
            "outer_top_bridge_mm": round(height-default_number("switch_z")-default_number("switch_height")/2, 3),
            "aperture_checked_on_both_wall_and_coupon_faces": True,
            "print_plate_aperture_checked": True,
            "matches_anchor_aperture": True,
            "physical_fit_verified": False,
            "excluded_from_clearance_model": "Actual flange/clips, terminal spread and flexible wire bends",
        },
        "volumetric_intersections_mm3": contacts,
        "contact_note": "Zero-volume PCB support and mating-face contacts are intentional.",
        "meshes": list(checked.values()),
    }
    (DESIGN / "mesh-checks.json").write_text(json.dumps(report, indent=2) + "\n")
    preview(front, rear, shield, read_stl(outputs["electronics"]), battery, height, length, width)
    switch_preview(front, rear, height)
    print("Default prototype meshes, collision checks and preview completed.")


if __name__ == "__main__":
    main()
