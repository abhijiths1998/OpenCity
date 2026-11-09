"""Give the coupe's windows their own material slot (M4b rain on car glass).

  ~/Applications/blender-4.2/blender -b -P Tools/blender/split_car_glass.py

In: SourceArt/Vehicles/SportsCar/SM_SportsCar_export.fbx, the vehicle template's SM_SportsCar
exported unchanged from the editor (slots M_SportsCarChasis, MI_SportsCarBody). Its windows are
UV islands of the body-paint slot that the paint texture colours dark. Out:
SourceArt/Vehicles/SportsCar/SM_SportsCar_Body.fbx with a third slot, Glass, holding those islands.
No geometry is added or moved. Content/Python/oc_m2_setup.py imports it as SM_SportsCar_Body.

The islands are found by their UV centre (stable across exports), checked against a render: the
windshield, both side windows and the rear window. The script fails if any is missing.
"""

import os
import sys

import bmesh
import bpy

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "SourceArt/Vehicles/SportsCar/SM_SportsCar_export.fbx")
OUT = os.path.join(ROOT, "SourceArt/Vehicles/SportsCar/SM_SportsCar_Body.fbx")
BODY_SLOT = 1
# UV-space centres of the glass islands in the body-paint slot.
GLASS_UV = {
    "windshield": (0.155, 0.949),
    "rear": (0.272, 0.946),
    "side_right": (0.034, 0.917),
    "side_left": (0.072, 0.919),
}
TOL = 0.02


def islands(bm, uv, slot):
    """Faces of `slot` grouped by UV island (neighbours that share both UVs of an edge)."""
    def key(loop):
        return round(loop[uv].uv.x, 5), round(loop[uv].uv.y, 5)

    seen, out = set(), []
    for f in bm.faces:
        if f.material_index != slot or f.index in seen:
            continue
        stack, comp = [f], []
        seen.add(f.index)
        while stack:
            g = stack.pop()
            comp.append(g)
            for loop in g.loops:
                for h in loop.edge.link_faces:
                    if h.index in seen or h.material_index != slot:
                        continue
                    other = next(x for x in h.loops if x.edge == loop.edge)
                    if {key(loop), key(loop.link_loop_next)} == {key(other), key(other.link_loop_next)}:
                        seen.add(h.index)
                        stack.append(h)
        out.append(comp)
    return out


def uv_centre(faces, uv):
    us = [loop[uv].uv for f in faces for loop in f.loops]
    return ((min(u.x for u in us) + max(u.x for u in us)) / 2, (min(u.y for u in us) + max(u.y for u in us)) / 2)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=SRC)
    obj = next(o for o in bpy.context.scene.objects if o.type == "MESH")
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.faces.ensure_lookup_table()
    uv = bm.loops.layers.uv.active
    glass = bpy.data.materials.new("Glass")
    obj.data.materials.append(glass)
    glass_slot = len(obj.data.materials) - 1
    found = {}
    for comp in islands(bm, uv, BODY_SLOT):
        cu, cv = uv_centre(comp, uv)
        for name, (u, v) in GLASS_UV.items():
            if abs(cu - u) < TOL and abs(cv - v) < TOL:
                found[name] = len(comp)
                for f in comp:
                    f.material_index = glass_slot
    missing = sorted(set(GLASS_UV) - set(found))
    if missing:
        sys.exit(f"split_car_glass: glass islands not found: {missing}")
    bm.to_mesh(obj.data)
    bm.free()
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={"MESH"}, mesh_smooth_type="OFF",
                             use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False)
    print(f"split_car_glass: {found} -> {OUT}")


main()
