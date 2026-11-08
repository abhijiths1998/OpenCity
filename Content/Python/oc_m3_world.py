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


def build_roads(world, layout):
    roads = layout["roads"]
    n = unreal.OCWorldBuilderLibrary.build_road_meshes(
        world, os.path.join(WORLD_DIR, "marevia_layout.json"), os.path.join(WORLD_DIR, "marevia_height.png"), "/Game/World/Roads",
        unreal.load_asset("/Game/Dev/Materials/M_Dev_Road"), unreal.load_asset("/Game/Dev/Materials/M_Dev_Sidewalk"),
        [unreal.Name(k) for k in roads["sidewalk_kinds"]], roads["sidewalk_width"] * 100.0, 25200.0)
    if n == 0:
        raise RuntimeError("no road chunks built")
    log(f"roads: {n} chunks")


def build_bridges(layout):
    """Decks (collidable, 1 m thick) and parapets for every bridge segment in the road graph."""
    import math
    nodes = layout["roads"]["nodes"]
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    mat = unreal.load_asset("/Game/Dev/Materials/M_Dev_Bridge")
    count = 0
    for s in layout["roads"]["segments"]:
        if not s["bridge"]:
            continue
        ax, ay, az = nodes[s["a"]]
        bx, by, bz = nodes[s["b"]]
        # Unreal: X east, Y south. Deck runs A -> B, 5 m onto each bank, tilted to meet both ends.
        dx, dy, dz = (bx - ax), -(by - ay), (bz - az)
        length = math.hypot(dx, dy) + 10.0
        yaw = math.degrees(math.atan2(dy, dx))
        pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
        width = s["width"] + 3.0
        mid = ((ax + bx) / 2.0 * 100.0, -(ay + by) / 2.0 * 100.0, (az + bz) / 2.0 * 100.0)
        deck = L.spawn(unreal.StaticMeshActor, (mid[0], mid[1], mid[2] - 45.0), (0, pitch, yaw), f"Bridge_{s['bridge']}", "Bridges")
        comp = deck.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_static_mesh(cube)
        comp.set_material(0, mat)
        deck.set_actor_scale3d(unreal.Vector(length, width, 1.0))
        # Parapets: 1.1 m high, 40 cm thick, along both edges (offset in the deck's local Y).
        right = (math.sin(math.radians(yaw)) * -1.0, math.cos(math.radians(yaw)))  # unit vector to the deck's side
        for sign in (-1, 1):
            off = sign * (width / 2.0 - 0.2) * 100.0
            p = (mid[0] + right[0] * off, mid[1] + right[1] * off, mid[2] + 55.0)
            rail = L.spawn(unreal.StaticMeshActor, p, (0, pitch, yaw), f"Bridge_{s['bridge']}_Parapet{'L' if sign < 0 else 'R'}", "Bridges")
            rc = rail.get_component_by_class(unreal.StaticMeshComponent)
            rc.set_static_mesh(cube)
            rc.set_material(0, mat)
            rail.set_actor_scale3d(unreal.Vector(length, 0.4, 1.1))
        count += 1
    log(f"bridges: {count}")


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

    build_roads(world, layout)
    build_bridges(layout)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved {MAP_PATH}")


main()
