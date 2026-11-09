"""Import the street props: Poly Haven models (Tools/fetch_models.py) merged into one mesh each by
Tools/blender/combine_gltf.py -> SourceArt/Downloads/Props/SM_Prop_<slug>.glb.

  scripts/run_python.sh Content/Python/oc_import_props.py

Each GLB becomes /Game/Environment/Props/SM_Prop_<slug> (with its materials and 1K textures in a
folder of the same name). Nanite on (foliage with "preserve area" so thin leaves don't vanish),
textures capped at 1024 (props, WORLD_METRICS.md). Re-running replaces them.
"""

import os

import unreal

import oc_lib as L

TAG = "oc_props"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJECT_DIR, "SourceArt/Downloads/Props")
DEST = "/Game/Environment/Props"
FOLIAGE = {"SM_Prop_island_tree_01", "SM_Prop_island_tree_02", "SM_Prop_fern_02"}
SURFACE = "/Game/Materials/M_Master_Surface"
TWO_SIDED = "/Game/Materials/M_Master_SurfaceTwoSided"
LAMP_GLOW = "/Game/Materials/M_Master_LampGlow"
MEL = unreal.MaterialEditingLibrary


FOLIAGE_MASTER = "/Game/Materials/M_Master_Foliage"


def material_for(slot, folder, textures, foliage=False):
    """MI_Prop_<slot> from the slot's Poly Haven maps (<slot>_diff/_nor_gl/_arm). Interchange imports
    the textures but no materials from these glTFs, so the props would render default grey."""
    if slot.endswith(("_glass", "_bulb")):
        return unreal.load_asset(LAMP_GLOW)
    def tex(kind):
        for name in (f"{slot}_{kind}_1k", f"{slot.rsplit('_', 1)[0]}_{kind}_1k"):
            if name in textures:
                return textures[name]
        return None
    bc, n, arm = tex("diff"), tex("nor_gl"), tex("arm")
    if bc is None:
        L.log(TAG, f"warning: no textures for slot {slot}")
        return None
    if n:  # glTF normals are OpenGL style (green up); the master expects DirectX
        L.set_props(n, flip_green_channel=True, srgb=False,
                    compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP)
        L.save(n)
    if arm:
        L.set_props(arm, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_MASKS)
        L.save(arm)
    leafy = "leaves" in slot or "fern" in slot
    mi = L.get_or_create(f"MI_Prop_{slot}", folder, unreal.MaterialInstanceConstant,
                         unreal.MaterialInstanceConstantFactoryNew(), TAG)
    # Plants: every slot (trunk, branches, leaves) on the foliage master, so the wind moves them together.
    parent = FOLIAGE_MASTER if foliage else (TWO_SIDED if leafy else SURFACE)
    MEL.set_material_instance_parent(mi, unreal.load_asset(parent))
    MEL.clear_all_material_instance_parameters(mi)
    for param, t in (("BaseColor", bc), ("Normal", n), ("ORM", arm)):
        if t:
            MEL.set_material_instance_texture_parameter_value(mi, param, t)
    settings = (("Subsurface", 1.0 if leafy else 0.0),) if foliage else (("TileSizeM", 1.0), ("Macro", 0.0), ("TintSlot", -1.0))
    for k, v in settings:
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    MEL.update_material_instance(mi)
    L.save(mi)
    return mi


def main():
    lib = unreal.EditorAssetLibrary
    # Earlier imports straight from the glTF packs split every node into its own mesh.
    for old in lib.list_assets(DEST, recursive=False, include_folder=True):
        name = old.rstrip("/").split("/")[-1]
        if not name.startswith("SM_Prop_") and lib.does_directory_exist(old):
            lib.delete_directory(old)
    files = sorted(f for f in os.listdir(SRC) if f.endswith(".glb"))
    tasks = []
    for f in files:
        name = os.path.splitext(f)[0]
        t = unreal.AssetImportTask()
        L.set_props(t, filename=os.path.join(SRC, f), destination_path=f"{DEST}/{name}", replace_existing=True,
                    automated=True, save=False)
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for f in files:
        name = os.path.splitext(f)[0]
        meshes = []
        for path in lib.list_assets(f"{DEST}/{name}", recursive=True):
            a = lib.load_asset(path)
            if isinstance(a, unreal.StaticMesh):
                ns = a.get_editor_property("nanite_settings")
                ns.enabled = True
                if name in FOLIAGE:
                    ns.shape_preservation = unreal.NaniteShapePreservation.PRESERVE_AREA
                a.set_editor_property("nanite_settings", ns)
                meshes.append(a)
            elif isinstance(a, unreal.Texture2D):
                a.set_editor_property("max_texture_size", 1024)
            else:
                continue
            L.save(a)
        if len(meshes) != 1:
            raise RuntimeError(f"{name}: expected one mesh, got {len(meshes)}")
        # Give the mesh the prop's name so placement can find it.
        mesh = meshes[0]
        target = f"{DEST}/{name}/{name}"
        if mesh.get_path_name().split(".")[0] != target:
            if lib.does_asset_exist(target):
                lib.delete_asset(target)
            lib.rename_asset(mesh.get_path_name().split(".")[0], target)
        mesh = unreal.load_asset(target)
        textures = {}
        for path in lib.list_assets(f"{DEST}/{name}", recursive=True):
            a = lib.load_asset(path)
            if isinstance(a, unreal.Texture2D):
                textures[a.get_name()] = a
        for i, sm in enumerate(mesh.get_editor_property("static_materials")):
            mi = material_for(str(sm.get_editor_property("material_slot_name")), f"{DEST}/{name}", textures, foliage=name in FOLIAGE)
            if mi:
                mesh.set_material(i, mi)
        L.save(mesh)
        b = mesh.get_bounds().box_extent
        L.log(TAG, f"{target}: {b.x * 2:.0f} x {b.y * 2:.0f} x {b.z * 2:.0f} cm")


main()
