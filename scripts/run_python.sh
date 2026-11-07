#!/usr/bin/env bash
# Run an editor Python script headless via the pythonscript commandlet.
# Fails if the script logs a Python error.
# Usage: scripts/run_python.sh <script.py> [args...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

SCRIPT="$(realpath "${1:?usage: run_python.sh <script.py> [args...]}")"
shift
LOG_DIR="$PROJECT_ROOT/Saved/Logs/Python"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/$(basename "$SCRIPT" .py)-$(oc_timestamp).log"

set +e
env "${OC_ENV[@]}" "$UE_EDITOR_CMD" "$UPROJECT" -run=pythonscript -script="$SCRIPT $*" \
  -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput 2>&1 | tee "$LOG" \
  | grep -E "LogPython|Error:|Fatal"
EXIT=${PIPESTATUS[0]}
set -e

if grep -qE "LogPython: Error|Traceback \(most recent call last\)" "$LOG"; then
  echo "PYTHON FAILED. Log: $LOG" >&2
  exit 1
fi
if [[ $EXIT -ne 0 ]]; then
  echo "EDITOR EXITED WITH $EXIT. Log: $LOG" >&2
  exit "$EXIT"
fi
echo "PYTHON OK. Log: $LOG"
