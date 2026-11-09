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
        b = unreal.load_asset(target).get_bounds().box_extent
        L.log(TAG, f"{target}: {b.x * 2:.0f} x {b.y * 2:.0f} x {b.z * 2:.0f} cm")


main()
