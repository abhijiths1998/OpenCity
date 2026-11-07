#!/usr/bin/env bash
# Rebuild the Marevia World Partition map from SourceArt/World (Tools/make_marevia_layout.py).
# Deletes the old map and its one-file-per-actor folders first: World Partition maps
# can't be reliably emptied and rebuilt in place.
# Usage: scripts/build_world.sh [--regen]   --regen also re-runs the layout generator
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

if [[ "${1:-}" == "--regen" ]]; then
  python3 -I "$PROJECT_ROOT/Tools/make_marevia_layout.py"
fi
rm -rf "$PROJECT_ROOT/Content/Maps/Marevia" \
       "$PROJECT_ROOT/Content/__ExternalActors__/Maps/Marevia" \
       "$PROJECT_ROOT/Content/__ExternalObjects__/Maps/Marevia" \
       "$PROJECT_ROOT/Content/World/Roads"
"$PROJECT_ROOT/scripts/run_python.sh" "$PROJECT_ROOT/Content/Python/oc_dev_materials.py"
OC_PY_RENDER=1 exec "$PROJECT_ROOT/scripts/run_python.sh" "$PROJECT_ROOT/Content/Python/oc_m3_world.py"
