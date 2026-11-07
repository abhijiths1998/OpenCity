#!/usr/bin/env python3
"""Generate the Marevia world plan: districts, water, landmarks, gates, roads and terrain.

One source of truth for M3 onwards. Unreal builds the landscape, roads, blockout and
gates from these files; traffic (M5), police (M7) and the minimap (M9) read the road graph.

Coordinates: metres, x = east (0..2016), y = north (-300..2724; the coast is near y = 60). The sea is south (y < coast)
and west of the cliffs. Heights are metres above sea level.

Outputs (default SourceArt/World/):
  marevia_layout.json   districts, water, landmarks, gates, road graph
  marevia_height.png    16-bit heightmap for the Unreal landscape (1 px = 1 m)
  marevia_preview.png   top-down picture for checking the layout

Usage: python3 -I Tools/make_marevia_layout.py [out_dir]
Deterministic: the same seed always gives the same city.
"""

import json
import math
import random
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SEED = 1974
W, H = 2016, 3024  # metres; matches the landscape (16 x 24 components of 126 quads at 1 m)
Y_MIN = -300        # the map starts 300 m out to sea, so the Molo Salvi pier is on it
SEA_LEVEL = 0.0
HEIGHT_MIN, HEIGHT_MAX = -100.0, 700.0  # heightmap 0..65535 maps to this range (metres)

# --- Water ------------------------------------------------------------------------

def coast_y(x):
    """South coastline: the beach curves gently, the port side sits a little further out."""
    return 60.0 + 25.0 * math.sin(x / 400.0) + (30.0 if x > 1050 else 0.0)


WEST_CLIFF_X = 40.0  # west edge is cliffs dropping to the sea

# Fiume Ardo centreline from the dam down to the sea: (x, y) points, south to north.
RIVER = [(1000, -40), (990, 150), (1010, 400), (985, 700), (1000, 1000), (1030, 1300),
         (1010, 1600), (1050, 1850), (1110, 2100), (1090, 2300), (1120, 2440)]


def river_half_width(y):
    if y < 260:      # estuary widens toward the sea
        return 25.0 + (260 - y) * 0.45
    return 25.0 if y < 2100 else 14.0  # narrower mountain stream above the hills


def river_x(y):
    pts = RIVER
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if y0 <= y <= y1:
            t = (y - y0) / (y1 - y0)
            return x0 + (x1 - x0) * t
    return pts[0][0] if y < pts[0][1] else pts[-1][0]


DARSENA = (260, 60, 820, 230)       # inner harbour basin (x0, y0, x1, y1), open to the sea at the west
RESERVOIR = (880, 2470, 1380, 2690)  # behind the dam
DAM_Y = 2455


def water_kind(x, y):
    """None for land, else the water body at (x, y)."""
    if y < coast_y(x) or x < WEST_CLIFF_X:
        return "sea"
    d0, d1 = DARSENA[0:2], DARSENA[2:4]
    if d0[0] <= x <= d1[0] and d0[1] <= y <= d1[1]:
        return "darsena"
    if RESERVOIR[0] <= x <= RESERVOIR[2] and RESERVOIR[1] <= y <= RESERVOIR[3]:
        return "reservoir"
    if y < DAM_Y and abs(x - river_x(y)) < river_half_width(y):
        return "river"
    return None


# Molo Salvi: the terminal pier south of the Darsena, reachable only by the swing bridge.
MOLO = (260, -260, 840, 60)
BREAKWATER = [(840, -200), (930, -240)]  # out to the lighthouse


def on_molo(x, y):
    return MOLO[0] <= x <= MOLO[2] and MOLO[1] <= y <= MOLO[3]


def is_land(x, y):
    return on_molo(x, y) or water_kind(x, y) is None


# --- Districts --------------------------------------------------------------------
# Axis-aligned for clarity; roads inside get an organic jitter and rotation.

DISTRICTS = [
    # name, rect (x0, y0, x1, y1), unlock milestone, road style
    ("PortoVecchio", (60, 230, 975, 880), "Story.Mission.Funerale", "grid90"),
    ("MoloSalvi", MOLO, "Story.Act2.Start", "terminal"),
    ("CittaAlta", (60, 880, 975, 1680), "", "oldtown"),
    ("LeFornaci", (1030, 900, 1980, 1680), "", "grid120"),
    ("Lungomare", (1160, 90, 1980, 900), "Story.Mission.LaFornace", "beach"),
    ("BorgoMarino", (1030, 90, 1160, 900), "Story.Mission.LaFornace", "village"),
    ("LeColline", (60, 1680, 1980, 2160), "Story.Mission.StradaCostiera", "hills"),
    ("MonteFerrale", (60, 2160, 1980, 2724), "Story.Mission.LaCava", "mountain"),
]


def district_at(x, y):
    if on_molo(x, y):
        return "MoloSalvi"
    for name, (x0, y0, x1, y1), _, _ in DISTRICTS:
        if name != "MoloSalvi" and x0 <= x <= x1 and y0 <= y <= y1:
            return name
    return None


# --- Terrain ----------------------------------------------------------------------

def value_noise(shape, cell, rng):
    """Smooth value noise in [0, 1]: random lattice every `cell` metres, smoothstep-interpolated."""
    ny, nx = shape
    gy, gx = ny // cell + 3, nx // cell + 3
    lattice = rng.random((gy, gx))
    oy, ox = rng.random() * cell, rng.random() * cell  # random offset per octave
    y = (np.arange(ny) + oy) / cell
    x = (np.arange(nx) + ox) / cell
    y0, x0 = np.floor(y).astype(int), np.floor(x).astype(int)
    fy, fx = y - y0, x - x0
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)  # smoothstep
    a = lattice[y0][:, x0]
    b = lattice[y0][:, x0 + 1]
    c = lattice[y0 + 1][:, x0]
    d = lattice[y0 + 1][:, x0 + 1]
    top = a + (b - a) * fx[None, :]
    bot = c + (d - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]


def fbm(shape, base_cell, octaves, rng, ridged=False):
    """Fractal sum of value noise, normalised to about [-1, 1] (ridged: [0, 1], sharp crests)."""
    total, amp, norm = np.zeros(shape, dtype=np.float32), 1.0, 0.0
    cell = base_cell
    for _ in range(octaves):
        n = value_noise(shape, max(4, int(cell)), rng)
        n = (1.0 - np.abs(2.0 * n - 1.0)) ** 1.3 if ridged else 2.0 * n - 1.0
        total += amp * n
        norm += amp
        amp *= 0.5
        cell /= 2.0
    return total / norm


def make_heightmap(rng):
    """Heights in metres on a 1 m grid, shape (H+1, W+1), row 0 = south edge."""
    ys, xs = np.mgrid[0:H + 1, 0:W + 1].astype(np.float32)
    ys += Y_MIN
    h = np.full_like(xs, 4.0)
    # Gentle rise inland from the coast.
    h += np.clip((ys - 200) / 700.0, 0, 1) * 8.0
    # Città Alta: a rounded hill topped by the old town.
    d = np.hypot((xs - 480) / 420.0, (ys - 1280) / 380.0)
    h += 48.0 * np.clip(1 - d, 0, 1) ** 1.6
    # Le Colline ramp, then Monte Ferrale.
    h += np.clip((ys - 1650) / 450.0, 0, 1) ** 1.3 * 110.0
    h += np.clip((ys - 2150) / 620.0, 0, 1) ** 1.5 * 330.0
    # Natural relief: gentle rolling noise everywhere, ridged mountains in the north.
    gentle = fbm(xs.shape, 400, 5, rng)
    ridges = 0.75 * fbm(xs.shape, 700, 4, rng, ridged=True) + 0.25 * (fbm(xs.shape, 350, 4, rng) * 0.5 + 0.5)
    mountain_mask = np.clip((ys - 2050) / 400.0, 0, 1) ** 1.2
    h += gentle * (2.0 + 10.0 * np.clip((ys - 1500) / 600.0, 0, 1))  # flat-ish city, rolling hills
    h += ridges * 170.0 * mountain_mask
    # Weathering: soften the mountains with a box blur (two passes, 15 m), keep the lowlands sharp.
    soft = h.copy()
    for _ in range(2):
        k = 15
        c = np.cumsum(np.pad(soft, ((0, 0), (k, k)), mode="edge"), axis=1)
        soft = (c[:, 2 * k:] - c[:, :-2 * k]) / (2 * k)
        soft = np.pad(soft, ((0, 0), (0, h.shape[1] - soft.shape[1])), mode="edge")
        c = np.cumsum(np.pad(soft, ((k, k), (0, 0)), mode="edge"), axis=0)
        soft = (c[2 * k:, :] - c[:-2 * k, :]) / (2 * k)
        soft = np.pad(soft, ((0, h.shape[0] - soft.shape[0]), (0, 0)), mode="edge")
    h = h * (1 - mountain_mask) + soft * mountain_mask
    # West cliffs drop to the sea.
    h = np.where(xs < WEST_CLIFF_X + 25, np.minimum(h, (xs - WEST_CLIFF_X) * 1.5), h)
    # Carve water: sea floor, Darsena, river channel, reservoir basin.
    coast = 60.0 + 25.0 * np.sin(xs / 400.0) + np.where(xs > 1050, 30.0, 0.0)
    shore = np.clip((ys - coast) / 40.0, 0, 1)  # beach slope over 40 m
    h = h * shore + (-6.0) * (1 - shore)
    dx0, dy0, dx1, dy1 = DARSENA
    darsena = (xs >= dx0) & (xs <= dx1) & (ys >= dy0) & (ys <= dy1)
    h = np.where(darsena, -6.0, h)
    # River: the bed follows the valley and must (a) always run downhill to the sea and
    # (b) sit below the land beside it. Sample the terrain along the centreline, take
    # bed(y) = min over everything upstream of (terrain - 4 m), clamp at the sea.
    rows = np.arange(H + 1) + Y_MIN
    rx_row = np.interp(rows, [p[1] for p in RIVER], [p[0] for p in RIVER])
    hc = h[np.arange(H + 1), np.clip(np.round(rx_row).astype(int), 0, W)]
    bed_row = np.minimum.accumulate((hc - 4.0)[::-1])[::-1]  # min over upstream (larger y)
    bed_row = np.maximum(bed_row, -3.0)
    bed_row = np.where(rows < coast_y(1000) + 30, -3.0, bed_row)
    rx = rx_row[:, None]
    bed = bed_row[:, None]
    rhw = np.where(ys < 260, 25.0 + (260 - ys) * 0.45, np.where(ys < 2100, 25.0, 14.0))
    dist = np.abs(xs - rx)
    channel = (dist < rhw + 12) & (ys < DAM_Y)
    bank = np.clip((dist - rhw) / 12.0, 0, 1)
    h = np.where(channel, np.minimum(h, bed + bank * (np.maximum(h, bed) - bed)), h)
    # Reservoir: the water level comes from the natural terrain around the basin, so the land
    # contains the lake by itself; only the southern outlet (the river valley) is closed by the
    # dam. No raised rim, so no artificial cliffs.
    dam_row = int(DAM_Y - Y_MIN) - 2
    bed_at_dam = float(bed_row[dam_row])
    rx0, ry0, rx1, ry1 = RESERVOIR
    cx, cy = (rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0
    ax, ay = (rx1 - rx0) / 2.0, (ry1 - ry0) / 2.0
    shore = []
    for deg in range(0, 360, 3):
        if abs(((deg - 270) + 180) % 360 - 180) < 40:   # skip the southern outlet arc
            continue
        bx, by = cx + ax * 1.15 * math.cos(math.radians(deg)), cy + ay * 1.15 * math.sin(math.radians(deg))
        shore.append(float(h[int(round(by - Y_MIN)), int(round(bx))]))
    level = max(min(shore) - 2.0, bed_at_dam + 12.0)    # dam at least 12 m high
    r = np.hypot((xs - cx) / ax, (ys - cy) / ay)          # 1 at the shore
    # Bowl under the water, and valley sides rising ~80 m per oval-radius beyond the shore,
    # so the lake sits in a natural basin instead of a pit with sheer walls.
    basin = level - 0.5 - 12.0 * np.clip(1.0 - r * r, 0, 1)
    sides = level + 80.0 * np.clip(r - 1.0, 0, None)
    h = np.minimum(h, np.where(r < 1.0, basin, sides))
    make_heightmap.dam_height = round(level + 3.0 - bed_at_dam, 1)
    make_heightmap.river_profile = [(float(rows[i]), float(rx_row[i]), round(float(bed_row[i]), 2))
                                    for i in range(0, H + 1, 20) if rows[i] < DAM_Y]
    make_heightmap.reservoir_level = round(level, 2)
    # Molo Salvi: flat concrete pier just above the sea.
    mx0, my0, mx1, my1 = MOLO
    molo = (xs >= mx0) & (xs <= mx1) & (ys >= my0) & (ys <= my1)
    h = np.where(molo, 2.5, h)
    # Breakwater out to the lighthouse: a 14 m wide causeway, plus a pad around the light.
    (bx0, by0), (bx1, by1) = BREAKWATER
    t = np.clip(((xs - bx0) * (bx1 - bx0) + (ys - by0) * (by1 - by0)) / ((bx1 - bx0) ** 2 + (by1 - by0) ** 2), 0, 1)
    dist_bw = np.hypot(xs - (bx0 + t * (bx1 - bx0)), ys - (by0 + t * (by1 - by0)))
    h = np.where((dist_bw < 7.0) & (h < 2.5), 2.5, h)
    h = np.where((np.hypot(xs - bx1, ys - by1) < 12.0) & (h < 2.5), 2.5, h)
    return h


# --- Roads --------------------------------------------------------------------------

class RoadGraph:
    def __init__(self, terrain):
        self.nodes = []      # [x, y, z]
        self.segments = []   # dicts
        self.terrain = terrain

    def height(self, x, y):
        xi = int(min(max(round(x), 0), W))
        yi = int(min(max(round(y - Y_MIN), 0), H))
        return float(self.terrain[yi, xi])

    def node(self, x, y, merge=6.0):
        for i, (nx, ny, _) in enumerate(self.nodes):
            if (nx - x) ** 2 + (ny - y) ** 2 < merge * merge:
                return i
        self.nodes.append([round(x, 1), round(y, 1), round(max(self.height(x, y), 1.0), 2)])
        return len(self.nodes) - 1

    def connect(self, a, b, kind, shape=None, bridge=None, gate=None):
        if a == b or any({s["a"], s["b"]} == {a, b} for s in self.segments):
            return
        lanes, width, speed = ROAD_KINDS[kind]
        self.segments.append({"a": a, "b": b, "kind": kind, "lanes_per_dir": lanes, "width": width,
                              "speed_kmh": speed, "shape": shape or [], "bridge": bridge, "gate": gate})

    def path(self, points, kind, **kw):
        """Chain of nodes through points."""
        ids = [self.node(x, y) for x, y in points]
        for a, b in zip(ids, ids[1:]):
            self.connect(a, b, kind, **kw)
        return ids


ROAD_KINDS = {  # lanes per direction, width m, speed km/h
    "avenue": (2, 14.0, 60),
    "street": (1, 9.0, 50),
    "alley": (1, 5.5, 25),
    "coast": (1, 8.0, 70),
    "pass": (1, 7.0, 50),
    "pier": (1, 10.0, 30),
}


def segment_on_land(x0, y0, x1, y1, step=4.0):
    n = max(2, int(math.hypot(x1 - x0, y1 - y0) / step))
    return all(is_land(x0 + (x1 - x0) * t / n, y0 + (y1 - y0) * t / n) for t in range(n + 1))


def grid(g, rng, rect, block, angle_deg, jitter, kind, district, keep=1.0):
    """Jittered, rotated street grid clipped to land inside rect."""
    x0, y0, x1, y1 = rect
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    ang = math.radians(angle_deg)
    span = int(max(x1 - x0, y1 - y0) / block) + 2
    pts = {}
    for i in range(-span, span + 1):
        for j in range(-span, span + 1):
            u, v = i * block + rng.uniform(-jitter, jitter), j * block + rng.uniform(-jitter, jitter)
            x = cx + u * math.cos(ang) - v * math.sin(ang)
            y = cy + u * math.sin(ang) + v * math.cos(ang)
            if x0 + 8 <= x <= x1 - 8 and y0 + 8 <= y <= y1 - 8 and is_land(x, y) and district_at(x, y) == district:
                pts[(i, j)] = (x, y)
    for (i, j), (x, y) in pts.items():
        for di, dj in ((1, 0), (0, 1)):
            q = pts.get((i + di, j + dj))
            if q and rng.random() < keep and segment_on_land(x, y, *q):
                g.connect(g.node(x, y), g.node(*q), kind)
    return pts


def build_roads(terrain, rng):
    g = RoadGraph(terrain)
    rx = river_x
    # District grids.
    grid(g, rng, (60, 240, 975, 880), 95, 0, 8, "street", "PortoVecchio")
    grid(g, rng, (60, 880, 975, 1680), 62, 14, 10, "alley", "CittaAlta", keep=0.85)
    grid(g, rng, (1030, 900, 1980, 1680), 125, 0, 6, "street", "LeFornaci")
    grid(g, rng, (1160, 160, 1980, 900), 105, -6, 6, "street", "Lungomare")
    grid(g, rng, (1030, 160, 1160, 900), 60, 20, 8, "alley", "BorgoMarino", keep=0.8)
    grid(g, rng, (300, -230, 820, 40), 110, 0, 0, "pier", "MoloSalvi")

    # Arterials.
    g.path([(140, 250), (140, 560), (150, 880), (180, 1200), (220, 1520)], "avenue")   # west avenue
    g.path([(950, 260), (930, 560), (935, 880)], "avenue")                              # port riverside
    g.path([(1200, 170), (1500, 175), (1800, 185), (1960, 200)], "avenue")              # Lungomare promenade
    g.path([(1080, 960), (1300, 950), (1600, 945), (1950, 950)], "avenue")             # east avenue
    g.path([(1100, 300), (1095, 600), (1090, 900), (1085, 1250), (1080, 1600)], "avenue")  # east riverside

    # Bridges over the Fiume Ardo (named, with gate tags where they are gates).
    def bridge(name, y, kind, gate=None):
        w = rx(y)
        hw = river_half_width(y)
        a, b = g.node(w - hw - 30, y), g.node(w + hw + 30, y)
        g.connect(a, b, kind, bridge=name, gate=gate)

    bridge("PonteSanRocco", 1250, "avenue")
    bridge("PonteMobile", 350, "avenue", gate="Gate.PonteMobile")
    bridge("Passerella", 800, "alley")                      # footbridge (narrow, cars allowed for now)
    bridge("PonteDelleColline", 1900, "street", gate="Gate.Colline")

    # Swing bridge to Molo Salvi across the Darsena.
    g.path([(560, 260), (560, 30)], "pier", bridge="PonteGirevole", gate="Gate.Molo")

    # Coast road: up the west cliffs from the port to the hills, with curves.
    coast_pts = [(90, 300), (70, 700), (75, 1100), (95, 1450), (160, 1700), (300, 1850),
                 (420, 1960), (600, 2030), (850, 2000), (1000, 1930)]
    g.path(coast_pts, "coast", gate=None)
    # Hills roads and gates (checkpoints where roads leave the open districts).
    g.path([(520, 1660), (540, 1760), (480, 1900), (450, 1960)], "street", gate="Gate.Colline")  # to Villa Salvi
    g.path([(1400, 1660), (1380, 1800), (1300, 1900), (1180, 1905)], "street", gate="Gate.Colline")
    # Monte Ferrale pass road: hairpins up to the dam, spur to the quarry and the hermitage.
    pass_pts = [(1300, 1905), (1420, 2050), (1300, 2150), (1450, 2250), (1300, 2350), (1200, 2440),
                (1020, 2455), (860, 2460)]  # across the top of the dam at y ~ 2455
    g.path(pass_pts, "pass", gate="Gate.Ferrale")
    g.path([(1450, 2250), (1600, 2320), (1700, 2380)], "pass")        # quarry spur
    g.path([(860, 2460), (700, 2520), (560, 2600), (500, 2640)], "pass")  # hermitage
    g.path([(1700, 2380), (1800, 2560), (1820, 2680)], "pass")        # radio mast

    # Stitch: join every road end that is still loose (bridge ends, arterial ends) and the
    # edge nodes of each district to the nearest node of a neighbouring district or grid.
    def degree():
        deg = [0] * len(g.nodes)
        for s in g.segments:
            deg[s["a"]] += 1
            deg[s["b"]] += 1
        return deg

    def nearest(i, max_d, other_district=False, exclude=()):
        x, y = g.nodes[i][0], g.nodes[i][1]
        di = district_at(x, y)
        best, bd = None, max_d * max_d
        for j, (nx, ny, _) in enumerate(g.nodes):
            if j == i or j in exclude:
                continue
            if other_district and district_at(nx, ny) == di:
                continue
            dd = (nx - x) ** 2 + (ny - y) ** 2
            if dd < bd and segment_on_land(x, y, nx, ny):
                best, bd = j, dd
        return best

    deg = degree()
    for i in range(len(g.nodes)):
        if deg[i] <= 1:  # loose end
            linked = {s["b"] if s["a"] == i else s["a"] for s in g.segments if i in (s["a"], s["b"])}
            j = nearest(i, 120.0, exclude=linked)
            if j is not None:
                g.connect(i, j, "street")
    # Border links between neighbouring districts, about every 150 m.
    for i, (x, y, _) in enumerate(list(g.nodes)):
        j = nearest(i, 75.0, other_district=True)
        if j is not None and rng.random() < 0.6:
            g.connect(i, j, "street")

    # Gates: every road that crosses between districts is closed by the gate of whichever side
    # unlocks later in the story. Explicit tags (bridges) are kept. This catches every
    # crossing, including ones nobody planned (the coast road leaked into the hills before).
    UNLOCK_RANK = {"CittaAlta": 0, "LeFornaci": 0, "PortoVecchio": 1, "LeColline": 2,
                   "MonteFerrale": 3, "Lungomare": 4, "BorgoMarino": 4, "MoloSalvi": 5}
    DISTRICT_GATE = {"PortoVecchio": "Gate.PortCordon", "LeColline": "Gate.Colline",
                     "MonteFerrale": "Gate.Ferrale", "Lungomare": "Gate.RailCrossing",
                     "BorgoMarino": "Gate.RailCrossing", "MoloSalvi": "Gate.Molo"}
    for s in g.segments:
        if s["gate"] is not None:
            continue
        a, b = g.nodes[s["a"]], g.nodes[s["b"]]
        da, db = district_at(a[0], a[1]), district_at(b[0], b[1])
        if da == db or da is None or db is None:
            continue
        later = da if UNLOCK_RANK[da] >= UNLOCK_RANK[db] else db
        if UNLOCK_RANK[later] > 0:
            s["gate"] = DISTRICT_GATE[later]
    return g


# --- Landmarks and gates ---------------------------------------------------------------

LANDMARKS = [  # name, district, (x, y), footprint (w, d) m, height m, kind
    ("LaCala", "PortoVecchio", (700, 275), (24, 16), 7, "ruin"),
    ("FishMarket", "PortoVecchio", (420, 330), (70, 35), 9, "hall"),
    ("FerryPier", "PortoVecchio", (880, 240), (20, 80), 2, "pier"),
    ("SalviTerminal", "MoloSalvi", (520, -120), (60, 40), 55, "tower"),
    ("Faro", "MoloSalvi", (930, -240), (8, 8), 28, "lighthouse"),
    ("SantaLucia", "CittaAlta", (520, 1320), (30, 55), 30, "church"),
    ("CastrovanniTownhouse", "CittaAlta", (380, 1180), (22, 18), 14, "house"),
    ("PoliceHQ", "CittaAlta", (760, 1000), (45, 35), 18, "civic"),
    ("OspedaleSanMartino", "CittaAlta", (250, 1500), (60, 40), 22, "civic"),
    ("BeppesGarage", "LeFornaci", (1450, 1150), (50, 40), 8, "yard"),
    ("AbandonedKiln", "LeFornaci", (1780, 1500), (60, 45), 40, "kiln"),
    ("StazioneCentrale", "LeFornaci", (1500, 980), (120, 30), 14, "station"),
    ("ClubMedusa", "Lungomare", (1450, 250), (40, 30), 10, "club"),
    ("HotelRiviera", "Lungomare", (1750, 300), (40, 25), 38, "hotel"),
    ("VillaSalvi", "LeColline", (450, 1960), (40, 30), 12, "villa"),
    ("Lookout", "LeColline", (250, 2030), (15, 15), 3, "lookout"),
    ("LaCava", "MonteFerrale", (1700, 2380), (120, 80), 15, "quarry"),
    ("DigaDelFerrale", "MonteFerrale", (1130, DAM_Y), (500, 15), 40, "dam"),
    ("EremoDelleAquile", "MonteFerrale", (500, 2640), (25, 20), 12, "hermitage"),
    ("RadioMast", "MonteFerrale", (1820, 2680), (8, 8), 60, "mast"),
]

GATES = [  # tag, opens with milestone, what stands there while locked
    ("Gate.PortCordon", "Story.Mission.Funerale", "police cordon at the fire scene"),
    ("Gate.Colline", "Story.Mission.StradaCostiera", "private-road checkpoint"),
    ("Gate.Ferrale", "Story.Mission.LaCava", "rockfall barrier on the pass road"),
    ("Gate.PonteMobile", "Story.Mission.LaFornace", "lifting bridge kept raised"),
    ("Gate.RailCrossing", "Story.Mission.LaFornace", "level crossing closed, freight train parked"),
    ("Gate.Molo", "Story.Act2.Start", "swing bridge turned open"),
]


# --- Preview ------------------------------------------------------------------------------

DISTRICT_COLORS = {
    "PortoVecchio": (205, 170, 120), "MoloSalvi": (160, 160, 170), "CittaAlta": (220, 190, 150),
    "LeFornaci": (170, 120, 95), "Lungomare": (235, 215, 160), "BorgoMarino": (210, 200, 175),
    "LeColline": (150, 175, 110), "MonteFerrale": (125, 135, 120),
}


def render_preview(path, terrain, g):
    scale = 0.5  # px per metre
    pw, ph = int(W * scale), int(H * scale)
    t = terrain[::2, ::2][: ph, : pw]
    shade = np.clip((t - t.min()) / max(1e-6, (t.max() - t.min())), 0, 1)
    img = np.zeros((t.shape[0], t.shape[1], 3), dtype=np.uint8)
    for py in range(t.shape[0]):
        for px in range(t.shape[1]):
            x, y = px / scale, py / scale + Y_MIN
            wk = water_kind(x, y) if not on_molo(x, y) else None
            if wk:
                img[py, px] = (60, 110, 170) if wk != "river" else (70, 130, 185)
                continue
            base = DISTRICT_COLORS.get(district_at(x, y), (180, 180, 180))
            k = 0.65 + 0.35 * shade[py, px]
            img[py, px] = tuple(int(c * k) for c in base)
    im = Image.fromarray(img[::-1])  # north up
    d = ImageDraw.Draw(im)
    font = ImageFont.load_default()

    def to_px(x, y):
        return x * scale, (Y_MIN + H - y) * scale

    gate_color = (220, 30, 30)
    for s in g.segments:
        a, b = g.nodes[s["a"]], g.nodes[s["b"]]
        col = (40, 40, 40)
        if s["bridge"]:
            col = (250, 250, 250)
        if s["gate"]:
            col = gate_color
        d.line([to_px(a[0], a[1]), to_px(b[0], b[1])], fill=col, width=max(1, int(s["width"] * scale * 0.6)))
    for name, _, (x, y), (fw, fd), _, _ in LANDMARKS:
        px, py = to_px(x, y)
        d.rectangle([px - fw * scale / 2, py - fd * scale / 2, px + fw * scale / 2, py + fd * scale / 2], outline=(0, 0, 0), fill=(255, 230, 0))
        d.text((px + 6, py - 6), name, fill=(0, 0, 0), font=font)
    for name, rect, _, _ in DISTRICTS:
        x0, y0, x1, y1 = rect
        px, py = to_px((x0 + x1) / 2, y1 - 25)
        d.text((px - 30, py), name.upper(), fill=(255, 255, 255), font=font)
    d.text((8, 8), "MAREVIA layout  N up  0.5 px/m   red = gated road   white = bridge   yellow = landmark", fill=(255, 255, 255), font=font)
    im.save(path)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "SourceArt/World")
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(SEED)
    terrain = make_heightmap(np.random.default_rng(SEED))
    g = build_roads(terrain, rng)

    # Connectivity: report pieces of the road network that can't be reached from the start.
    adj = {i: set() for i in range(len(g.nodes))}
    for s in g.segments:
        adj[s["a"]].add(s["b"])
        adj[s["b"]].add(s["a"])
    start = min(range(len(g.nodes)), key=lambda i: (g.nodes[i][0] - 520) ** 2 + (g.nodes[i][1] - 1290) ** 2)
    seen, stack = {start}, [start]
    while stack:
        for n in adj[stack.pop()]:
            if n not in seen:
                seen.add(n)
                stack.append(n)
    unreachable = [i for i in adj if i not in seen and adj[i]]

    def ground(x, y):
        xi = int(min(max(round(x), 0), W))
        yi = int(min(max(round(y - Y_MIN), 0), H))
        return round(float(terrain[yi, xi]), 2)

    layout = {
        "version": 1,
        "seed": SEED,
        "units": "metres; x east, y north; heights above sea level",
        "size": [W, H],
        "origin": [0, Y_MIN],
        "height_range": [HEIGHT_MIN, HEIGHT_MAX],
        "sea_level": SEA_LEVEL,
        "districts": [{"name": n, "rect": r, "unlock": u, "style": st} for n, r, u, st in DISTRICTS],
        "water": {
            "river": {"name": "FiumeArdo", "centerline": RIVER, "half_width_lowland": 25.0, "dam_y": DAM_Y,
                      "depth": 2.5, "profile_y_x_bed": make_heightmap.river_profile},
            "reservoir_level": make_heightmap.reservoir_level,
            "dam_height": make_heightmap.dam_height,
            "darsena": DARSENA, "reservoir": RESERVOIR, "molo": MOLO, "breakwater": BREAKWATER,
        },
        "landmarks": [{"name": n, "district": dst, "pos": p, "footprint": f, "height": h, "kind": k,
                       "ground_z": ground(p[0], p[1])} for n, dst, p, f, h, k in LANDMARKS],
        "gates": [{"tag": t, "opens_with": m, "while_locked": w} for t, m, w in GATES],
        "roads": {"kinds": ROAD_KINDS, "nodes": g.nodes, "segments": g.segments},
        "start": {"pos": [520, 1290], "district": "CittaAlta", "ground_z": ground(520, 1290)},
    }
    (out / "marevia_layout.json").write_text(json.dumps(layout, indent=1))

    hm = np.clip((terrain - HEIGHT_MIN) / (HEIGHT_MAX - HEIGHT_MIN) * 65535.0, 0, 65535).astype(np.uint16)
    Image.fromarray(hm[::-1]).save(out / "marevia_height.png")
    render_preview(out / "marevia_preview.png", terrain, g)

    kinds = {}
    for s in g.segments:
        kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
    gated = sum(1 for s in g.segments if s["gate"])
    print(f"nodes {len(g.nodes)}, segments {len(g.segments)} {kinds}, gated {gated}, "
          f"bridges {sum(1 for s in g.segments if s['bridge'])}, unreachable nodes {len(unreachable)}")
    print(f"terrain {terrain.min():.1f}..{terrain.max():.1f} m; heightmap {HEIGHT_MIN:.0f} m = 0, {HEIGHT_MAX:.0f} m = 65535")


if __name__ == "__main__":
    main()
