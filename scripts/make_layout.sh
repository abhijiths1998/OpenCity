#!/usr/bin/env bash
# Generate the Marevia world plan (UOCLayoutCommandlet, C++): districts, roads, terrain, walls, buildings.
# Writes marevia_layout.json, marevia_height.png and marevia_preview.png. CPU only, about a minute.
# Usage: scripts/make_layout.sh [out_dir] [-compare=<dir>] [-selftest] [-seed=N]   (default seed 1974)
#   out_dir defaults to SourceArt/World; -compare reports every difference from the plan in <dir>.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

OUT="SourceArt/World"
if [[ $# -gt 0 && "$1" != -* ]]; then
  OUT="$1"
  shift
fi
LOG_DIR="$PROJECT_ROOT/Saved/Logs/Layout"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/layout-$(oc_timestamp).log"

set +e
env "${OC_ENV[@]}" "$UE_EDITOR_CMD" "$UPROJECT" -run=OCLayout -out="$OUT" "$@" \
  -nullrhi -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput > "$LOG" 2>&1
EXIT=$?
set -e

grep -E "LogOpenCity|Error:|Fatal" "$LOG" | sed -E 's/^\[[^]]*\]\[ *[0-9]+\]//'
if [[ $EXIT -ne 0 ]] || grep -qE "OCLayout: |Fatal error" "$LOG" && ! grep -q "OCLayout: wrote" "$LOG"; then
  echo "LAYOUT FAILED ($EXIT). Log: $LOG" >&2
  exit 1
fi
# Footprints: no overlaps, no slits between 5 cm and 2.5 m (Rust, Tools/octool).
OCTOOL="$PROJECT_ROOT/Tools/octool/target/release/octool"
if [[ ! -x "$OCTOOL" ]]; then
  (cd "$PROJECT_ROOT/Tools/octool" && cargo build --release --quiet)
fi
LINT_JSON="$OUT/marevia_layout.json"
[[ "$LINT_JSON" = /* ]] || LINT_JSON="$PROJECT_ROOT/$LINT_JSON"
if ! "$OCTOOL" buildings-lint "$LINT_JSON"; then
  echo "LAYOUT FAILED: buildings-lint. Log: $LOG" >&2
  exit 1
fi
echo "LAYOUT OK. Log: $LOG"
