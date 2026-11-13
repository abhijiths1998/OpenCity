#!/usr/bin/env bash
# Rebuild the Marevia World Partition map from SourceArt/World (scripts/make_layout.sh).
# Deletes the old map and its one-file-per-actor folders first: World Partition maps
# can't be reliably emptied and rebuilt in place.
# Usage: scripts/build_world.sh [--regen] [-kitseed=N]   --regen also re-runs the layout generator;
#   -kitseed=N assembles the buildings with another seed (default 1951). Layout seed: make_layout.sh -seed=N.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

if [[ "${1:-}" == "--regen" ]]; then
  "$PROJECT_ROOT/scripts/make_layout.sh"
  shift
fi
rm -rf "$PROJECT_ROOT/Content/Maps/Marevia" \
       "$PROJECT_ROOT/Content/__ExternalActors__/Maps/Marevia" \
       "$PROJECT_ROOT/Content/__ExternalObjects__/Maps/Marevia" \
       "$PROJECT_ROOT/Content/World/Roads"
# Grey-box materials, then the map (UOCBuildWorldCommandlet); landscapes need the GPU renderer.
OC_RENDER=1 exec "$PROJECT_ROOT/scripts/run_commandlet.sh" OCBuildWorld "$@"
