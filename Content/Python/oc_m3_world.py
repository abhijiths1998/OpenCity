"""M3 world build: the Marevia World Partition map with terrain, lighting, player start, roads and
buildings; oc_m3_story.py adds bridges, landmarks, gates, water volumes and the progression data.

Reads SourceArt/World/marevia_layout.json and marevia_height.png (Tools/make_marevia_layout.py).
Run through scripts/build_world.sh, which deletes the old map first (World Partition keeps
one file per actor, so rebuilding in place is not reliable).

Layout metres (x east, y north) -> Unreal cm (X east, Y south): see FOCRoadGraph::LayoutToWorld.
"""

import heapq
import json
import math
import os

import unreal

import oc_lib as L
import oc_m3_story
import oc_m3_water

TAG = "oc_m3"
MAP_PATH = "/Game/Maps/Marevia/L_Marevia"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
WORLD_DIR = os.path.join(PROJECT_DIR, "SourceArt/World")

# World Partition streaming: 128 m cells; cars reach ~50 m/s, so load 384 m around the player.
CELL_SIZE_CM = 12800
LOADING_RANGE_CM = 38400
LANDSCAPE_GRID = 2  # components per streaming proxy edge (252 m)

# HLOD (ARCHITECTURE.md section 7): two layers, built by scripts/build_hlod.sh.
#   HLOD0 instanced: the real meshes (buildings, roads, bridges) and the landscape's own HLOD,
#                    256 m cells shown out to 1 km, past the 384 m streaming range.
#   HLOD1 merged: one merged mesh per 1 km cell, out to 8 km, so
#                    the whole map and the mountains stay visible from anywhere.
HLOD_PATH = "/Game/World/HLOD"
HLOD_CELLS_CM = [25600, 102400]
HLOD_RANGES_CM = [102400, 800000]


def log(msg):
    L.log(TAG, msg)


def to_world(x, y, z=0.0):
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def setup_streaming(world):
    n = unreal.OCWorldBuilderLibrary.configure_streaming(world, CELL_SIZE_CM, LOADING_RANGE_CM)
    if n == 0:
        raise RuntimeError("no streaming partitions configured")
    log(f"streaming: {n} partition(s), cell {CELL_SIZE_CM} cm, loading range {LOADING_RANGE_CM} cm")


def setup_hlod(world):
    factory = unreal.HLODLayerFactory()
    # Merged, not approximated: the approximate builder needs MeshModelingToolsetExp (Experimental).
    top = L.get_or_create("HLOD1_Merged", HLOD_PATH, unreal.HLODLayer, factory, TAG)
    L.set_props(top, layer_type=unreal.HLODLayerType.MESH_MERGE, parent_layer=None)
    L.save(top)
    base = L.get_or_create("HLOD0_Instanced", HLOD_PATH, unreal.HLODLayer, factory, TAG)
    L.set_props(base, layer_type=unreal.HLODLayerType.INSTANCING, parent_layer=top)
    L.save(base)
    levels = unreal.OCWorldBuilderLibrary.configure_hlod(world, base, HLOD_CELLS_CM, HLOD_RANGES_CM)
    if levels != 2:
        raise RuntimeError(f"HLOD setup failed ({levels} levels)")
    log(f"HLOD: {base.get_name()} -> {top.get_name()}, cells {HLOD_CELLS_CM} cm, ranges {HLOD_RANGES_CM} cm")


# Perf camera loop (scripts/perf_run.sh L_Marevia): through the two districts open in a new game,
# 4 m over the roads at 25 m/s (city driving pace; ~190 s a lap). Waypoints in layout metres; the loop follows the road graph.
PERF_WAYPOINTS = [(780, 1290), (1140, 1000), (2250, 980), (2175, 1150), (2670, 1500), (1725, 1600), (780, 1290)]


def road_path(layout, a, b):
    """Shortest ungated road path (node indices) between the nodes nearest layout points a and b."""
    nodes = layout["roads"]["nodes"]
    adj = {}
    for s in layout["roads"]["segments"]:
        if s["gate"]:
            continue
        d = math.dist(nodes[s["a"]][:2], nodes[s["b"]][:2])
        adj.setdefault(s["a"], []).append((s["b"], d))
        adj.setdefault(s["b"], []).append((s["a"], d))
    near = lambda p: min(range(len(nodes)), key=lambda i: math.dist(nodes[i][:2], p))
    start, goal = near(a), near(b)
    dist, came, todo = {start: 0.0}, {}, [(0.0, start)]
    while todo:
        d, n = heapq.heappop(todo)
        if n == goal:
            break
        if d > dist[n]:
            continue
        for m, w in adj.get(n, []):
            if d + w < dist.get(m, 1e18):
                dist[m], came[m] = d + w, n
                heapq.heappush(todo, (d + w, m))
    path = [goal]
    while path[-1] != start:
        path.append(came[path[-1]])
    return path[::-1]


def build_perf_path(layout):
    nodes = layout["roads"]["nodes"]
    loop = []
    for a, b in zip(PERF_WAYPOINTS, PERF_WAYPOINTS[1:]):
        leg = road_path(layout, a, b)
        loop.extend(leg if not loop else leg[1:])
    loop = loop[:-1]  # closed spline: the last node is the first
    cls = unreal.load_class(None, "/Script/OpenCity.OCPerfCameraPath")
    x0, y0, z0 = nodes[loop[0]]
    perf = L.spawn(cls, (x0 * 100.0, -y0 * 100.0, (z0 + 4.0) * 100.0), (0, 0, 0), "PerfCameraPath_Marevia", "Dev")
    perf.set_editor_property("is_spatially_loaded", False)
    L.set_props(perf, generate_circle=False, speed_cm_per_sec=2500.0, camera_pitch=-6.0, warmup_seconds=8.0)
    spline = perf.get_component_by_class(unreal.SplineComponent)
    spline.clear_spline_points(False)
    for i in loop:
        x, y, z = nodes[i]
        spline.add_spline_point(to_world(x, y, z + 4.0), unreal.SplineCoordinateSpace.WORLD, False)
    spline.set_closed_loop(True, True)
    log(f"perf camera loop: {len(loop)} road nodes, {spline.get_spline_length() / 100.0:.0f} m")


def build_roads(world, layout):
    roads = layout["roads"]
    n = unreal.OCWorldBuilderLibrary.build_road_meshes(
        world, os.path.join(WORLD_DIR, "marevia_layout.json"), os.path.join(WORLD_DIR, "marevia_height.png"), "/Game/World/Roads",
        unreal.load_asset("/Game/Dev/Materials/M_Dev_Road"), unreal.load_asset("/Game/Dev/Materials/M_Dev_Sidewalk"),
        [unreal.Name(k) for k in roads["sidewalk_kinds"]], roads["sidewalk_width"] * 100.0, 25200.0)
    if n == 0:
        raise RuntimeError("no road chunks built")
    log(f"roads: {n} chunks")


def build_buildings(world):
    n = unreal.OCWorldBuilderLibrary.build_buildings(
        world, os.path.join(WORLD_DIR, "marevia_layout.json"), unreal.load_asset("/Engine/BasicShapes/Cube"),
        unreal.load_asset("/Game/Dev/Materials/M_Dev_Building"), 25200.0)
    if n == 0:
        raise RuntimeError("no buildings built")
    log(f"buildings: {n}")


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
    setup_hlod(world)

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
    build_buildings(world)
    oc_m3_story.build(world, layout)
    oc_m3_water.build(layout)
    build_perf_path(layout)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved {MAP_PATH}")


main()
