#!/usr/bin/env bash
# Visual QA (ART_PIPELINE.md step 10): screenshots from every camera bookmark at noon, sunset and
# night, with frame/GPU time per view and VRAM sampled from nvidia-smi.
# Usage: scripts/qa_shots.sh [hours=13,19.4,22.5] [quality 0-3, default 1]
# Output: Saved/QA/<date>/ (screenshots, qa.log with per-view timings, vram.csv)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine
HOURS="${1:-hours=13,19.4,22.5}"
QUALITY="${2:-1}"
OUT="$PROJECT_ROOT/Saved/QA/$(date +%Y-%m-%d_%H%M)"
mkdir -p "$OUT"
MARK="$(mktemp)"
env "${OC_ENV[@]}" "$UE_EDITOR" "$UPROJECT" /Game/Maps/Marevia/L_Marevia -game -windowed -ResX=1600 -ResY=900 -nosplash \
  -ExecCmds="scalability $QUALITY" -log="OCQA.log" \
  -ocdevscript="3:t.MaxFPS 60;4:oc.dev.unlock all;6:oc.dev.qa $HOURS quit;3000:quit" > /dev/null 2>&1 &
PID=$!
echo "time_s,total_used_mib" > "$OUT/vram.csv"
START=$SECONDS
while kill -0 $PID 2>/dev/null; do
  echo "$((SECONDS - START)),$(timeout 4 nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | head -1)" >> "$OUT/vram.csv"
  sleep 2
done
grep -h "OCDEV qa" "$PROJECT_ROOT/Saved/Logs/OCQA.log" | sed 's/^.*OCDEV qa/qa/' > "$OUT/qa.log"
find "$PROJECT_ROOT/Saved/Screenshots" -newer "$MARK" -name "QA_*.png" -exec mv {} "$OUT/" \;
rm -f "$MARK"
cat "$OUT/qa.log"
echo "VRAM peak (whole GPU): $(tail -n +2 "$OUT/vram.csv" | cut -d, -f2 | sort -n | tail -1) MiB"
echo "Screenshots: $OUT"
