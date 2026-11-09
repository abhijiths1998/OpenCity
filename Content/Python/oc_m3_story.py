"""M3 story layer of the Marevia map: bridges, the 20 landmarks, area gates and water.

Called from oc_m3_world.py after the terrain and roads exist. Everything comes from
SourceArt/World/marevia_layout.json (Tools/make_marevia_layout.py) and STORY.md:

  Bridges     decks with parapets. The two moving bridges (Ponte Mobile lifts, Ponte Girevole
              swings) have their river span in two runtime Data Layers: lowered (open) and
              raised or turned (locked).
  Landmarks   a named grey-box (LM_<Name>_*) for every story landmark, in folder Landmarks/<Name>.
  Gates       barriers where gated roads leave open ground (cordon, checkpoint, rockfall, level
              crossing), each gate's props in DL_Gate_<Name>_Locked (active in a new game).
  Volumes     AOCLockedAreaVolume per locked district; AOCDeepWaterVolume for the sea, the river
              and the reservoir.
  Data        DA_MareviaRoads (road graph), DA_AreaUnlocks (the unlock schedule, STORY.md).

Layout metres (x east, y north) -> Unreal cm (X east, Y south).
"""

import json
import math
import random

import unreal

import oc_lib as L
import oc_networks

TAG = "oc_m3"
LAYER_PATH = "/Game/World/DataLayers"
CUBE = "/Engine/BasicShapes/Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder"
MAT = "/Game/Dev/Materials/M_Dev_"

# Decks meet the road at the bank nodes with a small overlap, a few cm low: a slab running 5 m onto a
# sloping bank left a lip the car hit at the footbridge.
DECK_LAP_M = 0.6
DECK_DROP_M = 0.04

# Bridge parapets start this far (m) from each bank node, clear of the junction there.
RAIL_CLEAR_M = 8.0

# The two bridges that move; every other gate is a barrier.
MOVING_BRIDGES = {"Gate.PonteMobile": "lift", "Gate.Molo": "swing"}

LOCKED_MESSAGES = {  # STORY.md "Unlocking the city"
    "PortoVecchio": "Police cordon. The fire scene at La Cala is still taped off.",
    "MoloSalvi": "Salvi's harbour. Not yet.",
    "Lungomare": "Salvi's territory. Not yet.",
    "BorgoMarino": "Salvi's territory. Not yet.",
    "LeColline": "Private road. The Hill families keep the hills quiet.",
    "MonteFerrale": "The pass road is blocked by a rockfall.",
}

_assets = {}


def log(msg):
    L.log(TAG, msg)


def asset(path):
    if path not in _assets:
        _assets[path] = unreal.load_asset(path)
        if _assets[path] is None:
            raise RuntimeError(f"missing asset {path}")
    return _assets[path]


def cm(x, y, z):
    """Layout metres to an Unreal location tuple (cm)."""
    return (x * 100.0, -y * 100.0, z * 100.0)


def ue_yaw(dx, dy):
    """Unreal yaw of a direction given in layout axes (y north)."""
    return math.degrees(math.atan2(-dy, dx))


# Grey-box material -> box-projected final-art instance (oc_art_materials.py), when built.
ART = {"Landmark": "MI_Box_Stucco", "LandmarkAccent": "MI_Box_Roof", "Concrete": "MI_Box_Concrete",
       "Bridge": "MI_Box_Stone", "Metal": "MI_Box_Metal", "Rock": "MI_Box_Rock"}


def material(name):
    if name in ART:
        art = unreal.load_asset(f"/Game/Environment/Materials/{ART[name]}")
        if art:
            return art
    return asset(MAT + name)


def shape(label, folder, center_m, size_m, mat, mesh=CUBE, rot=(0, 0, 0)):
    """A box or cylinder (engine meshes are 100 cm) centred at center_m, size in metres."""
    a = L.spawn(unreal.StaticMeshActor, cm(*center_m), rot, label, folder)
    comp = a.get_component_by_class(unreal.StaticMeshComponent)
    comp.set_static_mesh(asset(mesh))
    comp.set_material(0, material(mat))
    a.set_actor_scale3d(unreal.Vector(*size_m))
    return a


# --------------------------------------------------------------------------------------------
# Bridges

def river_span(layout, a, ux, uy, length, margin=6.0):
    """(first, last) distance along a bridge that lies over the Fiume Ardo channel plus `margin`,
    or None if the bridge doesn't cross the river."""
    line = layout["water"]["river"]["centerline"]

    def dist_to_river(x, y):
        best = 1e18
        for (x0, y0), (x1, y1) in zip(line, line[1:]):
            dx, dy = x1 - x0, y1 - y0
            t = max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / max(dx * dx + dy * dy, 1e-9)))
            best = min(best, math.hypot(x - x0 - t * dx, y - y0 - t * dy))
        return best

    over = [d for d in range(0, int(length) + 1)
            if dist_to_river(a[0] + ux * d, a[1] + uy * d) < river_half_width(a[1] + uy * d) + margin]
    return (float(over[0]), float(over[-1])) if over else None


def build_bridges(layout, layers):
    """Decks (1 m thick, 5 m onto each bank) and 1.1 m parapets. Moving bridges: the span over the
    water (all but the 30 m of approach at each end) goes in the Open layer, and its raised or
    turned twin in the Locked layer."""
    nodes = layout["roads"]["nodes"]
    count = 0
    for s in layout["roads"]["segments"]:
        if not s["bridge"]:
            continue
        a, b = nodes[s["a"]], nodes[s["b"]]
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        width = s["width"] + 3.0
        folder = f"Bridges/{s['bridge']}"
        mode = MOVING_BRIDGES.get(s.get("gate"))

        def at(d):
            t = d / length
            return a[0] + ux * d, a[1] + uy * d, a[2] + (b[2] - a[2]) * t

        wet = river_span(layout, a, ux, uy, length)

        def deck(d0, d1, suffix, layer=None):
            p0, p1 = at(d0), at(d1)
            mid = tuple((p0[i] + p1[i]) / 2.0 for i in range(3))
            pitch = math.degrees(math.atan2(p1[2] - p0[2], d1 - d0))
            yaw = ue_yaw(ux, uy)
            actors = [shape(f"Bridge_{s['bridge']}_{suffix}", folder, (mid[0], mid[1], mid[2] - 0.5 - DECK_DROP_M),
                            (d1 - d0, width, 1.0), "Bridge", rot=(0, pitch, yaw))]
            # Parapets along both edges, but only clear of the bank nodes: the deck runs 5 m onto the
            # bank, and parapets there stood across the junction roads at each end. Over the river
            # they also stop 6 m past the channel: the Passerella's west end lies beyond the riverside
            # avenue, and parapets across its lanes stopped traffic (drive test 2026-11-09).
            r0, r1 = max(d0, RAIL_CLEAR_M, wet[0] if wet else -1e9), min(d1, length - RAIL_CLEAR_M, wet[1] if wet else 1e9)
            q0, q1 = at(r0), at(r1)
            rmid = tuple((q0[i] + q1[i]) / 2.0 for i in range(3))
            for side in (-1, 1) if r1 - r0 > 1.0 else ():
                ox, oy = -uy * side * (width / 2.0 - 0.2), ux * side * (width / 2.0 - 0.2)
                actors.append(shape(f"Bridge_{s['bridge']}_{suffix}_Parapet{'L' if side < 0 else 'R'}", folder,
                                    (rmid[0] + ox, rmid[1] + oy, rmid[2] + 0.55), (r1 - r0, 0.4, 1.1), "Bridge",
                                    rot=(0, pitch, yaw)))
            for act in actors:
                if layer:
                    unreal.OCWorldBuilderLibrary.add_actor_to_data_layer(act, layer)
            return actors

        if not mode:
            deck(-DECK_LAP_M, length + DECK_LAP_M, "Deck")
        else:
            gate = s["gate"].split(".")[1]
            span0, span1 = 30.0, length - 30.0
            deck(-DECK_LAP_M, span0, "ApproachA")
            deck(span1, length + DECK_LAP_M, "ApproachB")
            deck(span0, span1, "Span", layers[f"{gate}_Open"])
            locked = layers[f"{gate}_Locked"]
            if mode == "lift":
                # Bascule: two leaves hinged at the banks, raised to 70 degrees.
                leaf = (span1 - span0) / 2.0
                for hinge, sign, suffix in ((span0, 1.0, "LeafA"), (span1, -1.0, "LeafB")):
                    hx, hy, hz = at(hinge)
                    c = (hx + ux * sign * leaf / 2.0 * math.cos(math.radians(70)),
                         hy + uy * sign * leaf / 2.0 * math.cos(math.radians(70)),
                         hz + leaf / 2.0 * math.sin(math.radians(70)))
                    pitch = 70.0 if sign > 0 else -70.0
                    act = shape(f"Bridge_{s['bridge']}_{suffix}_Raised", folder, c, (leaf, width, 1.0), "Bridge",
                                rot=(0, pitch, ue_yaw(ux, uy)))
                    unreal.OCWorldBuilderLibrary.add_actor_to_data_layer(act, locked)
            else:
                # Swing bridge: the span turned 90 degrees on its pivot pier, parallel to the quay.
                mx, my, mz = at((span0 + span1) / 2.0)
                act = shape(f"Bridge_{s['bridge']}_Span_Turned", folder, (mx, my, mz - 0.5),
                            (span1 - span0, width, 1.0), "Bridge", rot=(0, 0, ue_yaw(ux, uy) + 90.0))
                unreal.OCWorldBuilderLibrary.add_actor_to_data_layer(act, locked)
                shape(f"Bridge_{s['bridge']}_Pivot", folder, (mx, my, mz - 6.0), (8.0, 8.0, 10.0), "Concrete", CYLINDER)
        count += 1
    log(f"bridges: {count}")


# --------------------------------------------------------------------------------------------
# Landmarks

# Parts replaced by the kit-built main volume (make_marevia_layout.py KIT_LANDMARKS).
KIT_REPLACED = {"Main", "Roof", "Tower", "Crown", "Band", "Neon", "Door"}


def build_landmarks(layout):
    crest = layout["water"]["reservoir_level"] + 2.0
    built = []
    kit_built = {b["landmark"] for b in layout["buildings"] if b.get("landmark")}
    for lm in layout["landmarks"]:
        name, kind = lm["name"], lm["kind"]
        x, y = lm["pos"]
        g = lm["ground_z"]
        w, d = lm["footprint"]
        h = lm["height"]
        folder = f"Landmarks/{name}"

        def part(suffix, off, size, mat="Landmark", mesh=CUBE, rot=(0, 0, 0), sink=2.0):
            """off = (east, north, base above ground) in m; the base sinks `sink` m into slopes."""
            label = f"LM_{name}" if suffix == "Main" else f"LM_{name}_{suffix}"
            if name in kit_built and suffix in KIT_REPLACED:
                if suffix == "Main":  # keep the named marker missions and checks look for (a hidden
                    # 20 cm box: World Partition descriptors need real bounds to place it)
                    marker = shape(label, folder, (x + off[0], y + off[1], g + off[2] + 1.0), (0.2, 0.2, 0.2), mat)
                    c = marker.get_component_by_class(unreal.StaticMeshComponent)
                    c.set_visibility(False)
                    c.set_collision_profile_name("NoCollision")
                return None
            return shape(label, folder, (x + off[0], y + off[1], g + off[2] - sink + (size[2] + sink) / 2.0),
                         (size[0], size[1], size[2] + sink), mat, mesh, rot)

        if kind == "ruin":  # La Cala: the burned house, roofless walls
            part("Main", (0, 0, 0), (w, d, 0.4), "Burnt")
            for suffix, off, size in (("WallS", (0, -d / 2, 0), (w, 0.6, h)), ("WallN", (0, d / 2, 0), (w, 0.6, h * 0.5)),
                                      ("WallW", (-w / 2, 0, 0), (0.6, d, h * 0.8)), ("WallE", (w / 2, 0, 0), (0.6, d, h * 0.35))):
                part(suffix, off, size, "Burnt")
        elif kind == "hall":
            part("Main", (0, 0, 0), (w, d, h * 0.7))
            part("Roof", (0, 0, h * 0.7), (w + 2, d + 2, 1.2), "LandmarkAccent", sink=0)
            part("Canopy", (0, -d / 2 - 3, 3.5), (w, 6, 0.4), "Metal", sink=0)
        elif kind == "pier":
            part("Main", (0, 0, 0.3), (w, d, 0.6), "Concrete", sink=4.0)
            part("Booth", (0, d / 2 - 6, 0.9), (6, 4, 3), sink=0)
            for i, oy in enumerate((-d / 2 + 4, 0, d / 2 - 14)):
                part(f"Bollard{i}", (w / 2 - 1, oy, 0.9), (0.6, 0.6, 0.8), "Metal", CYLINDER, sink=0)
        elif kind == "tower":
            part("Main", (0, 0, 0), (w, d, 10))
            part("Tower", (0, 0, 10), (w * 0.5, d * 0.5, h - 10), "Glass", sink=0)
            part("Crown", (0, 0, h), (w * 0.5 + 1, d * 0.5 + 1, 2), sink=0)
        elif kind == "lighthouse":
            part("Main", (0, 0, 0), (w, w, h), mesh=CYLINDER)
            part("Gallery", (0, 0, h), (w * 1.4, w * 1.4, 0.6), "Metal", CYLINDER, sink=0)
            part("Lantern", (0, 0, h + 0.6), (w * 0.7, w * 0.7, 3), "Glass", CYLINDER, sink=0)
            part("Cap", (0, 0, h + 3.6), (w * 0.8, w * 0.8, 1.2), "LandmarkAccent", CYLINDER, sink=0)
        elif kind == "church":  # Santa Lucia
            part("Main", (0, -d * 0.125, 0), (w, d * 0.75, h * 0.5))
            part("Roof", (0, -d * 0.125, h * 0.5), (w + 1, d * 0.75 + 1, 1.2), "LandmarkAccent", sink=0)
            part("Apse", (0, d * 0.25, 0), (w * 0.8, w * 0.8, h * 0.45), mesh=CYLINDER)
            part("Campanile", (w / 2 + 4, -d / 2 + 4, 0), (7, 7, h))
            part("CampanileRoof", (w / 2 + 4, -d / 2 + 4, h), (8, 8, 1.5), "LandmarkAccent", sink=0)
        elif kind == "house":
            part("Main", (0, 0, 0), (w, d, h))
            part("Roof", (0, 0, h), (w + 0.6, d + 0.6, 0.8), "LandmarkAccent", sink=0)
            part("Door", (0, -d / 2 - 0.05, 0), (2.2, 0.3, 3.2), "Burnt", sink=0)
        elif kind == "civic":
            part("Main", (0, 0, 0), (w, d, h))
            part("Canopy", (0, -d / 2 - 2.5, 4), (w * 0.4, 5, 0.4), "Metal", sink=0)
            part("Band", (0, 0, h - 3), (w + 0.3, d + 0.3, 1.2), "Police" if "Police" in name else "Glass", sink=0)
        elif kind == "yard":  # Beppe's garage: workshop and a walled yard
            part("Main", (-w * 0.225, 0, 0), (w * 0.55, d, h))
            part("Roof", (-w * 0.225, 0, h), (w * 0.55 + 0.6, d + 0.6, 0.5), "Metal", sink=0)
            part("WallN", (w * 0.275, d / 2, 0), (w * 0.45, 0.4, 2.4))
            part("WallE", (w / 2, 0, 0), (0.4, d, 2.4))
            part("WallS", (w * 0.33, -d / 2, 0), (w * 0.34, 0.4, 2.4))
        elif kind == "kiln":  # the abandoned kiln (mission 7)
            part("Main", (0, -d * 0.15, 0), (w, d * 0.7, 12))
            part("Roof", (0, -d * 0.15, 12), (w + 1, d * 0.7 + 1, 0.6), "Metal", sink=0)
            part("Chimney", (w / 3, d * 0.3, 0), (5, 5, h), "LandmarkAccent", CYLINDER)
            part("Kiln", (-w / 4, d * 0.3, 0), (14, 14, 8), "LandmarkAccent", CYLINDER)
        elif kind == "station":
            part("Main", (0, 0, 0), (w * 0.4, d, h))
            for side in (-1, 1):
                part(f"Wing{'W' if side < 0 else 'E'}", (side * w * 0.35, 0, 0), (w * 0.3, d * 0.8, h * 0.6))
            part("Clock", (0, -d / 2 + 3, 0), (6, 6, h + 10))
            part("Canopy", (0, d / 2 + 5, 5), (w, 8, 0.4), "Metal", sink=0)
        elif kind == "club":
            part("Main", (0, 0, 0), (w, d, h))
            part("Neon", (0, 0, h - 2), (w + 0.3, d + 0.3, 1.5), "Glass", sink=0)
        elif kind == "hotel":
            part("Main", (0, 0, 0), (w + 6, d + 6, 6))
            part("Tower", (0, 0, 6), (w, d, h - 6), sink=0)
            part("Crown", (0, 0, h), (w * 0.5, d * 0.5, 3), "LandmarkAccent", sink=0)
        elif kind == "villa":
            part("Main", (-w * 0.2, 0, 0), (w * 0.6, d, h))
            part("Roof", (-w * 0.2, 0, h), (w * 0.6 + 1, d + 1, 1), "LandmarkAccent", sink=0)
            part("Wing", (w * 0.3, -d * 0.2, 0), (w * 0.4, d * 0.6, h * 0.6))
            part("Pool", (w * 0.3, d * 0.35, 0), (12, 6, 0.3), "Glass", sink=0.5)
        elif kind == "lookout":  # Il Belvedere (the finale)
            part("Main", (0, 0, -0.5), (w, w, 1.0), "Concrete")
            for side, off, size in (("S", (0, -w / 2, 0.5), (w, 0.2, 1.1)), ("N", (0, w / 2, 0.5), (w, 0.2, 1.1)),
                                    ("W", (-w / 2, 0, 0.5), (0.2, w, 1.1)), ("E", (w / 2, 0, 0.5), (0.2, w, 1.1))):
                part(f"Rail{side}", off, size, "Metal", sink=0)
        elif kind == "quarry":  # La Cava
            part("Main", (w / 3, d / 3, 0), (20, 15, h))
            part("Face", (0, d / 2, 0), (w, 4, 25), "Rock")
            part("Conveyor", (w / 3 - 18, d / 3, 6), (30, 2, 1.5), "Metal", rot=(0, 12, 0), sink=0)
            rng = random.Random(name)
            for i in range(3):
                part(f"Gravel{i}", (-w / 4 + i * 14, -d / 4, 0), (12, 12, 6), "Rock", rot=(0, 0, rng.uniform(0, 90)))
        elif kind == "dam":  # Diga del Ferrale: concrete face downstream of the crest road
            road_half = 3.5 + 1.0
            top = crest - 1.0
            part("Main", (0, -(road_half + d / 2), top - g), (w, d, 0.01), "Concrete", sink=h + (top - g) + 10)
            for side in (-1, 1):  # crest parapets either side of the road
                part(f"Parapet{'S' if side < 0 else 'N'}", (0, side * (road_half - 0.3), crest - g), (w, 0.4, 1.0), "Concrete", sink=0.3)
            part("Spillway", (0, -(road_half + d + 6), top - g - 20), (30, 12, 0.01), "Concrete", sink=20)
        elif kind == "hermitage":  # L'Eremo delle Aquile
            part("Main", (0, 0, 0), (w * 0.6, d * 0.7, h))
            part("Roof", (0, 0, h), (w * 0.6 + 1, d * 0.7 + 1, 1), "LandmarkAccent", sink=0)
            part("Bell", (w * 0.3 + 1.5, 0, h), (3, 2, 4), sink=0)
            part("Wall", (0, -d / 2, 0), (w, 0.5, 2.5))
        elif kind == "mast":
            part("Main", (0, 0, 0), (2, 2, h), "Metal", CYLINDER)
            part("Hut", (4, 0, 0), (6, 4, 3))
        else:
            part("Main", (0, 0, 0), (w, d, h))
            log(f"landmark {name}: unknown kind {kind}, plain block")
        built.append(name)
    log(f"landmarks: {len(built)}")
    return built


# --------------------------------------------------------------------------------------------
# Gates

def gate_frontiers(layout):
    """(gate tag, segment, node index) wherever a gated road meets road outside that gate."""
    segs = layout["roads"]["segments"]
    adj = {}
    for i, s in enumerate(segs):
        adj.setdefault(s["a"], []).append(i)
        adj.setdefault(s["b"], []).append(i)
    out = []
    for i, s in enumerate(segs):
        gate = s.get("gate")
        if not gate or gate in MOVING_BRIDGES:
            continue
        for n in (s["a"], s["b"]):
            if any(segs[t].get("gate") != gate for t in adj[n] if t != i):
                out.append((gate, s, n))
    return out


def build_gate_props(layout, layers):
    nodes = layout["roads"]["nodes"]
    rng = random.Random(1974)
    count = 0
    for k, (gate, s, n) in enumerate(gate_frontiers(layout)):
        name = gate.split(".")[1]
        a = nodes[n]
        b = nodes[s["b"] if s["a"] == n else s["a"]]
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length   # from open ground into the gate
        sx, sy = -uy, ux                                            # across the road
        off = min(8.0, length / 3.0)
        px, py = a[0] + ux * off, a[1] + uy * off
        pz = a[2] + (b[2] - a[2]) * off / length
        across = ue_yaw(sx, sy)
        width = s["width"] + (5.0 if s["kind"] in ("avenue", "street") else 2.0)
        folder = f"Gates/{name}"
        layer = layers[f"{name}_Locked"]
        acts = []

        def put(suffix, along, side, z, size, mat, rot=None, mesh=CUBE):
            c = (px + ux * along + sx * side, py + uy * along + sy * side, pz + z)
            acts.append(shape(f"Gate_{name}_{k}_{suffix}", folder, c, size, mat, mesh, rot or (0, 0, across)))

        if gate == "Gate.PortCordon":
            put("Barrier", 0, 0, 0.6, (width, 0.3, 1.0), "Barrier")
            for side in (-1, 1):
                put(f"Car{side}", 4.0, side * (width / 2 - 3.0), 0.75, (1.9, 4.6, 1.5), "Police")
        elif gate == "Gate.Colline":
            put("Arm", 0, 0, 1.0, (width * 0.9, 0.25, 0.25), "Barrier")
            put("Post", 0, -width / 2 + 0.3, 0.6, (0.5, 0.5, 1.2), "Metal")
            put("Booth", 2.0, width / 2 + 2.0, 1.5, (3, 3, 3), "Landmark")
            put("Block", 1.0, 0, 0.4, (width * 0.9, 0.6, 0.8), "Concrete")   # a car can't duck under the arm
        elif gate == "Gate.Ferrale":
            for i in range(6):
                sz = rng.uniform(2.5, 4.5)
                put(f"Rock{i}", rng.uniform(-2, 4), rng.uniform(-width / 2, width / 2), sz * 0.35, (sz, sz * 0.8, sz * 0.7),
                    "Rock", rot=(rng.uniform(-15, 15), rng.uniform(-15, 15), rng.uniform(0, 90)))
        elif gate == "Gate.RailCrossing":
            put("Arm", 0, 0, 1.0, (width * 0.9, 0.25, 0.25), "Barrier")
            put("Wagon", 6.0, 0, 2.2, (max(width + 6.0, 14.0), 3.0, 3.6), "Train")
        for act in acts:
            unreal.OCWorldBuilderLibrary.add_actor_to_data_layer(act, layer)
        count += 1
    log(f"gate barriers: {count}")


def make_layers(world, layout):
    layers = {}
    for gate in layout["gates"]:
        name = gate["tag"].split(".")[1]
        layers[f"{name}_Locked"] = unreal.OCWorldBuilderLibrary.create_runtime_data_layer(
            world, LAYER_PATH, f"DL_Gate_{name}_Locked", True)
        if gate["tag"] in MOVING_BRIDGES:
            layers[f"{name}_Open"] = unreal.OCWorldBuilderLibrary.create_runtime_data_layer(
                world, LAYER_PATH, f"DL_Gate_{name}_Open", False)
    if any(v is None for v in layers.values()):
        raise RuntimeError("could not create the gate data layers")
    log(f"data layers: {len(layers)}")
    return layers


# --------------------------------------------------------------------------------------------
# Volumes

def box_volume(cls, label, folder, center_m, extent_m, yaw=0.0, spatial=True):
    v = L.spawn(cls, cm(*center_m), (0, 0, yaw), label, folder)
    v.get_component_by_class(unreal.BoxComponent).set_box_extent(
        unreal.Vector(extent_m[0] * 100.0, extent_m[1] * 100.0, extent_m[2] * 100.0), False)
    if not spatial:
        v.set_editor_property("is_spatially_loaded", False)
    return v


def build_locked_volumes(layout):
    count = 0
    for dist in layout["districts"]:
        if not dist["unlock"]:
            continue
        x0, y0, x1, y1 = dist["rect"]
        v = box_volume(unreal.OCLockedAreaVolume, f"Locked_{dist['name']}", "Gates/Areas",
                       ((x0 + x1) / 2.0, (y0 + y1) / 2.0, 250.0), ((x1 - x0) / 2.0, (y1 - y0) / 2.0, 400.0), spatial=False)
        v.set_editor_property("area", unreal.OCWorldBuilderLibrary.make_tag(f"Area.{dist['name']}"))
        count += 1
    log(f"locked area volumes: {count}")


def river_half_width(y):  # Tools/make_marevia_layout.py
    if y < 260:
        return 25.0 + (260 - y) * 0.45
    return 25.0 if y < 2100 else 14.0


def build_water_volumes(layout):
    water = layout["water"]
    sea = layout.get("sea_level", 0.0)
    # The sea, the estuary and the Darsena: everything 1.2 m below sea level, over the whole map.
    box_volume(unreal.OCDeepWaterVolume, "Water_Sea", "Water", (1008.0, 1212.0, sea - 76.2), (1700.0, 2100.0, 75.0), spatial=False)
    # The river above sea level: boxes along the bed profile, 1 m below the surface, banks excluded.
    prof = water["river"]["profile_y_x_bed"]
    depth = water["river"]["depth"]
    n = 0
    for (y0, x0, b0), (y1, x1, b1) in zip(prof, prof[1:]):
        top = min(b0, b1) + depth - 1.0
        if top <= sea - 1.2:
            continue  # the sea volume covers it
        bottom = min(b0, b1) - 6.0
        hw = river_half_width((y0 + y1) / 2.0) - 3.0
        seg = math.hypot(x1 - x0, y1 - y0)
        box_volume(unreal.OCDeepWaterVolume, f"Water_River_{n:03d}", "Water/River",
                   ((x0 + x1) / 2.0, (y0 + y1) / 2.0, (top + bottom) / 2.0), (seg / 2.0 + 1.0, hw, (top - bottom) / 2.0),
                   yaw=ue_yaw(x1 - x0, y1 - y0))
        n += 1
    # The reservoir behind the dam.
    rx0, ry0, rx1, ry1 = water["reservoir"]
    level = water["reservoir_level"]
    box_volume(unreal.OCDeepWaterVolume, "Water_Reservoir", "Water", ((rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0, level - 21.2),
               ((rx1 - rx0) / 2.0, (ry1 - ry0) / 2.0, 20.0), spatial=False)
    log(f"deep water volumes: sea, reservoir and {n} river sections")


# --------------------------------------------------------------------------------------------
# Data assets

def build_data(layout, layers):
    roads, _rail, _water = oc_networks.write_network_assets(layout)  # road, rail and water data (INFRA_DESIGN.md)

    tag = unreal.OCWorldBuilderLibrary.make_tag
    areas = []
    for dist in layout["districts"]:
        e = unreal.OCAreaUnlock()
        e.set_editor_property("area", tag(f"Area.{dist['name']}"))
        if dist["unlock"]:
            e.set_editor_property("required_milestone", tag(dist["unlock"]))
            e.set_editor_property("locked_message", unreal.Text(LOCKED_MESSAGES.get(dist["name"], "You can't go there yet.")))
        areas.append(e)
    for gate in layout["gates"]:
        name = gate["tag"].split(".")[1]
        e = unreal.OCAreaUnlock()
        e.set_editor_property("area", tag(gate["tag"]))
        e.set_editor_property("required_milestone", tag(gate["opens_with"]))
        e.set_editor_property("locked_message", unreal.Text(gate["while_locked"].capitalize() + "."))
        e.set_editor_property("layers_while_locked", [layers[f"{name}_Locked"]])
        if f"{name}_Open" in layers:
            e.set_editor_property("layers_when_open", [layers[f"{name}_Open"]])
        areas.append(e)
    unlocks = L.data_asset("DA_AreaUnlocks", "/Game/Core/Progression", unreal.OCAreaUnlockData, TAG)
    unlocks.set_editor_property("areas", areas)
    L.save(unlocks)
    log(f"data: {roads.get_path_name()} ({len(layout['roads']['nodes'])} nodes), {unlocks.get_path_name()} ({len(areas)} areas and gates)")


def build(world, layout):
    layers = make_layers(world, layout)
    build_bridges(layout, layers)
    build_landmarks(layout)
    build_gate_props(layout, layers)
    build_locked_volumes(layout)
    build_water_volumes(layout)
    build_data(layout, layers)
