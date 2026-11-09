"""Camera bookmarks for the visual-QA loop: /Game/Dev/DA_QABookmarks (UOCCameraBookmarks).

  scripts/run_python.sh Content/Python/oc_qa_bookmarks.py

Three per district, picked from the road graph so they follow the layout:
  Street    1.7 m eye height on the pavement of the district's longest street, looking along it
  Driving   chase-camera height on its longest drivable road, looking down the lane
  Elevated  45 m up, 160 m outside the district centre, looking across it
"""

import json
import math
import os

import unreal

import oc_lib as L

TAG = "oc_qa"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def cm(x, y, z):
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def yaw(dx, dy):
    return math.degrees(math.atan2(-dy, dx))


def mark(name, district, kind, pos, rot, fov=75.0):
    b = unreal.OCCameraBookmark()
    L.set_props(b, name=name, district=district, kind=kind, location=pos, rotation=rot, fov=fov)
    return b


def main():
    with open(os.path.join(PROJECT_DIR, "SourceArt/World/marevia_layout.json"), encoding="utf-8") as f:
        layout = json.load(f)
    nodes = layout["roads"]["nodes"]
    segs = layout["roads"]["segments"]
    marks = []
    for d in layout["districts"]:
        name = d["name"]
        x0, y0, x1, y1 = d["rect"]
        inside = lambda p: x0 <= p[0] <= x1 and y0 <= p[1] <= y1

        def longest(kinds):
            best, blen = None, 0.0
            for s in segs:
                a, b = nodes[s["a"]], nodes[s["b"]]
                if s["kind"] in kinds and not s["bridge"] and inside(a) and inside(b):
                    length = math.dist(a[:2], b[:2])
                    if length > blen:
                        best, blen = s, length
            return best

        street = longest({"street", "avenue", "coast", "pier", "pass"}) or longest({"alley"})
        drive = longest({"avenue", "coast", "pass", "street", "pier"}) or street
        if street:
            a, b = nodes[street["a"]], nodes[street["b"]]
            ux, uy = (b[0] - a[0]), (b[1] - a[1])
            n = math.hypot(ux, uy)
            ux, uy = ux / n, uy / n
            side = street["width"] / 2 - 0.8  # on the road by the kerb: pavements have trees and lamps
            px, py = a[0] + ux * n * 0.25 - uy * side, a[1] + uy * n * 0.25 + ux * side
            marks.append(mark(f"{name}_Street", name, "Street", cm(px, py, a[2] + 0.3 + 1.7), unreal.Rotator(0.0, 2.0, yaw(ux, uy))))
        if drive:
            a, b = nodes[drive["a"]], nodes[drive["b"]]
            ux, uy = (b[0] - a[0]), (b[1] - a[1])
            n = math.hypot(ux, uy)
            ux, uy = ux / n, uy / n
            lane = drive["width"] / 4
            px, py = a[0] + ux * n * 0.15 + uy * lane, a[1] + uy * n * 0.15 - ux * lane
            marks.append(mark(f"{name}_Driving", name, "Driving", cm(px, py, a[2] + 2.2), unreal.Rotator(0.0, -4.0, yaw(ux, uy)), 80.0))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        near = min(nodes, key=lambda p: math.dist(p[:2], (cx, cy)))
        ex, ey = cx - 160.0, cy - 160.0
        h = near[2] + 45.0
        pitch = -math.degrees(math.atan2(45.0, math.dist((ex, ey), (cx, cy))))
        marks.append(mark(f"{name}_Elevated", name, "Elevated", cm(ex, ey, h), unreal.Rotator(0.0, pitch, yaw(cx - ex, cy - ey)), 70.0))
    asset = L.data_asset("DA_QABookmarks", "/Game/Dev", unreal.OCCameraBookmarks, TAG)
    asset.set_editor_property("bookmarks", marks)
    L.save(asset)
    L.log(TAG, f"{len(marks)} bookmarks in {asset.get_path_name()}")


main()
