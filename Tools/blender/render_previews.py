"""
render_previews.py -- preview renders of the Marevia kit, built from the exported FBX files.

Run (after make_kit.py):
    ~/Applications/blender-4.2/blender -b --factory-startup -P Tools/blender/render_previews.py -- [--only contact,facade,pieces] [--samples N]

Outputs (SourceArt/Kit/Previews/):
    contact_sheet.png          every piece side by side with labels
    facade_test.png            12 m x 8 m, 3-storey building assembled from the pieces like the game does
    facade_ground.png          ground-floor close-up with a 1.8 m capsule for scale
    facade_upper.png           upper-floor close-up (shutters, balcony, cornice)
    pieces/<Piece>.png         one 3/4 close-up per piece

The FBX files are re-imported (so the renders show exactly what was exported, including the custom
normals) and un-mirrored with an object scale of (1, -1, 1): the exporter bakes a Y mirror so Unreal
coordinates equal Blender coordinates (see make_kit.py header). Placeholder flat colours per slot.
"""

import bpy
import math
import os
import sys
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FBX_DIR = os.path.join(ROOT, "SourceArt", "Kit", "FBX")
OUT = os.path.join(ROOT, "SourceArt", "Kit", "Previews")

H = 3.2

COLORS = {  # linear RGB, roughness, metallic
    "Wall": ((0.80, 0.58, 0.32), 0.85, 0.0),       # warm cream / ochre plaster
    "Trim": ((0.74, 0.70, 0.60), 0.70, 0.0),       # pale limestone
    "Wood": ((0.045, 0.15, 0.085), 0.55, 0.0),     # shutter green
    "Glass": ((0.015, 0.02, 0.025), 0.05, 0.0),
    "Metal": ((0.045, 0.045, 0.05), 0.45, 0.7),
    "Roof": ((0.47, 0.11, 0.045), 0.75, 0.0),      # terracotta
    "Concrete": ((0.40, 0.38, 0.34), 0.85, 0.0),
    "_ground": ((0.20, 0.20, 0.20), 0.9, 0.0),
    "_person": ((0.10, 0.22, 0.45), 0.5, 0.0),
    "_label": ((0.01, 0.01, 0.01), 0.6, 0.0),
}


def args():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = {"contact", "facade", "pieces"}
    samples = 64
    if "--only" in a:
        only = set(a[a.index("--only") + 1].split(","))
    if "--samples" in a:
        samples = int(a[a.index("--samples") + 1])
    return only, samples


def make_mat(name):
    col, rough, metal = COLORS[name]
    m = bpy.data.materials.new("PV_" + name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*col, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if name == "Glass":
        b.inputs["Specular IOR Level"].default_value = 0.8
    if name == "Roof":
        # world-aligned tile courses (preview only; UE uses a world-aligned tile material)
        nt = m.node_tree
        tc = nt.nodes.new("ShaderNodeNewGeometry")
        wave = nt.nodes.new("ShaderNodeTexWave")
        wave.wave_type = "BANDS"
        wave.bands_direction = "Z"
        wave.inputs["Scale"].default_value = 2.4      # ~0.13 m vertical = ~0.3 m courses on a 25 deg slope
        wave.inputs["Distortion"].default_value = 0.0
        ramp = nt.nodes.new("ShaderNodeValToRGB")
        ramp.color_ramp.elements[0].position = 0.0
        ramp.color_ramp.elements[0].color = (0.30, 0.065, 0.03, 1)
        ramp.color_ramp.elements[1].position = 0.35
        ramp.color_ramp.elements[1].color = (*col, 1)
        nt.links.new(tc.outputs["Position"], wave.inputs["Vector"])
        nt.links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
        nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
        bump = nt.nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 0.4
        nt.links.new(wave.outputs["Fac"], bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], b.inputs["Normal"])
    return m


def setup_scene(samples):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    prefs = bpy.context.preferences.addons["cycles"].preferences
    for dev in ("OPTIX", "CUDA", "NONE"):
        try:
            prefs.compute_device_type = dev
            prefs.get_devices()
            if dev == "NONE" or any(d.type == dev for d in prefs.devices):
                for d in prefs.devices:
                    d.use = d.type == dev
                break
        except Exception:
            continue
    sc.cycles.device = "GPU" if prefs.compute_device_type != "NONE" else "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 6
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.film_transparent = False
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "AgX - Medium High Contrast"
    sc.view_settings.exposure = -0.3
    # sky + sun
    w = bpy.data.worlds.new("Sky")
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    sky = nt.nodes.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_elevation = math.radians(38)
    sky.sun_rotation = math.radians(215)
    sky.sun_disc = False
    sky.air_density = 1.0
    sky.dust_density = 1.5
    bg = nt.nodes["Background"]
    bg.inputs["Strength"].default_value = 0.35
    nt.links.new(sky.outputs["Color"], bg.inputs["Color"])
    sun = bpy.data.lights.new("Sun", "SUN")
    sun.energy = 4.0
    sun.angle = math.radians(1.0)
    sun.color = (1.0, 0.95, 0.86)
    so = bpy.data.objects.new("Sun", sun)
    sc.collection.objects.link(so)
    # sun direction: from elevation 38 deg, azimuth such that it rakes the -Y facades from the left
    el, az = math.radians(38), math.radians(-125)
    d = Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))
    so.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    mats = {n: make_mat(n) for n in COLORS}
    # ground
    me = bpy.data.meshes.new("Ground")
    s = 400
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    me.materials.append(mats["_ground"])
    g = bpy.data.objects.new("Ground", me)
    sc.collection.objects.link(g)
    return mats


def import_kit(mats):
    lib = bpy.data.collections.new("KitLib")
    bpy.context.scene.collection.children.link(lib)
    lib.hide_render = True
    lib.hide_viewport = True
    kit = {}
    for fn in sorted(os.listdir(FBX_DIR)):
        if not fn.endswith(".fbx"):
            continue
        before = set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=os.path.join(FBX_DIR, fn))
        new = [o for o in bpy.data.objects if o not in before]
        name = fn[:-4]
        for o in new:
            for c in list(o.users_collection):
                c.objects.unlink(o)
            if o.name.startswith("UCX_"):
                bpy.data.objects.remove(o)
                continue
            lib.objects.link(o)
            for slot in o.material_slots:
                base = slot.material.name.split(".")[0]
                slot.material = mats[base]
            kit[name] = o
    return kit


def place(kit, name, pos=(0, 0, 0), rot_deg=0.0, scale=(1, 1, 1), coll=None):
    """Instance a piece the way the game does: pivot at pos, rotated about Z. The (1,-1,1) object
    scale undoes the Y mirror baked into the FBX (so local Blender coords == KIT_SPEC coords)."""
    src = kit[name]
    ob = bpy.data.objects.new(name + "_inst", src.data)
    (coll or bpy.context.scene.collection).objects.link(ob)
    ob.matrix_world = (Matrix.Translation(pos) @ Matrix.Rotation(math.radians(rot_deg), 4, "Z")
                       @ Matrix.Diagonal((*scale, 1.0)) @ Matrix.Diagonal((1.0, -1.0, 1.0, 1.0)))
    return ob


def label(text, pos, mats, size=0.32, rot_x=60.0, rot_z=0.0):
    cu = bpy.data.curves.new("lbl", "FONT")
    cu.body = text
    cu.size = size
    cu.align_x = "CENTER"
    cu.extrude = 0.004
    ob = bpy.data.objects.new("lbl_" + text, cu)
    ob.data.materials.append(mats["_label"])
    bpy.context.scene.collection.objects.link(ob)
    ob.location = pos
    ob.rotation_euler = (math.radians(rot_x), 0, math.radians(rot_z))
    return ob


def camera(loc, target, lens=35.0, ortho=None):
    cd = bpy.data.cameras.new("Cam")
    if ortho:
        cd.type = "ORTHO"
        cd.ortho_scale = ortho
    else:
        cd.lens = lens
    cd.clip_end = 1000
    cam = bpy.data.objects.get("Cam")
    if cam:
        cam.data = cd
    else:
        cam = bpy.data.objects.new("Cam", cd)
        bpy.context.scene.collection.objects.link(cam)
    cam.location = loc
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = cam
    return cam


def render(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("[preview] wrote", path)


def person(mats, pos):
    me = bpy.data.meshes.new("Person")
    ob = bpy.data.objects.new("Person_1.8m", me)
    bpy.context.scene.collection.objects.link(ob)
    import bmesh
    bm = bmesh.new()
    r = 0.22
    bmesh.ops.create_cone(bm, cap_ends=True, segments=24, radius1=r, radius2=r, depth=1.8 - 2 * r,
                          matrix=Matrix.Translation((0, 0, 0.9)))
    for zc in (r, 1.8 - r):
        bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=r, matrix=Matrix.Translation((0, 0, zc)))
    bm.to_mesh(me)
    for p in me.polygons:
        p.use_smooth = True
    me.materials.append(mats["_person"])
    ob.location = pos
    return ob


# ------------------------------------------------------------------------------------------------
def contact_sheet(kit, mats, samples):
    rows = [
        # back row: 4 m storey pieces
        (["Wall_400", "Window_400", "WindowPlain_400", "Balcony_400", "Shopfront_400", "GroundWindow_400"], 0.0, 0.8),
        # middle row: 2 m pieces + plinths (plinths lifted 3 m so the buried part shows)
        (["Wall_200", "Window_200", "WindowPlain_200", "Door_200", "Corner_320", "Drainpipe_320",
          "Plinth_200", "Plinth_400"], -9.0, 0.7),
        # front row: trims, roofs, props
        (["Cornice_400", "Cornice_200", "CorniceCorner", "Parapet_400", "Parapet_200", "RoofHip", "RoofFlat",
          "Prop_WaterTank", "Prop_ACUnit", "Prop_Dish", "Prop_Chimney", "Prop_RoofHut"], -16.0, 0.6),
    ]
    widths = {"Corner_320": 1.3, "Drainpipe_320": 1.5, "CorniceCorner": 1.6, "RoofHip": 2.0, "RoofFlat": 2.0,
              "Prop_WaterTank": 1.9, "Prop_ACUnit": 1.9, "Prop_Dish": 1.6, "Prop_Chimney": 1.9, "Prop_RoofHut": 2.6}
    total_w = 0
    for names, y, gap in rows:
        w = sum(widths.get(n, 4.0 if n.endswith("_400") else 2.0) for n in names) + gap * (len(names) - 1)
        total_w = max(total_w, w)
    for names, y, gap in rows:
        w = sum(widths.get(n, 4.0 if n.endswith("_400") else 2.0) for n in names) + gap * (len(names) - 1)
        x = -w / 2
        for n in names:
            pw = widths.get(n, 4.0 if n.endswith("_400") else 2.0)
            if n not in kit:
                x += pw + gap
                continue
            centred = n in ("Corner_320", "Drainpipe_320", "RoofHip", "RoofFlat") or n.startswith("Prop_")
            px = x + pw / 2 if centred else x
            if n == "CorniceCorner":
                px = x + pw / 2 + 0.15
            z = 3.0 if n.startswith("Plinth") else 0.0
            sc = (2.0, 2.0, 0.8) if n in ("RoofHip", "RoofFlat") else (1, 1, 1)
            place(kit, n, (px, y, z), 0, sc)
            label(n, (x + pw / 2, y - 1.3, 0.12), mats, size=0.26 if len(n) < 14 else 0.21)
            x += pw + gap
    camera((4, -48, 26), (0, -6.5, 0.6), ortho=total_w + 5)
    render(os.path.join(OUT, "contact_sheet.png"))


def piece_closeups(kit, mats, samples):
    bpy.context.scene.render.resolution_x, bpy.context.scene.render.resolution_y = 960, 720
    for n, src in sorted(kit.items()):
        coll = bpy.data.collections.new("pc_" + n)
        bpy.context.scene.collection.children.link(coll)
        ob = place(kit, n, (0, 0, 0), 0, (1, 1, 1), coll)
        bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
        lo = Vector((min(p.x for p in bb), min(p.y for p in bb), max(0.0, min(p.z for p in bb))))
        hi = Vector((max(p.x for p in bb), max(p.y for p in bb), max(p.z for p in bb)))
        c = (lo + hi) / 2
        size = max((hi - lo).x, (hi - lo).z, 0.5)
        d = size * 1.9 + 0.5
        camera((c.x - 0.45 * d, c.y - d, c.z + 0.25 * d), c, lens=40)
        render(os.path.join(OUT, "pieces", n + ".png"))
        bpy.data.objects.remove(ob)
        bpy.data.collections.remove(coll)


def build_test_building(kit):
    """12 m x 8 m, ground + 2 upper storeys, hip roof, assembled on the bay grid like the game."""
    Wd, Dp = 12.0, 8.0
    front = [  # bays left->right, per storey (ground first)
        ["Shopfront_400", "Door_200", "GroundWindow_400", "Wall_200"],
        ["Window_400", "Window_200", "Balcony_400", "WindowPlain_200"],
        ["Window_400", "WindowPlain_200", "Window_400", "Window_200"],
    ]
    side = [["Wall_200", "GroundWindow_400", "Wall_200"],
            ["WindowPlain_200", "Window_400", "WindowPlain_200"],
            ["WindowPlain_200", "Window_400", "Window_200"]]
    back = [["Wall_400", "Door_200", "Wall_200", "Wall_400"],
            ["WindowPlain_400", "Wall_200", "WindowPlain_200", "Wall_400"]] + \
           [["WindowPlain_400", "Wall_200", "WindowPlain_200", "WindowPlain_400"]]
    # facade runs: (start corner, rotation, bay layout)
    runs = [((0, 0), 0, front), ((Wd, 0), 90, side), ((Wd, Dp), 180, back), ((0, Dp), 270, side)]
    for (sx, sy), rot, layout in runs:
        R = Matrix.Rotation(math.radians(rot), 3, "Z")
        dirn = R @ Vector((1, 0, 0))
        for s, bays in enumerate(layout):
            x = 0.0
            for b in bays:
                w = 4.0 if b.endswith("_400") else 2.0
                p = Vector((sx, sy, 0)) + dirn * x
                place(kit, b, (p.x, p.y, s * H), rot)
                # plinth row under every ground bay except the ones that carry their own base
                if s == 0 and b not in ("Shopfront_400", "Door_200"):
                    place(kit, "Plinth_400" if w == 4 else "Plinth_200", (p.x, p.y, 0), rot)
                x += w
        # cornice row
        x = 0.0
        for b in layout[-1]:
            w = 4.0 if b.endswith("_400") else 2.0
            p = Vector((sx, sy, 0)) + dirn * x
            place(kit, "Cornice_400" if w == 4 else "Cornice_200", (p.x, p.y, len(layout) * H), rot)
            x += w
    for (cx, cy), rot in (((0, 0), 0), ((Wd, 0), 90), ((Wd, Dp), 180), ((0, Dp), 270)):
        for s in range(3):
            place(kit, "Corner_320", (cx, cy, s * H), rot)
        place(kit, "CorniceCorner", (cx, cy, 3 * H), rot)
    roof_z = 3 * H + 0.35
    ov = 0.40
    place(kit, "RoofHip", (Wd / 2, Dp / 2, roof_z), 0, (Wd + 2 * ov, Dp + 2 * ov, 2.2))
    place(kit, "Prop_Chimney", (9.0, 5.6, roof_z + 0.9), 0)
    for s in (0, 1, 2):     # on the 6 m bay seam (no shutters there)
        place(kit, "Drainpipe_320", (6.0, 0, s * H), 0)
    place(kit, "Prop_ACUnit", (11.0, -0.2, 3.32), 0)    # wall-hung under the sill
    return Wd, Dp


def facade(kit, mats, samples):
    Wd, Dp = build_test_building(kit)
    person(mats, (5.0, -1.2, 0))
    camera((-7.5, -17.0, 1.7), (5.2, 0, 5.2), lens=24)
    render(os.path.join(OUT, "facade_test.png"))
    camera((1.0, -7.2, 1.65), (4.6, 0, 1.9), lens=30)
    render(os.path.join(OUT, "facade_ground.png"))
    camera((3.0, -6.0, 6.2), (7.6, 0, 6.8), lens=30)
    render(os.path.join(OUT, "facade_upper.png"))
    camera((-9.0, -14.0, 17.0), (6.0, 4.0, 7.0), lens=30)
    render(os.path.join(OUT, "facade_aerial.png"))


def main():
    only, samples = args()
    for job in ("contact", "facade", "pieces"):
        if job not in only:
            continue
        mats = setup_scene(samples)
        kit = import_kit(mats)
        {"contact": contact_sheet, "facade": facade, "pieces": piece_closeups}[job](kit, mats, samples)


if __name__ == "__main__":
    main()
