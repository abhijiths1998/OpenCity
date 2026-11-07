#!/usr/bin/env bash
# Fly the perf camera through a map in a standalone game window and report
# frame times (from the game) plus VRAM (sampled from nvidia-smi).
# Usage: scripts/perf_run.sh [Map] [Preset 0-3] [Laps]
#   default: L_PerfBaseline, preset 1 (Medium), 2 laps
# Numbers are only comparable with browsers closed.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine

MAP="${1:-L_PerfBaseline}"
PRESET="${2:-1}"
LAPS="${3:-2}"
OUT_DIR="$PROJECT_ROOT/Saved/Profiling/OCPerf"
mkdir -p "$OUT_DIR"
TS="$(oc_timestamp)"
VRAM_CSV="$OUT_DIR/vram_${MAP}_${TS}.csv"

if pgrep -x 'msedge|brave|firefox|chrome|chromium' >/dev/null; then
  echo "warning: a browser is running; its VRAM use makes these numbers non-comparable." >&2
fi

DESKTOP_MIB=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | head -1)

env "${OC_ENV[@]}" "$UE_EDITOR" "$UPROJECT" "/Game/Maps/Test/$MAP" -game \
  -windowed -ResX=1920 -ResY=1080 -WinX=0 -WinY=0 -nosplash \
  -ocperf -ocperfloops="$LAPS" -ocperfquit \
  -ExecCmds="scalability $PRESET" -log="OCPerf_${MAP}.log" &
GAME_PID=$!

echo "time_s,game_fb_mib,total_used_mib" > "$VRAM_CSV"
START=$SECONDS
while kill -0 "$GAME_PID" 2>/dev/null; do
  TOTAL=$(timeout 4 nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | head -1 || echo "")
  FB=$(timeout 4 nvidia-smi pmon -c 1 -s m 2>/dev/null | awk -v p="$GAME_PID" '$2==p {print $4}' | head -1 || true)
  echo "$((SECONDS - START)),${FB:-},${TOTAL:-}" >> "$VRAM_CSV"
  sleep 1
done
wait "$GAME_PID" || true

REPORT=$(ls -t "$OUT_DIR"/"${MAP}"_*.json 2>/dev/null | head -1 || true)
python3 - "$REPORT" "$VRAM_CSV" "$DESKTOP_MIB" "$PRESET" <<'PY'
import csv, json, sys
report, vram_csv, desktop, preset = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
names = {"0": "Low", "1": "Medium", "2": "High", "3": "Epic/Ultra"}
fb, total = [], []
with open(vram_csv) as f:
    for row in csv.DictReader(f):
        if row["game_fb_mib"]:
            fb.append(int(row["game_fb_mib"]))
        if row["total_used_mib"]:
            total.append(int(row["total_used_mib"]))
print(f"Preset: {names.get(preset, preset)}")
print(f"VRAM before launch (desktop + other apps): {desktop} MiB")
if fb:
    print(f"VRAM game process: peak {max(fb)} MiB, budget 5000 MiB")
if total:
    print(f"VRAM whole GPU: peak {max(total)} of 6144 MiB")
if not report:
    sys.exit("no OCPerf JSON report found: did the map contain an OCPerfCameraPath?")
r = json.load(open(report))
print(f"Map {r['map']}, {r['resolution']}, RHI {r['rhi']}, GPU {r['gpu']}")
for lap in r["laps"]:
    print("  lap {lap}: {avg_fps:.1f} fps avg, {one_percent_low_fps:.1f} 1% low, "
          "p99 {p99_ms:.2f} ms | game {game_ms:.2f} render {render_ms:.2f} gpu {gpu_ms:.2f} ms".format(**lap))
print(f"Report: {report}\nVRAM samples: {vram_csv}")
PY
