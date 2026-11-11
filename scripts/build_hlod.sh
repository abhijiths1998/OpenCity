#!/usr/bin/env bash
# Build World Partition HLODs for a map with the engine's HLOD builder commandlet.
# Needs the GPU (landscape HLODs and approximated meshes bake textures).
# The HLOD layers and ranges are set by the world build (UOCBuildWorldCommandlet, SetupHlod).
#
# Usage: scripts/build_hlod.sh [Map] [--delete]   default L_Marevia; --delete removes all HLOD actors
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

MAP="${1:-L_Marevia}"
RESOLVED=$(oc_resolve_map "$MAP")
read -r MAP MAP_PATH <<< "$RESOLVED"
STEP=()
[[ "${2:-}" == "--delete" ]] && STEP=(-DeleteHLODs)

LOG_DIR="$PROJECT_ROOT/Saved/Logs/HLOD"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/${MAP}-$(oc_timestamp).log"

set +e
env "${OC_ENV[@]}" "$UE_EDITOR_CMD" "$UPROJECT" "$MAP_PATH" -run=WorldPartitionBuilderCommandlet \
  -Builder=WorldPartitionHLODsBuilder -AllowCommandletRendering "${STEP[@]}" \
  -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput > "$LOG" 2>&1
EXIT=$?
set -e

grep -E "LogWorldPartition.*HLOD|LogHLOD|Error:|Fatal" "$LOG" | grep -v "^\s*$" | tail -25
if [[ $EXIT -ne 0 ]] || grep -qE "Fatal error|LogWindows: Error|Commandlet.*failed" "$LOG"; then
  echo "HLOD BUILD FAILED ($EXIT). Log: $LOG" >&2
  exit 1
fi
echo "HLOD BUILD OK. Log: $LOG"
