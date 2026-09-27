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


def export(openscad, part, destination):
    completed = subprocess.run(
        [openscad, "-D", f'part="{part}"', "-o", str(destination), str(SOURCE)],
        capture_output=True, text=True, cwd=ROOT)
    log = completed.stdout + completed.stderr
    (TEMP / f"{part}.log").write_text(log)
    # Old CGAL may export intentional touching faces as a zero-volume STL.
    contact_check = part in {"collision", "case_collision"}
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


def default_value(name):
    match = re.search(rf"^{name}\s*=\s*([^;]+)\s*;", SOURCE.read_text(), re.M)
    if not match:
        raise ValueError(f"Expected literal default for {name}")
    return json.loads(match[1])


def default_number(name):
    return float(default_value(name))


def translate(mesh, shift):
    return mesh + np.asarray(shift)


def preview(front, rear, shield, electronics, battery, height, length, width):
    image = Image.new("RGB", (1800, 1130), "#f3f6f8")
    draw = ImageDraw.Draw(image)

    def label(x, y, text, size=23, bold=False, fill="#263746"):
        draw.text((x, y), text, font=font(size, bold), fill=fill)

    label(40, 28, "Makerfabs STM32 AoA tag | M3 insert enclosure prototype", 36, True)
    label(40, 82, f"{length:.1f} x {width:.1f} x {height:.2f} mm | no header strips | removable 500 mAh cell", 25)
    label(40, 132, "1. Assembled", 27, True)
    label(40, 172, "Recessed display and button access", 21)
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
                 [310, 515], [520, 540], azimuth=-70, elevation=55)
    render_panel(image, [(rear, [83, 147, 162]), (battery, [183, 185, 188])],
                 [900, 515], [510, 510], azimuth=-70, elevation=55)
    render_panel(image, [(translate(front, [0, 0, 52]), [77, 139, 158]),
                         (translate(electronics_upper, [0, 0, 18]), upper_colors),
                         (translate(shield, [0, 0, 9]), [205, 170, 100]),
                         (rear, [83, 147, 162]), (battery, [183, 185, 188])],
                 [1500, 525], [520, 580], azimuth=-70, elevation=28)

    draw.rounded_rectangle((40, 855, 1760, 1040), 15, fill="#e1ebef")
    label(64, 875, "Battery stays under the display end; no cell or metal clip behind the radio module.", 25, True)
    label(64, 920, "Default shell covers USB sockets: remove the battery and charge it externally.", 24)
    label(64, 963, "PCB outline and holes come from vendor CAD. Z heights and plugged battery lead remain provisional.", 23)
    label(64, 1004, "Print the fit gauge first. The model is not waterproof and has not been physically fitted or RF-tested.", 22)
    label(40, 1070, "Four M3 x 4 x 5 mm inserts + M3 x 8 mm countersunk screws. Test the insert coupon before the case.", 22)
    image.save(DESIGN / "preview.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default_executable = shutil.which("openscad") or "/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD"
    parser.add_argument("--openscad", default=default_executable)
    parser.add_argument("--skip-export", action="store_true")
    args = parser.parse_args()
    TEMP.mkdir(parents=True, exist_ok=True)
    outputs = {name: DESIGN / f"{name.replace('_', '-')}.stl"
               for name in ["front", "rear", "shield", "print_plate", "fit_gauge", "insert_coupon"]}
    outputs.update({name: TEMP / f"{name}.stl"
                    for name in ["electronics", "pcb", "battery", "radio", "collision", "case_collision"]})
    if not args.skip_export:
        with ThreadPoolExecutor(max_workers=3) as executor:
            futures = [executor.submit(export, args.openscad, part, path)
                       for part, path in outputs.items()]
            for future in futures:
                future.result()

    checked = {}
    meshes = {}
    for name in ["front", "rear", "shield", "print_plate", "fit_gauge", "insert_coupon"]:
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
    contacts = {name: contact_volume(outputs[name]) for name in ["collision", "case_collision"]}
    np.testing.assert_allclose(checked["front"]["bounds_mm"][:2], [length, width], atol=0.001)

    report = {
        "prototype": True,
        "revision": "C (M3 heat-set inserts)",
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
        "volumetric_intersections_mm3": contacts,
        "contact_note": "Zero-volume PCB support and mating-face contacts are intentional.",
        "meshes": list(checked.values()),
    }
    (DESIGN / "mesh-checks.json").write_text(json.dumps(report, indent=2) + "\n")
    preview(front, rear, shield, read_stl(outputs["electronics"]), battery, height, length, width)
    print("Default prototype meshes, collision checks and preview completed.")


if __name__ == "__main__":
    main()
