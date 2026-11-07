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

# Shared DDC outside the repo, so it survives cleans and is never committed.
OC_DDC_DIR="${OC_DDC_DIR:-$HOME/.cache/OpenCity/DDC}"
mkdir -p "$OC_DDC_DIR"

# Environment for anything that runs the engine.
# SDL_VIDEODRIVER=x11: the editor must run through XWayland; Epic calls pure Wayland unusable.
# The DDC variable name contains a hyphen, so it can only be passed with `env`.
OC_ENV=(
  "SDL_VIDEODRIVER=x11"
  "UE-SharedDataCachePath=$OC_DDC_DIR"
)

oc_require_engine() {
  if [[ ! -x "$UE_EDITOR" ]]; then
    echo "error: Unreal Editor not found at $UE_EDITOR (set UE_ROOT)" >&2
    exit 1
  fi
}

oc_timestamp() { date +%Y%m%d-%H%M%S; }
