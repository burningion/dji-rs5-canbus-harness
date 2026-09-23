"""Check exported RS5 prototype meshes and produce assembly/wiring previews.

Run after exporting the base, cover, print plate and nominal connector reference.
Uses NumPy and Pillow; does not require a graphics display or modify system packages.
"""
from pathlib import Path
import json
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def read_stl(path):
    raw = path.read_bytes()
    if raw.startswith(b"solid"):
        vertices = re.findall(rb"vertex\s+([-+\d.eE]+)\s+([-+\d.eE]+)\s+([-+\d.eE]+)", raw)
        return np.array(vertices, dtype=float).reshape(-1, 3, 3)
    dtype = np.dtype([("normal", "<f4", (3,)), ("vertices", "<f4", (3, 3)), ("attr", "<u2")])
    return np.frombuffer(raw, dtype=dtype, offset=84)["vertices"].astype(float)


def verify_mesh(path, expected_components):
    triangles = read_stl(path)
    assert len(triangles) and np.isfinite(triangles).all(), path
    points, ids = np.unique(np.round(triangles.reshape(-1, 3), 5), axis=0, return_inverse=True)
    faces = ids.reshape(-1, 3)
    edges = np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]])
    edges.sort(axis=1)
    edges, count = np.unique(edges, axis=0, return_counts=True)
    assert np.all(count == 2), f"Open/non-manifold edges: {path}"
    parent = np.arange(len(points))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for a, b in edges:
        parent[find(a)] = find(b)
    components = len({find(i) for i in range(len(points))})
    assert components == expected_components, (path, components)
    volume = np.einsum("ij,ij->i", triangles[:, 0], np.cross(triangles[:, 1], triangles[:, 2])).sum() / 6
    assert volume > 0, path
    lo, hi = triangles.min((0, 1)), triangles.max((0, 1))
    assert lo[2] >= -0.00001, f"Part below print bed: {path}"
    result = {
        "file": str(path.relative_to(ROOT)), "triangles": len(triangles),
        "closed": True, "components": components,
        "bounds_mm": (hi - lo).round(4).tolist(), "volume_mm3": round(float(volume), 3),
    }
    print(json.dumps(result))
    return triangles, result


def font(size, bold=False):
    candidates = [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    for name in candidates:
        if Path(name).exists():
            return ImageFont.truetype(name, size)
    return ImageFont.load_default()


def render_panel(image, objects, center, box_size, azimuth=-55, elevation=50):
    az, el = np.radians([azimuth, elevation])
    right = np.array([-np.sin(az), np.cos(az), 0])
    depth = np.array([np.cos(el) * np.cos(az), np.cos(el) * np.sin(az), np.sin(el)])
    rotation = np.stack([right, -np.cross(depth, right), depth], axis=1)
    triangles = np.concatenate([obj[0] for obj in objects])
    colors = np.concatenate([
        np.tile(color, (len(tri), 1)) if np.ndim(color) == 1 else color
        for tri, color in objects
    ])
    projected = triangles @ rotation
    xy = projected[:, :, :2]
    lo, hi = xy.min((0, 1)), xy.max((0, 1))
    scale = min(box_size[0] / (hi[0] - lo[0]), box_size[1] / (hi[1] - lo[1]))
    xy = (xy - (lo + hi) / 2) * scale + center
    normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    normals /= np.maximum(np.linalg.norm(normals, axis=1)[:, None], 1e-12)
    light = 0.42 + 0.58 * np.abs(normals @ np.array([0.3, 0.4, 0.866]))
    colors = np.clip(colors * light[:, None], 0, 255).astype(int)
    # Rasterize with a depth buffer so long triangles cannot hide holes or pins.
    origin = np.floor(xy.min((0, 1))).astype(int) - 1
    end = np.ceil(xy.max((0, 1))).astype(int) + 2
    bounds = (*origin.tolist(), *end.tolist())
    pixels = np.array(image.crop(bounds))
    zbuffer = np.full(pixels.shape[:2], -np.inf)
    xy = xy - origin
    for i, triangle in enumerate(xy):
        low = np.maximum(np.floor(triangle.min(0)).astype(int), 0)
        high = np.minimum(np.ceil(triangle.max(0)).astype(int) + 1,
                          [pixels.shape[1], pixels.shape[0]])
        if np.any(high <= low):
            continue
        ax, ay = triangle[0]
        bx, by = triangle[1]
        cx, cy = triangle[2]
        denominator = (by-cy)*(ax-cx) + (cx-bx)*(ay-cy)
        if abs(denominator) < 1e-9:
            continue
        yy, xx = np.ogrid[low[1]:high[1], low[0]:high[0]]
        xx, yy = xx + 0.5, yy + 0.5
        a = ((by-cy)*(xx-cx) + (cx-bx)*(yy-cy)) / denominator
        b = ((cy-ay)*(xx-cx) + (ax-cx)*(yy-cy)) / denominator
        c = 1 - a - b
        z = a*projected[i, 0, 2] + b*projected[i, 1, 2] + c*projected[i, 2, 2]
        depths = zbuffer[low[1]:high[1], low[0]:high[0]]
        visible = (a >= -1e-7) & (b >= -1e-7) & (c >= -1e-7) & (z > depths)
        depths[visible] = z[visible]
        pixels[low[1]:high[1], low[0]:high[0]][visible] = colors[i]
    image.paste(Image.fromarray(pixels), tuple(origin))


def wiring_diagram():
    image = Image.new("RGB", (1600, 930), "#f3f6f8")
    d = ImageDraw.Draw(image)

    def label(x, y, text, size=24, bold=False, fill="#263746"):
        d.text((x, y), text, font=font(size, bold), fill=fill)

    label(40, 30, "RS5 + prewired Mill-Max + ESP32", 36, True)
    label(40, 85, "Proposed wiring: inherited SDK pinout; confirm RS5 pad orientation before powering.", 25)
    for left, right, title in [(40, 400, "RS5 RSA contacts"),
                               (650, 1050, "SN65HVD230"),
                               (1260, 1560, "ESP32")]:
        d.rounded_rectangle((left, 165, right, 635), 16, fill="white", outline="#b4c8d0", width=2)
        label(left+20, 185, title, 28, True)
    label(65, 228, "Identify each black lead", 21)
    for y, name, color in [(285, "CAN-H", "#2a8295"), (355, "CAN-L", "#ae672d"),
                           (425, "GND", "#4c5662")]:
        label(240, y-30, name, 24, True)
        label(670, y-30, name, 24, True)
        d.line((400, y, 650, y), fill=color, width=5)
    label(215, 468, "AD_COM", 24, True)
    d.line([(400, 495), (425, 495), (425, 535), (450, 535)], fill="#78529d", width=4)
    d.rectangle((450, 523, 510, 547), outline="#78529d", width=3)
    d.line([(510, 535), (550, 535), (550, 425)], fill="#4c5662", width=4)
    d.ellipse((544, 419, 556, 431), fill="#4c5662")
    label(440, 563, "47 kOhm", 22)
    label(65, 558, "VCC x2 and SBUS:", 22)
    label(65, 592, "insulate unused ends", 22)
    for y, chip, esp, rightward in [(285, "D / TXD", "TWAI TX", False),
                                  (355, "R / RXD", "TWAI RX", True),
                                  (425, "VCC", "3V3", False),
                                  (495, "GND", "GND", False)]:
        label(915, y-30, chip, 22, True)
        label(1280, y-30, esp, 22, True)
        d.line((1050, y, 1260, y), fill="#4c5662", width=4)
        if y < 400:
            tip, tail = (1235, 1218) if rightward else (1075, 1092)
            d.polygon([(tip,y), (tail,y-9), (tail,y+9)], fill="#4c5662")
    label(675, 555, "RS low: high-speed mode", 23)
    label(675, 592, "Check module termination", 23)
    label(1280, 560, "USB power", 24, True)
    d.rounded_rectangle((40, 685, 1560, 885), 16, fill="#e1ebef")
    label(65, 706, "Use the electrical port beside the joystick-mode switch (manual item 17).", 25, True)
    label(65, 751, "Twist CAN-H/L. Use common ground. GPIOs require the external CAN transceiver.", 24)
    label(65, 793, "Add 120 Ohm H-to-L termination only if the measured bus topology requires it.", 24)
    label(65, 835, "Signal labels only: this drawing does not define pad orientation or wire order.", 24)
    image.save(ROOT / "design/wiring.png")


def main():
    base, base_result = verify_mesh(ROOT / "design/rs5-millmax-base.stl", 1)
    cover, cover_result = verify_mesh(ROOT / "design/rs5-millmax-cover.stl", 1)
    _, plate_result = verify_mesh(ROOT / "design/rs5-millmax-print-plate.stl", 2)
    _, gauge_result = verify_mesh(ROOT / "design/rs5-fit-gauge.stl", 1)
    assert abs(gauge_result["bounds_mm"][2] - 1.2) < 0.001
    assert (ROOT / "design/rs5-carrier.stl").read_bytes() == (ROOT / "design/rs5-millmax-print-plate.stl").read_bytes()
    # The RS5 adaptation deliberately retains the nominal RS2 holder geometry.
    for kind, mesh in [("base", base), ("cover", cover)]:
        old = read_stl(ROOT / f"design/rs2-millmax-{kind}.stl")
        np.testing.assert_allclose(
            np.unique(mesh.reshape(-1, 3), axis=0),
            np.unique(old.reshape(-1, 3), axis=0), atol=0.00001)
    for target in re.findall(r"\]\(([^)]+)\)", (ROOT / "README.md").read_text()):
        if not target.startswith("https://"):
            assert (ROOT / target).exists(), target

    base_depth, cover_depth = 5.4198, 6.4316
    # These presentation values describe the default exported rev-B settings.
    assert abs(base.max((0, 1))[2] - base_depth - 1.2) < 0.001
    assert abs(cover.max((0, 1))[2] - cover_depth) < 0.001
    connector = read_stl(ROOT / "tmp/cad/millmax-connector-reference.stl")
    connector_colors = np.tile([206, 169, 91], (len(connector), 1))
    mid_z = connector[:, :, 2].mean(1)
    housing = (mid_z > base_depth - 2.794 + 0.001) & (mid_z < base_depth - 0.001)
    connector_colors[housing | (mid_z > base_depth + 5.1816)] = [47, 52, 59]
    shifted_connector = connector.copy()
    shifted_connector[:, :, 2] += 5
    mounted_cover = cover.copy()
    mounted_cover[:, :, 1] *= -1
    mounted_cover[:, :, 2] = cover_depth - mounted_cover[:, :, 2] + base_depth + 13

    image = Image.new("RGB", (1800, 990), "#f3f6f8")
    draw = ImageDraw.Draw(image)

    def text(x, y, content, size=24, bold=False):
        draw.text((x, y), content, font=font(size, bold), fill="#263746")

    text(40, 30, "RS5 prototype for prewired Mill-Max 889-22-008-70-501010", 36, True)
    text(40, 85, "Shared legacy RSA geometry | captive housing, enclosed crimps, rear cable support", 25)
    for x, title, subtitle in [
        (40, "1. Front base", "Pocket and two locating pins"),
        (640, "2. Rear cover", "Eight wire tunnels and cable-tie ear"),
        (1240, "Assembly order", "Exploded view; wires shortened for clarity"),
    ]:
        text(x, 155, title, 27, True)
        text(x, 198, subtitle, 21)
    render_panel(image, [(base, [75, 148, 163])], [300, 510], [530, 510])
    render_panel(image, [(cover, [110, 156, 174])], [900, 510], [530, 510])
    render_panel(image, [
        (base, [75, 148, 163]), (shifted_connector, connector_colors),
        (mounted_cover, [110, 156, 174]),
    ], [1500, 520], [530, 545], azimuth=-60, elevation=25)
    draw.rounded_rectangle((40, 820, 1760, 930), 15, fill="#e1ebef")
    text(65, 838, "Mount footprint 19.80 x 28.85 mm | assembled thickness 11.85 mm | two M4 mounting screws", 24, True)
    text(65, 883, "RS5 physical fit unverified: print the gauge first, then check pad recess, clearance and spring travel.", 23)
    text(40, 953, "Mill-Max dimensions from its drawing; mount dimensions inherited from RS2. Use the RS5 port with contacts.", 20)
    image.save(ROOT / "design/carrier-preview.png")
    (ROOT / "design/mesh-checks.json").write_text(json.dumps([base_result, cover_result, plate_result, gauge_result], indent=2) + "\n")
    wiring_diagram()
    print("README links valid; preview and mesh-check report written.")


if __name__ == "__main__":
    main()
