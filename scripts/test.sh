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

INDEX="$REPORT_DIR/index.json"
if [[ ! -f "$INDEX" ]]; then
  echo "TESTS DID NOT RUN: no report at $INDEX (editor exit $EXIT)" >&2
  exit 1
fi
# The report may start with a UTF-8 byte order mark; jq reads it after it is stripped.
REPORT_JSON=$(sed '1s/^\xEF\xBB\xBF//' "$INDEX")
jq -r '
  .tests[]? | select(.state == "Fail") |
  "  FAIL: \(.fullTestPath)", (.entries[]?.event | select(.type == "Error") | "      Error: \(.message)")
' <<< "$REPORT_JSON"
jq -r '[.tests[]?.entries[]?.event | select(.type == "Warning") | .message] | unique[] | "  warning (attached to tests): \(.[0:200])"' <<< "$REPORT_JSON"
read -r OK FAILED SKIPPED < <(jq -r '"\((.succeeded // 0) + (.succeededWithWarnings // 0)) \(.failed // 0) \(.notRun // 0)"' <<< "$REPORT_JSON")
TOTAL=$((OK + FAILED + SKIPPED))
echo "TESTS: $OK passed, $FAILED failed, $SKIPPED not run (of $TOTAL)"
if [[ $TOTAL -eq 0 ]]; then
  echo "TESTS FAILED: no tests matched the filter" >&2
  exit 1
fi
[[ $FAILED -eq 0 ]]
