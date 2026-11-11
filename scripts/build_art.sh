#!/usr/bin/env bash
# Rebuild all final-art assets from clean (ART_PIPELINE.md):
#   1. fetch CC0 textures and models (skips files already downloaded, MD5-checked)
#   2. merge each model into one mesh (Blender) and build the modular building kit (Blender)
#   3. import textures and build the masters and instances, then props, then the kit (Unreal)
# Then run scripts/build_world.sh and scripts/build_hlod.sh to put it all in the map.
# Usage: scripts/build_art.sh [--skip-fetch]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine
BLENDER="${OC_BLENDER:-$HOME/Applications/blender-4.2/blender}"
[[ -x "$BLENDER" ]] || { echo "error: Blender 4.2 LTS not found at $BLENDER (see CLAUDE.md)" >&2; exit 1; }
cd "$PROJECT_ROOT"

if [[ "${1:-}" != "--skip-fetch" ]]; then
  python3 -I Tools/fetch_assets.py
  python3 -I Tools/fetch_models.py
  python3 -I Tools/fetch_ambientcg.py
fi

# Props: one mesh each; packs with several variants keep one (object-name regex).
declare -A KEEP=( [metal_trash_can]='metal_trash_can(_handle_left|_handle_right)?' [fire_hydrant]='.*_aged' [fern_02]='fern_02_b' )
mkdir -p SourceArt/Downloads/Props
for slug in street_lamp_01 metal_trash_can fire_hydrant island_tree_01 island_tree_02 fern_02 ceramic_pot \
            plastic_crate_01 wooden_crate_01 Barrel_01 outdoor_table_chair_set_01 \
            boulder_01 namaqualand_boulder_03; do
  "$BLENDER" -b --factory-startup -P Tools/blender/combine_gltf.py -- \
    SourceArt/Downloads/Models/$slug/*.gltf "SourceArt/Downloads/Props/SM_Prop_$slug.glb" ${KEEP[$slug]:+"${KEEP[$slug]}"} \
    | grep -E "COMBINED|Error"
done

"$BLENDER" -b --factory-startup -P Tools/blender/make_kit.py | grep -vE "^(Blender|Read|Info)" | tail -5

python3 -I Tools/make_lut.py
scripts/run_commandlet.sh OCBuildArt -steps=decals   # before the masters: walls use a leak scan as grime
# The water normals are copied from the engine's DatasmithContent plugin, which this project doesn't
# mount; enable it for this one step (see ImportWaterNormals in OCArtMaterials.cpp).
cp OpenCity.uproject Saved/OpenCity.uproject.bak
jq --tab '.Plugins += [{"Name": "DatasmithContent", "Enabled": true}]' Saved/OpenCity.uproject.bak > OpenCity.uproject
OC_RENDER=1 scripts/run_commandlet.sh OCBuildArt -steps=materials; status=$?
cp Saved/OpenCity.uproject.bak OpenCity.uproject
[[ $status -eq 0 ]] || exit $status
scripts/run_commandlet.sh OCBuildArt -steps=weather,physmat,props,kit
echo "ART OK. Next: scripts/build_world.sh && scripts/build_hlod.sh"
