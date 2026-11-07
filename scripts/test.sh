#!/usr/bin/env bash
# Run automation tests headless and fail if any test fails.
# Usage: scripts/test.sh [filter]     default filter: OpenCity
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

FILTER="${1:-OpenCity}"
REPORT_DIR="$PROJECT_ROOT/Saved/Automation/$(oc_timestamp)"
mkdir -p "$REPORT_DIR"

set +e
env "${OC_ENV[@]}" "$UE_EDITOR_CMD" "$UPROJECT" \
  -ExecCmds="Automation RunTests $FILTER; Quit" \
  -TestExit="Automation Test Queue Empty" \
  -ReportExportPath="$REPORT_DIR" \
  -unattended -nopause -nosplash -nosound -nullrhi \
  -log="OCTests.log" -stdout -FullStdOutLogOutput 2>&1 | tee "$REPORT_DIR/run.log" \
  | grep -E "Test Completed|Error:|LogAutomationController|Fatal"
EXIT=${PIPESTATUS[0]}
set -e

python3 - "$REPORT_DIR/index.json" "$EXIT" <<'PY'
import json, sys
path, ue_exit = sys.argv[1], int(sys.argv[2])
try:
    with open(path, encoding="utf-8-sig") as f:
        r = json.load(f)
except FileNotFoundError:
    sys.exit(f"TESTS DID NOT RUN: no report at {path} (editor exit {ue_exit})")
ok, failed, skipped = r.get("succeeded", 0), r.get("failed", 0), r.get("notRun", 0)
for t in r.get("tests", []):
    if t.get("state") != "Success":
        print(f"  {t.get('state')}: {t.get('fullTestPath')}")
        for e in t.get("entries", []):
            ev = e.get("event", {})
            if ev.get("type") in ("Error", "Warning"):
                print(f"      {ev.get('type')}: {ev.get('message')}")
total = ok + failed + skipped
print(f"TESTS: {ok} passed, {failed} failed, {skipped} not run (of {total})")
if total == 0:
    sys.exit("TESTS FAILED: no tests matched the filter")
sys.exit(1 if failed else 0)
PY
