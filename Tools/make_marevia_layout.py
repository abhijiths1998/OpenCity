#!/usr/bin/env python3
"""Generate the Marevia world plan: districts, water, landmarks, gates, roads and terrain.

One source of truth for M3 onwards. Unreal builds the landscape, roads, blockout and
gates from these files; traffic (M5), police (M7) and the minimap (M9) read the road graph.

Coordinates: metres, x = east (0..3024), y = north (-300..2724; the coast is near y = 60). The sea is south (y < coast)
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
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SEED = 1974
W, H = 3024, 3024  # metres; matches the landscape (24 x 24 components of 126 quads at 1 m)
Y_MIN = -300        # the map starts 300 m out to sea, so the Molo Salvi pier is on it
SEA_LEVEL = 0.0
# The plan was first laid out for a 2016 m wide map. Every east-west plan coordinate below is written in
# those original units and widened by X_SCALE through X()/P()/R(); only x is stretched, so block sizes,
# road widths, river width and bridge lengths (all metres) stay as they were and the extra width
# becomes more city blocks.
X_SCALE = 1.5


def X(x):
    return x * X_SCALE


def P(pts):
    """Widen a list of (x, y) plan points."""
    return [(X(x), y) for x, y in pts]


def R(rect):
    """Widen a rect (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = rect
    return (X(x0), y0, X(x1), y1)
HEIGHT_MIN, HEIGHT_MAX = -100.0, 700.0  # heightmap 0..65535 maps to this range (metres)

# --- Water ------------------------------------------------------------------------

def coast_y(x):
    """South coastline: the beach curves gently, the port side sits a little further out."""
    return 60.0 + 25.0 * math.sin(x / X(400.0)) + (30.0 if x > X(1050) else 0.0)


WEST_CLIFF_X = X(40.0)  # west edge is cliffs dropping to the sea

# Fiume Ardo centreline from the dam down to the sea: (x, y) points, south to north.
RIVER = P([(1000, -40), (990, 150), (1010, 400), (985, 700), (1000, 1000), (1030, 1300),
           (1010, 1600), (1050, 1850), (1110, 2100), (1090, 2300), (1120, 2440)])


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


DARSENA = R((260, 60, 820, 230))      # inner harbour basin (x0, y0, x1, y1), open to the sea at the west
RESERVOIR = R((880, 2470, 1380, 2690)) # behind the dam
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
MOLO = R((260, -260, 840, 60))
BREAKWATER = P([(840, -200), (930, -240)])  # out to the lighthouse


def on_molo(x, y):
    return MOLO[0] <= x <= MOLO[2] and MOLO[1] <= y <= MOLO[3]


def is_land(x, y):
    return on_molo(x, y) or water_kind(x, y) is None


# --- Districts --------------------------------------------------------------------
# Axis-aligned for clarity; roads inside get an organic jitter and rotation.

DISTRICTS = [
    # name, rect (x0, y0, x1, y1), unlock milestone, road style
    ("PortoVecchio", R((60, 230, 975, 880)), "Story.Mission.Funerale", "grid90"),
    ("MoloSalvi", MOLO, "Story.Act2.Start", "terminal"),
    ("CittaAlta", R((60, 880, 975, 1680)), "", "oldtown"),
    ("LeFornaci", R((1030, 900, 1980, 1680)), "", "grid120"),
    ("Lungomare", R((1160, 90, 1980, 900)), "Story.Mission.LaFornace", "beach"),
    ("BorgoMarino", R((1030, 90, 1160, 900)), "Story.Mission.LaFornace", "village"),
    ("LeColline", R((60, 1680, 1980, 2160)), "Story.Mission.StradaCostiera", "hills"),
    ("MonteFerrale", R((60, 2160, 1980, 2724)), "Story.Mission.LaCava", "mountain"),
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
    d = np.hypot((xs - X(480)) / X(420.0), (ys - 1280) / 380.0)
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
    # West cliffs drop to the sea: a 56-degree cap rising from the shore everywhere (clamping only
    # the first 25 m left a sheer wall hundreds of metres high where the mountains reach the coast).
    h = np.minimum(h, (xs - WEST_CLIFF_X) * 1.5)
    # Carve water: sea floor, Darsena, river channel, reservoir basin.
    coast = 60.0 + 25.0 * np.sin(xs / X(400.0)) + np.where(xs > X(1050), 30.0, 0.0)
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

    def path(self, points, kind, merge=6.0, **kw):
        """Chain of nodes through points (points within `merge` m of an existing node reuse it)."""
        ids = [self.node(x, y, merge) for x, y in points]
        for a, b in zip(ids, ids[1:]):
            self.connect(a, b, kind, **kw)
        return ids


# --- Mountain roads: routed over the terrain ----------------------------------------------------

ROUTE_CELL = 6.0          # m: routing grid
ROUTE_GRADE_OK = 0.07     # grades up to this cost nothing extra
ROUTE_GRADE_MAX = 0.18    # never steeper (the old straight legs needed cuts tens of metres deep)
ROUTE_EDGE = 45.0         # m: routed roads keep this far from the map edge (a hairpin loop needs ~30 m beyond its apex)
ROUTE_STEP = 25.0         # m between nodes of a routed road: the corridor grading between nodes is
                          # linear, so short steps keep the road on the land instead of cutting it


HAIRPIN_R = 13.0  # m: hairpin loop radius, 1 m over the 12 m game minimum so node sampling stays above it (INFRA_DESIGN.md)


def bulb_hairpins(pts, r=HAIRPIN_R, avoid=None):
    """Hairpins tighter than r become "light bulbs": the road swings out, loops round a circle of
    radius r beyond the apex and swings back, joined by reverse curves of the same radius (the
    router packs switchback legs ~12 m apart, a 5 m turn no car makes). Returns the new dense
    path and the loops as lists of points (for an even grade round each one)."""
    def heading(a, b):
        return math.atan2(b[1] - a[1], b[0] - a[0])

    def wrap(t):
        return (t + math.pi) % (2 * math.pi) - math.pi

    acc = [0.0]
    for a, b in zip(pts, pts[1:]):
        acc.append(acc[-1] + math.dist(a, b))
    def at(sv):  # index of the point nearest arc length sv
        return min(range(len(pts)), key=lambda k: abs(acc[k] - sv))

    out, bulbs, i = [], [], 0
    while i < len(pts) - 2:
        # Total signed turn over the next 50 m, up to where the road turns back the other way (a
        # Z of two opposite hairpins 23 m apart summed to nothing and kept both 5 m bends).
        j, turn = i, 0.0
        while j < len(pts) - 2 and acc[j + 1] - acc[i] < 50.0:
            d = wrap(heading(pts[j + 1], pts[j + 2]) - heading(pts[j], pts[j + 1]))
            if abs(turn) > math.radians(30) and d * turn < 0:
                break
            turn += d
            j += 1
        if abs(turn) < math.radians(120):  # a 140-degree bend at 6 m radius is no better than a hairpin
            out.append(pts[i])
            i += 1
            continue
        # Apex: the sharpest point in the window. The router's legs converge in a V, so the loop
        # starts where they are at least 9 m apart (graph nodes merge within 6 m): back along the
        # in-leg to that point, and the nearest point of the out-leg opposite it.
        apex = max(range(i + 1, j + 1), key=lambda k: abs(wrap(heading(pts[k], pts[k + 1]) - heading(pts[k - 1], pts[k]))))
        out_leg = [m for m in range(apex, len(pts)) if acc[m] - acc[apex] < 90.0]
        k_in = None
        for k in range(apex - 1, 0, -1):
            if acc[apex] - acc[k] > 90.0:
                break
            if min(math.dist(pts[k], pts[m]) for m in out_leg) >= 9.0:
                k_in = k
                break
        if k_in is None:
            out.append(pts[i])
            i += 1
            continue
        k_out = min(out_leg, key=lambda m: math.dist(pts[m], pts[k_in]))
        d_in = heading(pts[k_in - 1], pts[k_in + 1])
        d_out = heading(pts[k_out - 1], pts[min(k_out + 1, len(pts) - 1)])
        u = (math.cos(d_in) - math.cos(d_out), math.sin(d_in) - math.sin(d_out))
        un = math.hypot(*u)
        if un < 1e-6 or k_in <= i:
            out.append(pts[i])
            i += 1
            continue
        u = (u[0] / un, u[1] / un)
        w = (pts[k_out][0] - pts[k_in][0], pts[k_out][1] - pts[k_in][1])
        along = w[0] * u[0] + w[1] * u[1]
        vv = (w[0] - along * u[0], w[1] - along * u[1])
        sep = math.hypot(*vv)
        if sep >= 2 * r or sep < 1.0:
            out.append(pts[i])
            i += 1
            continue
        v = (vv[0] / sep, vv[1] / sep)
        o = pts[k_in]

        def world(x, y):
            return (o[0] + x * u[0] + y * v[0], o[1] + x * u[1] + y * v[1])
        x1 = 0.0
        xc, yc = x1 + math.sqrt((2 * r) ** 2 - (sep / 2.0 + r) ** 2), sep / 2.0
        alpha = math.atan2(xc - x1, sep / 2.0 + r)
        loop = []
        step = 7.0 / r  # ~7 m between loop nodes (graph nodes merge within 6 m)
        n = max(1, int(alpha / step))
        for q in range(1, n + 1):  # entry curve, turning away from the other leg
            ph = alpha * q / n
            loop.append(world(x1 + r * math.sin(ph), -r + r * math.cos(ph)))
        t1 = (x1 + r * math.sin(alpha), -r + r * math.cos(alpha))
        t2 = (x1 + r * math.sin(alpha), sep + r - r * math.cos(alpha))
        b1 = math.atan2(t1[1] - yc, t1[0] - xc)
        b2 = math.atan2(t2[1] - yc, t2[0] - xc)
        while b2 <= b1:
            b2 += 2 * math.pi
        n = max(2, int((b2 - b1) / step))
        for q in range(1, n):  # round the loop (counter to the entry curve)
            b = b1 + (b2 - b1) * q / n
            loop.append(world(xc + r * math.cos(b), yc + r * math.sin(b)))
        n = max(1, int(alpha / step))
        for q in range(n, 0, -1):  # exit curve back onto the other leg (pts[k_out] closes it)
            ph = alpha * q / n
            loop.append(world(x1 + r * math.sin(ph), sep + r - r * math.cos(ph)))
        # A loop must stay on the map and on dry land; otherwise the hairpin keeps its shape (and the
        # validator reports it).
        # Nor may it touch the road elsewhere (a stacked leg 30 m+ along the road from its ends):
        # graph nodes there would merge into an accidental junction.
        far = [pts[m] for m in range(len(pts)) if acc[m] < acc[k_in] - 30.0 or acc[m] > acc[k_out] + 30.0]
        if any(not (15.0 < x < W - 15.0 and Y_MIN + 15.0 < y < Y_MIN + H - 15.0) or (avoid is not None and avoid(x, y))
               or any(math.dist((x, y), f) < 14.0 for f in far[::2])
               for x, y in loop):
            out.append(pts[i])
            i += 1
            continue
        out.extend(pts[i:k_in + 1])
        out.extend(loop)
        bulbs.append([pts[k_in]] + loop + [pts[k_out]])
        i = k_out
    out.extend(pts[i:])
    return out, bulbs


KEEP_OFF_COST = 150.0  # m of road a routed road would rather build than cross one 6 m cell of another road's corridor


def road_mask(g, ends, margin=12.0, free=40.0):
    """`keep_off(x, y)` for route_road: true within `margin` m of any road already in the graph,
    except within `free` m of the route's ends, where it joins the network. Cells in the mask cost
    KEEP_OFF_COST each, so a routed road crosses another road only where the detour would be
    longer than a few hundred metres (split_crossings then makes the crossing a junction), and it
    never runs beside one for long (the quarry spur and the radio-mast road ran across each other
    at grade, and the mast road's exit stub passed 5 m below the spur)."""
    nx, ny = int(W / ROUTE_CELL) + 2, int(H / ROUTE_CELL) + 2
    mask = np.zeros((ny, nx), dtype=bool)
    r = int(math.ceil(margin / ROUTE_CELL))
    offs = [(dx, dy) for dx in range(-r, r + 1) for dy in range(-r, r + 1) if math.hypot(dx, dy) * ROUTE_CELL <= margin]
    for s in g.segments:
        a, b = g.nodes[s["a"]], g.nodes[s["b"]]
        n = max(1, int(math.dist(a[:2], b[:2]) / (ROUTE_CELL / 2)))
        for k in range(n + 1):
            t = k / n
            cx = int(round((a[0] + (b[0] - a[0]) * t) / ROUTE_CELL))
            cy = int(round((a[1] + (b[1] - a[1]) * t - Y_MIN) / ROUTE_CELL))
            for dx, dy in offs:
                x, y = cx + dx, cy + dy
                if 0 <= x < nx and 0 <= y < ny:
                    mask[y, x] = True

    def keep_off(x, y):
        if any(math.dist((x, y), e) < free for e in ends):
            return False
        return bool(mask[int(round((y - Y_MIN) / ROUTE_CELL)), int(round(x / ROUTE_CELL))])
    return keep_off


SWITCHBACK_MIN = 12.0  # m between the legs of a switchback where it turns (a hairpin loop needs 9 m after smoothing)
SWITCHBACK_COST = 300.0  # m-equivalent for doubling back closer than SWITCHBACK_MIN
BRANCH_MIN_DEG = 30.0  # a branch leaves its parent road at least this far off its line (50 is too steep to start on the quarry hillside)


def route_road(terrain, start, goal, avoid=None, turn_cost=40.0, loop_avoid=None, keep_off=None, start_dir=None,
               strict=True, fallback=True):
    """Terrain-following road from start to goal (layout metres): A* on a ROUTE_CELL grid over the
    smoothed land, 16 move directions, cost = length x (1 + grade penalty) + a turn penalty, never
    steeper than ROUTE_GRADE_MAX. Switchbacks appear by themselves where the slope demands them.
    `avoid(x, y)` marks cells the road may not use (water); `keep_off(x, y)` (road_mask) cells
    beside roads already built, which cost KEEP_OFF_COST each. Returns a smoothed polyline [(x, y)]."""
    import heapq
    soft = terrain.copy()
    for _ in range(2):  # 10 m smoothing: route on the land's shape, not its noise
        k = 5
        c = np.cumsum(np.pad(soft, ((0, 0), (k, k)), mode="edge"), axis=1)
        soft = ((c[:, 2 * k:] - c[:, :-2 * k]) / (2 * k))[:, :terrain.shape[1]]
        soft = np.pad(soft, ((0, 0), (0, terrain.shape[1] - soft.shape[1])), mode="edge")
        c = np.cumsum(np.pad(soft, ((k, k), (0, 0)), mode="edge"), axis=0)
        soft = ((c[2 * k:, :] - c[:-2 * k, :]) / (2 * k))[:terrain.shape[0], :]
        soft = np.pad(soft, ((0, terrain.shape[0] - soft.shape[0]), (0, 0)), mode="edge")
    nx, ny = int(W / ROUTE_CELL), int(H / ROUTE_CELL)

    def hgt(i, j):
        return float(soft[min(int(j * ROUTE_CELL), H), min(int(i * ROUTE_CELL), W)])

    def cell(p):
        return (int(round(p[0] / ROUTE_CELL)), int(round((p[1] - Y_MIN) / ROUTE_CELL)))

    # 48 directions (moves up to 4 cells): on 35-degree mountainside the road has to run within
    # ~10 degrees of the contour, which 16 directions (22.5 degrees apart) often can't.
    moves = [(dx, dy) for dx in range(-4, 5) for dy in range(-4, 5)
             if (dx, dy) != (0, 0) and math.gcd(abs(dx), abs(dy)) == 1]
    s0, s1 = cell(start), cell(goal)
    pad = int(600 / ROUTE_CELL)  # search a box around the two ends (speed)
    bx0, bx1 = max(1, min(s0[0], s1[0]) - pad), min(nx - 1, max(s0[0], s1[0]) + pad)
    by0, by1 = max(1, min(s0[1], s1[1]) - pad), min(ny - 1, max(s0[1], s1[1]) + pad)
    best = {s0: 0.0}
    came = {s0: None}
    todo = [(0.0, s0)]
    while todo:
        f, c = heapq.heappop(todo)
        if c == s1:
            break
        g0 = best[c]
        par = came[c]
        h0 = hgt(*c)
        for dx, dy in moves:
            n = (c[0] + dx, c[1] + dy)
            if not (bx0 <= n[0] <= bx1 and by0 <= n[1] <= by1):
                continue
            x, y = n[0] * ROUTE_CELL, n[1] * ROUTE_CELL + Y_MIN
            if (n != s1 and not (ROUTE_EDGE <= x <= W - ROUTE_EDGE and Y_MIN + ROUTE_EDGE <= y <= Y_MIN + H - ROUTE_EDGE)
                    and math.dist((x, y), goal) > 60.0):  # the radio mast stands 44 m from the edge
                continue
            if avoid is not None and avoid(x, y) and n != s1:
                continue
            off_road = keep_off is not None and n != s1 and keep_off(x, y)
            length = math.hypot(dx, dy) * ROUTE_CELL
            grade = abs(hgt(*n) - h0) / length
            if grade > ROUTE_GRADE_MAX:
                continue
            cost = length * (1.0 + 60.0 * max(0.0, grade - ROUTE_GRADE_OK)) + (KEEP_OFF_COST if off_road else 0.0)
            if par is None and start_dir is not None and strict:
                # A branch leaves its road square-ish: at 17 degrees the quarry spur ran beside the
                # pass for 40 m and the pass parapet stood across it.
                if abs(dx * start_dir[0] + dy * start_dir[1]) / math.hypot(dx, dy) > math.cos(math.radians(BRANCH_MIN_DEG)):
                    continue
            if par is not None:
                ax, ay = c[0] - par[0], c[1] - par[1]
                cosang = (ax * dx + ay * dy) / (math.hypot(ax, ay) * math.hypot(dx, dy))
                cost += turn_cost * (1.0 - cosang)
                if strict:
                    # No switchback onto a leg closer than SWITCHBACK_MIN: the radio-mast road doubled
                    # back 5 m from itself (one cell over), no loop fitted and its parapets stood on
                    # the other leg. Checked against the last move and the one before it.
                    back = [(par, c)]
                    if came.get(par) is not None:
                        back.append((came[par], par))
                    for p0, p1 in back:
                        bx, by = p1[0] - p0[0], p1[1] - p0[1]
                        bl = math.hypot(bx, by)
                        if (bx * dx + by * dy) / (bl * math.hypot(dx, dy)) < -0.85:  # heading back (> 148 degrees)
                            off = abs(bx * (n[1] - p0[1]) - by * (n[0] - p0[0])) / bl * ROUTE_CELL
                            if off < SWITCHBACK_MIN:
                                break
                    else:
                        off = None
                    if off is not None and off < SWITCHBACK_MIN:
                        cost += SWITCHBACK_COST  # a cost, not a ban: the search keeps one parent per cell
            g1 = g0 + cost
            if g1 < best.get(n, 1e18):
                best[n] = g1
                came[n] = c
                heapq.heappush(todo, (g1 + math.hypot(n[0] - s1[0], n[1] - s1[1]) * ROUTE_CELL, n))
    if s1 not in came:
        if strict and fallback:  # the rules above boxed it in: route without them (the validator reports the result)
            print(f"  WARNING: no route from {start} to {goal} leaving at {BRANCH_MIN_DEG:.0f} degrees; routing without the rule")
            return route_road(terrain, start, goal, avoid, turn_cost, loop_avoid, keep_off, start_dir, strict=False)
        raise RuntimeError(f"no road route from {start} to {goal} within {ROUTE_GRADE_MAX:.0%}")
    cells = [s1]
    while came[cells[-1]] is not None:
        cells.append(came[cells[-1]])
    pts = [(c[0] * ROUTE_CELL, c[1] * ROUTE_CELL + Y_MIN) for c in reversed(cells)]
    pts[0], pts[-1] = start, goal
    # Smooth the grid staircase into curves (Chaikin, 3 rounds), then resample every ROUTE_STEP m.
    for _ in range(3):
        sm = [pts[0]]
        for a, b in zip(pts, pts[1:]):
            sm += [(0.75 * a[0] + 0.25 * b[0], 0.75 * a[1] + 0.25 * b[1]), (0.25 * a[0] + 0.75 * b[0], 0.25 * a[1] + 0.75 * b[1])]
        pts = sm + [pts[-1]]
    pts, bulbs = bulb_hairpins(pts, avoid=lambda x, y: (avoid is not None and avoid(x, y)) or (loop_avoid is not None and loop_avoid(x, y)))
    keep = {p for loop in bulbs for p in loop}
    route_road.bulbs = bulbs  # the callers grade each loop evenly (grade_bulbs)
    for loop in bulbs:
        print(f"  hairpin loop at ({loop[len(loop) // 2][0]:.0f}, {loop[len(loop) // 2][1]:.0f}), {len(loop)} points")
    out, acc = [pts[0]], 0.0
    for a, b in zip(pts, pts[1:]):
        acc += math.dist(a, b)
        if acc >= ROUTE_STEP or b in keep:
            out.append(b)
            acc = 0.0
    if math.dist(out[-1], goal) > 1.0:
        if math.dist(out[-1], goal) < ROUTE_STEP * 0.4:
            out[-1] = goal
        else:
            out.append(goal)
    return [(round(x, 1), round(y, 1)) for x, y in out]


def max_grade(seg, g):
    """The steepest a segment may be (INFRA_DESIGN.md section 4, by the profile it will get)."""
    kind = seg["kind"]
    a, b = g.nodes[seg["a"]], g.nodes[seg["b"]]
    district = district_at((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
    if kind == "pier":
        return 0.04
    if kind == "pass":
        return 0.12
    if kind == "coast" or district in ("LeColline", "MonteFerrale"):
        return 0.10
    if district == "CittaAlta":
        return 0.15 if kind in ("street", "alley") else 0.08
    return 0.10 if kind == "alley" else 0.08


def limit_grades(g, max_iters=400):
    """Hold every road node within its roads' maximum grade of its neighbours (iterated to a fixed
    point), so roads stop inheriting the hillside's slope (Le Colline streets climbed at 24%); the
    terrain is then cut and filled to the roads. Bridge ends and the dam crest stay where they are.
    Returns the number of segments still too steep (pinned ends closer than their grade allows)."""
    crest = make_heightmap.reservoir_level + 2.0
    pinned = set()
    for sg in g.segments:
        if sg["bridge"]:
            pinned.update((sg["a"], sg["b"]))
    for i, n in enumerate(g.nodes):
        if abs(n[1] - DAM_Y) < 2.0 and abs(n[2] - crest) < 0.05:
            pinned.add(i)
    edges = []
    for sg in g.segments:
        a, b = sg["a"], sg["b"]
        run = math.hypot(g.nodes[b][0] - g.nodes[a][0], g.nodes[b][1] - g.nodes[a][1])
        edges.append((a, b, max_grade(sg, g) * max(run, 1.0)))
    for _ in range(max_iters):
        moved = 0.0
        for a, b, rise in edges:
            za, zb = g.nodes[a][2], g.nodes[b][2]
            excess = abs(zb - za) - rise
            if excess <= 0.005:
                continue
            # Split the correction between the free ends (the higher comes down, the lower goes up).
            fa, fb = a not in pinned, b not in pinned
            if not (fa or fb):
                continue
            share = excess / (int(fa) + int(fb))
            sign = 1.0 if zb > za else -1.0
            if fa:
                g.nodes[a][2] = round(za + sign * share, 3)
            if fb:
                g.nodes[b][2] = round(zb - sign * share, 3)
            moved = max(moved, excess)
        if moved < 0.01:
            break
    return sum(1 for a, b, rise in edges if abs(g.nodes[b][2] - g.nodes[a][2]) - rise > 0.05)


LOOP_NODES = set()  # node ids on hairpin loops: no other road may join there


def grade_bulbs(g, bulbs):
    """An even grade round each hairpin loop, from the height where it leaves one leg to where it
    joins the other (the loop crosses the slope; terrain heights would make it a ramp), and the
    loop widened per D.M. 5/11/2001 5.2.7: E = 45 / R per lane (capped at 2 m)."""
    extra = 2.0 * min(45.0 / HAIRPIN_R, 2.0)
    for loop in bulbs:
        ids = [g.node(x, y, 2.0) for x, y in loop]
        ids = [k for n, k in enumerate(ids) if n == 0 or k != ids[n - 1]]
        LOOP_NODES.update(ids)
        if len(ids) > 2:
            grade_chain(g, ids, g.nodes[ids[0]][2], g.nodes[ids[-1]][2])
        # Widen only round the loop itself: at the neck the entry and exit are 9 m apart, and widened
        # surfaces there merged the two legs (cars left the loop onto the wrong one).
        core = ids[2:-2]
        pairs = {frozenset(p) for p in zip(core, core[1:])}
        for sg in g.segments:
            if frozenset((sg["a"], sg["b"])) in pairs:
                sg["width"] = ROAD_KINDS[sg["kind"]][1] + extra
                sg["widening"] = round(extra, 2)  # the validator compares width - widening with the profile


def grade_chain(g, ids, z0, z1):
    """Set the heights along a chain of nodes to a steady grade from z0 to z1 (by distance)."""
    d = [0.0]
    for a, b in zip(ids, ids[1:]):
        d.append(d[-1] + math.hypot(g.nodes[b][0] - g.nodes[a][0], g.nodes[b][1] - g.nodes[a][1]))
    for i, dist in zip(ids, d):
        g.nodes[i][2] = round(z0 + (z1 - z0) * dist / max(d[-1], 1e-6), 2)


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
    grid(g, rng, R((60, 240, 975, 880)), 95, 0, 8, "street", "PortoVecchio")
    grid(g, rng, R((60, 880, 975, 1680)), 62, 14, 10, "alley", "CittaAlta", keep=0.85)
    grid(g, rng, R((1030, 900, 1980, 1680)), 125, 0, 6, "street", "LeFornaci")
    grid(g, rng, R((1160, 160, 1980, 900)), 105, -6, 6, "street", "Lungomare")
    grid(g, rng, R((1030, 160, 1160, 900)), 60, 20, 8, "alley", "BorgoMarino", keep=0.8)
    grid(g, rng, R((300, -230, 820, 40)), 110, 0, 0, "pier", "MoloSalvi")

    # Arterials.
    g.path(P([(140, 250), (140, 560), (150, 880), (180, 1200), (220, 1520)]), "avenue")   # west avenue
    g.path(P([(950, 260), (930, 560), (935, 880)]), "avenue")                              # port riverside
    g.path(P([(1200, 170), (1500, 175), (1800, 185), (1960, 200)]), "avenue")              # Lungomare promenade
    g.path(P([(1080, 960), (1300, 950), (1600, 945), (1950, 950)]), "avenue")             # east avenue
    g.path(P([(1100, 300), (1095, 600), (1090, 900), (1085, 1250), (1080, 1600)]), "avenue")  # east riverside

    # Bridges over the Fiume Ardo (named, with gate tags where they are gates).
    def bridge(name, y, kind, gate=None):
        w = rx(y)
        hw = river_half_width(y)
        a, b = g.node(w - hw - 30, y), g.node(w + hw + 30, y)
        g.connect(a, b, kind, bridge=name, gate=gate)

    bridge("PonteSanRocco", 1250, "avenue")
    bridge("PonteMobile", 350, "avenue", gate="Gate.PonteMobile")
    bridge("Passerella", 800, "alley")                      # footbridge (pedestrian: annotate_networks)
    bridge("PonteDelleColline", 1900, "street", gate="Gate.Colline")

    # Swing bridge to Molo Salvi across the Darsena.
    g.path(P([(560, 260), (560, 30)]), "pier", bridge="PonteGirevole", gate="Gate.Molo")

    # Coast road: up the west cliffs from the port to the hills, with curves.
    coast_pts = P([(90, 300), (70, 700), (75, 1100), (95, 1450), (160, 1700), (300, 1850),
                 (420, 1960), (600, 2030), (850, 2000), (1000, 1930)])
    g.path(coast_pts, "coast", gate=None)
    # Hills roads and gates (checkpoints where roads leave the open districts).
    # Start on the centro grid itself: a free start 37 m off it got a connector running alongside an
    # alley 9 m below (two corridors in one place, an 11 m hump; the autopilot crashed there).
    villa = P([(520, 1660), (540, 1760), (480, 1900), (450, 1960)])
    near = min(g.nodes, key=lambda n: math.dist(n[:2], villa[0]))
    if math.dist(near[:2], villa[0]) < 45.0:
        villa[0] = (near[0], near[1])
    g.path(villa, "street", gate="Gate.Colline")  # to Villa Salvi
    g.path(P([(1400, 1660), (1380, 1800), (1300, 1900), (1180, 1905)]), "street", gate="Gate.Colline")
    # Monte Ferrale roads, routed over the terrain (route_road): the pass from the hills checkpoint up
    # to the dam's east end, then straight along the dam crest; the quarry spur, the radio-mast road
    # and the hermitage road. Straight 450 m legs at a steady grade cut tens of metres into the
    # mountain and left terraces of rock walls; routed roads follow the land and switch back by
    # themselves where it gets steep.
    crest = make_heightmap.reservoir_level + 2.0
    rx0, ry0, rx1, ry1 = RESERVOIR
    rcx, rcy, rax, ray = (rx0 + rx1) / 2, (ry0 + ry1) / 2, (rx1 - rx0) / 2 * 1.2, (ry1 - ry0) / 2 * 1.2

    def wet(x, y):
        if ((x - rcx) / rax) ** 2 + ((y - rcy) / ray) ** 2 < 1.0:
            return True  # the reservoir
        return y < DAM_Y + 20 and abs(x - river_x(y)) < river_half_width(y) + 14
    quarry_end = (X(1700), 2380)
    qw, qd = 120 / 2 + 15, 80 / 2 + 15  # La Cava's yard (LANDMARKS footprint) plus a margin

    def wet_or_quarry(x, y):
        # Only the spur enters the quarry yard: its gravel piles and conveyor stand in it.
        return wet(x, y) or (abs(x - quarry_end[0]) < qw and abs(y - quarry_end[1]) < qd)
    dam_e, dam_w = (X(1395), DAM_Y), (X(870), DAM_Y)
    # The climb ends just east of the dam where the land is already at crest height, so the crest
    # road continues level (ending below the crest made a 70% ramp onto the dam).
    abutment = (dam_e[0] + 60.0, DAM_Y)
    climb = route_road(terrain, (X(1300), 1900), abutment, avoid=wet_or_quarry)  # from the hills road's end node
    climb_bulbs = route_road.bulbs
    # Routed roads merge nodes only within 2 m: hairpin loops pass 9 m from the other leg.
    pass_ids = g.path(climb + [dam_e, (X(1130), DAM_Y), dam_w], "pass", merge=2.0, gate="Gate.Ferrale")
    grade_bulbs(g, climb_bulbs)
    n_climb = len(climb)
    grade_chain(g, pass_ids[n_climb - 1:n_climb + 1], g.nodes[pass_ids[n_climb - 1]][2], crest)
    for i in pass_ids[n_climb:]:
        g.nodes[i][2] = round(crest, 2)                               # along the dam crest
    # Branches start from the pass node from which the link is short but not steep: the node
    # nearest the quarry gate was 38 m above it 70 m away, and the spur became a Z of two 5 m
    # hairpins 23 m apart that no loop could replace.
    def branch_from(target):
        tz = g.height(*target)

        def cost(i):
            d = math.dist(g.nodes[i][:2], target)
            grade = abs(g.nodes[i][2] - tz) / max(d, 1.0)
            return (d if d >= 150.0 else 1e9) * (1.0 + 10.0 * max(0.0, grade - ROUTE_GRADE_OK))
        return sorted(pass_ids[:n_climb], key=cost)
    # The yard has one entrance, in the middle of its north side: the spur from the pass arrives
    # there and the radio-mast road leaves from it (the spur used to end in the yard's centre and
    # crossed the mast road's exit stub 5 m above it). Routed roads keep off roads already built
    # (road_mask), so the branches no longer cross each other or the pass legs at grade.
    gate_n = (quarry_end[0], quarry_end[1] + qd + 3.0)
    q_cands = branch_from(gate_n)
    def near_road(x, y):  # hairpin loops keep 14 m from roads already built
        return any((n[0] - x) ** 2 + (n[1] - y) ** 2 < 14.0 ** 2 for n in g.nodes)
    def tangent(ids, i):  # unit direction of a road through node i
        k = ids.index(i)
        a, b = g.nodes[ids[max(0, k - 1)]], g.nodes[ids[min(len(ids) - 1, k + 1)]]
        l = math.hypot(b[0] - a[0], b[1] - a[1]) or 1.0
        return ((b[0] - a[0]) / l, (b[1] - a[1]) / l)
    # The best branch points first; the first one from which the spur can leave the pass at
    # BRANCH_MIN_DEG wins (on a steep side slope a square exit is too steep to start).
    for q0 in q_cands[:12] + [None]:
        if q0 is None:
            q0 = q_cands[0]
            spur = route_road(terrain, tuple(g.nodes[q0][:2]), gate_n, avoid=wet_or_quarry, loop_avoid=near_road,
                              keep_off=road_mask(g, (tuple(g.nodes[q0][:2]), gate_n)), start_dir=tangent(pass_ids, q0))
            break
        try:
            spur = route_road(terrain, tuple(g.nodes[q0][:2]), gate_n, avoid=wet_or_quarry, loop_avoid=near_road,
                              keep_off=road_mask(g, (tuple(g.nodes[q0][:2]), gate_n)), start_dir=tangent(pass_ids, q0),
                              fallback=False)
            break
        except RuntimeError:
            continue
    q0p = tuple(g.nodes[q0][:2])
    print(f"quarry spur leaves the pass at ({q0p[0]:.0f}, {q0p[1]:.0f})")
    quarry = g.path([q0p] + spur[1:] + [quarry_end], "pass", merge=2.0)
    grade_bulbs(g, route_road.bulbs)
    mast_end = (X(1820), 2680)
    mast = g.path(route_road(terrain, gate_n, mast_end, avoid=wet_or_quarry, loop_avoid=near_road,
                             keep_off=road_mask(g, (gate_n, mast_end)), start_dir=tangent(quarry, quarry[-2])), "pass", merge=2.0)
    grade_bulbs(g, route_road.bulbs)
    hermitage_end = (X(500), 2640)
    def wet_or_crest_side(x, y):  # the first leg may not double back east along the dam (a 158-degree bend at the junction)
        return wet(x, y) or (math.dist((x, y), dam_w) < 70.0 and x > dam_w[0] + 8.0)
    hermitage = g.path(route_road(terrain, dam_w, hermitage_end, avoid=wet_or_crest_side, loop_avoid=near_road,
                                  keep_off=road_mask(g, (dam_w, hermitage_end))), "pass", merge=2.0)
    grade_bulbs(g, route_road.bulbs)
    print(f"hairpin loops: {len(climb_bulbs)} on the pass (radius {HAIRPIN_R:.0f} m)")
    print(f"mountain roads: pass {len(climb)} nodes, quarry {len(quarry)}, mast {len(mast)}, hermitage {len(hermitage)}")

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
            if j == i or j in exclude or j in LOOP_NODES:
                continue
            if other_district and district_at(nx, ny) == di:
                continue
            dd = (nx - x) ** 2 + (ny - y) ** 2
            if dd < bd and segment_on_land(x, y, nx, ny):
                best, bd = j, dd
        return best

    def nearest_foot(i, max_d):
        """Nearest point on the interior of another road's segment (a T-junction onto it), or None."""
        x, y = g.nodes[i][0], g.nodes[i][1]
        best, bd = None, max_d
        for sg in g.segments:
            if sg["bridge"] or sg["kind"] == "pier" or i in (sg["a"], sg["b"]) or sg["a"] in LOOP_NODES or sg["b"] in LOOP_NODES:
                continue
            a, b = g.nodes[sg["a"]], g.nodes[sg["b"]]
            vx, vy = b[0] - a[0], b[1] - a[1]
            l2 = vx * vx + vy * vy
            if l2 < 1.0:
                continue
            t = ((x - a[0]) * vx + (y - a[1]) * vy) / l2
            if not 0.1 < t < 0.9:
                continue
            fx, fy = a[0] + vx * t, a[1] + vy * t
            d = math.hypot(fx - x, fy - y)
            if d < bd and segment_on_land(x, y, fx, fy):
                best, bd = (fx, fy), d
        return best, bd

    deg = degree()
    pass_end = {s[k] for s in g.segments if s["kind"] == "pass" for k in ("a", "b")}
    for i in range(len(g.nodes)):
        if deg[i] <= 1 and i not in pass_end:  # loose end (mountain road ends stay dead ends)
            linked = {s["b"] if s["a"] == i else s["a"] for s in g.segments if i in (s["a"], s["b"])}
            j = nearest(i, 120.0, exclude=linked)
            # Joining the nearest *node* can run the link alongside a road at a few degrees (the
            # Villa Salvi drive met the coast road 45 m along it at 15 degrees, and the two road
            # meshes overlapped); a right-angle T onto the road itself is used when it is shorter.
            foot, fd = nearest_foot(i, 120.0)
            if foot is not None and (j is None or fd + 5.0 < math.dist(g.nodes[i][:2], g.nodes[j][:2])):
                g.connect(i, g.node(foot[0], foot[1], merge=3.0), "street")  # split_crossings joins it to the road
            elif j is not None:
                g.connect(i, j, "street")
    # Border links between neighbouring districts, about every 150 m.
    for i, (x, y, _) in enumerate(list(g.nodes)):
        j = nearest(i, 75.0, other_district=True)
        if j is not None and rng.random() < 0.6:
            g.connect(i, j, "street")

    drop_coincident(g)
    merge_short_segments(g)
    split_crossings(g)
    merge_short_segments(g)
    split_crossings(g)  # a junction node a metre off a road's line can leave one crossing; none after this
    fix_shallow_forks(g)

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


SIGNAL_SPACING = 180.0  # m: corso junctions get signals at most this often (the others stay priority junctions)
ROAD_RANK = {"pier": 0, "alley": 1, "street": 2, "coast": 3, "avenue": 3, "pass": 3}


def merge_short_segments(g, min_len=4.0):
    """Merge the two ends of any segment shorter than `min_len` m (junction nodes that landed a
    few metres from an existing node): the stubs made one-node-wide zigzags in the road meshes and
    a road end could cross another road a metre past its junction. The end with more roads stays."""
    merged = 0
    while True:
        degree = {}
        for sg in g.segments:
            degree[sg["a"]] = degree.get(sg["a"], 0) + 1
            degree[sg["b"]] = degree.get(sg["b"], 0) + 1
        victim = None
        for sg in g.segments:
            if sg["bridge"] or sg["kind"] == "pier":
                continue
            if math.dist(g.nodes[sg["a"]][:2], g.nodes[sg["b"]][:2]) < min_len:
                victim = sg
                break
        if victim is None:
            break
        keep, drop = (victim["a"], victim["b"]) if degree[victim["a"]] >= degree[victim["b"]] else (victim["b"], victim["a"])
        out, have = [], set()
        for sg in g.segments:
            if sg is victim:
                continue
            if sg["a"] == drop:
                sg["a"] = keep
            if sg["b"] == drop:
                sg["b"] = keep
            key = frozenset((sg["a"], sg["b"]))
            if sg["a"] == sg["b"] or key in have:
                continue
            have.add(key)
            out.append(sg)
        g.segments = out
        merged += 1
    print(f"short segments: {merged} under {min_len:.0f} m merged away")


def fix_shallow_forks(g, max_angle=30.0):
    """At a junction where two roads leave within `max_angle` degrees of each other, drop the lesser
    one (lower rank, then shorter) when the network stays connected without it: the two road meshes
    overlap in a long wedge and one road's kerbs stand across the other (the D.M. 19/4/2006 wants
    junction arms at 70 to 110 degrees). Hairpin bends (two-road nodes) are not junctions."""
    def connected_without(drop):
        segs = [sg for sg in g.segments if sg is not drop]
        adj = {}
        for sg in segs:
            adj.setdefault(sg["a"], set()).add(sg["b"])
            adj.setdefault(sg["b"], set()).add(sg["a"])
        if drop["a"] not in adj or drop["b"] not in adj:
            return False  # one end would be stranded
        seen, stack = {drop["a"]}, [drop["a"]]
        while stack:
            for n in adj[stack.pop()]:
                if n not in seen:
                    seen.add(n)
                    stack.append(n)
        return drop["b"] in seen

    dropped, tried = 0, set()
    while True:
        arms = {}
        for sg in g.segments:
            arms.setdefault(sg["a"], []).append(sg)
            arms.setdefault(sg["b"], []).append(sg)
        victim = None
        for n, segs in arms.items():
            if len(segs) < 3:
                continue
            dirs = []
            for sg in segs:
                o = g.nodes[sg["b"] if sg["a"] == n else sg["a"]]
                dx, dy = o[0] - g.nodes[n][0], o[1] - g.nodes[n][1]
                l = math.hypot(dx, dy) or 1.0
                dirs.append((dx / l, dy / l, l, sg))
            for i in range(len(dirs)):
                for k in range(i + 1, len(dirs)):
                    if dirs[i][0] * dirs[k][0] + dirs[i][1] * dirs[k][1] < math.cos(math.radians(max_angle)):
                        continue
                    pair = sorted((dirs[i], dirs[k]), key=lambda d: (ROAD_RANK[d[3]["kind"]], d[2]))
                    for d in pair:
                        sg = d[3]
                        if sg["bridge"] or sg["kind"] == "pier" or id(sg) in tried:
                            continue
                        tried.add(id(sg))
                        if connected_without(sg):
                            victim = sg
                            break
                    if victim is not None:
                        break
                if victim is not None:
                    break
            if victim is not None:
                break
        if victim is None:
            break
        g.segments.remove(victim)
        dropped += 1
    print(f"shallow forks: {dropped} road(s) dropped that left a junction within {max_angle:.0f} degrees of another")


def drop_coincident(g, max_angle=30.0):
    """Drop a street that runs inside another road's corridor: the district grids' edge columns lay
    along the viale and the coast road at a few degrees, so two road meshes (and one road's kerbs,
    bins and poles) sat on the other road's asphalt. The wider road (or the longer one of equal
    width) stays; the streets that ended on the dropped one are joined to it by split_crossings."""
    cell = 50.0
    grid = {}
    for j, s in enumerate(g.segments):
        a, b = g.nodes[s["a"]], g.nodes[s["b"]]
        for gx in range(int(min(a[0], b[0]) // cell) - 1, int(max(a[0], b[0]) // cell) + 2):
            for gy in range(int(min(a[1], b[1]) // cell) - 1, int(max(a[1], b[1]) // cell) + 2):
                grid.setdefault((gx, gy), set()).add(j)
    dropped = set()
    for p, sp in enumerate(g.segments):
        if sp["bridge"] or sp["kind"] in ("pier", "pass"):
            continue
        A, B = g.nodes[sp["a"]], g.nodes[sp["b"]]
        lp = math.dist(A[:2], B[:2])
        if lp < 1.0:
            continue
        near = set()
        for gx in range(int(min(A[0], B[0]) // cell), int(max(A[0], B[0]) // cell) + 1):
            for gy in range(int(min(A[1], B[1]) // cell), int(max(A[1], B[1]) // cell) + 1):
                near |= grid.get((gx, gy), set())
        for q in near:
            sq = g.segments[q]
            if q == p or q in dropped or sq["bridge"]:
                continue
            C, D = g.nodes[sq["a"]], g.nodes[sq["b"]]
            lq = math.dist(C[:2], D[:2])
            # The road that matters stays: the coast road and the avenues over streets over alleys,
            # then the longer one.
            if lq < 1.0 or (ROAD_RANK[sq["kind"]], lq) <= (ROAD_RANK[sp["kind"]], lp):
                continue
            cosang = abs(((B[0] - A[0]) * (D[0] - C[0]) + (B[1] - A[1]) * (D[1] - C[1])) / (lp * lq))
            if cosang < math.cos(math.radians(max_angle)):
                continue
            reach = (sp["width"] + sq["width"]) / 2.0  # edges overlap
            vx, vy = D[0] - C[0], D[1] - C[1]
            inside = True
            for k in range(5):
                x, y = A[0] + (B[0] - A[0]) * k / 4.0, A[1] + (B[1] - A[1]) * k / 4.0
                t = ((x - C[0]) * vx + (y - C[1]) * vy) / (lq * lq)
                if not -0.05 <= t <= 1.05 or math.hypot(x - C[0] - vx * t, y - C[1] - vy * t) > reach:
                    inside = False
                    break
            if inside:
                dropped.add(p)
                break
    g.segments = [s for j, s in enumerate(g.segments) if j not in dropped]
    print(f"coincident roads: {len(dropped)} street segment(s) dropped from inside another road's corridor")


def split_crossings(g, same_level=2.5, snap=4.0, bridge_snap=15.0):
    """Roads that cross in plan without sharing a node get a junction node at the crossing, and
    a road end that all but touches another road is joined to it (65 grid streets ran through
    the long viale and coast-road segments with no junction: no turn in the lane graph, two road
    meshes on top of each other, and a bump where their heights disagreed). Urban roads sit on
    the terrain, so they always cross at grade. Graded mountain roads ("pass") are only joined
    when the two heights are within `same_level` m; other crossings are reported, because they
    need a bridge or a different route. A bridge deck that overruns the riverside street by up to
    `bridge_snap` m is shortened to end at the street."""
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    def project(p, a, b):  # (t along a-b, distance) of the point nearest p
        dx, dy = b[0] - a[0], b[1] - a[1]
        l2 = dx * dx + dy * dy
        t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2)) if l2 else 0.0
        return t, math.hypot(p[0] - a[0] - dx * t, p[1] - a[1] - dy * t)

    cell = 50.0
    grid = {}
    for j, s in enumerate(g.segments):
        a, b = g.nodes[s["a"]], g.nodes[s["b"]]
        for gx in range(int(min(a[0], b[0]) // cell), int(max(a[0], b[0]) // cell) + 1):
            for gy in range(int(min(a[1], b[1]) // cell), int(max(a[1], b[1]) // cell) + 1):
                grid.setdefault((gx, gy), []).append(j)
    splits = {}   # segment -> [(t, node)]
    joined = {}   # node -> segments it was joined into (an end touching a road)
    unbridged, crossings, touches = [], 0, 0
    done = set()
    for js in grid.values():
        for i in range(len(js)):
            for k in range(i + 1, len(js)):
                p, q = min(js[i], js[k]), max(js[i], js[k])
                if (p, q) in done:
                    continue
                done.add((p, q))
                sp, sq = g.segments[p], g.segments[q]
                if {sp["a"], sp["b"]} & {sq["a"], sq["b"]}:
                    continue
                A, B = g.nodes[sp["a"]], g.nodes[sp["b"]]
                C, D = g.nodes[sq["a"]], g.nodes[sq["b"]]
                # A road end that all but touches the other road: join it there.
                touched = False
                for n, seg, (E, F) in ((sp["a"], q, (C, D)), (sp["b"], q, (C, D)), (sq["a"], p, (A, B)), (sq["b"], p, (A, B))):
                    t, d = project(g.nodes[n], E, F)
                    if d < snap and 0.02 < t < 0.98 and seg not in joined.get(n, ()):
                        splits.setdefault(seg, []).append((t, n))
                        joined.setdefault(n, []).append(seg)
                        touches += 1
                        touched = True
                if touched:
                    continue
                d1, d2, d3, d4 = cross(C, D, A), cross(C, D, B), cross(A, B, C), cross(A, B, D)
                if not (((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0))) or d1 == d2 or d3 == d4:
                    continue
                t, u = d1 / (d1 - d2), d3 / (d3 - d4)
                x, y = A[0] + (B[0] - A[0]) * t, A[1] + (B[1] - A[1]) * t
                za, zb = A[2] + (B[2] - A[2]) * t, C[2] + (D[2] - C[2]) * u
                if (sp["kind"] == "pass" or sq["kind"] == "pass") and abs(za - zb) > same_level:
                    unbridged.append((p, q, x, y, za, zb))
                    continue
                # A bridge deck overrunning the street at its end: pull the deck's end node to the street.
                moved = None
                for seg, s_, tt in ((p, sp, t), (q, sq, u)):
                    if s_["bridge"]:
                        end = s_["a"] if tt < 0.5 else s_["b"]
                        if math.dist(g.nodes[end][:2], (x, y)) < bridge_snap:
                            g.nodes[end][0], g.nodes[end][1] = round(x, 1), round(y, 1)
                            moved = (seg, end)
                if moved is not None:
                    other = q if moved[0] == p else p
                    tt = u if other == q else t
                    splits.setdefault(other, []).append((tt, moved[1]))
                    crossings += 1
                    continue
                n = g.node(x, y, merge=1.0)
                g.nodes[n][2] = round((za + zb) / 2.0, 2)
                splits.setdefault(p, []).append((t, n))
                splits.setdefault(q, []).append((u, n))
                crossings += 1
    have = {frozenset((s["a"], s["b"])) for s in g.segments}
    out = []
    for j, s in enumerate(g.segments):
        if j not in splits:
            out.append(s)
            continue
        chain = [s["a"]] + [n for _, n in sorted(splits[j])] + [s["b"]]
        for a, b in zip(chain, chain[1:]):
            key = frozenset((a, b))
            if a == b or (key in have and key != frozenset((s["a"], s["b"]))):
                continue
            ns = dict(s)
            ns["a"], ns["b"] = a, b
            out.append(ns)
            have.add(key)
    g.segments = out
    for p, q, x, y, za, zb in unbridged:
        print(f"  WARNING: roads cross without a junction at ({x:.0f}, {y:.0f}): heights {za:.1f} and {zb:.1f} m "
              f"({g.segments[p]['kind']} over {g.segments[q]['kind']}); needs a bridge or another route")
    print(f"crossings: {crossings} junction(s) added where roads crossed, {touches} road end(s) joined to the road "
          f"they touched, {len(unbridged)} unbridged crossing(s) left")
    return unbridged


# --- Landmarks and gates ---------------------------------------------------------------

LANDMARKS = [  # name, district, (x, y), footprint (w, d) m, height m, kind
    ("LaCala", "PortoVecchio", (X(700), 275), (24, 16), 7, "ruin"),
    ("FishMarket", "PortoVecchio", (X(420), 330), (70, 35), 9, "hall"),
    ("FerryPier", "PortoVecchio", (X(880), 240), (20, 80), 2, "pier"),
    ("SalviTerminal", "MoloSalvi", (X(520), -120), (60, 40), 55, "tower"),
    ("Faro", "MoloSalvi", (X(930), -240), (8, 8), 28, "lighthouse"),
    ("SantaLucia", "CittaAlta", (X(520), 1320), (30, 55), 30, "church"),
    ("CastrovanniTownhouse", "CittaAlta", (X(380), 1180), (22, 18), 14, "house"),
    ("PoliceHQ", "CittaAlta", (X(760), 1000), (45, 35), 18, "civic"),
    ("OspedaleSanMartino", "CittaAlta", (X(250), 1500), (60, 40), 22, "civic"),
    ("BeppesGarage", "LeFornaci", (X(1450), 1150), (50, 40), 8, "yard"),
    ("AbandonedKiln", "LeFornaci", (X(1780), 1500), (60, 45), 40, "kiln"),
    ("StazioneCentrale", "LeFornaci", (X(1500), 980), (120, 30), 14, "station"),
    ("ClubMedusa", "Lungomare", (X(1450), 250), (40, 30), 10, "club"),
    ("HotelRiviera", "Lungomare", (X(1750), 300), (40, 25), 38, "hotel"),
    ("VillaSalvi", "LeColline", (X(450), 1960), (40, 30), 12, "villa"),
    ("Lookout", "LeColline", (X(250), 2030), (15, 15), 3, "lookout"),
    ("LaCava", "MonteFerrale", (X(1700), 2380), (120, 80), 15, "quarry"),
    ("DigaDelFerrale", "MonteFerrale", (X(1130), DAM_Y), (X(500), 15), 40, "dam"),
    ("EremoDelleAquile", "MonteFerrale", (X(500), 2640), (25, 20), 12, "hermitage"),
    ("RadioMast", "MonteFerrale", (X(1820), 2680), (8, 8), 60, "mast"),
]

GATES = [  # tag, opens with milestone, what stands there while locked
    ("Gate.PortCordon", "Story.Mission.Funerale", "police cordon at the fire scene"),
    ("Gate.Colline", "Story.Mission.StradaCostiera", "private-road checkpoint"),
    ("Gate.Ferrale", "Story.Mission.LaCava", "rockfall barrier on the pass road"),
    ("Gate.PonteMobile", "Story.Mission.LaFornace", "lifting bridge kept raised"),
    ("Gate.RailCrossing", "Story.Mission.LaFornace", "level crossing closed, freight train parked"),
    ("Gate.Molo", "Story.Act2.Start", "swing bridge turned open"),
]


# Landmarks whose ground-plan includes the road on purpose: the dam carries the pass road on its
# crest, and the quarry yard is where the pass road's branch ends.
LANDMARKS_ON_ROAD = {"dam", "quarry", "pier", "lighthouse"}
LANDMARK_ROAD_MARGIN = 3.0   # m kept clear between a landmark's footprint and a road's edge
LANDMARK_SEARCH = 150.0      # m: furthest a landmark moves from its planned spot


def settle_landmarks(terrain, g):
    """Move each landmark to the nearest spot (to its planned position, in its district) where its
    footprint plus a margin is clear of every road corridor and of water. Roads came first, so a
    landmark placed by hand on a street would otherwise stand in the middle of it."""
    blocked = terrain < 1.5
    ys = np.arange(terrain.shape[0]) + Y_MIN
    xs = np.arange(terrain.shape[1])
    rx = np.array([river_x(float(y)) for y in ys])
    rhw = np.array([river_half_width(float(y)) for y in ys]) + 4.0
    blocked |= (np.abs(xs[None, :] - rx[:, None]) <= rhw[:, None]) & (ys[:, None] < DAM_Y + 30)
    for seg in g.segments:
        ax, ay, _ = g.nodes[seg["a"]]
        bx, by, _ = g.nodes[seg["b"]]
        reach = seg["width"] / 2.0 + LANDMARK_ROAD_MARGIN
        x0, x1 = int(max(0, min(ax, bx) - reach - 2)), int(min(W, max(ax, bx) + reach + 2)) + 1
        y0, y1 = int(max(0, min(ay, by) - reach - 2 - Y_MIN)), int(min(H, max(ay, by) + reach + 2 - Y_MIN)) + 1
        yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
        yy += Y_MIN
        dx, dy = bx - ax, by - ay
        t = np.clip(((xx - ax) * dx + (yy - ay) * dy) / max(dx * dx + dy * dy, 1e-6), 0.0, 1.0)
        blocked[y0:y1, x0:x1] |= np.hypot(xx - (ax + t * dx), yy - (ay + t * dy)) < reach
    rects = {n: r for n, r, _, _ in DISTRICTS}
    placed = []
    for name, dist, (x, y), (fw, fd), h, kind in LANDMARKS:
        if kind in LANDMARKS_ON_ROAD:
            placed.append((name, dist, (x, y), (fw, fd), h, kind))
            continue
        dx0, dy0, dx1, dy1 = rects[dist]
        best = None
        step = 2.0
        rings = int(LANDMARK_SEARCH / step)
        cands = sorted(((i * step, j * step) for i in range(-rings, rings + 1) for j in range(-rings, rings + 1)),
                       key=lambda o: o[0] * o[0] + o[1] * o[1])
        for ox, oy in cands:
            if ox * ox + oy * oy > LANDMARK_SEARCH ** 2:
                break
            cx, cy = x + ox, y + oy
            if not (dx0 + fw / 2 <= cx <= dx1 - fw / 2 and dy0 + fd / 2 <= cy <= dy1 - fd / 2):
                continue
            c0, c1 = int(cx - fw / 2), int(cx + fw / 2) + 1
            r0, r1 = int(cy - fd / 2 - Y_MIN), int(cy + fd / 2 - Y_MIN) + 1
            if c0 < 0 or r0 < 0 or c1 > W or r1 > H:
                continue
            if not blocked[r0:r1, c0:c1].any():
                best = (round(cx, 1), round(cy, 1))
                break
        if best is None:
            best = claim_block(g, terrain, name, dist, (x, y), (fw, fd), rects[dist])
            placed.append((name, dist, best, (fw, fd), h, kind))
            continue
        if best != (x, y):
            print(f"landmark {name}: moved {math.hypot(best[0] - x, best[1] - y):.0f} m off the roads to {best}")
        placed.append((name, dist, best, (fw, fd), h, kind))
    return placed


CLAIMABLE_KINDS = {"alley", "street"}  # a large landmark may take over a block's minor streets


def claim_block(g, terrain, name, dist, pos, size, rect):
    """For a landmark too big to fit between streets (hospital, station): the nearest spot where
    only minor, ungated streets cross its footprint and removing them keeps the network connected;
    those streets are removed, like a real hospital or station taking a whole block."""
    fw, fd = size
    pts, seg_of, reach = [], [], []
    for i, seg in enumerate(g.segments):
        ax, ay, _ = g.nodes[seg["a"]]
        bx, by, _ = g.nodes[seg["b"]]
        n = max(2, int(math.hypot(bx - ax, by - ay) / 2.0) + 1)
        for k in range(n):
            t = k / (n - 1)
            pts.append((ax + (bx - ax) * t, ay + (by - ay) * t))
            seg_of.append(i)
            reach.append(seg["width"] / 2.0 + LANDMARK_ROAD_MARGIN)
    pts, seg_of, reach = np.array(pts), np.array(seg_of), np.array(reach)

    def connected_without(drop):
        adj = {}
        for i, seg in enumerate(g.segments):
            if i in drop:
                continue
            adj.setdefault(seg["a"], []).append(seg["b"])
            adj.setdefault(seg["b"], []).append(seg["a"])
        start = next(iter(adj))
        seen, stack = {start}, [start]
        while stack:
            for m in adj[stack.pop()]:
                if m not in seen:
                    seen.add(m)
                    stack.append(m)
        return len(seen) == len(adj)

    x, y = pos
    for r in np.arange(0.0, LANDMARK_SEARCH, 4.0):
        for a in np.arange(0.0, 360.0, 360.0 / max(1, int(2 * math.pi * r / 4.0))) if r > 0 else (0.0,):
            cx, cy = x + r * math.cos(math.radians(a)), y + r * math.sin(math.radians(a))
            if not (rect[0] + fw / 2 <= cx <= rect[2] - fw / 2 and rect[1] + fd / 2 <= cy <= rect[3] - fd / 2):
                continue
            c0, c1, r0, r1 = int(cx - fw / 2), int(cx + fw / 2) + 1, int(cy - fd / 2 - Y_MIN), int(cy + fd / 2 - Y_MIN) + 1
            if (terrain[r0:r1, c0:c1] < 1.5).any():
                continue
            inside = (np.abs(pts[:, 0] - cx) < fw / 2 + reach) & (np.abs(pts[:, 1] - cy) < fd / 2 + reach)
            drop = set(int(i) for i in np.unique(seg_of[inside]))
            if any(g.segments[i]["kind"] not in CLAIMABLE_KINDS or g.segments[i]["gate"] or g.segments[i]["bridge"] for i in drop):
                continue
            if drop and not connected_without(drop):
                continue
            g.segments = [seg for i, seg in enumerate(g.segments) if i not in drop]
            print(f"landmark {name}: claimed a block at ({cx:.0f}, {cy:.0f}), {math.hypot(cx - x, cy - y):.0f} m from plan, removing {len(drop)} minor street segment(s)")
            return (round(cx, 1), round(cy, 1))
    print(f"warning: no spot for landmark {name}; left on its planned spot (roads run through it)")
    return pos


def drop_orphan_nodes(g):
    """Remove nodes no segment uses any more, and renumber the segments' node indices."""
    used = sorted({seg["a"] for seg in g.segments} | {seg["b"] for seg in g.segments})
    remap = {old: new for new, old in enumerate(used)}
    g.nodes = [g.nodes[i] for i in used]
    for seg in g.segments:
        seg["a"], seg["b"] = remap[seg["a"]], remap[seg["b"]]


# Landmarks whose main volume is built from the building kit (Tools/blender/KIT_SPEC.md) instead of a
# grey box: kind -> (offset of the main volume as a fraction of (w, d), its size fraction, storeys,
# building style). oc_m3_story.py keeps their other parts (bell tower, wings, canopies...).
KIT_LANDMARKS = {
    "church":  ((0.0, -0.125), (1.0, 0.75), 4, "oldtown"),
    "house":   ((0.0, 0.0), (1.0, 1.0), 4, "oldtown"),
    "civic":   ((0.0, 0.0), (1.0, 1.0), None, "grid90"),
    "hall":    ((0.0, 0.0), (1.0, 1.0), 2, "grid90"),
    "club":    ((0.0, 0.0), (1.0, 1.0), 3, "beach"),
    "hotel":   ((0.0, 0.0), (1.0, 1.0), None, "beach"),
    "station": ((0.0, 0.0), (0.4, 1.0), 4, "grid120"),
    "villa":   ((-0.2, 0.0), (0.6, 1.0), 3, "hills"),
}


def landmark_buildings(terrain, landmarks):
    """Building records for the kit-built landmark volumes (same format as place_buildings)."""
    out = []
    for name, dist, (x, y), (fw, fd), h, kind in landmarks:
        if kind not in KIT_LANDMARKS:
            continue
        (ox, oy), (sx, sy), storeys, style = KIT_LANDMARKS[kind]
        cx, cy = x + ox * fw, y + oy * fd
        w = max(MIN_FOOTPRINT, round(fw * sx / BAY) * BAY)
        d = max(MIN_FOOTPRINT, round(fd * sy / BAY) * BAY)
        n = storeys or max(1, round(h / STOREY_H))
        r0, r1 = int(cy - d / 2 - Y_MIN), int(cy + d / 2 - Y_MIN) + 1
        c0, c1 = int(cx - w / 2), int(cx + w / 2) + 1
        ground = terrain[r0:r1, c0:c1]
        ci = float(terrain[int(round(cy - Y_MIN)), int(round(cx))])
        out.append({"pos": [round(cx, 2), round(cy, 2)], "size": [w, d], "yaw": 0.0,
                    "base": round(float(ground.min()) - 1.0, 2), "top": round(ci + n * STOREY_H, 2), "storeys": n,
                    "style": BUILDING_STYLES.index(style), "district": dist, "landmark": name})
    return out


# --- Preview ------------------------------------------------------------------------------

DISTRICT_COLORS = {
    "PortoVecchio": (205, 170, 120), "MoloSalvi": (160, 160, 170), "CittaAlta": (220, 190, 150),
    "LeFornaci": (170, 120, 95), "Lungomare": (235, 215, 160), "BorgoMarino": (210, 200, 175),
    "LeColline": (150, 175, 110), "MonteFerrale": (125, 135, 120),
}


def render_preview(path, terrain, g, buildings=(), landmarks=LANDMARKS):
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
    for b in buildings:
        c, sn = math.cos(math.radians(b["yaw"])), math.sin(math.radians(b["yaw"]))
        hw, hd = b["size"][0] / 2, b["size"][1] / 2
        d.polygon([to_px(b["pos"][0] + u * c - v * sn, b["pos"][1] + u * sn + v * c)
                   for u, v in ((-hw, -hd), (hw, -hd), (hw, hd), (-hw, hd))],
                  fill=(150, 150, 155), outline=(105, 105, 110))
    for name, _, (x, y), (fw, fd), _, _ in landmarks:
        px, py = to_px(x, y)
        d.rectangle([px - fw * scale / 2, py - fd * scale / 2, px + fw * scale / 2, py + fd * scale / 2], outline=(0, 0, 0), fill=(255, 230, 0))
        d.text((px + 6, py - 6), name, fill=(0, 0, 0), font=font)
    for name, rect, _, _ in DISTRICTS:
        x0, y0, x1, y1 = rect
        px, py = to_px((x0 + x1) / 2, y1 - 25)
        d.text((px - 30, py), name.upper(), fill=(255, 255, 255), font=font)
    d.text((8, 8), "MAREVIA layout  N up  0.5 px/m   red = gated road   white = bridge   yellow = landmark", fill=(255, 255, 255), font=font)
    im.save(path)


SIDEWALK_KINDS = {"avenue", "street"}   # get 2.5 m sidewalks on both sides in the city
SIDEWALK_W = 2.5


SHOULDER = 10.0  # m of smooth blend from a road's graded corridor back into the land (minimum)
SHOULDER_MAX = 70.0  # m: deep cuts and fills blend over up to this far (about 1:1.5, ~34 degrees)
CUT_RATIO = 1.5      # m of shoulder per m of height difference between road grade and land
BLEND = 1.5      # m: soft-minimum scale where road corridors meet


def flatten_roads(h, g):
    """Grade a corridor under every road (not bridges): the surface runs straight between its
    junction heights, sidewalks included, with a smooth shoulder blending into the land.

    Where corridors meet, their grades are blended with a soft minimum over the distance to each
    road's edge (scale BLEND m): inside one road its own grade dominates, and two roads of
    different slope meet without a step. (Applying roads one after another let a later shoulder
    bend an earlier road; picking only the nearest road left steps where two grades met.)
    """
    best = np.full(h.shape, np.inf, dtype=np.float32)   # distance past the nearest road edge (m), < 0 inside
    wsum = np.zeros(h.shape, dtype=np.float32)
    tsum = np.zeros(h.shape, dtype=np.float32)
    csum = np.zeros(h.shape, dtype=np.float32)   # inside a corridor only the roads whose corridor it is count:
    cnum = np.zeros(h.shape, dtype=np.float32)   # a stacked hairpin leg 9 m below no longer tilts the road above
    near_grade = np.zeros(h.shape, dtype=np.float32)
    near_dist = np.full(h.shape, np.inf, dtype=np.float32)
    cores = []
    for s in g.segments:
        if s["bridge"]:
            continue
        ax, ay, az = g.nodes[s["a"]]
        bx, by, bz = g.nodes[s["b"]]
        half = s["width"] / 2.0 + (SIDEWALK_W if s["kind"] in SIDEWALK_KINDS else 0.0) + 1.0
        reach = half + SHOULDER_MAX
        x0, x1 = int(max(0, min(ax, bx) - reach)), int(min(W, max(ax, bx) + reach)) + 1
        y0, y1 = int(max(0, min(ay, by) - reach - Y_MIN)), int(min(H, max(ay, by) + reach - Y_MIN)) + 1
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy
        if x1 <= x0 or y1 <= y0 or L2 < 1e-6:
            continue
        yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
        yy += Y_MIN
        t = np.clip(((xx - ax) * dx + (yy - ay) * dy) / L2, 0.0, 1.0)
        edge = np.hypot(xx - (ax + t * dx), yy - (ay + t * dy)) - half
        k = np.exp(-np.clip(edge, -half, SHOULDER_MAX) / BLEND)
        wsum[y0:y1, x0:x1] += k
        # 20 cm below the road surface, so distant (coarser) landscape LODs never poke through.
        grade = az + t * (bz - az) - 0.2
        tsum[y0:y1, x0:x1] += k * grade
        cores.append((y0, y1, x0, x1, edge, grade))
        nearer = edge + half < near_dist[y0:y1, x0:x1]   # nearest centreline (a wide road must not win inside a narrow one)
        near_grade[y0:y1, x0:x1] = np.where(nearer, grade, near_grade[y0:y1, x0:x1])
        near_dist[y0:y1, x0:x1] = np.where(nearer, edge + half, near_dist[y0:y1, x0:x1])
        best[y0:y1, x0:x1] = np.minimum(best[y0:y1, x0:x1], edge)
    # Inside corridors, average only the roads whose grade is within 1 m of the road the cell is
    # most inside: junction approaches still blend, a leg passing metres above or below does not.
    for y0, y1, x0, x1, edge, grade in cores:
        use = (edge <= 0.0) & (np.abs(grade - near_grade[y0:y1, x0:x1]) < 1.0)
        csum[y0:y1, x0:x1] += np.where(use, grade, 0.0)
        cnum[y0:y1, x0:x1] += use
    target = np.where(wsum > 0, tsum / np.maximum(wsum, 1e-12), h)
    target = np.where(cnum > 0, csum / np.maximum(cnum, 1.0), target)
    # 1 inside a road, fading to 0 over a shoulder that widens with the cut or fill height, so the
    # pass road gets graded slopes instead of sheer walls (a fixed 10 m left 30 m cliffs).
    need = np.clip(np.abs(target - h) * CUT_RATIO, SHOULDER, SHOULDER_MAX)
    w = np.clip(1.0 - best / need, 0.0, 1.0)
    w = w * w * (3 - 2 * w)
    h[:] = h * (1 - w) + target * w
    return best


def box_blur(a, r):
    """Mean over a (2r+1)^2 window (separable cumulative sums, edges clamped)."""
    out = a
    for axis in (0, 1):
        p = np.pad(out, [(r + 1, r) if ax == axis else (0, 0) for ax in (0, 1)], mode="edge")
        c = np.cumsum(p, axis=axis, dtype=np.float64)
        hi = np.take(c, np.arange(2 * r + 1, c.shape[axis]), axis=axis)
        lo = np.take(c, np.arange(0, c.shape[axis] - 2 * r - 1), axis=axis)
        out = ((hi - lo) / (2 * r + 1)).astype(np.float32)
    return out


def relax_slopes(h, best, passes=((8, 3), (6, 3), (4, 2))):
    """Ease cliffs: where the land is steeper than ~45 degrees, blend toward a blurred copy of itself
    (radius r m, n times per pass), so ridged-noise crags and deep road cuts become steep slopes
    instead of sheer walls. Roads, their pavements and the water stay exactly as graded."""
    ys = np.arange(h.shape[0])[:, None] + Y_MIN
    xs = np.arange(h.shape[1])[None, :]
    rx = np.array([river_x(float(y)) for y in ys[:, 0]])[:, None]
    rhw = np.array([river_half_width(float(y)) for y in ys[:, 0]])[:, None]
    free = (best > 1.0) & (h > 1.5)
    free &= ~((np.abs(xs - rx) < rhw + 14.0) & (ys < DAM_Y + 30))
    rx0, ry0, rx1, ry1 = RESERVOIR
    free &= ~((xs > rx0 - 60) & (xs < rx1 + 60) & (ys > ry0 - 60) & (ys < ry1 + 60))
    # Feather the mask edge outward only: a blur reaching into the corridors tilted the outer lane of
    # roads beside drops by up to 2.4 m (cars slid into the parapets).
    free = box_blur(free.astype(np.float32), 3) * free
    for r, n in passes:
        for _ in range(n):
            gy, gx = np.gradient(h)
            steep = np.clip((np.hypot(gx, gy) - 0.9) / 0.6, 0.0, 1.0) * free  # 0 at ~42 deg, 1 at ~56 deg
            h[:] = h * (1.0 - steep) + box_blur(h, r) * steep
    return h


MAX_SLOPE = 1.6  # m of rise per m (~58 degrees): the steepest the landscape may be anywhere


def clamp_slopes(h, best, max_slope=MAX_SLOPE, max_iters=200):
    """Remove sheer walls that relax_slopes can't reach: cuts and fills beside protected ground
    (roads, the reservoir basin, the river at the dam) kept 1 m-wide steps up to 67 m tall, which
    render as torn, sky-coloured slivers (QA 2026-11-09). Every unprotected cell is held within
    max_slope of its eight neighbours (iterated to a fixed point), so each wall becomes a cut or
    fill slope that meets the protected level. Roads, pavements and water keep their exact levels."""
    ys = np.arange(h.shape[0])[:, None] + Y_MIN
    xs = np.arange(h.shape[1])[None, :]
    rx = np.array([river_x(float(y)) for y in ys[:, 0]])[:, None]
    rhw = np.array([river_half_width(float(y)) for y in ys[:, 0]])[:, None]
    rx0, ry0, rx1, ry1 = RESERVOIR
    fixed = (best <= 1.0) | (h <= 1.5)
    fixed |= (np.abs(xs - rx) < rhw) & (ys < DAM_Y + 30)  # the river channel itself
    fixed |= (xs > rx0) & (xs < rx1) & (ys > ry0) & (ys < ry1) & (h < make_heightmap.reservoir_level)  # under the lake
    free = ~fixed
    shifts = [(dy, dx, max_slope * math.hypot(dy, dx)) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dy or dx]
    for it in range(max_iters):
        p = np.pad(h, 1, mode="edge")
        lo = np.full(h.shape, np.inf)
        hi = np.full(h.shape, -np.inf)
        for dy, dx, d in shifts:
            n = p[1 + dy:1 + dy + h.shape[0], 1 + dx:1 + dx + h.shape[1]]
            np.minimum(lo, n + d, out=lo)
            np.maximum(hi, n - d, out=hi)
        new = np.where(free, np.maximum(np.minimum(h, lo), np.minimum(hi, lo)), h)
        change = float(np.abs(new - h).max())
        h[:] = new
        if change < 0.01:
            break
    return it + 1


WALL_STEP = 4.0      # m: retaining-wall panels are 4 m long (Tools/blender/KIT_SPEC.md)
WALL_CUT_MIN = 2.0   # m of ground above the road edge before it gets a cut wall
WALL_FILL_MIN = 2.5  # m of drop below the road edge before it gets a fill wall (with a 1 m parapet)
WALL_GAP = 0.9       # m from the road edge to a wall's road-side face (cut and fill)


def retaining_walls(terrain, g):
    """Retaining walls where the ground right beside a road edge jumps (INFRA_DESIGN.md Phase 3):
    stacked hairpin legs and other near-vertical steps the slope clamp can't soften. Fill walls carry
    a road over a drop (their top is a 1 m parapet); cut walls hold back ground rising at the edge.
    Material by era: stone on old roads, the coast road and the 1930s pass, concrete elsewhere.
    Returns runs: {"material", "type", "outward": [x, y] away from the road, "points": [[x, y, top_z,
    bottom_z], ...]} along the wall's road-side line."""
    def ground(x, y):
        xi = int(min(max(round(x), 0), W))
        yi = int(min(max(round(y - Y_MIN), 0), H))
        return float(terrain[yi, xi])

    degree = [0] * len(g.nodes)
    for sg in g.segments:
        degree[sg["a"]] += 1
        degree[sg["b"]] += 1
    half_w = []
    for sg in g.segments:
        half_w.append(sg["width"] / 2.0 + (SIDEWALK_W if sg["kind"] in SIDEWALK_KINDS else 0.0))
    # Segment index on a 50 m grid: a wall may not stand on another road.
    grid = {}
    for i, sg in enumerate(g.segments):
        a, b = g.nodes[sg["a"]], g.nodes[sg["b"]]
        for t in np.linspace(0.0, 1.0, max(2, int(math.dist(a[:2], b[:2]) / 25.0) + 1)):
            key = (int((a[0] + (b[0] - a[0]) * t) // 50), int((a[1] + (b[1] - a[1]) * t) // 50))
            grid.setdefault(key, set()).add(i)

    def on_other_road(x, y, own, z):
        own_ends = {g.segments[own]["a"], g.segments[own]["b"]}
        """A wall here would stand on another road at about its height (a junction). A road metres
        below or above is what the wall separates it from (stacked hairpin legs): keep the wall."""
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for i in grid.get((int(x // 50) + dx, int(y // 50) + dy), ()):
                    # The same road's neighbouring segments are not "another road": skipping walls
                    # near every bend left 8 m gaps in the parapets, and cars fell through them.
                    if i == own or {g.segments[i]["a"], g.segments[i]["b"]} & own_ends:
                        continue
                    a, b = g.nodes[g.segments[i]["a"]], g.nodes[g.segments[i]["b"]]
                    vx, vy = b[0] - a[0], b[1] - a[1]
                    t = max(0.0, min(1.0, ((x - a[0]) * vx + (y - a[1]) * vy) / max(vx * vx + vy * vy, 1e-9)))
                    if math.hypot(x - a[0] - t * vx, y - a[1] - t * vy) < half_w[i] + 1.0 and abs(a[2] + (b[2] - a[2]) * t - z) < 1.5:
                        return True
        return False

    def on_road_surface(x, y, margin=0.2):
        """True if (x, y) lies on any road's carriageway (any height, any road, its own included):
        on the inside of a bend a segment's wall line runs into the next segment's road, and where
        two roads part at a shallow angle one road's parapet stood across the other."""
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for i in grid.get((int(x // 50) + dx, int(y // 50) + dy), ()):
                    sg = g.segments[i]
                    a, b = g.nodes[sg["a"]], g.nodes[sg["b"]]
                    vx, vy = b[0] - a[0], b[1] - a[1]
                    t = max(0.0, min(1.0, ((x - a[0]) * vx + (y - a[1]) * vy) / max(vx * vx + vy * vy, 1e-9)))
                    if math.hypot(x - a[0] - t * vx, y - a[1] - t * vy) < sg["width"] / 2.0 + margin:
                        return True
        return False

    samples = []  # (segment, side, type, material, x, y, top, bottom, along)
    for i, sg in enumerate(g.segments):
        if sg["bridge"] or sg["kind"] == "pier":
            continue
        a, b = g.nodes[sg["a"]], g.nodes[sg["b"]]
        length = math.dist(a[:2], b[:2])
        if length < WALL_STEP:
            continue
        ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        clear_a = max(half_w) + 4.0 if degree[sg["a"]] > 2 else 0.0
        clear_b = max(half_w) + 4.0 if degree[sg["b"]] > 2 else 0.0
        district = district_at((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
        material = "Stone" if sg["kind"] in ("pass", "coast") or district == "CittaAlta" else "Concrete"
        d = clear_a
        while d <= length - clear_b:
            zr = a[2] + (b[2] - a[2]) * d / length
            for side in (-1.0, 1.0):
                rx, ry = -uy * side, ux * side
                # Walls stand 0.9 m beyond the edge (a shoulder to the parapet, as on real mountain
                # roads): at 0.3 m cars swinging wide out of a hairpin loop hit them.
                ex, ey = a[0] + ux * d + rx * (half_w[i] + WALL_GAP), a[1] + uy * d + ry * (half_w[i] + WALL_GAP)
                hp = ground(ex + rx * 1.5, ey + ry * 1.5)
                dz = hp - zr
                # A drop a few metres out is still a drop a car can fall into: fill if the lowest
                # ground within 4.5 m is far below the road.
                # The wall itself only reaches down to the ground at its foot (mostly a parapet).
                low = min(hp, ground(ex + rx * 3.0, ey + ry * 3.0), ground(ex + rx * 4.5, ey + ry * 4.5))
                foot = hp
                if dz <= WALL_CUT_MIN and low - zr < -WALL_FILL_MIN:
                    dz, foot = low - zr, min(hp, zr)
                # Fill walls stand 0.9 m out too: flatten_roads keeps each corridor flat to 1 m past
                # the edge, so the shoulder in front of the parapet no longer drops away.
                if dz > WALL_CUT_MIN:
                    kind, top, bottom = "cut", hp + 0.3, zr - 0.3
                elif dz < -WALL_FILL_MIN:
                    kind, top, bottom = "fill", zr + 1.0, foot - 0.5  # 1 m parapet: at 0.8 m wheels climbed it
                else:
                    continue
                if on_other_road(ex + rx * 0.25, ey + ry * 0.25, i, zr):
                    continue
                half = WALL_STEP / 2.0  # the panel reaches 2 m along the road each way
                if any(on_road_surface(ex + ux * f * half, ey + uy * f * half) for f in (-1.0, 0.0, 1.0)):
                    continue
                samples.append((i, side, kind, material, ex, ey, top, bottom, d, (round(rx, 4), round(ry, 4))))
            d += WALL_STEP
    # A cut wall where another road's fill wall already holds the step is a double wall: drop it.
    fills = {}
    for smp in samples:
        if smp[2] == "fill":
            fills.setdefault((int(smp[4] // 10), int(smp[5] // 10)), []).append(smp)
    def near_fill(smp):
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for f in fills.get((int(smp[4] // 10) + dx, int(smp[5] // 10) + dy), ()):
                    if f[0] != smp[0] and math.dist(f[4:6], smp[4:6]) < 4.0:
                        return True
        return False
    samples = [smp for smp in samples if smp[2] == "fill" or not near_fill(smp)]
    # Consecutive samples (same segment, side and type, 4 m apart) form a run.
    samples.sort(key=lambda smp: (smp[0], smp[1], smp[2], smp[8]))
    runs, cur = [], None
    for smp in samples:
        key = (smp[0], smp[1], smp[2])
        if cur is None or cur["key"] != key or smp[8] - cur["last"] > WALL_STEP + 0.01:
            cur = {"key": key, "material": smp[3], "type": smp[2], "outward": list(smp[9]), "points": [], "last": smp[8]}
            runs.append(cur)
        cur["points"].append([round(smp[4], 2), round(smp[5], 2), round(smp[6], 2), round(smp[7], 2)])
        cur["last"] = smp[8]
    out = [{"material": r["material"], "type": r["type"], "outward": r["outward"], "points": r["points"]}
           for r in runs if len(r["points"]) >= 2]
    # Runs stop up to a step short of each node, and on the outside of a bend the two segments'
    # wall lines diverge: join run ends that meet (same type, within 6 m) with a bridging panel, or
    # a car swinging wide finds the gap.
    ends = [(k, e) for k in range(len(out)) for e in (0, -1)]
    joined = set()
    bridges = []
    for i, (k, e) in enumerate(ends):
        best = None
        for k2, e2 in ends[i + 1:]:
            if k2 == k or out[k2]["type"] != out[k]["type"] or (k2, e2) in joined:
                continue
            p, q = out[k]["points"][e], out[k2]["points"][e2]
            dist = math.dist(p[:2], q[:2])
            o1, o2 = out[k]["outward"], out[k2]["outward"]
            if 0.05 < dist < 6.0 and o1[0] * o2[0] + o1[1] * o2[1] > 0.5 and (best is None or dist < best[0]):
                best = (dist, k2, e2)
        if best is None or (k, e) in joined:
            continue
        _, k2, e2 = best
        p, q = out[k]["points"][e], out[k2]["points"][e2]
        if any(on_road_surface(p[0] + (q[0] - p[0]) * f, p[1] + (q[1] - p[1]) * f) for f in (0.25, 0.5, 0.75)):
            continue  # a bridging panel across a road mouth
        joined |= {(k, e), (k2, e2)}
        o = [out[k]["outward"][0] + out[k2]["outward"][0], out[k]["outward"][1] + out[k2]["outward"][1]]
        n = math.hypot(*o)
        bridges.append({"material": out[k]["material"], "type": out[k]["type"], "outward": [round(o[0] / n, 4), round(o[1] / n, 4)],
                        "points": [p, q[:2] + [max(p[2], q[2]), min(p[3], q[3])]]})
    return out + bridges


# --- Buildings -----------------------------------------------------------------------------

BUILDING_STYLES = ["oldtown", "grid90", "grid120", "beach", "village", "terminal", "hills"]
# w, d, h ranges (m), gap range along the road, slope limit (m), extra setback (m)
BUILDING_PARAMS = {  # setback: m from the pavement edge (town frontages sit on the pavement)
    "oldtown":  dict(w=(7, 12), d=(9, 14), h=(9, 18), gap=(0, 0.3), slope=8, setback=(0.2, 0.4), extra=0),
    "grid90":   dict(w=(10, 20), d=(12, 20), h=(10, 24), gap=(0, 1.0), slope=5, setback=(0.2, 0.6), extra=0),
    "grid120":  dict(w=(20, 45), d=(20, 35), h=(7, 14), gap=(3, 8), slope=4, setback=(1.0, 3), extra=0),
    "beach":    dict(w=(15, 30), d=(12, 20), h=(12, 30), gap=(1, 4), slope=5, setback=(0.3, 1.0), extra=0),
    "village":  dict(w=(6, 10), d=(8, 12), h=(6, 10), gap=(0, 1.5), slope=6, setback=(0.2, 0.8), extra=0),
    "terminal": dict(w=(25, 50), d=(15, 30), h=(8, 16), gap=(6, 15), slope=3, setback=(1.5, 3), extra=0),
    "hills":    dict(w=(12, 20), d=(10, 16), h=(6, 9), gap=(25, 60), slope=6, setback=(1.5, 3), extra=6),
}


STOREY_H = 3.2       # m floor to floor (KIT_SPEC.md)
BAY = 2.0            # m: building widths and depths are whole multiples of this
MIN_FOOTPRINT = 4.0  # m


def rect_mask(cx, cy, w, d, yaw, grow=0.0):
    """Pixels (centres at integer m) inside a rotated rectangle: (y0, y1, x0, x1, bool mask) or None."""
    c, sn = math.cos(yaw), math.sin(yaw)
    hw, hd = w / 2 + grow, d / 2 + grow
    r = abs(c) * hw + abs(sn) * hd, abs(sn) * hw + abs(c) * hd
    x0, x1 = max(0, int(math.floor(cx - r[0]))), min(W, int(math.ceil(cx + r[0])) + 1)
    y0, y1 = max(0, int(math.floor(cy - r[1] - Y_MIN))), min(H, int(math.ceil(cy + r[1] - Y_MIN)) + 1)
    if x1 <= x0 or y1 <= y0:
        return None
    yy, xx = np.mgrid[y0:y1, x0:x1]
    px, py = xx - cx, (yy + Y_MIN) - cy
    u, v = px * c + py * sn, -px * sn + py * c
    return y0, y1, x0, x1, (np.abs(u) <= hw) & (np.abs(v) <= hd)


def place_buildings(terrain, best, g, rng, landmarks=LANDMARKS):
    ground_ok = terrain >= 1.5
    ys = np.arange(terrain.shape[0]) + Y_MIN
    rx = np.array([river_x(float(y)) for y in ys])
    rhw = np.array([river_half_width(float(y)) for y in ys]) + 6.0
    xs = np.arange(terrain.shape[1])
    ground_ok &= ~((np.abs(xs[None, :] - rx[:, None]) <= rhw[:, None]) & (ys[:, None] < DAM_Y + 30))
    for (x0, y0, x1, y1) in (DARSENA, RESERVOIR):
        ground_ok[max(0, int(y0 - Y_MIN)):int(y1 - Y_MIN) + 1, max(0, int(x0)):int(x1) + 1] = False
    # best = metres past (road + pavement + 1 m): frontages may come to 0.3 m from the pavement edge.
    FRONTAGE = -0.7
    blocked = ~ground_ok | (best < FRONTAGE)
    for _, _, (x, y), (fw, fd), _, _ in landmarks:
        blocked[max(0, int(y - fd / 2 - 8 - Y_MIN)):int(y + fd / 2 + 8 - Y_MIN) + 2,
                max(0, int(x - fw / 2 - 8)):int(x + fw / 2 + 8) + 2] = True
    # Bridges are not in `best` (their corridors aren't graded): keep their decks and approaches clear.
    for seg in g.segments:
        if not seg["bridge"]:
            continue
        ax, ay, _ = g.nodes[seg["a"]]
        bx, by, _ = g.nodes[seg["b"]]
        reach = seg["width"] / 2.0 + 6.0
        x0, x1 = int(max(0, min(ax, bx) - reach - 6)), int(min(W, max(ax, bx) + reach + 6)) + 1
        y0, y1 = int(max(0, min(ay, by) - reach - 6 - Y_MIN)), int(min(H, max(ay, by) + reach + 6 - Y_MIN)) + 1
        yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
        yy += Y_MIN
        dx, dy = bx - ax, by - ay
        t = np.clip(((xx - ax) * dx + (yy - ay) * dy) / (dx * dx + dy * dy), -0.05, 1.05)
        blocked[y0:y1, x0:x1] |= np.hypot(xx - (ax + t * dx), yy - (ay + t * dy)) < reach
    occ = np.zeros(terrain.shape, dtype=bool)
    rects = {n: (r, st) for n, r, _, st in DISTRICTS}
    out = []
    for seg in g.segments:
        if seg["bridge"]:
            continue
        ax, ay, _ = g.nodes[seg["a"]]
        bx, by, _ = g.nodes[seg["b"]]
        dist = district_at((ax + bx) / 2, (ay + by) / 2)
        if seg["kind"] == "pier" and dist != "MoloSalvi":  # the terminal's own road grid is pier segments
            continue
        if dist is None or rects[dist][1] == "mountain":
            continue
        style = rects[dist][1]
        P = BUILDING_PARAMS[style]
        rect = rects[dist][0]
        L = math.hypot(bx - ax, by - ay)
        if L < 1e-6:
            continue
        ux, uy = (bx - ax) / L, (by - ay) / L
        # Rasterise exactly what gets stored (rounded), so the self-check below sees the same masks.
        yaw = math.radians(round(math.degrees(math.atan2(uy, ux)), 1))
        edge = seg["width"] / 2 + (SIDEWALK_W if seg["kind"] in SIDEWALK_KINDS else 0.0) + 0.3
        for side in (1, -1):
            s = rng.uniform(0, 3)
            while s < L:
                # Footprints snap to the modular facade kit's 2 m bay grid (KIT_SPEC.md), min 4 m.
                w = max(MIN_FOOTPRINT, BAY * round(rng.uniform(*P["w"]) / BAY))
                d = max(MIN_FOOTPRINT, BAY * round(rng.uniform(*P["d"]) / BAY))
                gap = rng.uniform(*P["gap"])
                if s + w > L:
                    break
                off = edge + rng.uniform(*P["setback"]) + P["extra"] + d / 2
                cx = round(ax + ux * (s + w / 2) - uy * off * side, 2)
                cy = round(ay + uy * (s + w / 2) + ux * off * side, 2)
                w, d = float(w), float(d)
                ok = rect[0] <= cx <= rect[2] and rect[1] <= cy <= rect[3]
                if ok:
                    m = rect_mask(cx, cy, w, d, yaw)
                    if m is None:
                        ok = False
                    else:
                        y0, y1, x0, x1, mk = m
                        if not mk.any() or blocked[y0:y1, x0:x1][mk].any():
                            ok = False
                        else:
                            gr = terrain[y0:y1, x0:x1][mk]
                            if gr.max() - gr.min() > P["slope"]:
                                ok = False
                            else:
                                gm = min(gap * 0.5, 1.0)
                                m2 = rect_mask(cx, cy, w, d, yaw, gm) if gm > 0 else m
                                a0, a1, b0, b1, mk2 = m2
                                if occ[a0:a1, b0:b1][mk2].any():
                                    ok = False
                if ok:
                    occ[y0:y1, x0:x1] |= mk
                    ci = terrain[int(round(cy - Y_MIN)), int(round(cx))]
                    storeys = max(1, round(rng.uniform(*P["h"]) / STOREY_H))
                    out.append({"pos": [round(cx, 2), round(cy, 2)], "size": [w, d],
                                "yaw": round(math.degrees(yaw), 1), "base": round(float(gr.min()) - 1.0, 2),
                                "storeys": storeys,
                                "top": round(float(ci) + storeys * STOREY_H, 2),
                                "style": BUILDING_STYLES.index(style), "district": dist})
                    s += w + gap
                else:
                    s += max(3.0, w * 0.5)
    # self-check: re-rasterise the stored (rounded) records
    chk = np.zeros(terrain.shape, dtype=np.int16)
    for b in out:
        y0, y1, x0, x1, mk = rect_mask(b["pos"][0], b["pos"][1], b["size"][0], b["size"][1], math.radians(b["yaw"]))
        chk[y0:y1, x0:x1] += mk
    assert chk.max() <= 1, "buildings overlap"
    assert not (best[chk > 0] < FRONTAGE - 0.15).any(), "building inside road clearance"
    return out


# --- Infrastructure metadata (INFRA_DESIGN.md) -----------------------------------------------

PROFILES_PATH = Path(__file__).resolve().parent.parent / "SourceArt/Infra/road_profiles.json"
CORE = (700.0, 1230.0, 400.0, 290.0)  # Città Alta's walled core: ellipse centre x, y and radii (m), around Santa Lucia
PEDESTRIAN_PROFILES = {"passerella", "vicolo"}  # no cars: routing, lanes and traffic skip them
ERA_WEAR = {"roman": 0.85, "medieval": 0.8, "c19": 0.6, "postwar": 0.65, "modern": 0.3}


def in_core(x, y):
    cx, cy, rx, ry = CORE
    return ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0


def classify_segment(seg, mid, district, dead_end):
    """Profile, paving, era, parking and ZTL for one segment, from its geometry family and place.
    Geometry is untouched: Phase 3 rebuilds widths to the profiles (INFRA_DESIGN.md)."""
    kind, x, y = seg["kind"], mid[0], mid[1]
    core = district == "CittaAlta" and in_core(x, y)
    if seg["bridge"] == "Passerella":  # STORY.md: "two road bridges plus a footbridge"
        return "passerella", "stone_slab", "c19", "none", False
    if kind == "pier":
        return "banchina_porto", "concrete", "postwar", "none", False
    if kind == "coast":
        return "costiera", "asphalt", "postwar", "none", False
    if kind == "pass":
        return "montana_tornanti", "asphalt", "postwar", "none", False
    if kind == "avenue":
        if district in ("Lungomare", "BorgoMarino") and y < 300:
            return "lungomare", "asphalt", "c19", "blue", False
        return "viale", "asphalt", "c19", "blue", False
    if kind == "alley":
        if core:
            return "centro_storico", "lastricato", "medieval", "none", True
        if district == "CittaAlta":
            return "centro_storico", "sampietrini", "c19", "none", False
        return "urbana_locale", "asphalt", "postwar", "white", False
    # streets
    if core:
        return "centro_storico", "sampietrini", "medieval", "none", True
    if district == "CittaAlta":
        return "urbana_quartiere", "asphalt", "c19", "blue", False
    if district == "LeColline" and dead_end:
        return "strada_bianca", "gravel_white", "c19", "none", False
    if district in ("LeColline", "MonteFerrale"):
        return "locale_f2", "asphalt", "postwar", "none", False
    if district == "Lungomare":
        return "urbana_quartiere", "asphalt", "modern", "blue", False
    if district == "BorgoMarino":
        return "urbana_quartiere", "asphalt", "c19", "white", False
    return "urbana_quartiere", "asphalt", "postwar", "white", False


def tram_route(g, landmarks):
    """Planned tram (INFRA_DESIGN.md, Phase 7): Stazione Centrale to Città Alta's civic centre over
    Ponte San Rocco, preferring avenues, then streets; alleys only where nothing else connects."""
    import heapq
    pos = {lm[0]: lm[2] for lm in landmarks}  # (name, district, pos, footprint, height, kind)
    def nearest(p):
        return min(range(len(g.nodes)), key=lambda i: (g.nodes[i][0] - p[0]) ** 2 + (g.nodes[i][1] - p[1]) ** 2)
    a, b = nearest(pos["StazioneCentrale"]), nearest(pos["PoliceHQ"])
    adj = {}
    for i, sg in enumerate(g.segments):
        if sg["kind"] in ("avenue", "street", "alley") and not sg["gate"]:
            adj.setdefault(sg["a"], []).append((sg["b"], i))
            adj.setdefault(sg["b"], []).append((sg["a"], i))
    dist, came, todo = {a: 0.0}, {a: None}, [(0.0, a)]
    while todo:
        d, n = heapq.heappop(todo)
        if n == b:
            break
        if d > dist[n]:
            continue
        for m, i in adj.get(n, []):
            nd = d + math.dist(g.nodes[n][:2], g.nodes[m][:2]) * {"avenue": 0.6, "street": 1.0}.get(g.segments[i]["kind"], 3.0)
            if nd < dist.get(m, 1e18):
                dist[m], came[m] = nd, (n, i)
                heapq.heappush(todo, (nd, m))
    segs, n = set(), b
    while came.get(n):
        n, i = came[n]
        segs.add(i)
    return segs


def annotate_networks(g, landmarks, rng):
    """Add INFRA_DESIGN.md metadata to every road segment and return (profiles, junctions)."""
    profiles = {k: v for k, v in json.loads(PROFILES_PATH.read_text()).items() if not k.startswith("_")}
    degree = [0] * len(g.nodes)
    for sg in g.segments:
        degree[sg["a"]] += 1
        degree[sg["b"]] += 1
    tram = tram_route(g, landmarks)
    for i, sg in enumerate(g.segments):
        a, b = g.nodes[sg["a"]], g.nodes[sg["b"]]
        mid = ((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
        district = district_at(*mid)
        profile, paving, era, parking, ztl = classify_segment(sg, mid, district, min(degree[sg["a"]], degree[sg["b"]]) == 1)
        if profile not in profiles:
            raise ValueError(f"segment {i}: profile {profile} missing from {PROFILES_PATH}")
        sg.update(profile=profile, paving=paving, era=era, parking=parking, ztl=ztl, one_way=False,
                  pedestrian=profile in PEDESTRIAN_PROFILES,
                  tram=i in tram, wear=round(min(1.0, max(0.0, ERA_WEAR[era] + rng.uniform(-0.15, 0.15))), 2))
        # Italian limits: the profile's design maximum (urban 50), Zona 30 inside the walls.
        sg["speed_kmh"] = min(sg["speed_kmh"], profiles[profile]["design_speed_max_kmh"], 30 if ztl else 999)

    junctions = []
    by_node = {}
    for i, sg in enumerate(g.segments):
        by_node.setdefault(sg["a"], []).append(sg)
        by_node.setdefault(sg["b"], []).append(sg)
    piazza = min(range(len(g.nodes)), key=lambda i: (g.nodes[i][0] - 702) ** 2 + (g.nodes[i][1] - 1224) ** 2)  # Santa Lucia
    signals = []
    for n, segs in by_node.items():
        x, y = g.nodes[n][0], g.nodes[n][1]
        district = district_at(x, y)
        ztl = {sg["ztl"] for sg in segs}
        bridge = {bool(sg["bridge"]) for sg in segs}
        avenues = sum(sg["kind"] == "avenue" for sg in segs)
        if len(ztl) == 2:
            kind = "ztl_gate"
        elif len(bridge) == 2:
            kind = "bridge_head"
        elif n == piazza and len(segs) >= 3:
            kind = "piazza"
        elif len(segs) < 3:
            continue
        elif avenues >= 3 and district in ("PortoVecchio", "LeFornaci", "Lungomare"):
            kind = "roundabout"  # two avenues crossing in the periphery
        elif avenues >= 3 or (avenues == 2 and len(segs) >= 4 and any(sg["kind"] == "street" for sg in segs)
                              and all(math.dist(g.nodes[n][:2], g.nodes[m][:2]) > SIGNAL_SPACING for m in signals)):
            kind = "signalized"  # corso junctions: a street crossing an avenue, at most one per SIGNAL_SPACING
            signals.append(n)
        elif len(segs) == 3 and district in ("CittaAlta", "LeColline", "BorgoMarino", "MonteFerrale"):
            kind = "blind_t"
        else:
            kind = "priority"
        # Signals, ZTL panels, STOP and give-way signs are placed by BuildStreetFixtures (Phase 5);
        # roundabouts and gates are still only planned.
        junctions.append({"node": n, "type": kind, "planned": True})
    return profiles, junctions


def water_network(river_profile, reservoir_level):
    """Water bodies with depth, flow and edge treatments (INFRA_DESIGN.md "Water"). Edge ranges are
    metres along the centreline (rivers) or along x (coast and basins)."""
    along, total = [0.0], 0.0
    for p, q in zip(RIVER, RIVER[1:]):
        total += math.dist(p, q)
        along.append(total)
    def at_y(yv):  # metres along the river centreline where it reaches y
        for (p, q), a0, a1 in zip(zip(RIVER, RIVER[1:]), along, along[1:]):
            if p[1] <= yv <= q[1]:
                return round(a0 + (a1 - a0) * (yv - p[1]) / max(q[1] - p[1], 1e-6), 1)
        return round(total, 1)
    rx0, ry0, rx1, ry1 = RESERVOIR
    return {"bodies": [
        {"id": "GolfoDiMarevia", "type": "sea", "depth_m": 30.0, "flow_mps": 0.0, "level_m": SEA_LEVEL,
         "edges": [{"treatment": "breakwater", "side": "all", "from_m": MOLO[0], "to_m": BREAKWATER[-1][0]},
                   {"treatment": "quay", "side": "all", "from_m": DARSENA[0], "to_m": X(1000)},
                   {"treatment": "delta", "side": "all", "from_m": X(1000) - 40, "to_m": X(1160)},
                   {"treatment": "beach_sand", "side": "all", "from_m": X(1160), "to_m": W}]},
        {"id": "Darsena", "type": "port", "depth_m": 6.0, "flow_mps": 0.0, "level_m": SEA_LEVEL,
         "edges": [{"treatment": "quay", "side": "all", "from_m": DARSENA[0], "to_m": DARSENA[2]}]},
        {"id": "FiumeArdo", "type": "river", "depth_m": 2.5, "flow_mps": 0.8, "level_m": SEA_LEVEL, "rises_with_weather": True,
         "centerline": [list(p) for p in RIVER],
         "edges": [{"treatment": "embankment_wall", "side": "both", "from_m": at_y(40), "to_m": at_y(1650)},
                   {"treatment": "walkway_low", "side": "both", "from_m": at_y(300), "to_m": at_y(1500)},
                   {"treatment": "natural_bank", "side": "both", "from_m": at_y(1650), "to_m": at_y(2040)},
                   {"treatment": "cliff", "side": "both", "from_m": at_y(2040), "to_m": round(total, 1)}]},
        {"id": "FociArdo", "type": "estuary", "depth_m": 1.0, "flow_mps": 0.3, "level_m": SEA_LEVEL,
         "edges": [{"treatment": "delta", "side": "both", "from_m": 0.0, "to_m": at_y(40)}]},
        {"id": "LagoFerrale", "type": "reservoir", "depth_m": 25.0, "flow_mps": 0.0, "level_m": reservoir_level,
         "edges": [{"treatment": "dam", "side": "south", "from_m": rx0, "to_m": rx1},
                   {"treatment": "natural_bank", "side": "north", "from_m": rx0, "to_m": rx1}]},
    ]}


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "SourceArt/World")
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(SEED)
    terrain = make_heightmap(np.random.default_rng(SEED))
    g = build_roads(terrain, rng)
    landmarks = settle_landmarks(terrain, g)   # may remove minor streets under big landmarks
    drop_orphan_nodes(g)
    still_steep = limit_grades(g)
    best = flatten_roads(terrain, g)
    relax_slopes(terrain, best)
    clamp_iters = clamp_slopes(terrain, best)
    for n in g.nodes:  # the road surface sits 20 cm above the flattened ground
        n[2] = round(max(float(terrain[int(round(n[1] - Y_MIN)), int(round(n[0]))]) + 0.2, 1.0), 2)

    profiles, junctions = annotate_networks(g, landmarks, random.Random(SEED + 11))
    walls = retaining_walls(terrain, g)

    t0 = time.time()
    buildings = place_buildings(terrain, best, g, random.Random(SEED + 7), landmarks)
    buildings += landmark_buildings(terrain, landmarks)
    bt = time.time() - t0

    # Connectivity: report pieces of the road network that can't be reached from the start.
    adj = {i: set() for i in range(len(g.nodes))}
    for s in g.segments:
        adj[s["a"]].add(s["b"])
        adj[s["b"]].add(s["a"])
    start = min(range(len(g.nodes)), key=lambda i: (g.nodes[i][0] - X(520)) ** 2 + (g.nodes[i][1] - 1290) ** 2)
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
                       "ground_z": ground(p[0], p[1])} for n, dst, p, f, h, k in landmarks],
        "gates": [{"tag": t, "opens_with": m, "while_locked": w} for t, m, w in GATES],
        "roads": {"kinds": ROAD_KINDS, "sidewalk_kinds": sorted(SIDEWALK_KINDS), "sidewalk_width": SIDEWALK_W,
                  "nodes": g.nodes, "segments": g.segments, "profiles": profiles, "junctions": junctions},
        "retaining_walls": walls,
        # Rail (regional line, tram, funicular) is designed in INFRA_DESIGN.md and waits for approval.
        "rail": {"lines": []},
        "water_network": water_network(make_heightmap.river_profile, make_heightmap.reservoir_level),
        "building_styles": BUILDING_STYLES,
        "buildings": buildings,
        "start": {"pos": [X(520), 1290], "district": "CittaAlta", "ground_z": ground(X(520), 1290)},
    }
    (out / "marevia_layout.json").write_text(json.dumps(layout, indent=1))

    hm = np.clip((terrain - HEIGHT_MIN) / (HEIGHT_MAX - HEIGHT_MIN) * 65535.0, 0, 65535).astype(np.uint16)
    Image.fromarray(hm[::-1]).save(out / "marevia_height.png")
    render_preview(out / "marevia_preview.png", terrain, g, buildings, landmarks)

    kinds = {}
    for s in g.segments:
        kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
    gated = sum(1 for s in g.segments if s["gate"])
    prof = {}
    for sg in g.segments:
        prof[sg["profile"]] = prof.get(sg["profile"], 0) + 1
    wall_m = sum(WALL_STEP * (len(w["points"]) - 1) for w in walls)
    print(f"retaining walls: {len(walls)} runs, {wall_m:.0f} m "
          f"({sum(1 for w in walls if w['type'] == 'fill')} fill, {sum(1 for w in walls if w['type'] == 'cut')} cut)")
    print(f"profiles {prof}; ZTL {sum(sg['ztl'] for sg in g.segments)}, tram {sum(sg['tram'] for sg in g.segments)}, junctions {len(junctions)}")
    print(f"nodes {len(g.nodes)}, segments {len(g.segments)} {kinds}, gated {gated}, "
          f"bridges {sum(1 for s in g.segments if s['bridge'])}, unreachable nodes {len(unreachable)}")
    per = {}
    for b in buildings:
        per[b["district"]] = per.get(b["district"], 0) + 1
    print(f"buildings {per} total {len(buildings)} in {bt:.1f}s")
    print(f"slope clamp: {clamp_iters} passes to hold every free cell under {MAX_SLOPE} m/m; "
          f"{still_steep} road segment(s) still over their grade limit")
    print(f"terrain {terrain.min():.1f}..{terrain.max():.1f} m; heightmap {HEIGHT_MIN:.0f} m = 0, {HEIGHT_MAX:.0f} m = 65535")


if __name__ == "__main__":
    main()
