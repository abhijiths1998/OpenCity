#!/usr/bin/env bash
# Run one of the project's C++ editor commandlets (OpenCityEditor, UOC<Name>Commandlet) headless.
# Fails if the editor exits non-zero or logs a fatal error.
# Usage: scripts/run_commandlet.sh <Name> [args...]     e.g. run_commandlet.sh OCBuildWorld -skipmaterials
#   OC_RENDER=1 gives the commandlet a real GPU renderer (needed to build landscapes and HLODs).
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

NAME="${1:?usage: run_commandlet.sh <Name> [args...]}"
shift
LOG_DIR="$PROJECT_ROOT/Saved/Logs/Commandlets"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/$NAME-$(oc_timestamp).log"

EXTRA=(-nullrhi)
[[ "${OC_RENDER:-0}" == "1" ]] && EXTRA=(-AllowCommandletRendering)

set +e
env "${OC_ENV[@]}" "$UE_EDITOR_CMD" "$UPROJECT" -run="$NAME" "$@" "${EXTRA[@]}" \
  -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput 2>&1 | tee "$LOG" \
  | grep -E "LogOpenCity|Error:|Fatal" | sed -E 's/^\[[^]]*\]\[ *[0-9]+\]//'
EXIT=${PIPESTATUS[0]}
set -e

if [[ $EXIT -ne 0 ]] || grep -qE "Fatal error|LogOpenCity: Error" "$LOG"; then
  echo "$NAME FAILED ($EXIT). Log: $LOG" >&2
  exit 1
fi
echo "$NAME OK. Log: $LOG"
