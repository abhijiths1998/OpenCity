"""M3 world build: the Marevia World Partition map with terrain, lighting and player start.

Reads SourceArt/World/marevia_layout.json and marevia_height.png (Tools/make_marevia_layout.py).
Run through scripts/build_world.sh, which deletes the old map first (World Partition keeps
one file per actor, so rebuilding in place is not reliable).

Layout metres (x east, y north) -> Unreal cm (X east, Y south): see FOCRoadGraph::LayoutToWorld.
"""

import json
import os

import unreal

import oc_lib as L

TAG = "oc_m3"
MAP_PATH = "/Game/Maps/Marevia/L_Marevia"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
WORLD_DIR = os.path.join(PROJECT_DIR, "SourceArt/World")

# World Partition streaming: 128 m cells; cars reach ~50 m/s, so load 384 m around the player.
CELL_SIZE_CM = 12800
LOADING_RANGE_CM = 38400
LANDSCAPE_GRID = 2  # components per streaming proxy edge (252 m)


def log(msg):
    L.log(TAG, msg)


def to_world(x, y, z=0.0):
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def setup_streaming(world):
    n = unreal.OCWorldBuilderLibrary.configure_streaming(world, CELL_SIZE_CM, LOADING_RANGE_CM)
    if n == 0:
        raise RuntimeError("no streaming partitions configured")
    log(f"streaming: {n} partition(s), cell {CELL_SIZE_CM} cm, loading range {LOADING_RANGE_CM} cm")


def main():
    with open(os.path.join(WORLD_DIR, "marevia_layout.json"), encoding="utf-8") as f:
        layout = json.load(f)
    size_x, size_y = layout["size"]
    origin_x, origin_y = layout["origin"]
    hmin, hmax = layout["height_range"]

    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level(MAP_PATH, True):
        raise RuntimeError(f"could not create World Partition map {MAP_PATH} (delete it first)")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    setup_streaming(world)

    # Landscape: 1 m quads; heightmap 0..65535 spans hmin..hmax metres.
    #   world Z = Location.Z + Scale.Z * (v - 32768) / 128   must equal   100 * (hmin + v / 65535 * range)
    rng = hmax - hmin
    scale_z = 12800.0 * rng / 65535.0
    loc_z = 100.0 * hmin + scale_z * 256.0
    north_edge_y = origin_y + size_y  # PNG row 0 is the north edge = landscape min Y in Unreal
    location = unreal.Vector(origin_x * 100.0, -north_edge_y * 100.0, loc_z)
    scale = unreal.Vector(100.0, 100.0, scale_z)
    landscape = unreal.OCWorldBuilderLibrary.create_landscape_from_heightmap(
        world, os.path.join(WORLD_DIR, "marevia_height.png"), location, scale, 2, 63, LANDSCAPE_GRID,
        unreal.load_asset("/Game/Dev/Materials/M_Dev_Terrain"))  # grey-box height colours; M4 replaces it
    if landscape is None:
        raise RuntimeError("landscape creation failed (see LogOpenCity)")
    landscape.set_actor_label("Landscape_Marevia")
    log(f"landscape at {location}, scale {scale}")

    # Lighting and sky: always loaded (not spatially streamed).
    L.daylight()
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if isinstance(a, (unreal.DirectionalLight, unreal.SkyAtmosphere, unreal.SkyLight, unreal.ExponentialHeightFog)):
            a.set_editor_property("is_spatially_loaded", False)

    start = layout["start"]
    ps = L.spawn(unreal.PlayerStart, (start["pos"][0] * 100.0, -start["pos"][1] * 100.0, start["ground_z"] * 100.0 + 150.0),
                 (0, 0, 0), "PlayerStart_CittaAlta")
    ps.set_editor_property("is_spatially_loaded", False)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved {MAP_PATH}")


main()
