#!/usr/bin/env python3
"""Write the grey-box motorbike physics chassis as a skinned glTF (no Blender needed).

The chassis is invisible in game: it only gives Chaos Vehicles a physics body and four
wheel bones. Bones: root (box skinned here) + PhysWheel_FL/FR/RL/RR. The narrow track is
the "hidden four-wheel" approach from ARCHITECTURE.md section 3.

Axes: Unreal 5.8's glTF importer maps glTF (x, y, z) to Unreal (x, z, y), in metres to cm
(measured: AOCVehicle logs imported wheel bone positions at BeginPlay). So positions here
are written as Unreal (forward X, right Y, up Z) and converted with to_gltf().

Usage: python3 -I Tools/make_bike_chassis.py [out.gltf]
       default: SourceArt/Vehicles/Bike/SKM_BikeChassis.gltf
"""

import base64
import json
import struct
import sys
from pathlib import Path

WHEEL_RADIUS = 0.32
WHEELBASE_HALF = 0.70   # front and rear axle distance from the centre
TRACK_HALF = 0.20       # each hidden wheel pair is 40 cm wide
BODY_LENGTH, BODY_WIDTH, BODY_HEIGHT = 1.50, 0.30, 0.45
BODY_BOTTOM = 0.30      # gap under the body for suspension travel


def to_gltf(forward, right, up):
    """Unreal axes -> glTF axes, inverse of the importer's (x, y, z) -> (x, z, y)."""
    return (forward, up, right)


# Unreal convention: left wheels at -Y.
JOINTS = [
    ("root", to_gltf(0.0, 0.0, 0.0)),
    ("PhysWheel_FL", to_gltf(WHEELBASE_HALF, -TRACK_HALF, WHEEL_RADIUS)),
    ("PhysWheel_FR", to_gltf(WHEELBASE_HALF, TRACK_HALF, WHEEL_RADIUS)),
    ("PhysWheel_RL", to_gltf(-WHEELBASE_HALF, -TRACK_HALF, WHEEL_RADIUS)),
    ("PhysWheel_RR", to_gltf(-WHEELBASE_HALF, TRACK_HALF, WHEEL_RADIUS)),
]


def box(width, height, length, bottom):
    """Box in glTF space: width along x, height along y (from `bottom`), length along z.
    24 vertices (4 per face, so normals are flat) and 36 indices."""
    hx, hz = width / 2, length / 2
    y0, y1 = bottom, bottom + height
    faces = [  # (normal, four corners counter-clockwise seen from outside)
        ((1, 0, 0), [(hx, y0, hz), (hx, y0, -hz), (hx, y1, -hz), (hx, y1, hz)]),
        ((-1, 0, 0), [(-hx, y0, -hz), (-hx, y0, hz), (-hx, y1, hz), (-hx, y1, -hz)]),
        ((0, 1, 0), [(-hx, y1, hz), (hx, y1, hz), (hx, y1, -hz), (-hx, y1, -hz)]),
        ((0, -1, 0), [(-hx, y0, -hz), (hx, y0, -hz), (hx, y0, hz), (-hx, y0, hz)]),
        ((0, 0, 1), [(-hx, y0, hz), (hx, y0, hz), (hx, y1, hz), (-hx, y1, hz)]),
        ((0, 0, -1), [(hx, y0, -hz), (-hx, y0, -hz), (-hx, y1, -hz), (hx, y1, -hz)]),
    ]
    positions, normals, indices = [], [], []
    for normal, corners in faces:
        base = len(positions)
        positions += corners
        normals += [normal] * 4
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, indices


def pad4(data: bytearray):
    while len(data) % 4:
        data.append(0)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "SourceArt/Vehicles/Bike/SKM_BikeChassis.gltf")
    # glTF x = Unreal forward, glTF z = Unreal right (see to_gltf).
    positions, normals, indices = box(BODY_LENGTH, BODY_HEIGHT, BODY_WIDTH, BODY_BOTTOM)
    n = len(positions)

    buf = bytearray()
    views, accessors = [], []

    def add(data: bytes, target=None):
        pad4(buf)
        offset = len(buf)
        buf.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        return len(views) - 1

    def accessor(view, comp, count, typ, **extra):
        accessors.append({"bufferView": view, "componentType": comp, "count": count, "type": typ, **extra})
        return len(accessors) - 1

    FLOAT, USHORT, UBYTE = 5126, 5123, 5121
    ARRAY, ELEMENTS = 34962, 34963

    pos_acc = accessor(add(b"".join(struct.pack("<3f", *p) for p in positions), ARRAY), FLOAT, n, "VEC3",
                       min=[min(p[i] for p in positions) for i in range(3)],
                       max=[max(p[i] for p in positions) for i in range(3)])
    nrm_acc = accessor(add(b"".join(struct.pack("<3f", *v) for v in normals), ARRAY), FLOAT, n, "VEC3")
    jnt_acc = accessor(add(b"".join(struct.pack("<4B", 0, 0, 0, 0) for _ in range(n)), ARRAY), UBYTE, n, "VEC4")
    wgt_acc = accessor(add(b"".join(struct.pack("<4f", 1.0, 0.0, 0.0, 0.0) for _ in range(n)), ARRAY), FLOAT, n, "VEC4")
    idx_acc = accessor(add(b"".join(struct.pack("<H", i) for i in indices), ELEMENTS), USHORT, len(indices), "SCALAR")

    # Inverse bind matrices (column-major): joints are pure translations from the root.
    ibm = bytearray()
    for _, (x, y, z) in JOINTS:
        ibm += struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1)
    ibm_acc = accessor(add(bytes(ibm)), FLOAT, len(JOINTS), "MAT4")

    nodes = [{"name": "SKM_BikeChassis", "mesh": 0, "skin": 0}]
    joint_nodes = []
    for name, translation in JOINTS:
        node = {"name": name}
        if any(translation):
            node["translation"] = list(translation)
        nodes.append(node)
        joint_nodes.append(len(nodes) - 1)
    nodes[joint_nodes[0]]["children"] = joint_nodes[1:]

    gltf = {
        "asset": {"version": "2.0", "generator": "OpenCity Tools/make_bike_chassis.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, joint_nodes[0]]}],
        "nodes": nodes,
        "meshes": [{"name": "SKM_BikeChassis", "primitives": [{
            "attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc, "JOINTS_0": jnt_acc, "WEIGHTS_0": wgt_acc},
            "indices": idx_acc, "material": 0}]}],
        "materials": [{"name": "M_BikeChassis_Hidden", "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.5, 0.5, 1]}}],
        "skins": [{"name": "SK_BikeChassis", "joints": joint_nodes, "skeleton": joint_nodes[0],
                   "inverseBindMatrices": ibm_acc}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(buf),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(buf)).decode()}],
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(gltf, indent=1))
    print(f"wrote {out}: {n} vertices, {len(indices) // 3} triangles, {len(JOINTS)} joints")


if __name__ == "__main__":
    main()
