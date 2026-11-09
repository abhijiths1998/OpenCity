"""Top-down network plan for INFRA_DESIGN.md: built roads coloured by profile, ZTL streets, junction
types, the planned tram corridor and water bodies from SourceArt/World/marevia_layout.json, plus the
proposed infrastructure from SourceArt/Infra/proposed_networks.json drawn dashed.

    python3 -I Tools/render_network_plan.py [out.png]   (default SourceArt/Infra/network_plan.png)
"""

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
LAYOUT = ROOT / "SourceArt/World/marevia_layout.json"
HEIGHT = ROOT / "SourceArt/World/marevia_height.png"
PROPOSED = ROOT / "SourceArt/Infra/proposed_networks.json"
SIZE = 1800          # px for the 3024 m map
MARGIN_M = 700       # show proposals past the east edge
PROFILE_COLOURS = {
    "autostrada": (40, 140, 60), "viale": (200, 60, 50), "lungomare": (230, 110, 40), "urbana_quartiere": (90, 90, 95),
    "urbana_locale": (140, 140, 145), "centro_storico": (150, 90, 40), "vicolo": (120, 70, 30), "costiera": (40, 90, 170),
    "montana_tornanti": (120, 60, 150), "locale_f2": (70, 120, 200), "strada_bianca": (235, 230, 210), "banchina_porto": (60, 60, 60),
}
JUNCTION_COLOURS = {"roundabout": (40, 140, 60), "signalized": (220, 30, 30), "ztl_gate": (255, 200, 0), "piazza": (255, 255, 255),
                    "bridge_head": (30, 30, 30), "blind_t": None, "priority": None}


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "SourceArt/Infra/network_plan.png"
    lay = json.loads(LAYOUT.read_text())
    prop = json.loads(PROPOSED.read_text())
    ox, oy = lay["origin"]
    w_m, h_m = lay["size"]
    scale = SIZE / w_m
    width = int((w_m + MARGIN_M) * scale)

    def px(x, y):
        return ((x - ox) * scale, (oy + h_m - y) * scale)

    # Hillshade background from the heightmap (row 0 = north).
    lo, hi = lay["height_range"]
    h = lo + np.asarray(Image.open(HEIGHT), dtype=np.float64) / 65535.0 * (hi - lo)
    h = np.asarray(Image.fromarray(h.astype(np.float32)).resize((SIZE, SIZE), Image.BILINEAR), dtype=np.float64)
    gy, gx = np.gradient(h, w_m / SIZE)
    shade = np.clip(0.75 + (gx * -0.5 + gy * 0.5) * 0.6, 0.3, 1.0)
    land = np.clip((h - lo) / (hi - lo), 0, 1)
    rgb = np.stack([200 + 30 * land, 205 + 20 * land, 180 + 10 * land], -1) * shade[..., None]
    sea = h <= lay["sea_level"] + 0.3
    rgb[sea] = (150, 190, 215)
    img = Image.new("RGB", (width, SIZE), (215, 215, 210))  # outside the map (not sea)
    img.paste(Image.fromarray(rgb.astype(np.uint8)), (0, 0))
    d = ImageDraw.Draw(img)
    font = ImageFont.load_default()
    d.text((SIZE + 20, SIZE - 30), "outside the playable map", fill=(90, 90, 90), font=font)

    # Water: river centreline as a band.
    river = next(b for b in lay["water_network"]["bodies"] if b["id"] == "FiumeArdo")
    d.line([px(*p) for p in river["centerline"]], fill=(110, 160, 200), width=max(2, int(50 * scale)))

    # Roads by profile; ZTL outlined in yellow; tram in magenta.
    nodes = lay["roads"]["nodes"]
    for s in lay["roads"]["segments"]:
        a, b = px(*nodes[s["a"]][:2]), px(*nodes[s["b"]][:2])
        wpx = max(2, int(s["width"] * scale * 1.6))
        if s["ztl"]:
            d.line([a, b], fill=(255, 210, 0), width=wpx + 3)
        d.line([a, b], fill=PROFILE_COLOURS.get(s["profile"], (0, 0, 0)), width=wpx)
    for s in lay["roads"]["segments"]:
        if s["tram"]:
            d.line([px(*nodes[s["a"]][:2]), px(*nodes[s["b"]][:2])], fill=(220, 0, 160), width=3)
    for j in lay["roads"]["junctions"]:
        c = JUNCTION_COLOURS.get(j["type"])
        if c:
            x, y = px(*nodes[j["node"]][:2])
            r = 6 if j["type"] in ("roundabout", "piazza") else 4
            d.ellipse([x - r, y - r, x + r, y + r], fill=c, outline=(0, 0, 0))

    # Proposed (dashed).
    def dashed(points, colour, wpx, dash=14, gap=8):
        pts = [px(*p) for p in points]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            seg = math.hypot(x1 - x0, y1 - y0)
            t = 0.0
            while t < seg:
                t1 = min(seg, t + dash)
                d.line([(x0 + (x1 - x0) * t / seg, y0 + (y1 - y0) * t / seg), (x0 + (x1 - x0) * t1 / seg, y0 + (y1 - y0) * t1 / seg)],
                       fill=colour, width=wpx)
                t = t1 + gap

    a = prop["autostrada"]
    dashed(a["points"], PROFILE_COLOURS["autostrada"], 7)
    dashed(a["link"], PROFILE_COLOURS["autostrada"], 4)
    for key, label in (("toll_plaza", "Casello"), ("interchange", "trumpet")):
        x, y = px(*a[key]["at"])
        d.rectangle([x - 7, y - 7, x + 7, y + 7], outline=(40, 140, 60), width=3)
        d.text((x + 9, y - 6), label, fill=(0, 60, 0), font=font)
    for line in prop["rail"]:
        if "points" not in line:
            continue
        dashed(line["points"], (30, 30, 30), 4, dash=10, gap=6)
        dashed(line["tunnel"], (120, 120, 120), 4, dash=4, gap=6)
        for st in line["stations"]:
            x, y = px(*st["at"])
            d.rectangle([x - 6, y - 4, x + 6, y + 4], fill=(30, 30, 30))
            d.text((x + 8, y - 12), st["name"], fill=(0, 0, 0), font=font)
    t = prop["torrent"]
    dashed(t["points"], (150, 120, 80), 5)
    dashed(t["culvert_to_sea"], (150, 120, 80), 2, dash=4, gap=8)
    cx, cy, rx, ry = 700.0, 1230.0, 400.0, 290.0  # walled core (make_marevia_layout.CORE)
    ring = [(cx + (rx + 45) * math.cos(k / 48 * math.tau), cy + (ry + 45) * math.sin(k / 48 * math.tau)) for k in range(49)]
    dashed(ring, PROFILE_COLOURS["viale"], 4)
    wall = [(cx + rx * math.cos(k / 64 * math.tau), cy + ry * math.sin(k / 64 * math.tau)) for k in range(65)]
    d.line([px(*p) for p in wall], fill=(110, 70, 40), width=2)

    # District names.
    for dist in lay["districts"]:
        x0, y0, x1, y1 = dist["rect"]
        x, y = px((x0 + x1) / 2, (y0 + y1) / 2)
        d.text((x - 30, y), dist["name"], fill=(20, 20, 20), font=font)

    # Legend.
    lx, ly = SIZE + 20, 20
    d.rectangle([SIZE + 10, 10, width - 10, 470], fill=(250, 250, 245), outline=(0, 0, 0))
    d.text((lx, ly), "MAREVIA NETWORK PLAN (draft, Phase 1)", fill=(0, 0, 0), font=font)
    ly += 20
    for name, c in PROFILE_COLOURS.items():
        d.line([(lx, ly + 6), (lx + 30, ly + 6)], fill=c, width=5)
        d.text((lx + 38, ly), name, fill=(0, 0, 0), font=font)
        ly += 16
    for text, c in (("ZTL (yellow outline)", (255, 210, 0)), ("tram corridor (planned)", (220, 0, 160)),
                    ("dashed = proposed, not built", (0, 0, 0)), ("rail (dashed black, grey = tunnel)", (30, 30, 30)),
                    ("walled core / ring viale", (110, 70, 40)), ("torrent / culvert", (150, 120, 80))):
        d.line([(lx, ly + 6), (lx + 30, ly + 6)], fill=c, width=4)
        d.text((lx + 38, ly), text, fill=(0, 0, 0), font=font)
        ly += 16
    for name, c in JUNCTION_COLOURS.items():
        if c:
            d.ellipse([lx + 10, ly + 2, lx + 20, ly + 12], fill=c, outline=(0, 0, 0))
            d.text((lx + 38, ly), name, fill=(0, 0, 0), font=font)
            ly += 16
    img.save(out)
    print(f"network plan -> {out} ({img.size[0]}x{img.size[1]})")


if __name__ == "__main__":
    main()
