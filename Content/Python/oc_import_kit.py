"""Import the Blender building kit (Tools/blender/make_kit.py -> SourceArt/Kit/FBX/*.fbx).

  scripts/run_python.sh Content/Python/oc_import_kit.py

Each FBX becomes /Game/Environment/Meshes/Kit/SM_Kit_<Piece> (Tools/blender/KIT_SPEC.md):
- Nanite on, no lightmap UVs (fully dynamic lighting), no auto-generated collision. The FBX's UCX_
  boxes are not picked up by the 5.8 importer; building pieces are placed without collision
  (buildings collide through the hidden blockout boxes), and the SOLID pieces (walls, street
  fixtures) collide with their render mesh (complex as simple).
- Material slots are matched by name to the kit instances: Wall -> MI_Kit_Wall, Trim -> MI_Kit_Trim,
  Wood, Glass, Metal, Roof, Concrete (built by oc_art_materials.py; run that first).
Re-running replaces the meshes in place.
"""

import os

import unreal

import oc_lib as L

TAG = "oc_kit"
# Pieces placed with collision on (BuildRetainingWalls, BuildStreetFixtures): complex as simple.
SOLID = ("RetainingWall_", "WallCoping_", "Sign_", "Prop_Bollard", "Prop_LedPole", "Prop_ConvexMirror",
         "Prop_RecyclingBin", "Prop_Fountain", "Prop_TrafficLight")
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJECT_DIR, "SourceArt/Kit/FBX")
DEST = "/Game/Environment/Meshes/Kit"
MI_ROOT = "/Game/Environment/Materials"
SLOTS = ("Wall", "Trim", "Wood", "Glass", "Metal", "Roof", "Concrete",
         "PaintRed", "PaintWhite", "PaintBlack", "PaintBlue", "Paint")  # paint: M_Master_Paint (street fixtures)


def log(msg):
    L.log(TAG, msg)


def import_task(fbx, name):
    ui = unreal.FbxImportUI()
    L.set_props(ui, import_mesh=True, import_as_skeletal=False, import_materials=False, import_textures=False,
                import_animations=False, create_physics_asset=False,
                mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH, automated_import_should_detect_type=False)
    sm = ui.get_editor_property("static_mesh_import_data")
    L.set_props(sm, combine_meshes=True, auto_generate_collision=False, generate_lightmap_u_vs=False,
                remove_degenerates=True, normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    t = unreal.AssetImportTask()
    L.set_props(t, filename=fbx, destination_path=DEST, destination_name=name, replace_existing=True,
                automated=True, save=False, options=ui)
    return t


def main():
    files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".fbx"))
    if not files:
        raise RuntimeError(f"no FBX in {SRC}: run Tools/blender/make_kit.py")
    tasks = [import_task(os.path.join(SRC, f), "SM_Kit_" + os.path.splitext(f)[0]) for f in files]
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    mis = {slot: unreal.load_asset(f"{MI_ROOT}/MI_Kit_{slot}") for slot in SLOTS}
    missing_mi = [s for s, m in mis.items() if m is None]
    if missing_mi:
        raise RuntimeError(f"missing kit materials {missing_mi}: run oc_art_materials.py first")
    done = []
    for f in files:
        name = "SM_Kit_" + os.path.splitext(f)[0]
        mesh = unreal.load_asset(f"{DEST}/{name}")
        if mesh is None:
            log(f"FAILED to import {f}")
            continue
        nanite = mesh.get_editor_property("nanite_settings")
        nanite.enabled = True
        mesh.set_editor_property("nanite_settings", nanite)
        unknown = []
        for i, sm in enumerate(mesh.get_editor_property("static_materials")):
            slot = str(sm.get_editor_property("material_slot_name"))
            base = slot.split(".")[0].split("_")[0]  # Blender may append .001; FBX may add _ suffixes
            if base in mis:
                mesh.set_material(i, mis[base])
            else:
                unknown.append(slot)
        if any(name.startswith("SM_Kit_" + k) for k in SOLID):
            # The UCX_ boxes don't survive the 5.8 FBX import (0 simple shapes), and Chaos vehicles
            # only collide with simple shapes: walls, coping and street fixtures use their render
            # mesh as collision, so cars stop at parapets instead of driving through them.
            bs = mesh.get_editor_property("body_setup")
            bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            mesh.set_editor_property("body_setup", bs)
        b = mesh.get_bounds()
        ext = b.box_extent
        L.save(mesh)
        done.append(name)
        log(f"{name}: {ext.x * 2:.0f} x {ext.y * 2:.0f} x {ext.z * 2:.0f} cm, origin {b.origin}"
            + (f"; unknown slots {unknown}" if unknown else ""))
    log(f"imported {len(done)} of {len(files)} kit pieces into {DEST}")


main()
