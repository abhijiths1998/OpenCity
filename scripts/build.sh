#!/usr/bin/env bash
# Build a target and fail on any compiler warning in project code.
# Usage: scripts/build.sh [Target] [Config] [extra UBT args...]
#   default: OpenCityEditor Development
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

TARGET="${1:-${PROJECT_NAME}Editor}"
CONFIG="${2:-Development}"
shift $(( $# > 2 ? 2 : $# ))

LOG_DIR="$PROJECT_ROOT/Saved/Logs/Build"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/${TARGET}-${CONFIG}-$(oc_timestamp).log"

set +e
"$UE_BUILD" "$TARGET" Linux "$CONFIG" -Project="$UPROJECT" -WaitMutex \
  -MaxParallelActions="$OC_MAX_JOBS" "${OC_UBT_ARGS[@]}" "$@" 2>&1 | tee "$LOG"
STATUS=${PIPESTATUS[0]}
set -e

# Warnings from our own sources only (engine headers are not ours to fix).
WARNINGS=$(grep -E "^$PROJECT_ROOT/(Source|Plugins)/.*warning" "$LOG" | sort -u || true)
if [[ $STATUS -ne 0 ]]; then
  echo "BUILD FAILED (exit $STATUS). Log: $LOG" >&2
  exit "$STATUS"
fi
if [[ -n "$WARNINGS" ]]; then
  echo "$WARNINGS" >&2
  echo "BUILD HAS $(echo "$WARNINGS" | wc -l) WARNING(S) in project code. Log: $LOG" >&2
  exit 2
fi
echo "BUILD OK: $TARGET $CONFIG. Log: $LOG"
