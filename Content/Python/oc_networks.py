"""Road, rail and water network data assets (INFRA_DESIGN.md, Phase 2).

The networks are data, written by Tools/make_marevia_layout.py into SourceArt/World/marevia_layout.json
(the single source of truth); meshes, props, lanes and checks are generated from them.

  DA_MareviaRoads  UOCRoadGraphAsset   {"roads": {nodes, segments, profiles, junctions}}
  DA_MareviaRail   UOCRailNetworkAsset {"rail": {"lines": [...]}}
  DA_MareviaWater  UOCWaterNetworkAsset {"water_network": {"bodies": [...]}}

oc_m3_story.py calls write_network_assets() during the world build; oc_validate_networks.py --import
refreshes the assets alone after a layout change that doesn't move geometry.
"""

import json

import unreal

import oc_lib as L

TAG = "oc_networks"
FOLDER = "/Game/World"


def _cm(x, y, z):
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def write_network_assets(layout):
    compact = dict(separators=(",", ":"))
    roads = L.data_asset("DA_MareviaRoads", FOLDER, unreal.OCRoadGraphAsset, TAG)
    roads.set_editor_property("layout_json", json.dumps({"roads": layout["roads"]}, **compact))
    st = layout["start"]
    roads.set_editor_property("start_location", _cm(st["pos"][0], st["pos"][1], st["ground_z"]))
    L.save(roads)
    rail = L.data_asset("DA_MareviaRail", FOLDER, unreal.OCRailNetworkAsset, TAG)
    rail.set_editor_property("layout_json", json.dumps({"rail": layout.get("rail", {"lines": []})}, **compact))
    L.save(rail)
    water = L.data_asset("DA_MareviaWater", FOLDER, unreal.OCWaterNetworkAsset, TAG)
    water.set_editor_property("layout_json", json.dumps({"water_network": layout.get("water_network", {"bodies": []})}, **compact))
    L.save(water)
    L.log(TAG, f"network assets: {len(layout['roads']['segments'])} road segments, "
               f"{len(layout.get('rail', {}).get('lines', []))} rail lines, "
               f"{len(layout.get('water_network', {}).get('bodies', []))} water bodies")
    return roads, rail, water
