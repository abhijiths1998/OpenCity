"""M3 water surfaces and distant scenery for the Marevia map (grey-box).

Called from oc_m3_world.py after the story layer. Water is a custom surface, not the Water
plugin (decision and reasons in ARCHITECTURE.md section 7, "Water"):

  Water_SeaSurface      one plane at sea level reaching to the horizon; the landscape cuts the
                        coastline, the Darsena and the estuary out of it (not spatially loaded)
  Water_RiverSurface_*  tilted planes along the Fiume Ardo bed profile, 2.5 m above the bed,
                        wide enough to run into both banks (spatially loaded with their cells)
  Water_ReservoirSurface  the lake behind the Diga del Ferrale, cut off at the dam crest

Scenery (STORY.md: seen, never reached in Part 1), not spatially loaded, no collision, no shadows:

  Scenery_Peak_*        distant peaks north of Monte Ferrale
  Scenery_EastHill_*    the coast continuing past the east edge, so the map has no visible end
  Scenery_IsolaVentura_*  the island offshore with its sea fortress

Deep-water gameplay (reset to bank, wreck vehicles) stays in the AOCDeepWaterVolume actors that
oc_m3_story.py places; these surfaces are visual only and have no collision.
"""

import math
import random

import unreal

import oc_lib as L

TAG = "oc_m3"
MAT = "/Game/Dev/Materials/M_Dev_"
PLANE = "/Engine/BasicShapes/Plane"
SPHERE = "/Engine/BasicShapes/Sphere"
CONE = "/Engine/BasicShapes/Cone"
CUBE = "/Engine/BasicShapes/Cube"

SEA_EXTENT_M = 40000.0   # half size of the sea plane: past the horizon from any hill in the map

_assets = {}


def log(msg):
    L.log(TAG, msg)


def asset(path):
    if path not in _assets:
        _assets[path] = unreal.load_asset(path)
        if _assets[path] is None:
            raise RuntimeError(f"missing asset {path}")
    return _assets[path]


def visual(label, folder, center_m, size_m, mat, mesh, rot=(0, 0, 0), spatial=True, shadows=True):
    """A collision-free mesh centred at center_m (layout metres), size in metres (engine meshes are 1 m)."""
    x, y, z = center_m
    a = L.spawn(unreal.StaticMeshActor, (x * 100.0, -y * 100.0, z * 100.0), rot, label, folder)
    comp = a.get_component_by_class(unreal.StaticMeshComponent)
    comp.set_static_mesh(asset(mesh))
    art = {"Terrain": "MI_Box_Rock", "Hill": "MI_Box_Hill", "Landmark": "MI_Box_Stucco", "Water": "MI_Water"}.get(mat)
    final = unreal.load_asset(f"/Game/Environment/Materials/{art}") if art else None
    comp.set_material(0, final or asset(MAT + mat))
    comp.set_collision_profile_name("NoCollision")
    comp.set_editor_property("cast_shadow", shadows)
    a.set_actor_scale3d(unreal.Vector(*size_m))
    if not spatial:
        a.set_editor_property("is_spatially_loaded", False)
    return a


def river_half_width(y):  # Tools/make_marevia_layout.py
    if y < 260:
        return 25.0 + (260 - y) * 0.45
    return 25.0 if y < 2100 else 14.0


BANK_M = 12.0  # Tools/make_marevia_layout.py: banks rise from the bed over 12 m beside the channel


def build_water(layout):
    water = layout["water"]
    sea = layout.get("sea_level", 0.0)
    ox, oy = layout["origin"]
    sx, sy = layout["size"]
    cx, cy = ox + sx / 2.0, oy + sy / 2.0

    visual("Water_SeaSurface", "Water/Surface", (cx, cy, sea), (2 * SEA_EXTENT_M, 2 * SEA_EXTENT_M, 1.0),
           "Water", PLANE, spatial=False, shadows=False)

    # River: one plane per bed-profile step, surface = bed + depth, tilted to follow the bed.
    prof = water["river"]["profile_y_x_bed"]
    depth = water["river"]["depth"]
    n = 0
    for (y0, x0, b0), (y1, x1, b1) in zip(prof, prof[1:]):
        z0, z1 = b0 + depth, b1 + depth
        if max(z0, z1) <= sea + 0.05:
            continue  # the sea surface covers the river mouth
        horiz = math.hypot(x1 - x0, y1 - y0)
        pitch = math.degrees(math.atan2(z1 - z0, horiz))
        yaw = math.degrees(math.atan2(-(y1 - y0), x1 - x0))
        width = 2.0 * (river_half_width((y0 + y1) / 2.0) + BANK_M)
        length = math.hypot(horiz, z1 - z0) + 1.0  # 0.5 m overlap at each joint: no cracks on bends
        visual(f"Water_RiverSurface_{n:03d}", "Water/Surface/River", ((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0),
               (length, width, 1.0), "Water", PLANE, rot=(0, pitch, yaw), shadows=False)
        n += 1

    # Reservoir: bounded by the basin's rising sides, cut at the dam crest road (the dam's centre line).
    rx0, ry0, rx1, ry1 = water["reservoir"]
    level = water["reservoir_level"]
    dam_y = water["river"]["dam_y"]
    rcx, rcy = (rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0
    ax, ay = (rx1 - rx0) / 2.0 * 1.3, (ry1 - ry0) / 2.0 * 1.3
    north = rcy + ay
    visual("Water_ReservoirSurface", "Water/Surface", (rcx, (dam_y + north) / 2.0, level), (2 * ax, north - dam_y, 1.0),
           "Water", PLANE, spatial=False, shadows=False)
    log(f"water surfaces: sea, reservoir and {n} river sections")


def build_scenery(layout):
    ox, oy = layout["origin"]
    sx, sy = layout["size"]
    north_edge, east_edge = oy + sy, ox + sx
    rng = random.Random(1951)
    count = 0

    # Distant peaks: a ragged ridge 1.5-6 km behind Monte Ferrale (cones, rock above 300 m).
    for i in range(16):
        x = ox - 2500.0 + i * (sx + 5000.0) / 15.0 + rng.uniform(-200, 200)
        h = rng.uniform(700.0, 1500.0)
        r = h * rng.uniform(1.4, 2.2)
        y = north_edge + r + rng.uniform(300.0, 2500.0)  # the whole cone stays behind the map edge
        visual(f"Scenery_Peak_{i:02d}", "Scenery/Peaks", (x, y, h / 2.0 - 60.0), (2 * r, 2 * r * rng.uniform(0.7, 1.0), h),
               "Terrain", CONE, rot=(0, 0, rng.uniform(0, 360)), spatial=False, shadows=False)
        count += 1

    # The coast continues east: low hills near the sea rising to the mountains in the north.
    for i in range(12):
        y = oy + 250.0 + i * (sy + 600.0) / 11.0
        t = max(0.0, min(1.0, (y - 300.0) / (sy - 300.0)))
        h = 40.0 + 520.0 * t * t + rng.uniform(-20, 40)
        size_x = rng.uniform(900, 1400)
        x = east_edge + size_x / 2.0 + rng.uniform(60.0, 300.0)  # the whole hill stays past the map edge
        visual(f"Scenery_EastHill_{i:02d}", "Scenery/EastCoast", (x, y, -h * 0.15),
               (size_x, rng.uniform(500, 800), 2.3 * h), "Hill", SPHERE, spatial=False, shadows=False)
        count += 1

    # Isola Ventura: a rocky island ~3.5 km offshore with an old sea fortress (STORY.md).
    ix, iy = ox + sx * 0.62, oy - 3500.0
    parts = [((0, 0), (2200, 900, 2 * 70), 0), ((-450, 80), (900, 600, 2 * 160), 10), ((520, -60), (700, 450, 2 * 110), -15)]
    for k, ((dx, dy), size, yaw) in enumerate(parts):
        visual(f"Scenery_IsolaVentura_Rock{k}", "Scenery/IsolaVentura", (ix + dx, iy + dy, -size[2] * 0.12), size, "Terrain", SPHERE,
               rot=(0, 0, yaw), spatial=False, shadows=False)
    fort_z = parts[1][1][2] * (0.5 - 0.12) - 6.0  # top of the western rock, fortress sunk 6 m
    visual("Scenery_IsolaVentura_Fortress", "Scenery/IsolaVentura", (ix - 450, iy + 80, fort_z + 12), (110, 70, 24), "Landmark", CUBE,
           spatial=False, shadows=False)
    visual("Scenery_IsolaVentura_Keep", "Scenery/IsolaVentura", (ix - 480, iy + 90, fort_z + 30), (24, 24, 36), "Landmark", CUBE,
           spatial=False, shadows=False)
    count += len(parts) + 2
    log(f"scenery: {count} pieces (peaks, east coast, Isola Ventura)")


def build(layout):
    build_water(layout)
    build_scenery(layout)
