"""Check the saved Marevia map against the M3 acceptance list (ROADMAP.md), without loading actors.

  scripts/run_python.sh Content/Python/oc_m3_verify.py

Reads World Partition actor descriptors, so it sees every actor whatever is streamed in.
Raises (non-zero exit) if any check fails.
"""

import json
import os

import unreal

import oc_lib as L

MAP_PATH = "/Game/Maps/Marevia/L_Marevia"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
failures = []


def check(name, ok, detail=""):
    L.log("oc_m3_verify", f"{'PASS' if ok else 'FAIL'} {name}{': ' + str(detail) if detail else ''}")
    if not ok:
        failures.append(name)


def main():
    with open(os.path.join(PROJECT_DIR, "SourceArt/World/marevia_layout.json"), encoding="utf-8") as f:
        layout = json.load(f)
    unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    by_label = {}
    for d in descs:
        by_label.setdefault(str(d.label), []).append(d)
    labels = list(by_label)
    check("actor descriptors", len(descs) > 0, len(descs))

    # Every story landmark: a named grey-box, centred in its district.
    rects = {d["name"]: d["rect"] for d in layout["districts"]}
    for lm in layout["landmarks"]:
        main = by_label.get(f"LM_{lm['name']}")
        if not main:
            check(f"landmark {lm['name']}", False, "missing")
            continue
        c = main[0].bounds.get_center() if hasattr(main[0].bounds, "get_center") else (main[0].bounds.min + main[0].bounds.max) * 0.5
        x, y = c.x / 100.0, -c.y / 100.0
        x0, y0, x1, y1 = rects[lm["district"]]
        inside = x0 - 30 <= x <= x1 + 30 and y0 - 30 <= y <= y1 + 30
        if not inside and lm["district"] == "MoloSalvi":  # the lighthouse stands on the Molo's breakwater
            (bx0, by0), (bx1, by1) = layout["water"]["breakwater"]
            inside = min(bx0, bx1) - 20 <= x <= max(bx0, bx1) + 20 and min(by0, by1) - 20 <= y <= max(by0, by1) + 20
        check(f"landmark {lm['name']} in {lm['district']}", inside, f"({x:.0f}, {y:.0f})")

    # Locked areas, water, roads, buildings, bridges.
    for dist in layout["districts"]:
        if dist["unlock"]:
            check(f"locked volume {dist['name']}", f"Locked_{dist['name']}" in by_label)
    check("sea volume", "Water_Sea" in by_label)
    check("reservoir volume", "Water_Reservoir" in by_label)
    rivers = sum(1 for lb in labels if lb.startswith("Water_River_"))
    check("river volumes", rivers >= 100, rivers)
    roads = sum(1 for lb in labels if lb.startswith("Roads_"))
    check("road chunks", roads >= 70, roads)
    buildings = sum(1 for lb in labels if lb.startswith(("Buildings_", "BLOCKOUT_Buildings_")))  # renamed in M4a step 4
    check("building chunks", buildings >= 30, buildings)
    for s in layout["roads"]["segments"]:
        if s["bridge"]:
            check(f"bridge {s['bridge']}", any(lb.startswith(f"Bridge_{s['bridge']}") for lb in labels))

    # Water surfaces and scenery (oc_m3_water.py), and the perf camera loop.
    check("sea surface", "Water_SeaSurface" in by_label)
    check("reservoir surface", "Water_ReservoirSurface" in by_label)
    river_surfaces = sum(1 for lb in labels if lb.startswith("Water_RiverSurface_"))
    check("river surfaces", river_surfaces >= 90, river_surfaces)
    check("distant peaks", sum(1 for lb in labels if lb.startswith("Scenery_Peak_")) >= 10)
    check("Isola Ventura", any(lb.startswith("Scenery_IsolaVentura_Fortress") for lb in labels))
    check("perf camera loop", "PerfCameraPath_Marevia" in by_label)

    # HLOD: the project's two layers are the world's default, and HLOD actors exist once built.
    hlod0 = unreal.load_asset("/Game/World/HLOD/HLOD0_Instanced")
    check("HLOD layer", hlod0 is not None)
    hlod_actors = sum(1 for d in descs if "WorldPartitionHLOD" in str(d.native_class))
    if os.environ.get("OC_VERIFY_HLOD") == "1":
        check("HLOD actors built (scripts/build_hlod.sh)", hlod_actors > 0, hlod_actors)
    else:
        L.log("oc_m3_verify", f"INFO HLOD actors: {hlod_actors} (set OC_VERIFY_HLOD=1 to require them)")

    # Gates: each gate's locked props are in its Data Layer; moving bridges also have an open state.
    per_layer = {}
    for d in descs:
        for layer in d.data_layer_assets:
            name = layer.export_text().strip("'\"").split(".")[-1]
            per_layer[name] = per_layer.get(name, 0) + 1
    for gate in layout["gates"]:
        name = gate["tag"].split(".")[1]
        check(f"gate {name} locked props", per_layer.get(f"DL_Gate_{name}_Locked", 0) > 0, per_layer.get(f"DL_Gate_{name}_Locked", 0))
        if name in ("PonteMobile", "Molo"):
            check(f"gate {name} open state", per_layer.get(f"DL_Gate_{name}_Open", 0) > 0, per_layer.get(f"DL_Gate_{name}_Open", 0))

    # Progression data and settings.
    unlocks = unreal.load_asset("/Game/Core/Progression/DA_AreaUnlocks")
    areas = unlocks.get_editor_property("areas") if unlocks else []
    check("unlock data", len(areas) == len(layout["districts"]) + len(layout["gates"]), len(areas))
    for e in areas:
        tag = str(e.get_editor_property("area").get_editor_property("tag_name"))
        if tag.startswith("Gate."):
            check(f"unlock {tag} has layers", len(e.get_editor_property("layers_while_locked")) > 0)
    open_at_start = [str(e.get_editor_property("area").get_editor_property("tag_name")) for e in areas
                     if str(e.get_editor_property("required_milestone").get_editor_property("tag_name")) in ("", "None")]
    check("new game opens only Città Alta and Le Fornaci", sorted(open_at_start) == ["Area.CittaAlta", "Area.LeFornaci"], open_at_start)
    roads_asset = unreal.load_asset("/Game/World/DA_MareviaRoads")
    graph = json.loads(roads_asset.get_editor_property("layout_json")) if roads_asset else {}
    check("road graph asset", len(graph.get("roads", {}).get("nodes", [])) == len(layout["roads"]["nodes"]))
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/OpenCity.OCProgressionSettings"))
    check("settings: unlock data", "DA_AreaUnlocks" in str(settings.get_editor_property("unlock_data")))
    check("settings: road graph", "DA_MareviaRoads" in str(settings.get_editor_property("road_graph")))

    if failures:
        raise RuntimeError(f"{len(failures)} check(s) failed: {', '.join(failures)}")
    L.log("oc_m3_verify", "all checks passed")


main()
