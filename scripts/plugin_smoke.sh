#!/usr/bin/env bash
# Check that plugins load on Linux: starts the editor headless once with the
# milestone-later plugins switched on, and fails on any plugin load error.
# Those plugins are deliberately absent from OpenCity.uproject (they are off by
# default), because -EnablePlugins= ignores plugins the .uproject lists.
# Usage: scripts/plugin_smoke.sh [Plugin,Plugin,...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

PLUGINS="${1:-Water,MassGameplay,MassAI,MassCrowd,ZoneGraph,CommonUI}"
LOG_DIR="$PROJECT_ROOT/Saved/Logs/PluginSmoke"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/plugin-smoke-$(oc_timestamp).log"
PROBE="$PROJECT_ROOT/Content/Python/oc_plugin_probe.py"

set +e
env "${OC_ENV[@]}" "$UE_EDITOR_CMD" "$UPROJECT" -EnablePlugins="$PLUGINS" \
  -run=pythonscript -script="$PROBE $PLUGINS" \
  -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput > "$LOG" 2>&1
EXIT=$?
set -e

grep -E "OCPROBE|LogPluginManager: (Error|Warning)|failed to load|Unable to load" "$LOG" || true
if [[ $EXIT -ne 0 ]] || grep -qE "OCPROBE FAIL|LogPluginManager: Error|LogPython: Error" "$LOG"; then
  echo "PLUGIN SMOKE FAILED (exit $EXIT). Log: $LOG" >&2
  exit 1
fi
echo "PLUGIN SMOKE OK: $PLUGINS. Log: $LOG"
