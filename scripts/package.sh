#!/usr/bin/env bash
# Build, cook and package a Linux build into Packaged/<Config>/.
# Usage: scripts/package.sh [Shipping|Development]     default: Shipping
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

CONFIG="${1:-Shipping}"
OUT="$PROJECT_ROOT/Packaged/$CONFIG"
LOG_DIR="$PROJECT_ROOT/Saved/Logs/Package"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/package-$CONFIG-$(oc_timestamp).log"

set +e
env "${OC_ENV[@]}" "$UE_UAT" BuildCookRun \
  -project="$UPROJECT" -platform=Linux -clientconfig="$CONFIG" \
  -build -cook -stage -pak -iostore -compressed -archive -archivedirectory="$OUT" \
  -ubtargs="${OC_UBT_ARGS[*]}" -unattended -utf8output -nop4 2>&1 | tee "$LOG"
STATUS=${PIPESTATUS[0]}
set -e

if [[ $STATUS -ne 0 ]]; then
  echo "PACKAGE FAILED (exit $STATUS). Log: $LOG" >&2
  exit "$STATUS"
fi
echo "PACKAGE OK: $OUT. Log: $LOG"
