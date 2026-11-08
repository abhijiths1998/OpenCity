#!/usr/bin/env bash
# Shared settings for OpenCity scripts. Source this, don't run it.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_NAME="OpenCity"
UPROJECT="$PROJECT_ROOT/$PROJECT_NAME.uproject"

UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine}"
UE_BUILD="$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
UE_UAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
UE_EDITOR="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
UE_EDITOR_CMD="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"

# 4 cores / 8 threads and 24 GB RAM: leave headroom for the desktop.
OC_MAX_JOBS="${OC_MAX_JOBS:-6}"

# DDC: UE 5.8 uses a local Zen server at ~/.config/Epic/UnrealEngine/Common/Zen/Data,
# outside the repo and shared by all projects on this machine. Nothing to configure.

# Environment for anything that runs the engine.
# SDL_VIDEODRIVER=x11: the editor must run through XWayland; Epic calls pure Wayland unusable.
OC_ENV=(
  "SDL_VIDEODRIVER=x11"
)

# Build accelerator (UBA) listens on 0.0.0.0 by default: keep it local-only.
OC_UBT_ARGS=(-UBAHost=127.0.0.1 -UBADisableRemote)

oc_require_engine() {
  if [[ ! -x "$UE_EDITOR" ]]; then
    echo "error: Unreal Editor not found at $UE_EDITOR (set UE_ROOT)" >&2
    exit 1
  fi
}

oc_timestamp() { date +%Y%m%d-%H%M%S; }

# Resolve a map name (L_Marevia) or /Game path to "<name> <path>" by finding its .umap under Content/Maps.
oc_resolve_map() {
  local map="$1" umap rel
  if [[ "$map" == /Game/* ]]; then
    echo "${map##*/} $map"; return
  fi
  umap=$(find "$PROJECT_ROOT/Content/Maps" -name "$map.umap" | head -1)
  [[ -n "$umap" ]] || { echo "error: map $map not found under Content/Maps" >&2; return 1; }
  rel="${umap#"$PROJECT_ROOT/Content/"}"
  echo "$map /Game/${rel%.umap}"
}
