"""
make_kit.py -- Marevia modular building kit generator (headless Blender 4.2 LTS).

Run:
    ~/Applications/blender-4.2/blender -b --factory-startup -P Tools/blender/make_kit.py -- [--only A,B] [--no-check]

Contract: Tools/blender/KIT_SPEC.md (piece names, sizes, pivots, material slots, UVs, collision).
Output:   SourceArt/Kit/FBX/<Piece>.fbx  (one file per piece, mesh + UCX_<Piece>_NN collision)

-----------------------------------------------------------------------------------------------
Modelling approach
-----------------------------------------------------------------------------------------------
Every piece is assembled from single-material "parts" (bmesh boxes, profile prisms, cylinders).
Wall slabs get their openings cut with a Boolean modifier (EXACT solver, evaluated = applied) so
openings are real recesses (glass 17.5 cm behind the wall face). Convex edges are bevelled with
bmesh.ops.bevel (same operator as the Bevel modifier, applied directly; 2 cm / 2 segments on
masonry, smaller on joinery), EXCEPT edges lying on a tiling seam plane (X=0, X=W, Z=0, Z=H,
wall back) so adjacent bays and storeys butt without a groove. Wall slabs have no end caps at
X=0/X=W (avoids coplanar z-fighting with the perpendicular facade at building corners).
Finally the mesh is triangulated, UV0 is a world-scale cube projection (1 UV = 1 m), faces are
smooth-shaded with sharp edges above 50 deg, and a Weighted Normal modifier (face area, keep
sharp) is applied so bevels shade as soft highlights while flat faces stay flat.

-----------------------------------------------------------------------------------------------
FBX axes and scale -- why these settings
-----------------------------------------------------------------------------------------------
Export: bpy.ops.export_scene.fbx(use_selection=True, apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_ALL', global_scale=1.0,
        axis_forward='-Z', axis_up='Y', bake_space_transform=False, mesh_smooth_type='FACE')

* Scale: the scene is metres (unit scale 1.0). apply_unit_scale + FBX_SCALE_ALL writes the
  geometry in metres with UnitScaleFactor = 100 (1 file unit = 100 cm) and leaves every node at
  scale 1.0. Unreal converts the file to centimetres on import (FBX "Convert Scene Unit" /
  Interchange always does), so a 4 m wall becomes 400 uu with a clean (1,1,1) transform.
  The UE import script must therefore keep Convert Scene Unit ON (Interchange default).
* Axes: '-Z forward, Y up' is Blender's own description of how its Z-up right-handed space maps
  to FBX's Y-up right-handed convention; Unreal's importer ("Convert Scene" ON, "Force Front X
  Axis" OFF -- both defaults) undoes exactly that, so Z stays up and X stays X. Choosing other
  forward/up pairs (e.g. axis_forward='-Y') only ROTATES the mesh, which would break "Z up,
  X along the facade".
* Handedness: Unreal is LEFT-handed. Its importer keeps shapes un-mirrored by negating Y, i.e.
  Blender (x, y, z) m  ->  Unreal (100x, -100y, 100z) cm. A pure rotation can never turn
  "outside face toward Blender -Y" into "outside face toward Unreal -Y" while keeping X 0..W and
  Z up (that would need a reflection). So the exporter bakes a mirror in Y into the exported
  copy (mesh.transform(diag(1,-1,1)) + flip_normals, normals recomputed afterwards, UVs computed
  before the mirror so textures are not mirrored). Net result:
      Unreal coordinates (cm) == 100 x Blender coordinates (m) of this script, for every piece.
  i.e. in Unreal the facade runs X 0..W, Z up, outside face at Y=0 facing -Y, wall into +Y --
  exactly the numbers in KIT_SPEC.md. Visually a piece in UE is the left-right mirror of the
  Blender preview; every facade piece is mirror-symmetric about its bay centre, so nothing
  observable changes (only the hinge side of shutter knuckles swaps).
* The script re-imports every FBX into an empty scene and prints the bounding box both in the
  re-imported Blender frame and converted to Unreal cm, and checks it against KIT_SPEC.

UE import notes for Content/Python/oc_import_kit.py: Convert Scene ON, Force Front X OFF,
Convert Scene Unit ON, Normal Import Method = Import Normals (custom weighted normals),
Combine Meshes ON, Auto Generate Collision OFF (UCX_ is supplied), Nanite ON.

Assembly notes (what render_previews.py does; the UE assembler should match):
* Plinth_200/400 go under every ground bay EXCEPT Door_200 and Shopfront_400: those carry their
  own plinth stubs either side of the opening plus a stone skirt down to Z=-3.0 (so their Z range
  is -3.0..3.2), because a 0.6 m plinth in front of a doorway would block it.
* Corner_320 covers 0.2 m of each adjoining bay; shuttered windows are laid out so their folded
  shutter stacks stay clear of it (3-leaf folding shutters, 0.2 m stack).
* Wall slabs are open at X=0/X=W; at an outer corner the perpendicular wall's outer face closes
  the end, and Corner_320 covers the seam.
* RoofHip: scale X/Y to footprint + 2 x 0.40 m overhang, Z to the ridge height; ridge runs along
  X (rotate 90 deg when the building is deeper than wide). Sits on the cornice top (+0.35 m).
* Drainpipe_320: pipe axis at (0, -0.09); brackets reach Y=0 so the pivot snaps to a wall face.
  Stack at 3.2 m (socket at the top of each segment).

Pieces beyond KIT_SPEC: CorniceCorner (outer-corner mitre block for the cornice, pivot at the
building corner, occupies X<=0, Y<=0). Without it two perpendicular cornices leave a 30x30 cm
notch at every outer corner. Proposed addition to KIT_SPEC.
"""

import bpy
import bmesh
import math
import os
import sys
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
KIT_DIR = os.path.join(ROOT, "SourceArt", "Kit")
FBX_DIR = os.path.join(KIT_DIR, "FBX")

MAT_ORDER = ["Wall", "Trim", "Wood", "Glass", "Metal", "Roof", "Concrete",
             "PaintRed", "PaintWhite", "PaintBlack", "PaintBlue", "Paint"]  # paint: street fixtures (Paint = colour per instance)
PREVIEW_RGBA = {  # viewport colour only (FBX carries just the slot name)
    "Wall": (0.80, 0.58, 0.30, 1), "Trim": (0.75, 0.70, 0.58, 1), "Wood": (0.05, 0.14, 0.08, 1),
    "Glass": (0.02, 0.025, 0.03, 1), "Metal": (0.06, 0.06, 0.065, 1), "Roof": (0.46, 0.09, 0.04, 1),
    "Concrete": (0.38, 0.35, 0.31, 1),
    "PaintRed": (0.6, 0.03, 0.03, 1), "PaintWhite": (0.85, 0.85, 0.85, 1), "PaintBlack": (0.02, 0.02, 0.02, 1),
    "PaintBlue": (0.02, 0.12, 0.45, 1), "Paint": (0.2, 0.4, 0.2, 1),
}

# ---- metrics (KIT_SPEC / WORLD_METRICS) --------------------------------------------------------
H = 3.20          # storey
T = 0.30          # wall thickness
WIN_W, WIN_H, SILL = 1.10, 1.50, 0.90
FD_W, FD_H = 1.10, 2.40          # French door
DOOR_W, DOOR_H = 1.10, 2.20      # street door
SHOP_W, SHOP_H = 3.40, 2.60
RECESS = 0.22                    # depth of the opening recess (back face of the reveal)
GLASS_Y = 0.175                  # glass plane, 17.5 cm behind the wall face
BAND_H, BAND_D = 0.16, 0.03      # render band at the floor line
EPS = 1e-4
X, Y, Z = 0, 1, 2


# =================================================================================================
# geometry helpers (all bmesh, metres, piece-local Blender frame)
# =================================================================================================
def new_bm():
    return bmesh.new()


def add_box(bm, x0, x1, y0, y1, z0, z1):
    r = bmesh.ops.create_cube(bm, size=1.0)
    for v in r["verts"]:
        v.co = Vector((x0 if v.co.x < 0 else x1, y0 if v.co.y < 0 else y1, z0 if v.co.z < 0 else z1))
    return r["verts"]


def box(x0, x1, y0, y1, z0, z1):
    bm = new_bm()
    add_box(bm, x0, x1, y0, y1, z0, z1)
    return bm


def _axis_map(axis):
    # right-handed (u, v, axis) frames
    if axis == X:
        return lambda u, v, a: Vector((a, u, v))   # u=y, v=z
    if axis == Y:
        return lambda u, v, a: Vector((v, a, u))   # u=z, v=x
    return lambda u, v, a: Vector((u, v, a))       # u=x, v=y


def add_prism(bm, poly, axis, a0, a1, caps=True):
    """Extrude a 2D polygon along an axis. poly coordinates: axis X -> (y,z); axis Y -> (z,x);
    axis Z -> (x,y). Orientation is normalised so normals point outward."""
    area = sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
               for i in range(len(poly)))
    if area < 0:
        poly = list(reversed(poly))
    m = _axis_map(axis)
    bot = [bm.verts.new(m(u, v, a0)) for u, v in poly]
    top = [bm.verts.new(m(u, v, a1)) for u, v in poly]
    n = len(poly)
    faces = []
    for i in range(n):
        j = (i + 1) % n
        faces.append(bm.faces.new((bot[i], bot[j], top[j], top[i])))
    if caps:
        faces.append(bm.faces.new(top))
        faces.append(bm.faces.new(list(reversed(bot))))
    return faces


def prism_x(poly_yz, x0, x1, caps=True):
    bm = new_bm()
    add_prism(bm, poly_yz, X, x0, x1, caps)
    return bm


def add_cyl(bm, center, r, length, seg=12, axis="Z", caps=True, r2=None):
    """Cylinder whose base centre is `center`, extending `length` along +axis."""
    rot = {"Z": Matrix.Identity(4), "X": Matrix.Rotation(math.pi / 2, 4, "Y"),
           "Y": Matrix.Rotation(-math.pi / 2, 4, "X")}[axis]
    d = {"Z": Vector((0, 0, 1)), "X": Vector((1, 0, 0)), "Y": Vector((0, 1, 0))}[axis]
    mat = Matrix.Translation(Vector(center) + d * (length / 2)) @ rot
    bmesh.ops.create_cone(bm, cap_ends=caps, cap_tris=False, segments=seg, radius1=r,
                          radius2=r if r2 is None else r2, depth=length, matrix=mat)


def transform(bm, mat):
    bmesh.ops.transform(bm, matrix=mat, verts=bm.verts)


def on_plane(v, axis, val):
    return abs(v.co[axis] - val) < EPS


def del_plane(bm, axis, val):
    fs = [f for f in bm.faces if all(on_plane(v, axis, val) for v in f.verts)]
    if fs:
        bmesh.ops.delete(bm, geom=fs, context="FACES")


def bevel(bm, width, segs=2, keep=(), convex_only=True, min_angle=30.0):
    """Bevel convex edges sharper than min_angle, skipping edges that lie on a `keep` plane
    (list of (axis, value)) -- those are tiling seams that must stay square."""
    bm.normal_update()
    edges = []
    lim = math.radians(min_angle)
    for e in bm.edges:
        if len(e.link_faces) != 2:
            continue
        if e.calc_face_angle(0.0) < lim:
            continue
        if convex_only and not e.is_convex:
            continue
        if any(on_plane(e.verts[0], a, val) and on_plane(e.verts[1], a, val) for a, val in keep):
            continue
        edges.append(e)
    if edges:
        bmesh.ops.bevel(bm, geom=edges, offset=width, offset_type="OFFSET", profile_type="SUPERELLIPSE",
                        segments=segs, profile=0.5, affect="EDGES", clamp_overlap=True,
                        miter_outer="SHARP", miter_inner="SHARP", vmesh_method="ADJ")
    return bm


_tmp_coll = None


def _scratch_coll():
    global _tmp_coll
    if _tmp_coll is None or _tmp_coll.name not in bpy.data.collections:
        _tmp_coll = bpy.data.collections.new("_kit_tmp")
        bpy.context.scene.collection.children.link(_tmp_coll)
    return _tmp_coll


def bool_diff(bm, cutter_bm):
    """Boolean DIFFERENCE via the Boolean modifier (EXACT), returns the applied result."""
    coll = _scratch_coll()
    me_a = bpy.data.meshes.new("_bool_a")
    bm.to_mesh(me_a)
    me_b = bpy.data.meshes.new("_bool_b")
    cutter_bm.to_mesh(me_b)
    ob_a = bpy.data.objects.new("_bool_a", me_a)
    ob_b = bpy.data.objects.new("_bool_b", me_b)
    coll.objects.link(ob_a)
    coll.objects.link(ob_b)
    mod = ob_a.modifiers.new("cut", "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.solver = "EXACT"
    mod.object = ob_b
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob_a.evaluated_get(dg)
    m = ev.to_mesh()
    out = new_bm()
    out.from_mesh(m)
    ev.to_mesh_clear()
    for ob in (ob_a, ob_b):
        bpy.data.objects.remove(ob)
    bpy.data.meshes.remove(me_a)
    bpy.data.meshes.remove(me_b)
    bm.free()
    cutter_bm.free()
    return out


def join_bm(dst, src):
    me = bpy.data.meshes.new("_join")
    src.to_mesh(me)
    dst.from_mesh(me)
    bpy.data.meshes.remove(me)


# =================================================================================================
# Piece container
# =================================================================================================
class Piece:
    def __init__(self, name, kind="facade"):
        self.name = name
        self.kind = kind
        self.parts = []      # (bmesh, material)
        self.ucx = []        # list of boxes (x0,x1,y0,y1,z0,z1) or ("hull", [verts])

    def add(self, bm, mat):
        assert mat in MAT_ORDER, mat
        self.parts.append((bm, mat))
        return bm

    def merged(self):
        out = new_bm()
        for bm, mat in self.parts:
            idx = MAT_ORDER.index(mat)
            for f in bm.faces:
                f.material_index = idx
            join_bm(out, bm)
            bm.free()
        self.parts = []
        return out


# =================================================================================================
# shared building elements
# =================================================================================================
def wall_slab(P, W, cuts=(), z0=0.0, z1=H, thick=T, mat="Wall"):
    """Wall slab X 0..W with box recesses cut by Boolean. cuts: (x0, x1, zb, zt, depth)."""
    bm = box(0, W, 0, thick, z0, z1)
    if cuts:
        cb = new_bm()
        for (cx0, cx1, zb, zt, depth) in cuts:
            add_box(cb, cx0, cx1, -0.10, depth, zb if zb > z0 + EPS else z0 - 0.10, zt)
        bm = bool_diff(bm, cb)
    del_plane(bm, X, 0.0)
    del_plane(bm, X, W)
    bevel(bm, 0.02, 2, keep=[(X, 0.0), (X, W), (Z, z0), (Z, z1), (Y, thick)])
    P.add(bm, mat)


def render_band(P, x0, x1, keep_ends=True):
    bm = box(x0, x1, -BAND_D, 0.0, 0.0, BAND_H)
    del_plane(bm, Y, 0.0)
    if keep_ends:
        del_plane(bm, X, x0)
        del_plane(bm, X, x1)
    bevel(bm, 0.012, 2, keep=[(Z, 0.0), (X, x0), (X, x1)])
    P.add(bm, "Wall")


PLINTH_PROFILE = [(-0.06, -3.0), (0.30, -3.0), (0.30, 0.60), (-0.045, 0.60), (-0.085, 0.565),
                  (-0.085, 0.495), (-0.06, 0.47)]


def plinth_block(P, x0, x1, seams=(), joint_step=1.0, joint_origin=0.0):
    """Stone base X x0..x1, Z -3.0..0.6, with sawn joints every joint_step (half joints at seams)."""
    bm = prism_x(PLINTH_PROFILE, x0, x1)
    cb = new_bm()
    k = math.ceil((x0 - joint_origin) / joint_step - EPS)
    xs = []
    while joint_origin + k * joint_step <= x1 + EPS:
        xs.append(joint_origin + k * joint_step)
        k += 1
    for xj in xs:
        add_box(cb, xj - 0.008, xj + 0.008, -0.2, -0.047, -3.1, 0.7)
    if xs:
        bm = bool_diff(bm, cb)
    else:
        cb.free()
    bevel(bm, 0.012, 2, keep=[(X, s) for s in seams] + [(Z, -3.0), (Y, 0.30)])
    P.add(bm, "Trim")


def sill(P, ox0, ox1, ztop, over=0.12, proj=0.08, back=0.16, th=0.075):
    bm = box(ox0 - over, ox1 + over, -proj, back, ztop - th, ztop)
    bevel(bm, 0.015, 2)
    P.add(bm, "Trim")
    # drip lip under the projecting part
    bm = box(ox0 - over + 0.02, ox1 + over - 0.02, -proj + 0.01, 0.0, ztop - th - 0.025, ztop - th + 0.002)
    bevel(bm, 0.006, 1, keep=[(Y, 0.0)])
    P.add(bm, "Trim")


def surround(P, ox0, ox1, zb, zt, sw=0.10, proj=0.03, head_h=0.20, cap=True, keystone=False):
    """Stone architrave: two jambs + head + small cap moulding. Back face sits on the wall face."""
    for a0, a1 in ((ox0 - sw, ox0), (ox1, ox1 + sw)):
        bm = box(a0, a1, -proj, 0.0, zb, zt)
        bevel(bm, 0.01, 2, keep=[(Y, 0.0)])
        P.add(bm, "Trim")
    bm = box(ox0 - sw, ox1 + sw, -proj - 0.005, 0.0, zt, zt + head_h)
    bevel(bm, 0.012, 2, keep=[(Y, 0.0)])
    P.add(bm, "Trim")
    top = zt + head_h
    if keystone:
        cx = (ox0 + ox1) / 2
        bm = box(cx - 0.08, cx + 0.08, -proj - 0.025, 0.0, zt - 0.03, top + 0.02)
        bevel(bm, 0.01, 2, keep=[(Y, 0.0)])
        P.add(bm, "Trim")
    if cap:
        prof = [(0.0, top), (-proj - 0.035, top), (-proj - 0.05, top + 0.02), (-proj - 0.05, top + 0.045),
                (-proj - 0.035, top + 0.06), (0.0, top + 0.06)]
        bm = prism_x(prof, ox0 - sw - 0.04, ox1 + sw + 0.04)
        del_plane(bm, Y, 0.0)
        bevel(bm, 0.008, 2)
        P.add(bm, "Trim")
        return top + 0.06
    return top


def casement(P, ox0, ox1, zb, zt, leaves=2, bars=2, panel_h=0.0, fw=0.06, y0=0.145, y1=0.205):
    """Fixed frame + casement leaves with glazing bars + one glass plane at GLASS_Y.
    panel_h > 0 gives the leaves a solid lower wooden panel (French doors)."""
    # outer fixed frame, embedded 1 cm into the reveal
    for bm in (box(ox0 - 0.01, ox0 + fw, y0, y1, zb, zt + 0.01),
               box(ox1 - fw, ox1 + 0.01, y0, y1, zb, zt + 0.01),
               box(ox0 + fw - 0.002, ox1 - fw + 0.002, y0, y1, zt - fw, zt + 0.01),
               box(ox0 + fw - 0.002, ox1 - fw + 0.002, y0 - 0.01, y1, zb, zb + fw)):
        bevel(bm, 0.006, 1, keep=[(Y, y1)])
        P.add(bm, "Wood")
    ix0, ix1, iz0, iz1 = ox0 + fw, ox1 - fw, zb + fw, zt - fw
    lw = (ix1 - ix0) / leaves
    lf = 0.05
    ly0, ly1 = y0 + 0.01, y1 - 0.01
    for i in range(leaves):
        a0, a1 = ix0 + i * lw, ix0 + (i + 1) * lw
        members = [box(a0, a0 + lf, ly0, ly1, iz0, iz1), box(a1 - lf, a1, ly0, ly1, iz0, iz1),
                   box(a0 + lf - 0.002, a1 - lf + 0.002, ly0, ly1, iz1 - lf, iz1),
                   box(a0 + lf - 0.002, a1 - lf + 0.002, ly0, ly1, iz0, iz0 + lf + 0.02)]
        gz0 = iz0 + lf + 0.02
        if panel_h > 0:
            members.append(box(a0 + lf - 0.002, a1 - lf + 0.002, ly0, ly1, gz0 + panel_h - 0.05, gz0 + panel_h))
        for bm in members:
            P.add(bm, "Wood")
        if panel_h > 0:
            # raised field on the lower panel
            pnl = box(a0 + lf - 0.002, a1 - lf + 0.002, ly0 + 0.012, ly1 - 0.012, gz0 - 0.002, gz0 + panel_h - 0.048)
            P.add(pnl, "Wood")
            fld = box(a0 + lf + 0.04, a1 - lf - 0.04, ly0 + 0.002, ly1 - 0.02, gz0 + 0.05, gz0 + panel_h - 0.10)
            bevel(fld, 0.012, 1)
            P.add(fld, "Wood")
            gz0 += panel_h
        # glazing bars
        gz1 = iz1 - lf
        for k in range(1, bars + 1):
            zc = gz0 + (gz1 - gz0) * k / (bars + 1)
            P.add(box(a0 + lf - 0.002, a1 - lf + 0.002, ly0 + 0.008, ly1 - 0.008, zc - 0.011, zc + 0.011), "Wood")
    # glass plane (single quad facing -Y)
    g = new_bm()
    vs = [g.verts.new((ix0, GLASS_Y, iz0)), g.verts.new((ix1, GLASS_Y, iz0)),
          g.verts.new((ix1, GLASS_Y, iz1)), g.verts.new((ix0, GLASS_Y, iz1))]
    g.faces.new(vs)
    P.add(g, "Glass")


def louvred_leaf(P, xa, xb, z0, z1, yb, t=0.03, pitch=0.062):
    """Louvred shutter leaf lying in the XZ plane, back face at y=yb, front at yb-t."""
    yf = yb - t
    sw = 0.045
    for bm in (box(xa, xa + sw, yf, yb, z0, z1), box(xb - sw, xb, yf, yb, z0, z1)):
        bevel(bm, 0.004, 1, keep=[(Y, yb)])
        P.add(bm, "Wood")
    rails = [(z0, z0 + 0.10), (z1 - 0.07, z1)]
    if z1 - z0 > 1.8:
        zm = z0 + (z1 - z0) * 0.42
        rails.append((zm - 0.035, zm + 0.035))
    rails.sort()
    for r0, r1 in rails:
        bm = box(xa + sw - 0.002, xb - sw + 0.002, yf + 0.002, yb, r0, r1)
        bevel(bm, 0.004, 1, keep=[(Y, yb)])
        P.add(bm, "Wood")
    # louvres between rails: thin blades at 45 deg, front edge low (sheds rain outward)
    yc = (yf + yb) / 2
    L, th = 0.0185, 0.0065
    d = Vector((1, 1)).normalized()     # (y, z) from front-bottom to back-top
    nrm = Vector((-1, 1)).normalized()
    for (ra, rb) in zip(rails[:-1], rails[1:]):
        zlo, zhi = ra[1], rb[0]
        n = max(1, int((zhi - zlo) / pitch))
        step = (zhi - zlo) / n
        bm = new_bm()
        for k in range(n):
            zc = zlo + step * (k + 0.5)
            c = Vector((yc, zc))
            pts = [c - d * L - nrm * th / 2, c + d * L - nrm * th / 2, c + d * L + nrm * th / 2,
                   c - d * L + nrm * th / 2]
            add_prism(bm, [(p.x, p.y) for p in pts], X, xa + sw - 0.004, xb - sw + 0.004, caps=False)
        P.add(bm, "Wood")


def shutter_stack(P, xa, xb, z0, z1, y_back, hinge_x):
    """Three-leaf folding shutter folded flat against the wall: two leaves (one block, with a
    reveal line) under a louvred outer leaf. hinge_x = x of the hinge side (knuckles)."""
    inner = box(xa + 0.006, xb - 0.006, y_back - 0.052, y_back, z0 + 0.004, z1 - 0.004)
    bevel(inner, 0.004, 1, keep=[(Y, y_back)])
    P.add(inner, "Wood")
    # dark line between the two inner leaves
    louvred_leaf(P, xa, xb, z0, z1, y_back - 0.052, t=0.032)
    knuckles = new_bm()
    for zz in (z0 + 0.18, z1 - 0.22):
        add_cyl(knuckles, (hinge_x, y_back - 0.045, zz), 0.011, 0.07, seg=6, caps=False)
    P.add(knuckles, "Metal")
    # hold-back catch on the wall beside the free edge
    free_x = xa - 0.012 if hinge_x > (xa + xb) / 2 else xb + 0.012
    catch = box(free_x - 0.006, free_x + 0.006, y_back - 0.045, y_back + 0.01, z0 + 0.64, z0 + 0.68)
    P.add(catch, "Metal")


def window_unit(P, cx, kind="shutters", zb=SILL):
    """One 1.10 x 1.50 window centred on cx: recess cut is added by the caller.
    kind: 'shutters' | 'plain' | 'grille'"""
    ox0, ox1 = cx - WIN_W / 2, cx + WIN_W / 2
    zt = zb + WIN_H
    sill(P, ox0, ox1, zb + 0.02)
    surround(P, ox0, ox1, zb + 0.02, zt)
    casement(P, ox0, ox1, zb + 0.015, zt)
    if kind == "shutters":
        sw = 0.195
        shutter_stack(P, ox0 - sw, ox0 - 0.005, zb + 0.02, zt + 0.02, -0.03, ox0 - 0.005)
        shutter_stack(P, ox1 + 0.005, ox1 + sw, zb + 0.02, zt + 0.02, -0.03, ox1 + 0.005)
    elif kind == "grille":
        g = new_bm()
        n = 10
        for k in range(n):
            xx = ox0 + (ox1 - ox0) * (k + 0.5) / n
            add_box(g, xx - 0.009, xx + 0.009, 0.03, 0.048, zb + 0.015, zt + 0.012)
        for zz in (zb + 0.22, zb + WIN_H / 2, zt - 0.22):
            add_box(g, ox0 - 0.01, ox1 + 0.01, 0.034, 0.044, zz - 0.02, zz + 0.02)
        P.add(g, "Metal")
    return (ox0, ox1, zb, zt, RECESS)


# =================================================================================================
# pieces
# =================================================================================================
def make_wall(W):
    P = Piece(f"Wall_{int(W * 100)}")
    wall_slab(P, W)
    render_band(P, 0, W)
    P.ucx = [(0, W, 0, T, 0, H)]
    return P


def make_window(W, kind):
    name = {"shutters": "Window", "plain": "WindowPlain"}[kind]
    P = Piece(f"{name}_{int(W * 100)}")
    centres = [1.0] if W < 3 else [1.0, 3.0]
    cuts = [window_unit(P, c, kind) for c in centres]
    wall_slab(P, W, cuts)
    render_band(P, 0, W)
    P.ucx = [(0, W, 0, T, 0, H)]
    return P


def make_ground_window():
    P = Piece("GroundWindow_400")
    W = 4.0
    cuts = [window_unit(P, c, "grille") for c in (1.0, 3.0)]
    wall_slab(P, W, cuts)
    P.ucx = [(0, W, 0, T, 0, H)]
    return P


def add_bar(bm, x, y, z0, z1, hw):
    """Square bar along Z without end caps (ends are buried in rails)."""
    add_prism(bm, [(x - hw, y - hw), (x + hw, y - hw), (x + hw, y + hw), (x - hw, y + hw)], Z, z0, z1, caps=False)


def railing(P, x0, x1, yf, zb, ht=1.0):
    """Wrought-iron railing: front run at y=yf from x0..x1, side returns back to the wall."""
    zt = zb + ht
    m = new_bm()
    post = 0.03
    # corner posts
    for (px, py) in ((x0, yf), (x1, yf)):
        add_box(m, px - post / 2, px + post / 2, py - post / 2, py + post / 2, zb - 0.01, zt - 0.02)
    # rails: bottom flat bar, upper rail of the cross band
    zr0, zr1 = zb + 0.08, zt - 0.14
    runs = [("x", x0, x1, yf), ("y", yf, -0.0, x0), ("y", yf, -0.0, x1)]
    for kind, a, b, c in runs:
        for zz, hh in ((zr0, 0.025), (zr1, 0.018)):
            if kind == "x":
                add_box(m, a, b, c - 0.006, c + 0.006, zz, zz + hh)
            else:
                add_box(m, c - 0.006, c + 0.006, a, b + 0.01, zz, zz + hh)
    # balusters + cross band
    sp = 0.115
    for kind, a, b, c in runs:
        L = abs(b - a)
        n = max(1, int(round(L / sp)))
        step = (b - a) / n
        for k in range(1, n):
            p = a + step * k
            if kind == "x":
                add_bar(m, p, c, zr0, zt - 0.03, 0.007)
            else:
                add_bar(m, c, p, zr0, zt - 0.03, 0.007)
        # X crosses in the band between zr1+0.018 and the handrail
        zc0, zc1 = zr1 + 0.018, zt - 0.03
        for k in range(n):
            p0, p1 = a + step * k, a + step * (k + 1)
            for (s0, s1) in ((p0, p1), (p1, p0)):
                v = Vector((s1 - s0, 0, zc1 - zc0)) if kind == "x" else Vector((0, s1 - s0, zc1 - zc0))
                length = v.length
                bar = new_bm()
                add_bar(bar, 0.0, 0.0, 0.0, length, 0.005)
                ang = math.atan2(s1 - s0, zc1 - zc0)
                rot = Matrix.Rotation(ang, 4, "Y") if kind == "x" else Matrix.Rotation(-ang, 4, "X")
                pos = Vector((s0, c, zc0)) if kind == "x" else Vector((c, s0, zc0))
                transform(bar, Matrix.Translation(pos) @ rot)
                join_bm(m, bar)
                bar.free()
    P.add(m, "Metal")
    # handrail (moulded flat bar) -- separate so it can be bevelled
    for kind, a, b, c in runs:
        if kind == "x":
            hb = box(a - 0.025, b + 0.025, c - 0.025, c + 0.025, zt - 0.03, zt)
        else:
            hb = box(c - 0.025, c + 0.025, a - 0.025 if a < b else b, -0.0 + 0.02, zt - 0.03, zt)
        bevel(hb, 0.008, 1)
        P.add(hb, "Metal")


def make_balcony():
    P = Piece("Balcony_400")
    W, cx = 4.0, 2.0
    ox0, ox1 = cx - FD_W / 2, cx + FD_W / 2
    zs = 0.17               # slab top = door threshold level
    zb, zt = zs + 0.02, zs + 0.02 + FD_H - 0.02
    # slab (concrete/stone), moulded edge, stays above Z=0
    sx0, sx1, sd = 0.80, 3.20, 0.80
    bm = box(sx0, sx1, -sd, 0.01, 0.035, zs)
    bevel(bm, 0.02, 2, keep=[(Y, 0.01)])
    P.add(bm, "Concrete")
    bm = box(sx0 + 0.05, sx1 - 0.05, -sd + 0.05, 0.01, 0.0, 0.045)
    bevel(bm, 0.015, 2, keep=[(Y, 0.01)])
    P.add(bm, "Concrete")
    # threshold
    bm = box(ox0 - 0.02, ox1 + 0.02, -0.02, 0.17, zs - 0.02, zb)
    bevel(bm, 0.008, 1)
    P.add(bm, "Trim")
    surround(P, ox0, ox1, zb, zt + 0.02, keystone=True)
    casement(P, ox0, ox1, zb - 0.005, zt + 0.02, leaves=2, bars=2, panel_h=0.55)
    sw = 0.195
    shutter_stack(P, ox0 - sw, ox0 - 0.005, zb, zt + 0.04, -0.03, ox0 - 0.005)
    shutter_stack(P, ox1 + 0.005, ox1 + sw, zb, zt + 0.04, -0.03, ox1 + 0.005)
    railing(P, sx0 + 0.04, sx1 - 0.04, -sd + 0.04, zs, 1.0)
    wall_slab(P, W, [(ox0, ox1, zs - 0.02, zt + 0.02, RECESS)])
    render_band(P, 0, W)
    P.ucx = [(0, W, 0, T, 0, H)]
    return P


def make_door():
    P = Piece("Door_200")
    W, cx = 2.0, 1.0
    ox0, ox1 = cx - DOOR_W / 2, cx + DOOR_W / 2
    step_h = 0.17
    zb, zt = step_h, step_h + DOOR_H
    sw = 0.18
    # step + below-ground skirt (so slopes never show a gap)
    bm = box(ox0 - sw, ox1 + sw, -0.32, 0.17, 0.0, step_h)
    bevel(bm, 0.02, 2, keep=[(Z, 0.0)])
    P.add(bm, "Trim")
    bm = box(ox0 - sw, ox1 + sw, -0.30, T, -3.0, 0.0)
    P.add(bm, "Trim")
    # plinth stubs left/right of the surround
    plinth_block(P, 0.0, ox0 - sw, seams=(0.0,))
    plinth_block(P, ox1 + sw, W, seams=(W,))
    # stone surround with keystone
    surround(P, ox0, ox1, zb, zt, sw=sw, proj=0.05, head_h=0.24, keystone=True)
    # frame
    fw = 0.07
    for bm in (box(ox0 - 0.01, ox0 + fw, 0.13, 0.21, zb, zt + 0.01),
               box(ox1 - fw, ox1 + 0.01, 0.13, 0.21, zb, zt + 0.01),
               box(ox0 + fw - 0.002, ox1 - fw + 0.002, 0.13, 0.21, zt - fw, zt + 0.01)):
        bevel(bm, 0.006, 1, keep=[(Y, 0.21)])
        P.add(bm, "Wood")
    # two panelled leaves
    lx0, lx1, lz0, lz1 = ox0 + fw, ox1 - fw, zb + 0.005, zt - fw
    mid = (lx0 + lx1) / 2
    for a0, a1 in ((lx0, mid - 0.003), (mid + 0.003, lx1)):
        leaf = box(a0, a1, 0.15, 0.20, lz0, lz1)
        bevel(leaf, 0.006, 1, keep=[(Y, 0.20)])
        P.add(leaf, "Wood")
        # three raised fields: bottom, tall middle, top
        sw_ = 0.085
        fields = [(lz0 + 0.12, lz0 + 0.55), (lz0 + 0.67, lz1 - 0.42), (lz1 - 0.32, lz1 - 0.10)]
        for f0, f1 in fields:
            # sunk moulding: a frame ring proud of the leaf, raised field inside
            fr = box(a0 + sw_, a1 - sw_, 0.138, 0.151, f0, f1)
            bevel(fr, 0.006, 1, keep=[(Y, 0.151)])
            P.add(fr, "Wood")
            fl = box(a0 + sw_ + 0.035, a1 - sw_ - 0.035, 0.130, 0.140, f0 + 0.035, f1 - 0.035)
            bevel(fl, 0.012, 1, keep=[(Y, 0.140)])
            P.add(fl, "Wood")
    # meeting-stile astragal
    bm = box(mid - 0.018, mid + 0.018, 0.138, 0.152, lz0, lz1)
    bevel(bm, 0.006, 1, keep=[(Y, 0.152)])
    P.add(bm, "Wood")
    # ironmongery: knob, keyhole plate, knocker ring (box ring)
    m = new_bm()
    add_cyl(m, (mid + 0.07, 0.15, zb + 1.02), 0.025, 0.05, seg=10, axis="Y", r2=0.025)
    add_box(m, mid - 0.06, mid - 0.035, 0.143, 0.15, zb + 0.95, zb + 1.07)
    add_cyl(m, (mid, 0.13, zb + 1.45), 0.035, 0.02, seg=10, axis="Y")
    P.add(m, "Metal")
    # house number plate (blank) on the wall
    bm = box(ox1 + sw + 0.06, ox1 + sw + 0.20, -0.012, 0.0, zb + 1.65, zb + 1.77)
    bevel(bm, 0.004, 1, keep=[(Y, 0.0)])
    P.add(bm, "Trim")
    wall_slab(P, W, [(ox0, ox1, zb - 0.02, zt, RECESS)])
    P.ucx = [(0, W, 0, T, 0, H)]
    return P


def make_shopfront():
    P = Piece("Shopfront_400")
    W = 4.0
    ox0, ox1 = (W - SHOP_W) / 2, (W + SHOP_W) / 2     # 0.30 .. 3.70
    zt = SHOP_H
    jw = 0.10
    # stone frame: jambs + lintel
    for a0, a1 in ((ox0 - jw, ox0), (ox1, ox1 + jw)):
        bm = box(a0, a1, -0.045, 0.0, 0.0, zt + 0.10)
        bevel(bm, 0.012, 2, keep=[(Y, 0.0), (Z, 0.0)])
        P.add(bm, "Trim")
    bm = box(ox0 - jw, ox1 + jw, -0.05, 0.0, zt, zt + 0.10)
    bevel(bm, 0.012, 2, keep=[(Y, 0.0)])
    P.add(bm, "Trim")
    # plinth stubs + skirt below the opening
    plinth_block(P, 0.0, ox0 - jw, seams=(0.0,))
    plinth_block(P, ox1 + jw, W, seams=(W,))
    P.add(box(ox0 - jw, ox1 + jw, -0.045, T, -3.0, 0.0), "Trim")
    # threshold
    bm = box(ox0, ox1, -0.03, RECESS + 0.02, 0.0, 0.035)
    bevel(bm, 0.01, 1, keep=[(Z, 0.0)])
    P.add(bm, "Trim")
    # sign board (blank, painted wood) with raised border and a small cap
    sz0, sz1 = zt + 0.10, 3.12
    sx0, sx1 = ox0, ox1
    bm = box(sx0, sx1, -0.04, 0.0, sz0, sz1)
    P.add(bm, "Wood")
    for b in (box(sx0, sx0 + 0.05, -0.06, 0.0, sz0, sz1), box(sx1 - 0.05, sx1, -0.06, 0.0, sz0, sz1),
              box(sx0 + 0.05 - 0.002, sx1 - 0.05 + 0.002, -0.06, 0.0, sz0, sz0 + 0.05),
              box(sx0 + 0.05 - 0.002, sx1 - 0.05 + 0.002, -0.06, 0.0, sz1 - 0.05, sz1)):
        bevel(b, 0.008, 1, keep=[(Y, 0.0)])
        P.add(b, "Wood")
    prof = [(0.0, sz1), (-0.075, sz1), (-0.09, sz1 + 0.02), (-0.09, sz1 + 0.04), (-0.07, sz1 + 0.06), (0.0, sz1 + 0.06)]
    bm = prism_x(prof, sx0 - 0.08, sx1 + 0.08)
    del_plane(bm, Y, 0.0)
    bevel(bm, 0.008, 2)
    P.add(bm, "Trim")
    # shopfront joinery deep in the recess
    fy0, fy1 = 0.17, 0.235
    fw = 0.065
    wood = []
    wood.append(box(ox0 - 0.01, ox0 + fw, fy0, fy1, 0.03, zt + 0.01))            # left post
    wood.append(box(ox1 - fw, ox1 + 0.01, fy0, fy1, 0.03, zt + 0.01))            # right post
    wood.append(box(ox0 + fw, ox1 - fw, fy0, fy1, zt - fw, zt + 0.01))            # head
    dx0, dx1 = ox1 - fw - 1.0, ox1 - fw                                           # door 1.0 wide
    wood.append(box(dx0 - fw, dx0, fy0, fy1, 0.03, zt - fw))                     # door post
    wood.append(box(ox0 + fw, ox1 - fw, fy0, fy1, 2.12, 2.12 + 0.06))            # transom
    wood.append(box(ox0 + fw, dx0 - fw, fy0, fy1, 0.48, 0.48 + 0.06))            # stallriser top rail
    mx = (ox0 + fw + dx0 - fw) / 2
    wood.append(box(mx - 0.03, mx + 0.03, fy0, fy1, 0.54, 2.12))                 # display mullion
    wood.append(box(ox0 + fw, dx0 - fw, fy0 + 0.02, fy1, 0.03, 0.48))            # stallriser panel
    # door leaf: stiles, rails
    wood.append(box(dx0, dx0 + 0.09, fy0 + 0.01, fy1 - 0.01, 0.035, 2.12))
    wood.append(box(dx1 - 0.09, dx1, fy0 + 0.01, fy1 - 0.01, 0.035, 2.12))
    wood.append(box(dx0 + 0.09, dx1 - 0.09, fy0 + 0.01, fy1 - 0.01, 0.035, 0.30))
    wood.append(box(dx0 + 0.09, dx1 - 0.09, fy0 + 0.01, fy1 - 0.01, 2.02, 2.12))
    for b in wood:
        bevel(b, 0.007, 1)
        P.add(b, "Wood")
    # raised fields on the stallriser
    for a0, a1 in ((ox0 + fw + 0.08, mx - 0.10), (mx + 0.10, dx0 - fw - 0.08)):
        b = box(a0, a1, fy0 + 0.008, fy0 + 0.022, 0.12, 0.40)
        bevel(b, 0.012, 1)
        P.add(b, "Wood")
    # door handle bar
    m = new_bm()
    add_box(m, dx0 + 0.13, dx0 + 0.15, fy0 - 0.05, fy0 - 0.03, 0.85, 1.35)
    for zz in (0.88, 1.32):
        add_box(m, dx0 + 0.13, dx0 + 0.15, fy0 - 0.03, fy0 + 0.01, zz - 0.01, zz + 0.01)
    P.add(m, "Metal")
    # glass
    g = new_bm()
    gy = 0.205
    for (a0, a1, z0, z1) in ((ox0 + fw, dx0 - fw, 0.54, 2.12), (ox0 + fw, ox1 - fw, 2.18, zt - fw),
                             (dx0 + 0.09, dx1 - 0.09, 0.30, 2.02)):
        vs = [g.verts.new((a0, gy, z0)), g.verts.new((a1, gy, z0)), g.verts.new((a1, gy, z1)), g.verts.new((a0, gy, z1))]
        g.faces.new(vs)
    P.add(g, "Glass")
    # rolling shutter: guides + half-raised corrugated curtain + bottom bar with handles
    m = new_bm()
    for a0, a1 in ((ox0 - 0.01, ox0 + 0.05), (ox1 - 0.05, ox1 + 0.01)):
        add_box(m, a0, a1, 0.005, 0.065, 0.035, zt + 0.01)
    P.add(m, "Metal")
    cz0, cz1 = 1.38, zt + 0.01
    cx0, cx1 = ox0 + 0.02, ox1 - 0.02
    pitch = 0.07
    n = int((cz1 - (cz0 + 0.07)) / pitch)
    prof = []
    zb_ = cz0 + 0.07
    # front zig-zag (y small = outside), back flat
    pts_front = []
    for k in range(n + 1):
        z = zb_ + k * pitch
        pts_front.append((0.026, z))
        if k < n:
            pts_front.append((0.016, z + pitch * 0.35))
            pts_front.append((0.016, z + pitch * 0.65))
    prof = pts_front + [(0.04, zb_ + n * pitch), (0.04, zb_)]
    bm = prism_x(prof, cx0, cx1)
    P.add(bm, "Metal")
    bm = box(cx0, cx1, 0.008, 0.045, cz0, zb_ + 0.002)
    bevel(bm, 0.006, 1)
    P.add(bm, "Metal")
    m = new_bm()
    for hx in (cx0 + 0.5, cx1 - 0.5):
        add_box(m, hx - 0.06, hx + 0.06, -0.005, 0.008, cz0 + 0.02, cz0 + 0.035)
        add_box(m, hx - 0.06, hx - 0.045, -0.005, 0.01, cz0 + 0.02, cz0 + 0.05)
        add_box(m, hx + 0.045, hx + 0.06, -0.005, 0.01, cz0 + 0.02, cz0 + 0.05)
    P.add(m, "Metal")
    wall_slab(P, W, [(ox0, ox1, 0.0, zt, RECESS + 0.02)])
    P.ucx = [(0, W, 0, T, 0, H)]
    return P


def make_corner():
    P = Piece("Corner_320")
    n = 8
    ch = H / n
    for k in range(n):
        z0, z1 = k * ch, (k + 1) * ch
        r = 0.2 if k % 2 == 0 else 0.175
        bm = box(-r, 0.2, -r, 0.2, z0, z1)
        bevel(bm, 0.02, 2, keep=[(X, 0.2), (Y, 0.2)])
        P.add(bm, "Trim")
    P.ucx = [(-0.2, 0.2, -0.2, 0.2, 0, H)]
    return P


def make_plinth(W):
    P = Piece(f"Plinth_{int(W * 100)}")
    plinth_block(P, 0.0, W, seams=(0.0, W))
    P.ucx = [(0, W, -0.06, T, -3.0, 0.6)]
    return P


CORNICE_PROFILE_OUT = [  # (y, z) from wall face at the bottom, outward and up, back to the wall at the top
    (0.0, 0.0), (-0.04, 0.0), (-0.04, 0.07), (-0.06, 0.075), (-0.06, 0.16), (-0.12, 0.16),
]


def _cornice_outline():
    pts = list(CORNICE_PROFILE_OUT)
    # ovolo: from (-0.12, 0.16) to (-0.22, 0.25), centre (-0.12, 0.25)
    for i in range(1, 7):
        t = (math.pi / 2) * i / 6
        pts.append((-0.12 - 0.10 * math.sin(t), 0.25 - 0.09 * math.cos(t)))
    pts += [(-0.275, 0.25), (-0.275, 0.243), (-0.285, 0.243), (-0.285, 0.31), (-0.30, 0.315),
            (-0.30, 0.335), (-0.285, 0.35), (0.0, 0.35)]
    return pts


def make_cornice(W):
    P = Piece(f"Cornice_{int(W * 100)}")
    outer = _cornice_outline()
    prof = outer + [(T, 0.35), (T, 0.0)]
    bm = prism_x(prof, 0.0, W)
    bevel(bm, 0.008, 2, keep=[(X, 0.0), (X, W), (Y, T)])
    P.add(bm, "Trim")
    # dentils every 0.20 m, offset 0.10 so bays continue the rhythm
    d = new_bm()
    k = 0
    while 0.10 + 0.20 * k < W:
        xc = 0.10 + 0.20 * k
        add_box(d, xc - 0.045, xc + 0.045, -0.11, -0.055, 0.085, 0.16)
        k += 1
    P.add(d, "Trim")
    P.ucx = [(0, W, -0.30, T, 0, 0.35)]
    return P


def make_cornice_corner():
    P = Piece("CorniceCorner")
    outer = _cornice_outline()
    bm = new_bm()
    A, M, B = [], [], []
    for (y, z) in outer:
        dd = max(0.0, -y)
        A.append(bm.verts.new((0.0, -dd, z)))
        M.append(bm.verts.new((-dd, -dd, z)))
        B.append(bm.verts.new((-dd, 0.0, z)))
    for i in range(len(outer) - 1):
        for (p, q) in ((A, M), (M, B)):
            vs = [p[i], p[i + 1], q[i + 1], q[i]]
            if len({id(v) for v in vs}) == 4:
                try:
                    bm.faces.new(vs)
                except ValueError:
                    pass
    bm.faces.new(A)
    bm.faces.new(B)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bevel(bm, 0.008, 2, keep=[(X, 0.0), (Y, 0.0)])
    del_plane(bm, X, 0.0)
    del_plane(bm, Y, 0.0)
    P.add(bm, "Trim")
    d = box(-0.11, -0.02, -0.11, -0.02, 0.085, 0.16)
    P.add(d, "Trim")
    P.ucx = [(-0.30, 0.0, -0.30, 0.0, 0, 0.35)]
    return P


def make_parapet(W):
    P = Piece(f"Parapet_{int(W * 100)}")
    pt = 0.25
    bm = box(0, W, 0, pt, 0, 0.80)
    del_plane(bm, X, 0.0)
    del_plane(bm, X, W)
    del_plane(bm, Z, 0.80)
    P.add(bm, "Wall")
    prof = [(-0.045, 0.77), (-0.045, 0.86), (-0.025, 0.90), (0.275, 0.885), (0.295, 0.865), (0.295, 0.77),
            (0.27, 0.77), (0.27, 0.785), (-0.02, 0.785), (-0.02, 0.77)]
    bm = prism_x(prof, 0.0, W)
    bevel(bm, 0.01, 2, keep=[(X, 0.0), (X, W)])
    P.add(bm, "Concrete")
    P.ucx = [(0, W, -0.045, 0.295, 0, 0.90)]
    return P


def make_roof_hip():
    P = Piece("RoofHip", kind="roof")
    t = 0.04        # eave fascia height (unit space)
    r = 0.15        # half ridge length (ridge along X); equal pitch for footprints ~1.45:1
    bm = new_bm()
    c = [bm.verts.new(p) for p in ((-0.5, -0.5, 0), (0.5, -0.5, 0), (0.5, 0.5, 0), (-0.5, 0.5, 0))]
    e = [bm.verts.new(p) for p in ((-0.5, -0.5, t), (0.5, -0.5, t), (0.5, 0.5, t), (-0.5, 0.5, t))]
    rad = 0.012
    zr = 1.0 - 0.65 * rad   # ridge line so that the capping tiles top out at exactly 1.0
    rl, rr = bm.verts.new((-r, 0, zr)), bm.verts.new((r, 0, zr))
    bm.faces.new(list(reversed(c)))                            # soffit
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new((c[i], c[j], e[j], e[i]))                 # fascia
    bm.faces.new((e[0], e[1], rr, rl))                         # front slope
    bm.faces.new((e[1], e[2], rr))                             # +X hip end
    bm.faces.new((e[2], e[3], rl, rr))                         # back slope
    bm.faces.new((e[3], e[0], rl))                             # -X hip end
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    P.add(bm, "Roof")
    # rounded ridge/hip capping tiles (half-round, slightly sunk)
    caps = new_bm()
    lines = [((-r, 0, zr), (r, 0, zr))]
    for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        a, b = Vector((sx * 0.5, sy * 0.5, t)), Vector((sx * r, 0, zr))
        lines.append((tuple(a + (b - a) * 0.04), tuple(b)))   # start inside the eave corner
    for a, b in lines:
        a, b = Vector(a), Vector(b)
        d = (b - a).normalized()
        up = Vector((0, 0, 1))
        side = d.cross(up).normalized()
        up2 = side.cross(d).normalized()
        if up2.z < 0:
            up2 = -up2
        seg = 6
        ring_a, ring_b = [], []
        for k in range(seg + 1):
            ang = math.pi * k / seg
            off = side * math.cos(ang) * rad + up2 * (math.sin(ang) * rad - rad * 0.35)
            ring_a.append(caps.verts.new(a + off))
            ring_b.append(caps.verts.new(b + off))
        for k in range(seg):
            caps.faces.new((ring_a[k], ring_b[k], ring_b[k + 1], ring_a[k + 1]))
        caps.faces.new(ring_a)
        caps.faces.new(list(reversed(ring_b)))
    P.add(caps, "Roof")
    P.ucx = [("hull", [(-0.5, -0.5, 0), (0.5, -0.5, 0), (0.5, 0.5, 0), (-0.5, 0.5, 0),
                       (-0.5, -0.5, t), (0.5, -0.5, t), (0.5, 0.5, t), (-0.5, 0.5, t), (-r, 0, zr), (r, 0, zr)])]
    return P


def make_roof_flat():
    P = Piece("RoofFlat", kind="roof")
    P.add(box(-0.5, 0.5, -0.5, 0.5, 0, 0.25), "Concrete")
    P.ucx = [(-0.5, 0.5, -0.5, 0.5, 0, 0.25)]
    return P


def make_drainpipe():
    P = Piece("Drainpipe_320", kind="prop")
    m = new_bm()
    yc = -0.09
    add_cyl(m, (0, yc, 0.0), 0.06, H - 0.16, seg=12, caps=False)
    add_cyl(m, (0, yc, H - 0.17), 0.07, 0.17, seg=12, caps=True)            # socket for the next segment
    add_cyl(m, (0, yc, H - 0.20), 0.06, 0.03, seg=12, caps=False, r2=0.07)   # socket taper
    for zz in (0.55, 2.15):
        add_cyl(m, (0, yc, zz), 0.068, 0.035, seg=12)
        add_box(m, -0.01, 0.01, yc + 0.06, 0.0, zz + 0.005, zz + 0.03)
        add_box(m, -0.04, 0.04, -0.006, 0.0, zz - 0.02, zz + 0.055)
    P.add(m, "Metal")
    return P


def make_water_tank():
    P = Piece("Prop_WaterTank", kind="prop")
    m = new_bm()
    # stand: 4 legs + top frame
    for sx in (-1, 1):
        for sy in (-1, 1):
            add_box(m, sx * 0.40 - 0.025, sx * 0.40 + 0.025, sy * 0.40 - 0.025, sy * 0.40 + 0.025, 0, 0.40)
            add_box(m, sx * 0.40 - 0.06, sx * 0.40 + 0.06, sy * 0.40 - 0.06, sy * 0.40 + 0.06, 0, 0.01)
    for sy in (-1, 1):
        add_box(m, -0.45, 0.45, sy * 0.40 - 0.025, sy * 0.40 + 0.025, 0.36, 0.40)
        add_box(m, sy * 0.40 - 0.025, sy * 0.40 + 0.025, -0.45, 0.45, 0.36, 0.40)
    P.add(m, "Metal")
    t = new_bm()
    add_cyl(t, (0, 0, 0.40), 0.50, 1.05, seg=24)
    for zz in (0.62, 1.05):
        add_cyl(t, (0, 0, zz), 0.515, 0.05, seg=24)
    add_cyl(t, (0, 0, 1.45), 0.50, 0.10, seg=24, r2=0.30)        # domed shoulder
    add_cyl(t, (0, 0, 1.55), 0.22, 0.07, seg=16)                  # lid
    add_cyl(t, (0.38, 0, 0.55), 0.03, 0.25, seg=8, axis="X")       # outlet
    add_cyl(t, (0, 0, 1.62), 0.05, 0.04, seg=8)
    P.add(t, "Metal")
    P.ucx = [(-0.52, 0.52, -0.52, 0.52, 0, 1.66)]
    return P


def make_ac_unit():
    P = Piece("Prop_ACUnit", kind="prop")
    w, d, h, z0 = 0.84, 0.32, 0.58, 0.10
    body = box(-w / 2, w / 2, -d / 2, d / 2, z0, z0 + h)
    cut = new_bm()
    add_cyl(cut, (-0.10, -d / 2 - 0.05, z0 + h / 2), 0.22, 0.08, seg=24, axis="Y")
    body = bool_diff(body, cut)
    bevel(body, 0.015, 2)
    P.add(body, "Metal")
    m = new_bm()
    # fan grille: rings + spokes inside the recess
    for rr in (0.08, 0.15, 0.21):
        for k in range(16):
            a0, a1 = 2 * math.pi * k / 16, 2 * math.pi * (k + 1) / 16
            p0 = Vector((-0.10 + rr * math.cos(a0), z0 + h / 2 + rr * math.sin(a0)))
            p1 = Vector((-0.10 + rr * math.cos(a1), z0 + h / 2 + rr * math.sin(a1)))
            mid, L = (p0 + p1) / 2, (p1 - p0).length
            bar = new_bm()
            add_box(bar, -L / 2 - 0.002, L / 2 + 0.002, -0.003, 0.003, -0.003, 0.003)
            ang = math.atan2(p1.y - p0.y, p1.x - p0.x)
            transform(bar, Matrix.Translation((mid.x, -d / 2 - 0.004, mid.y)) @ Matrix.Rotation(-ang, 4, "Y"))
            join_bm(m, bar)
            bar.free()
    for k in range(4):
        ang = math.pi / 4 + k * math.pi / 2
        bar = new_bm()
        add_box(bar, 0, 0.215, -0.003, 0.003, -0.004, 0.004)
        transform(bar, Matrix.Translation((-0.10, -d / 2 - 0.004, z0 + h / 2)) @ Matrix.Rotation(-ang, 4, "Y"))
        join_bm(m, bar)
        bar.free()
    add_cyl(m, (-0.10, -d / 2 + 0.02, z0 + h / 2), 0.04, 0.03, seg=12, axis="Y")
    # side louvres on the right panel area of the front
    for k in range(9):
        zz = z0 + 0.10 + k * 0.042
        add_box(m, 0.18, 0.37, -d / 2 - 0.012, -d / 2 + 0.002, zz, zz + 0.012)
    # feet
    for sx in (-1, 1):
        add_box(m, sx * 0.30 - 0.03, sx * 0.30 + 0.03, -d / 2 - 0.02, d / 2 + 0.02, 0.0, z0)
    # refrigerant pipes leaving the right side
    add_cyl(m, (w / 2, 0.05, z0 + 0.12), 0.012, 0.10, seg=8, axis="X")
    add_cyl(m, (w / 2, 0.09, z0 + 0.12), 0.009, 0.10, seg=8, axis="X")
    P.add(m, "Metal")
    P.ucx = [(-w / 2, w / 2, -d / 2 - 0.02, d / 2 + 0.02, 0, z0 + h)]
    return P


def make_dish():
    P = Piece("Prop_Dish", kind="prop")
    m = new_bm()
    add_box(m, -0.12, 0.12, -0.12, 0.12, 0.0, 0.012)
    add_cyl(m, (0, 0, 0.012), 0.025, 0.80, seg=10)
    # dish: shallow paraboloid shell, opening toward -Y, tilted 30 deg up
    dish = new_bm()
    R, depth, rings, seg = 0.40, 0.08, 5, 20
    grid = []
    for i in range(rings + 1):
        rr = R * i / rings
        row = []
        for k in range(seg):
            a = 2 * math.pi * k / seg
            if i == 0 and k > 0:
                row.append(row[0])
                continue
            row.append(dish.verts.new((rr * math.cos(a), -depth * (rr / R) ** 2, rr * math.sin(a))))
        grid.append(row)
    for i in range(rings):
        for k in range(seg):
            k2 = (k + 1) % seg
            if i == 0:
                dish.faces.new((grid[0][0], grid[1][k2], grid[1][k]))
            else:
                dish.faces.new((grid[i][k], grid[i][k2], grid[i + 1][k2], grid[i + 1][k]))
    # thickness (solidify equivalent)
    geo = bmesh.ops.extrude_face_region(dish, geom=list(dish.faces))
    vs = [v for v in geo["geom"] if isinstance(v, bmesh.types.BMVert)]
    bmesh.ops.translate(dish, vec=(0, 0.012, 0), verts=vs)
    bmesh.ops.recalc_face_normals(dish, faces=dish.faces)
    # LNB arm + LNB
    arm = new_bm()
    a0, a1 = Vector((0, -0.07, -0.37)), Vector((0, -0.40, -0.03))
    L = (a1 - a0).length
    add_box(arm, -0.012, 0.012, -0.012, 0.012, 0.0, L)
    transform(arm, Matrix.Translation(a0) @ (a1 - a0).to_track_quat("Z", "Y").to_matrix().to_4x4())
    add_cyl(arm, (0, -0.47, -0.03), 0.032, 0.11, seg=10, axis="Y")
    join_bm(dish, arm)
    arm.free()
    # bracket behind the dish
    add_box(dish, -0.06, 0.06, 0.02, 0.10, -0.06, 0.06)
    tilt = Matrix.Rotation(math.radians(-25), 4, "X")
    transform(dish, Matrix.Translation((0, -0.02, 0.80)) @ tilt)
    join_bm(m, dish)
    dish.free()
    P.add(m, "Metal")
    P.ucx = [(-0.42, 0.42, -0.45, 0.15, 0, 1.18)]
    return P


def make_chimney():
    P = Piece("Prop_Chimney", kind="prop")
    s = 0.30
    bm = box(-s, s, -s, s, 0, 1.0)
    del_plane(bm, Z, 0.0)
    bevel(bm, 0.02, 2, keep=[(Z, 0.0)])
    P.add(bm, "Wall")
    bm = box(-s - 0.03, s + 0.03, -s - 0.03, s + 0.03, 1.0, 1.06)
    bevel(bm, 0.01, 2)
    P.add(bm, "Trim")
    for sx in (-1, 1):
        for sy in (-1, 1):
            px0, px1 = (s - 0.12, s) if sx > 0 else (-s, -s + 0.12)
            py0, py1 = (s - 0.12, s) if sy > 0 else (-s, -s + 0.12)
            bm = box(px0, px1, py0, py1, 1.06, 1.40)
            bevel(bm, 0.01, 2)
            P.add(bm, "Wall")
    bm = box(-s - 0.06, s + 0.06, -s - 0.06, s + 0.06, 1.40, 1.46)
    bevel(bm, 0.01, 2)
    P.add(bm, "Concrete")
    # small hipped tile hood
    hood = new_bm()
    b = [hood.verts.new(p) for p in ((-s - 0.08, -s - 0.08, 1.46), (s + 0.08, -s - 0.08, 1.46),
                                    (s + 0.08, s + 0.08, 1.46), (-s - 0.08, s + 0.08, 1.46))]
    top = hood.verts.new((0, 0, 1.68))
    for i in range(4):
        hood.faces.new((b[i], b[(i + 1) % 4], top))
    hood.faces.new(list(reversed(b)))
    bmesh.ops.recalc_face_normals(hood, faces=hood.faces)
    P.add(hood, "Roof")
    # dark flue (soot) top inside the piers
    P.add(box(-s + 0.02, s - 0.02, -s + 0.02, s - 0.02, 1.0, 1.08), "Metal")
    P.ucx = [(-s - 0.08, s + 0.08, -s - 0.08, s + 0.08, 0, 1.68)]
    return P


def make_roof_hut():
    P = Piece("Prop_RoofHut", kind="prop")
    hw, hd, hh = 1.20, 1.00, 2.40
    dx0, dx1, dz1 = -0.45, 0.45, 2.05
    wall = box(-hw, hw, -hd, hd, 0, hh)
    cut = new_bm()
    add_box(cut, dx0, dx1, -hd - 0.1, -hd + 0.12, -0.1, dz1)
    add_box(cut, hw - 0.10, hw + 0.1, -0.35, 0.35, 1.45, 1.95)    # small side window
    wall = bool_diff(wall, cut)
    bevel(wall, 0.02, 2, keep=[(Z, 0.0)])
    P.add(wall, "Wall")
    # base band
    bm = box(-hw - 0.03, hw + 0.03, -hd - 0.03, hd + 0.03, 0, 0.18)
    del_plane(bm, Z, 0.0)
    cut = new_bm()
    add_box(cut, dx0, dx1, -hd - 0.2, -hd + 0.2, -0.1, dz1)
    bm = bool_diff(bm, cut)
    bevel(bm, 0.01, 2, keep=[(Z, 0.0)])
    P.add(bm, "Wall")
    # step + door
    bm = box(dx0 - 0.1, dx1 + 0.1, -hd - 0.25, -hd + 0.10, 0, 0.12)
    bevel(bm, 0.015, 2, keep=[(Z, 0.0)])
    P.add(bm, "Concrete")
    door = box(dx0 + 0.01, dx1 - 0.01, -hd + 0.06, -hd + 0.10, 0.12, dz1)
    P.add(door, "Wood")
    for k in range(7):
        xx = dx0 + 0.01 + (dx1 - dx0 - 0.02) * (k + 0.5) / 7
        b = box(xx - 0.055, xx + 0.055, -hd + 0.045, -hd + 0.065, 0.13, dz1 - 0.01)
        bevel(b, 0.005, 1)
        P.add(b, "Wood")
    for zz in (0.35, 1.10, 1.80):
        b = box(dx0 + 0.04, dx1 - 0.04, -hd + 0.03, -hd + 0.046, zz, zz + 0.10)
        bevel(b, 0.005, 1)
        P.add(b, "Wood")
    m = new_bm()
    add_box(m, dx1 - 0.14, dx1 - 0.10, -hd + 0.015, -hd + 0.03, 1.0, 1.06)
    P.add(m, "Metal")
    # door head (concrete lintel)
    bm = box(dx0 - 0.08, dx1 + 0.08, -hd - 0.02, -hd + 0.05, dz1, dz1 + 0.15)
    bevel(bm, 0.01, 2)
    P.add(bm, "Concrete")
    # side window: louvre vent
    g = new_bm()
    for k in range(6):
        zz = 1.47 + k * 0.08
        add_box(g, hw - 0.08, hw - 0.05, -0.35, 0.35, zz, zz + 0.03)
    P.add(g, "Metal")
    # roof slab with drip
    bm = box(-hw - 0.12, hw + 0.12, -hd - 0.12, hd + 0.12, hh, hh + 0.14)
    bevel(bm, 0.02, 2)
    P.add(bm, "Concrete")
    P.ucx = [(-hw, hw, -hd, hd, 0, hh + 0.14)]
    return P


# =================================================================================================
# finishing, export, verification
# =================================================================================================
def cube_uv(bm):
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for l in f.loops:
            p = l.vert.co
            if ax == Y:
                u, v = (p.x if n.y < 0 else -p.x), p.z
            elif ax == X:
                u, v = (p.y if n.x > 0 else -p.y), p.z
            else:
                u, v = p.x, (p.y if n.z > 0 else -p.y)
            l[uv].uv = (u, v)


def finish_bm(bm):
    bmesh.ops.dissolve_degenerate(bm, dist=1e-6, edges=bm.edges)
    bm.normal_update()
    bmesh.ops.triangulate(bm, faces=bm.faces, quad_method="BEAUTY", ngon_method="BEAUTY")
    bm.normal_update()
    cube_uv(bm)


def make_object(name, bm, mats, mirror):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    if mirror:
        me.transform(Matrix.Diagonal((1.0, -1.0, 1.0, 1.0)))
        me.flip_normals()
    # compact material slots to the used ones
    used = sorted({p.material_index for p in me.polygons})
    remap = {old: i for i, old in enumerate(used)}
    for p in me.polygons:
        p.material_index = remap[p.material_index]
    for old in used:
        me.materials.append(mats[MAT_ORDER[old]])
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    # smooth + sharp-by-angle + weighted normals (applied)
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(50.0))
    mod = ob.modifiers.new("WN", "WEIGHTED_NORMAL")
    mod.mode = "FACE_AREA"
    mod.weight = 50
    mod.keep_sharp = True
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.ops.object.modifier_apply(modifier="WN")
    return ob


def make_ucx(P, mirror):
    obs = []
    for i, spec in enumerate(P.ucx):
        bm = new_bm()
        if isinstance(spec, tuple) and spec and spec[0] == "hull":
            vs = [bm.verts.new(v) for v in spec[1]]
            bmesh.ops.convex_hull(bm, input=vs)
        else:
            add_box(bm, *spec)
        name = f"UCX_{P.name}_{i:02d}"
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        bm.free()
        if mirror:
            me.transform(Matrix.Diagonal((1.0, -1.0, 1.0, 1.0)))
            me.flip_normals()
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        ob.display_type = "WIRE"
        obs.append(ob)
    return obs


def ensure_materials():
    mats = {}
    for n in MAT_ORDER:
        m = bpy.data.materials.get(n) or bpy.data.materials.new(n)
        m.diffuse_color = PREVIEW_RGBA[n]
        mats[n] = m
    return mats


def export_fbx(path, objs):
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"MESH"},
        global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL",
        axis_forward="-Z", axis_up="Y", bake_space_transform=False,
        use_mesh_modifiers=True, mesh_smooth_type="FACE", use_triangles=True, use_tspace=False,
        use_custom_props=False, add_leaf_bones=False, bake_anim=False, path_mode="AUTO",
        embed_textures=False, use_mesh_edges=False)


# expected bounding boxes (KIT_SPEC; Unreal cm == 100 * these Blender m). None = not constrained.
def expected(name):
    W = None
    for w in (200, 400):
        if name.endswith(f"_{w}"):
            W = w / 100
    if name in ("Shopfront_400", "Door_200"):   # carry their own plinth stubs / skirt down to -3.0
        return {"x": (0, W), "z": (-3.0, H)}
    if name.startswith(("Wall_", "Window_", "WindowPlain_", "Balcony_", "GroundWindow_")):
        return {"x": (0, W), "z": (0, H)}
    if name.startswith("Plinth_"):
        return {"x": (0, W), "z": (-3.0, 0.6)}
    if name.startswith("Cornice_"):
        return {"x": (0, W), "z": (0, 0.35)}
    if name.startswith("Parapet_"):
        return {"x": (0, W), "z": (0, 0.9)}
    if name.startswith("RetainingWall_"):
        return {"x": (0, W), "z": (0, RW_H)}
    if name == "WallCoping_400":
        return {"x": (0, W), "z": (0, 0.16)}
    if name == "Corner_320":
        return {"x": (-0.2, 0.2), "y": (-0.2, 0.2), "z": (0, H)}
    if name == "CorniceCorner":
        return {"x": (-0.30, 0.0), "y": (-0.30, 0.0), "z": (0, 0.35)}
    if name == "RoofHip":
        return {"x": (-0.5, 0.5), "y": (-0.5, 0.5), "z": (0, 1.0)}
    if name == "RoofFlat":
        return {"x": (-0.5, 0.5), "y": (-0.5, 0.5), "z": (0, 0.25)}
    if name == "Drainpipe_320":
        return {"z": (0, H)}
    return {"z": (0, None)}


def recenter_prop(P, bm):
    """Props: pivot at bottom centre of the bounding box."""
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    off = Vector((-(min(xs) + max(xs)) / 2, -(min(ys) + max(ys)) / 2, -min(zs)))
    if off.length > 1e-6:
        transform(bm, Matrix.Translation(off))
        P.ucx = [
            (s[0] + off.x, s[1] + off.x, s[2] + off.y, s[3] + off.y, s[4] + off.z, s[5] + off.z)
            if not (s and s[0] == "hull") else s for s in P.ucx]


RW_H, RW_T = 2.0, 0.50  # retaining wall panel: 2 m tall rows, 0.5 m thick (stacked by the assembler)


def make_retaining_wall(kind):
    """Retaining wall panel, 4 x 2 m, face at Y=0. Stone: coursed blocks with sawn joints (old roads,
    the 1930s pass); concrete: smooth pours with lift joints every metre (post-war roads). Stacked
    in rows by UOCWorldBuilderLibrary::BuildRetainingWalls; the X ends and Z faces are seams."""
    W = 4.0
    P = Piece(f"RetainingWall_{kind}_400")
    bm = box(0.0, W, 0.0, RW_T, 0.0, RW_H)
    cb = new_bm()
    if kind == "Stone":
        rows = 5
        for r in range(1, rows):  # bed joints
            z = RW_H * r / rows
            add_box(cb, -0.1, W + 0.1, -0.1, 0.015, z - 0.008, z + 0.008)
        for r in range(rows):  # staggered head joints, irregular block lengths
            z0, z1 = RW_H * r / rows, RW_H * (r + 1) / rows
            x = 0.25 + 0.31 * (r % 3)
            k = 0
            while x < W - 0.15:
                # Stop short of the bed joints: overlapping cutters make the exact boolean fail.
                add_box(cb, x - 0.008, x + 0.008, -0.1, 0.015, z0 + (0.009 if r else -0.1), z1 - (0.009 if r < rows - 1 else -0.1))
                x += (0.55, 0.72, 0.63, 0.8)[(r + k) % 4]
                k += 1
        mat = "Trim"
    else:
        add_box(cb, -0.1, W + 0.1, -0.1, 0.01, 0.995, 1.005)  # pour lift
        for x in (1.0, 2.0, 3.0):  # formwork panel seams (split at the lift joint: no overlapping cutters)
            add_box(cb, x - 0.004, x + 0.004, -0.1, 0.006, -0.1, 0.994)
            add_box(cb, x - 0.004, x + 0.004, -0.1, 0.006, 1.006, RW_H + 0.1)
        mat = "Concrete"
    bm = bool_diff(bm, cb)
    bevel(bm, 0.012, 2, keep=[(X, 0.0), (X, W), (Z, 0.0), (Z, RW_H), (Y, RW_T)])
    P.add(bm, mat)
    P.ucx = [(0, W, 0, RW_T, 0, RW_H)]
    return P


def make_wall_coping():
    """Stone coping that caps the top row of a retaining wall (5 cm overhang both sides)."""
    W = 4.0
    P = Piece("WallCoping_400")
    bm = box(0.0, W, -0.05, RW_T + 0.05, 0.0, 0.16)
    bevel(bm, 0.015, 2, keep=[(X, 0.0), (X, W), (Z, 0.0)])
    P.add(bm, "Trim")
    P.ucx = [(0, W, -0.05, RW_T + 0.05, 0, 0.16)]
    return P


# ---- street fixtures (INFRA_DESIGN.md Phase 5) -------------------------------------------------
# kind "fixture": no recentring; the pivot is the base of a post, or the wall-attachment point of
# a bracket (the fixture reaches out along -Y like facade pieces). Sizes are real-world.

def text_mesh(text, size, z0, depth=0.004, y=0.0):
    """Extruded text (Blender's built-in font) centred on X, standing in the XZ plane facing -Y."""
    cu = bpy.data.curves.new("txt", type="FONT")
    cu.body = text
    cu.size = size
    cu.align_x = "CENTER"
    cu.extrude = depth / 2
    cu.resolution_u = 3  # lettering is read at 2-20 m: coarse curves keep signs inside the triangle budget
    ob = bpy.data.objects.new("txt", cu)
    bpy.context.scene.collection.objects.link(ob)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    bm = new_bm()
    bm.from_mesh(me)
    bpy.data.objects.remove(ob)
    bpy.data.curves.remove(cu)
    bpy.data.meshes.remove(me)
    # Font space: X right, Y up, Z out of the page. Stand it up: Y -> Z, Z -> -Y (toward the viewer).
    transform(bm, Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1))))
    zs = [v.co.z for v in bm.verts]
    transform(bm, Matrix.Translation((0, y, z0 - (min(zs) + max(zs)) / 2)))
    return bm


def disc_y(r, y0, y1, z, seg=32, r_in=None):
    """Disc (or ring, with r_in) facing -Y, between y0 and y1, centred at height z."""
    bm = new_bm()
    if r_in is None:
        add_cyl(bm, (0, y0, z), r, y1 - y0, seg=seg, axis="Y")
        return bm
    pts = [(math.cos(2 * math.pi * k / seg), math.sin(2 * math.pi * k / seg)) for k in range(seg)]
    for k in range(seg):
        (c0, s0), (c1, s1) = pts[k], pts[(k + 1) % seg]
        quad = [(r * c0, z + r * s0), (r * c1, z + r * s1), (r_in * c1, z + r_in * s1), (r_in * c0, z + r_in * s0)]
        vs = [bm.verts.new((x, yy, zz)) for yy in (y0, y1) for x, zz in quad]
        for a, b, c, d in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
            bm.faces.new((vs[a], vs[b], vs[c], vs[d]))
    return bm


def poly_y(points_xz, y0, y1):
    """Convex polygon in XZ extruded along Y (y0 front .. y1 back)."""
    bm = new_bm()
    n = len(points_xz)
    vs = [bm.verts.new((x, yy, z)) for yy in (y0, y1) for x, z in points_xz]
    bm.faces.new([vs[k] for k in range(n)][::-1])
    bm.faces.new([vs[n + k] for k in range(n)])
    for k in range(n):
        a, b = k, (k + 1) % n
        bm.faces.new((vs[a], vs[b], vs[n + b], vs[n + a]))
    return bm


def sign_post(P, height=2.90):
    m = new_bm()
    add_cyl(m, (0, 0.03, 0), 0.03, height, seg=12)          # galvanised post, plate in front of it
    add_cyl(m, (0, 0.03, height), 0.035, 0.03, seg=12)      # cap
    P.add(m, "Metal")


SIGN_Z, SIGN_R = 2.25, 0.30  # plate centre height and radius (urban 60 cm discs)


def make_sign(kind):
    """Codice della Strada sign on a post (original artwork: shapes, colours and text only)."""
    P = Piece(f"Sign_{kind}", kind="fixture")
    sign_post(P)
    y0, y1 = -0.02, 0.0
    if kind.startswith("Speed"):
        P.add(disc_y(SIGN_R, y0, y1, SIGN_Z, r_in=SIGN_R * 0.78), "PaintRed")
        P.add(disc_y(SIGN_R * 0.78, y0 + 0.002, y1, SIGN_Z), "PaintWhite")
        P.add(text_mesh(kind[5:], 0.26, SIGN_Z, y=y0 - 0.001), "PaintBlack")
    elif kind == "Stop":
        r = SIGN_R
        octo = [(r * math.cos(math.pi / 8 + k * math.pi / 4), SIGN_Z + r * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8)]
        P.add(poly_y(octo, y0, y1), "PaintRed")
        P.add(text_mesh("STOP", 0.15, SIGN_Z, y=y0 - 0.001), "PaintWhite")
    elif kind == "GiveWay":
        h = 0.78
        tri = [(-0.45, SIGN_Z + h * 0.4), (0.45, SIGN_Z + h * 0.4), (0.0, SIGN_Z - h * 0.6)]
        P.add(poly_y(tri, y0, y1), "PaintRed")
        inner = [(-0.32, SIGN_Z + h * 0.4 - 0.08), (0.32, SIGN_Z + h * 0.4 - 0.08), (0.0, SIGN_Z - h * 0.6 + 0.15)]
        P.add(poly_y(inner, y0 - 0.002, y1), "PaintWhite")
    elif kind == "NoEntry":
        P.add(disc_y(SIGN_R, y0, y1, SIGN_Z), "PaintRed")
        bar = new_bm()
        add_box(bar, -0.21, 0.21, y0 - 0.003, y1, SIGN_Z - 0.05, SIGN_Z + 0.05)
        P.add(bar, "PaintWhite")
    elif kind == "ZTL":
        panel = new_bm()
        add_box(panel, -0.40, 0.40, y0, y1, 1.75, 2.75)
        P.add(panel, "PaintWhite")
        P.add(disc_y(0.22, y0 - 0.002, y1, 2.45, r_in=0.17), "PaintRed")
        P.add(text_mesh("ZTL", 0.16, 1.95, y=y0 - 0.003), "PaintBlack")
        P.add(text_mesh("ZONA TRAFFICO LIMITATO", 0.045, 2.12, y=y0 - 0.003), "PaintBlack")
    P.ucx = [(-0.1, 0.1, -0.05, 0.1, 0, 2.9)]
    return P


def make_convex_mirror():
    P = Piece("Prop_ConvexMirror", kind="fixture")
    sign_post(P, 2.6)
    P.add(disc_y(0.33, -0.08, -0.02, 2.45, r_in=0.29), "PaintRed")
    face = new_bm()
    add_cyl(face, (0, -0.07, 2.45), 0.29, 0.05, seg=28, axis="Y", r2=0.24)  # domed mirror
    P.add(face, "Glass")
    P.ucx = [(-0.1, 0.1, -0.05, 0.1, 0, 2.6)]
    return P


def make_bollard(kind):
    P = Piece(f"Prop_Bollard{kind}", kind="fixture")
    m = new_bm()
    if kind == "Stone":
        add_cyl(m, (0, 0, 0), 0.15, 0.48, seg=16, r2=0.13)
        add_cyl(m, (0, 0, 0.48), 0.13, 0.10, seg=16, r2=0.05)          # rounded crown
        bevel(m, 0.01, 2)
        P.add(m, "Trim")
    else:  # cast iron
        add_cyl(m, (0, 0, 0), 0.09, 0.08, seg=14)
        add_cyl(m, (0, 0, 0.08), 0.06, 0.70, seg=14, r2=0.05)
        for z in (0.12, 0.72):
            add_cyl(m, (0, 0, z), 0.075, 0.04, seg=14)
        add_cyl(m, (0, 0, 0.78), 0.07, 0.07, seg=14, r2=0.03)          # knob
        P.add(m, "Metal")
    P.ucx = [(-0.15, 0.15, -0.15, 0.15, 0, 0.9)]
    return P


def make_wall_lantern():
    """Wall lantern on an iron bracket; pivot at the wall plate's foot, reaching out along -Y."""
    P = Piece("Prop_WallLantern", kind="fixture")
    m = new_bm()
    add_box(m, -0.06, 0.06, -0.02, 0.0, 0.0, 0.30)                    # wall plate
    add_cyl(m, (0, 0.0, 0.24), 0.018, 0.62, seg=8, axis="Y")          # arm (built along +Y, mirrored below)
    transform(m, Matrix.Scale(-1, 4, (0, 1, 0)))                       # arm toward -Y
    add_cyl(m, (0, -0.55, 0.12), 0.012, 0.12, seg=8)                   # drop rod
    lantern = new_bm()
    for sx in (-1, 1):
        for sy in (-1, 1):
            add_box(lantern, sx * 0.11 - 0.012, sx * 0.11 + 0.012, -0.55 + sy * 0.11 - 0.012, -0.55 + sy * 0.11 + 0.012, -0.30, 0.04)
    add_cyl(lantern, (0, -0.55, 0.04), 0.17, 0.10, seg=4, r2=0.02)    # roof
    add_cyl(lantern, (0, -0.55, -0.34), 0.08, 0.04, seg=8)            # base
    P.add(m, "Metal")
    P.add(lantern, "Metal")
    glass = new_bm()
    add_box(glass, -0.10, 0.10, -0.65, -0.45, -0.28, 0.02)
    P.add(glass, "Glass")
    transform_piece_z(P, 0.34)
    P.ucx = [(-0.15, 0.15, -0.7, 0.0, 0, 0.7)]
    return P


def transform_piece_z(P, dz):
    for bm, _ in P.parts:
        transform(bm, Matrix.Translation((0, 0, dz)))


def make_led_pole():
    """Galvanised LED street light: 8 m tapered pole, 1.6 m arm toward -Y, flat head."""
    P = Piece("Prop_LedPole", kind="fixture")
    m = new_bm()
    add_cyl(m, (0, 0, 0), 0.10, 0.30, seg=12)                           # base collar
    add_cyl(m, (0, 0, 0.30), 0.075, 7.70, seg=12, r2=0.045)
    arm = new_bm()
    add_cyl(arm, (0, 0, 7.85), 0.035, 1.60, seg=8, axis="Y")
    transform(arm, Matrix.Scale(-1, 4, (0, 1, 0)))
    transform(arm, Matrix.Translation((0, 0, 0)))
    head = new_bm()
    add_box(head, -0.16, 0.16, -1.85, -1.30, 7.80, 7.92)
    P.add(m, "Metal")
    P.add(arm, "Metal")
    P.add(head, "Metal")
    lens = new_bm()
    add_box(lens, -0.13, 0.13, -1.80, -1.35, 7.785, 7.80)
    P.add(lens, "Glass")
    P.ucx = [(-0.12, 0.12, -0.12, 0.12, 0, 8.0)]
    return P


def make_bin():
    """Wheeled kerbside recycling bin (cassonetto); colour per instance (Paint slot)."""
    P = Piece("Prop_RecyclingBin", kind="fixture")
    body = new_bm()
    add_box(body, -0.65, 0.65, -0.42, 0.42, 0.18, 1.20)
    bevel(body, 0.04, 2)
    lid = new_bm()
    add_box(lid, -0.68, 0.68, -0.46, 0.46, 1.20, 1.30)
    bevel(lid, 0.03, 2)
    P.add(body, "Paint")
    P.add(lid, "Paint")
    wheels = new_bm()
    for sx in (-0.5, 0.5):
        for sy in (-0.3, 0.3):
            # a 10-sided wheel's lowest edge is r cos 18 deg below its centre: sit it on z = 0
            add_cyl(wheels, (sx - 0.04, sy, 0.09 * math.cos(math.pi / 10)), 0.09, 0.08, seg=10, axis="X")
    P.add(wheels, "Metal")
    pedal = new_bm()
    add_box(pedal, -0.15, 0.15, -0.55, -0.42, 0.15, 0.20)
    P.add(pedal, "Metal")
    P.ucx = [(-0.68, 0.68, -0.46, 0.46, 0, 1.3)]
    return P


def make_fountain():
    """Cast-iron drinking fountain (original design): fluted column, spout, grate."""
    P = Piece("Prop_Fountain", kind="fixture")
    m = new_bm()
    add_box(m, -0.30, 0.30, -0.45, 0.15, 0.0, 0.05)                    # grate base
    add_cyl(m, (0, 0, 0.05), 0.17, 0.10, seg=8)                        # plinth
    add_cyl(m, (0, 0, 0.15), 0.13, 0.85, seg=8, r2=0.11)               # octagonal column
    add_cyl(m, (0, 0, 1.00), 0.15, 0.06, seg=8)                        # cap
    add_cyl(m, (0, 0, 1.06), 0.09, 0.08, seg=8, r2=0.02)
    spout = new_bm()
    add_cyl(spout, (0, 0.0, 0.70), 0.02, 0.22, seg=8, axis="Y")
    transform(spout, Matrix.Scale(-1, 4, (0, 1, 0)))
    P.add(m, "Metal")
    P.add(spout, "Metal")
    P.ucx = [(-0.3, 0.3, -0.45, 0.17, 0, 1.15)]
    return P


BUILDERS = [
    lambda: make_wall(2.0), lambda: make_wall(4.0),
    lambda: make_window(2.0, "shutters"), lambda: make_window(4.0, "shutters"),
    lambda: make_window(2.0, "plain"), lambda: make_window(4.0, "plain"),
    make_balcony, make_shopfront, make_door, make_ground_window, make_corner,
    lambda: make_plinth(2.0), lambda: make_plinth(4.0),
    lambda: make_cornice(2.0), lambda: make_cornice(4.0), make_cornice_corner,
    lambda: make_parapet(2.0), lambda: make_parapet(4.0),
    make_roof_hip, make_roof_flat,
    make_water_tank, make_ac_unit, make_dish, make_chimney, make_roof_hut, make_drainpipe,
    lambda: make_retaining_wall("Stone"), lambda: make_retaining_wall("Concrete"), make_wall_coping,
    lambda: make_sign("Speed30"), lambda: make_sign("Speed50"), lambda: make_sign("Stop"), lambda: make_sign("GiveWay"),
    lambda: make_sign("NoEntry"), lambda: make_sign("ZTL"), make_convex_mirror,
    lambda: make_bollard("Stone"), lambda: make_bollard("Iron"), make_wall_lantern, make_led_pole, make_bin, make_fountain,
]


def clear_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    for m in list(bpy.data.meshes):
        bpy.data.meshes.remove(m)


def bbox(ob):
    pts = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    return [(min(p[i] for p in pts), max(p[i] for p in pts)) for i in range(3)]


def verify(names):
    """Re-import every FBX into an empty scene and check sizes."""
    print("\n=== FBX re-import check (sizes in Unreal cm = re-imported Blender m * 100, Y negated) ===")
    print(f"{'piece':<18}{'tris':>6}  {'X range cm':>16}{'Y range cm':>16}{'Z range cm':>16}  {'W x D x H m':>20}  check")
    ok_all = True
    for name in names:
        clear_scene()
        for m in list(bpy.data.materials):
            bpy.data.materials.remove(m)
        bpy.ops.import_scene.fbx(filepath=os.path.join(FBX_DIR, name + ".fbx"))
        obs = {o.name: o for o in bpy.context.scene.objects if o.type == "MESH"}
        mesh = obs.get(name)
        ucx = [o for n, o in obs.items() if n.startswith(f"UCX_{name}_")]
        if mesh is None:
            print(f"{name:<18} MISSING MESH, objects: {list(obs)}")
            ok_all = False
            continue
        bb = bbox(mesh)
        ue = [(bb[0][0] * 100, bb[0][1] * 100), (-bb[1][1] * 100, -bb[1][0] * 100), (bb[2][0] * 100, bb[2][1] * 100)]
        tris = sum(len(p.vertices) - 2 for p in mesh.data.polygons)
        exp = expected(name)
        ok = True
        for ax, i in (("x", 0), ("y", 1), ("z", 2)):
            if ax in exp:
                lo, hi = exp[ax]
                if lo is not None and abs(ue[i][0] - lo * 100) > 0.05:
                    ok = False
                if hi is not None and abs(ue[i][1] - hi * 100) > 0.05:
                    ok = False
        scale_ok = all(abs(s - 1.0) < 1e-6 for s in mesh.scale)
        slots = [s.material.name if s.material else "-" for s in mesh.material_slots]
        rng = lambda r: f"{r[0]:7.1f}..{r[1]:<7.1f}"
        dims = f"{(bb[0][1]-bb[0][0]):.2f} x {(bb[1][1]-bb[1][0]):.2f} x {(bb[2][1]-bb[2][0]):.2f}"
        status = ("PASS" if ok and scale_ok else "FAIL") + f" ucx={len(ucx)} slots={','.join(slots)}"
        ok_all &= ok and scale_ok
        print(f"{name:<18}{tris:>6}  {rng(ue[0]):>16}{rng(ue[1]):>16}{rng(ue[2]):>16}  {dims:>20}  {status}")
    print("ALL PASS" if ok_all else "SOME CHECKS FAILED")
    return ok_all


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = None
    if "--only" in argv:
        only = set(argv[argv.index("--only") + 1].split(","))
    os.makedirs(FBX_DIR, exist_ok=True)
    clear_scene()
    mats = ensure_materials()
    report = []
    names = []
    for b in BUILDERS:
        P = b()
        if only and P.name not in only:
            for bm, _ in P.parts:
                bm.free()
            continue
        bm = P.merged()
        if P.kind == "prop" and P.name != "Drainpipe_320":
            recenter_prop(P, bm)
        finish_bm(bm)
        ob = make_object(P.name, bm, mats, mirror=True)
        ucx = make_ucx(P, mirror=True)
        bm.free()
        tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
        per = {}
        for p in ob.data.polygons:
            mn = ob.data.materials[p.material_index].name
            per[mn] = per.get(mn, 0) + 1
        bb = bbox(ob)
        path = os.path.join(FBX_DIR, P.name + ".fbx")
        export_fbx(path, [ob] + ucx)
        budget = 3000 if P.kind == "facade" else 2000
        flag = "" if tris <= budget * 1.1 else "  OVER BUDGET"
        report.append((P.name, tris, bb, flag))
        names.append(P.name)
        print(f"[kit] {P.name:<18} tris={tris:5d}  size(m) X {bb[0][1]-bb[0][0]:.3f}  Y {bb[1][1]-bb[1][0]:.3f}  "
              f"Z {bb[2][1]-bb[2][0]:.3f}{flag}  {per}")
        for o in [ob] + ucx:
            me = o.data
            bpy.data.objects.remove(o)
            bpy.data.meshes.remove(me)
    if "--no-check" not in argv:
        verify(names)
    print("\n=== triangle counts ===")
    for n, t, _, flag in report:
        print(f"  {n:<18}{t:6d}{flag}")


if __name__ == "__main__":
    main()
