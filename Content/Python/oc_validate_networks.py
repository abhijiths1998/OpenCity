"""Validate the road, rail and water networks (INFRA_DESIGN.md, Phase 13 data checks).

  scripts/run_python.sh Content/Python/oc_validate_networks.py [--import]

--import first rewrites DA_MareviaRoads/Rail/Water from SourceArt/World/marevia_layout.json (no
world rebuild; use it when only metadata changed). The checks are FOCNetworkValidator (C++, unit
tested): profile, width, lanes, gradient, radius, speed, connectivity, one-way traps, ZTL gates,
tram continuity, junction types, rail gradient/power/stations, water bodies and edges.
The report goes to the log and to Saved/Validation/networks.md. Errors fail the script.
"""

import datetime
import json
import os
import sys

import unreal

import oc_lib as L
import oc_networks

TAG = "oc_validate"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def main():
    if "--import" in sys.argv:
        with open(os.path.join(PROJECT_DIR, "SourceArt/World/marevia_layout.json")) as f:
            roads, rail, water = oc_networks.write_network_assets(json.load(f))
    else:
        roads = unreal.load_asset("/Game/World/DA_MareviaRoads")
        rail = unreal.load_asset("/Game/World/DA_MareviaRail")
        water = unreal.load_asset("/Game/World/DA_MareviaWater")
    report, errors, issues = unreal.OCNetworkValidationLibrary.validate_networks(roads, rail, water, 8)
    out = os.path.join(PROJECT_DIR, "Saved/Validation")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "networks.md"), "w") as f:
        f.write(f"# Network validation {datetime.datetime.now():%Y-%m-%d %H:%M}\n\n```\n{report}```\n")
    for line in report.splitlines():
        L.log(TAG, line)
    if errors:
        raise RuntimeError(f"{errors} network error(s) of {issues} issue(s); see Saved/Validation/networks.md")


main()
