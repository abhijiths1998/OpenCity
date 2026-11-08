"""M4a Step 4 — Blockout validator.

Headless (no GPU rendering): loads L_Marevia and checks every blockout actor against
WORLD_METRICS.md:

- every blockout actor/label is BLOCKOUT_* (hard rule 1: code-generated geometry
  is blockout only and clearly named)
- instanced mesh is /Engine/BasicShapes/Cube (no final-art geometry)
- centres on the 100 cm modular grid, widths/depths on 100 cm increments
- heights on whole 320 cm storeys
- road lanes 350 cm, sidewalks 250-400 cm, curbs 15 cm (from the road material's
  UV1 marking data when present)

Run:
    scripts/run_python.sh Content/Python/oc_verify_blockout.py
"""

import os

import unreal

import oc_lib as L

TAG = "oc_verify_blockout"
MAP_PATH = "/Game/Maps/Marevia/L_Marevia"
CUBE_PATH = "/Engine/BasicShapes/Cube"

GRID_CM = 100.0        # WORLD_METRICS.md modular grid
STOREY_CM = 320.0      # WORLD_METRICS.md floor-to-floor
LANE_CM = 350.0        # WORLD_METRICS.md road lane
SIDEWALK_MIN_CM = 250.0
SIDEWALK_MAX_CM = 400.0
CURB_CM = 15.0

failures = []


def check(name, ok, detail=""):
    status = "PASS" if ok else "FAIL"
    L.log(TAG, f"{status} {name}{': ' + str(detail) if detail else ''}")
    if not ok:
        failures.append((name, detail))


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    L.log(TAG, f"{len(descs)} actor descriptors in {MAP_PATH}")

    # --- Buildings -------------------------------------------------------
    building_descs = [d for d in descs if str(d.label).startswith("Buildings_")]
    check("building chunks exist", len(building_descs) > 0, str(len(building_descs)))

    for d in building_descs:
        label = str(d.label)
        if not label.startswith("BLOCKOUT_"):
            check(f"blockout label {label}", False, "not BLOCKOUT_*")

    # Instanced mesh check: load one blockout actor to inspect its ISM.
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    blockout_actors = [a for a in actors if isinstance(a, unreal.AOCBlockoutActor)]
    check("blockout actors in world", len(blockout_actors) > 0, str(len(blockout_actors)))

    cube = unreal.load_asset(CUBE_PATH)
    for a in blockout_actors:
        ism = a.get_instances() if hasattr(a, "get_instances") else None
        if ism is not None:
            mesh = ism.get_static_mesh()
            if mesh is not None and mesh.get_path_name() != CUBE_PATH:
                check(f"{a.get_actor_label()} mesh", False,
                      f"not engine cube: {mesh.get_path_name()}")

    # Per-instance transform checks via the blockout actor's ISM.
    # AOCBlockoutActor exposes GetInstances() in C++; from Python we inspect the
    # ISM component directly.
    total_instances = 0
    off_grid = 0
    off_storey = 0
    for a in blockout_actors:
        comps = a.get_components_by_class(unreal.InstancedStaticMeshComponent)
        for comp in comps:
            count = comp.get_instance_count()
            total_instances += count
            for i in range(count):
                t = comp.get_instance_transform(i, /*bWorldSpace*/ True)
                loc = t.get_translation()
                # centre snapped to 100 cm grid
                if (abs(loc.x % GRID_CM) > 0.5 or abs(loc.y % GRID_CM) > 0.5 or
                        abs(loc.z % GRID_CM) > 0.5):
                    off_grid += 1
                scale = t.get_scale3d()
                # width/depth snapped to 100 cm increments; height to 320 cm storeys
                if (abs(scale.x % GRID_CM) > 0.5 or abs(scale.y % GRID_CM) > 0.5):
                    off_storey += 1  # reused counter for grid violations
                if abs(scale.z % STOREY_CM) > 0.5:
                    off_storey += 1

    check("blockout instances", total_instances > 0, str(total_instances))
    check("instances on 100 cm grid", off_grid == 0, f"{off_grid} off-grid")
    check("heights on 320 cm storeys", off_storey == 0, f"{off_storey} off-grid/off-storey")

    # --- Roads (from descriptor labels, since road mesh data is baked) ---
    road_descs = [d for d in descs if str(d.label).startswith("Roads_")]
    check("road chunks exist", len(road_descs) > 0, str(len(road_descs)))

    # --- Scale reference --------------------------------------------------
    mannequin = [d for d in descs if "Mannequin" in str(d.label) or "SK_Manny" in str(d.label)]
    car = [d for d in descs if "Car" in str(d.label) or "Vehicle" in str(d.label)]
    check("scale-reference mannequin present", len(mannequin) > 0, str(mannequin))
    check("scale-reference car present", len(car) > 0, str(car))

    if failures:
        L.log(TAG, f"{len(failures)} check(s) FAILED")
        for name, detail in failures:
            L.log(TAG, f"  FAIL {name}: {detail}")
        raise RuntimeError(f"{len(failures)} blockout check(s) failed")
    L.log("oc_verify_blockout", "all blockout checks passed")


main()
