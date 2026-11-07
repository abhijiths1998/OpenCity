#!/usr/bin/env bash
# Fly the perf camera through a map and report frame times (from the game)
# plus VRAM (sampled from nvidia-smi).
#
# Usage: scripts/perf_run.sh [Map] [Preset 0-3] [Laps] [extra game args...]
#   default: L_PerfBaseline, preset 1 (Medium), 2 laps
#   e.g. -ocperfprofilegpu logs a one-frame GPU pass breakdown
#
# OC_PERF_BUILD selects what runs:
#   editor    (default) editor binary in -game mode, uncooked. Quick, but extra render-thread cost.
#   dev       packaged Development build (scripts/package.sh Development). Cooked; cvars allowed.
#   shipping  packaged Shipping build. The numbers players get. Ignores the preset argument
#             and -dpcvars: it uses the saved or first-run settings (Medium).
#
# Numbers are only comparable with browsers closed, and laptop heat moves them:
# compare runs made back to back, or interleave A/B runs.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

MAP="${1:-L_PerfBaseline}"
PRESET="${2:-1}"
LAPS="${3:-2}"
shift $(( $# > 3 ? 3 : $# ))
BUILD="${OC_PERF_BUILD:-editor}"

COMMON_ARGS=(-windowed -ResX=1920 -ResY=1080 -WinX=0 -WinY=0 -nosplash
             -ocperf -ocperfloops="$LAPS" -ocperfquit)
case "$BUILD" in
  editor)
    oc_require_engine
    CMD=("$UE_EDITOR" "$UPROJECT" "/Game/Maps/Test/$MAP" -game "${COMMON_ARGS[@]}"
         -ExecCmds="scalability $PRESET" -log="OCPerf_${MAP}.log")
    REPORT_DIR="$PROJECT_ROOT/Saved/Profiling/OCPerf" ;;
  dev)
    CMD=("$PROJECT_ROOT/Packaged/Development/Linux/OpenCity/Binaries/Linux/OpenCity" OpenCity
         "/Game/Maps/Test/$MAP" "${COMMON_ARGS[@]}" -ExecCmds="scalability $PRESET")
    REPORT_DIR="$PROJECT_ROOT/Packaged/Development/Linux/OpenCity/Saved/Profiling/OCPerf" ;;
  shipping)
    CMD=("$PROJECT_ROOT/Packaged/Shipping/Linux/OpenCity/Binaries/Linux/OpenCity-Linux-Shipping" OpenCity
         "/Game/Maps/Test/$MAP" "${COMMON_ARGS[@]}")
    REPORT_DIR="$HOME/.config/Epic/OpenCity/Saved/Profiling/OCPerf"
    PRESET="first-run/saved"
    # Shipping ignores a map on the command line (UE_ALLOW_MAP_OVERRIDE_IN_SHIPPING=0 in the
    # prebuilt engine), so it always loads the default map.
    if [[ "$MAP" != "L_PerfBaseline" ]]; then
      echo "error: Shipping always loads the default map (L_PerfBaseline); use OC_PERF_BUILD=dev for $MAP" >&2
      exit 1
    fi ;;
  *) echo "error: OC_PERF_BUILD must be editor, dev or shipping" >&2; exit 1 ;;
esac
[[ -x "${CMD[0]}" ]] || { echo "error: ${CMD[0]} not found (package it first)" >&2; exit 1; }

OUT_DIR="$PROJECT_ROOT/Saved/Profiling/OCPerf"
mkdir -p "$OUT_DIR" "$REPORT_DIR"
VRAM_CSV="$OUT_DIR/vram_${BUILD}_${MAP}_$(oc_timestamp).csv"

if pgrep -x 'msedge|brave|firefox|chrome|chromium' >/dev/null; then
  echo "warning: a browser is running; its VRAM use makes these numbers non-comparable." >&2
fi

DESKTOP_MIB=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | head -1)
MARKER="$(mktemp)"

env "${OC_ENV[@]}" "${CMD[@]}" "$@" > "$OUT_DIR/last_run_stdout.txt" 2>&1 &
GAME_PID=$!

echo "time_s,game_fb_mib,total_used_mib,temp_c,clock_mhz" > "$VRAM_CSV"
START=$SECONDS
while kill -0 "$GAME_PID" 2>/dev/null; do
  GPU=$(timeout 4 nvidia-smi --query-gpu=memory.used,temperature.gpu,clocks.gr --format=csv,noheader,nounits 2>/dev/null | head -1 | tr -d ' ' || true)
  FB=$(timeout 4 nvidia-smi pmon -c 1 -s m 2>/dev/null | awk -v p="$GAME_PID" '$2==p {print $4}' | head -1 || true)
  echo "$((SECONDS - START)),${FB:-},${GPU:-,,}" >> "$VRAM_CSV"
  sleep 1
done
wait "$GAME_PID" || true

REPORT=$(find "$REPORT_DIR" -name "${MAP}_*.json" -newer "$MARKER" 2>/dev/null | sort | tail -1)
rm -f "$MARKER"
python3 - "$REPORT" "$VRAM_CSV" "$DESKTOP_MIB" "$PRESET" "$BUILD" <<'PY'
import csv, json, sys
report, vram_csv, desktop, preset, build = sys.argv[1:6]
names = {"0": "Low", "1": "Medium", "2": "High", "3": "Ultra (Epic)"}
fb, total, temp, clock = [], [], [], []
with open(vram_csv) as f:
    for row in csv.DictReader(f):
        for key, dst in (("game_fb_mib", fb), ("total_used_mib", total), ("temp_c", temp), ("clock_mhz", clock)):
            if row.get(key, "").strip().isdigit():
                dst.append(int(row[key]))
print(f"Build: {build}, preset: {names.get(preset, preset)}")
print(f"VRAM before launch (desktop + other apps): {desktop} MiB")
if fb:
    print(f"VRAM game process: peak {max(fb)} MiB (budget 5000 MiB)")
if total:
    print(f"VRAM whole GPU: peak {max(total)} of 6144 MiB")
if temp and clock:
    print(f"GPU: temp {min(temp)}-{max(temp)} C, clock {min(clock)}-{max(clock)} MHz")
if not report:
    sys.exit("no new OCPerf JSON report: did the game crash, or does the map lack an OCPerfCameraPath?")
r = json.load(open(report))
print(f"Map {r['map']}, {r['resolution']}, RHI {r['rhi']}, GPU {r['gpu']}")
print(f"Scalability: {r.get('scalability', 'n/a')}")
for lap in r["laps"]:
    print("  lap {lap}: {avg_fps:.1f} fps avg, {one_percent_low_fps:.1f} 1% low, p99 {p99_ms:.2f} ms"
          " | game {game_ms:.2f} render {render_ms:.2f} gpu {gpu_ms:.2f} ms".format(**lap))
print(f"Report: {report}\nSamples: {vram_csv}")
PY
