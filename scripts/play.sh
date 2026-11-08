#!/usr/bin/env bash
# Play a map as the game (no editor UI): lighter on VRAM than Play-in-Editor.
# Uses the uncooked content through the editor binary in -game mode.
#
# Usage: scripts/play.sh [Map] [extra game args...]
#   default: L_Marevia. e.g. scripts/play.sh L_DevDriving
#   OC_PLAY_QUALITY  scalability 0-3 (default 1 = Medium, the shipping default)
#   OC_PLAY_WINDOW   "windowed" (default, 1920x1080) or "fullscreen"
# In game: ` (backtick) opens the console. oc.dev.spawn coupe|offroad|bike puts a vehicle in front of you.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

MAP="${1:-L_Marevia}"
shift $(( $# > 0 ? 1 : 0 ))
RESOLVED=$(oc_resolve_map "$MAP")
read -r MAP MAP_PATH <<< "$RESOLVED"

QUALITY="${OC_PLAY_QUALITY:-1}"
case "${OC_PLAY_WINDOW:-windowed}" in
  fullscreen) WINDOW=(-fullscreen) ;;
  *)          WINDOW=(-windowed -ResX=1920 -ResY=1080) ;;
esac

echo "Playing $MAP_PATH at scalability $QUALITY. Close the window or type 'quit' in the console to stop."
exec env "${OC_ENV[@]}" "$UE_EDITOR" "$UPROJECT" "$MAP_PATH" -game "${WINDOW[@]}" -nosplash \
  -ExecCmds="scalability $QUALITY" -log="OCPlay_${MAP}.log" "$@"
