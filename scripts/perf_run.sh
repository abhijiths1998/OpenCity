#!/usr/bin/env bash
# Fly the perf camera through a map and report frame times (from the game)
# plus VRAM (sampled from nvidia-smi).
#
# Usage: scripts/perf_run.sh [Map] [Preset 0-3] [Laps] [extra game args...]
#   default: L_PerfBaseline, preset 1 (Medium), 2 laps
#   e.g. -ocperfprofilegpu logs a one-frame GPU pass breakdown
# OC_PERF_EXEC="r.Foo 0, r.Bar 1" runs console commands after the preset (for A/B tests; r.* cvars
# passed with -dpcvars crash this build at startup).
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

RESOLVED=$(oc_resolve_map "$MAP")
read -r MAP MAP_PATH <<< "$RESOLVED"

COMMON_ARGS=(-windowed -ResX=1920 -ResY=1080 -WinX=0 -WinY=0 -nosplash
             -ocperf -ocperfloops="$LAPS" -ocperfquit)
case "$BUILD" in
  editor)
    oc_require_engine
    CMD=("$UE_EDITOR" "$UPROJECT" "$MAP_PATH" -game "${COMMON_ARGS[@]}"
         -ExecCmds="scalability $PRESET${OC_PERF_EXEC:+, $OC_PERF_EXEC}" -log="OCPerf_${MAP}.log")
    REPORT_DIR="$PROJECT_ROOT/Saved/Profiling/OCPerf" ;;
  dev)
    CMD=("$PROJECT_ROOT/Packaged/Development/Linux/OpenCity/Binaries/Linux/OpenCity" OpenCity
         "$MAP_PATH" "${COMMON_ARGS[@]}" -ExecCmds="scalability $PRESET${OC_PERF_EXEC:+, $OC_PERF_EXEC}")
    REPORT_DIR="$PROJECT_ROOT/Packaged/Development/Linux/OpenCity/Saved/Profiling/OCPerf" ;;
  shipping)
    CMD=("$PROJECT_ROOT/Packaged/Shipping/Linux/OpenCity/Binaries/Linux/OpenCity-Linux-Shipping" OpenCity
         "$MAP_PATH" "${COMMON_ARGS[@]}")
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
case "$PRESET" in 0) PNAME=Low ;; 1) PNAME=Medium ;; 2) PNAME=High ;; 3) PNAME="Ultra (Epic)" ;; *) PNAME="$PRESET" ;; esac
echo "Build: $BUILD, preset: $PNAME"
echo "VRAM before launch (desktop + other apps): $DESKTOP_MIB MiB"
# Columns: time_s, game_fb_mib, total_used_mib, temp_c, clock_mhz (empty when a sample failed).
awk -F, 'NR > 1 {
    if ($2 ~ /^[0-9]+$/) { fb = ($2 > fb ? $2 : fb); nfb++ }
    if ($3 ~ /^[0-9]+$/) { tot = ($3 > tot ? $3 : tot); ntot++ }
    if ($4 ~ /^[0-9]+$/) { tmax = ($4 > tmax ? $4 : tmax); tmin = (ntemp == 0 || $4 < tmin ? $4 : tmin); ntemp++ }
    if ($5 ~ /^[0-9]+$/) { cmax = ($5 > cmax ? $5 : cmax); cmin = (nclk == 0 || $5 < cmin ? $5 : cmin); nclk++ }
  }
  END {
    if (nfb) printf "VRAM game process: peak %d MiB (budget 5000 MiB)\n", fb
    if (ntot) printf "VRAM whole GPU: peak %d of 6144 MiB\n", tot
    if (ntemp && nclk) printf "GPU: temp %d-%d C, clock %d-%d MHz\n", tmin, tmax, cmin, cmax
  }' "$VRAM_CSV"
if [[ -z "$REPORT" ]]; then
  echo "no new OCPerf JSON report: did the game crash, or does the map lack an OCPerfCameraPath?" >&2
  exit 1
fi
jq -r '"Map \(.map), \(.resolution), RHI \(.rhi), GPU \(.gpu)", "Scalability: \(.scalability // "n/a")",
  (.laps[] | "  lap \(.lap): \(.avg_fps * 10 | round / 10) fps avg, \(.one_percent_low_fps * 10 | round / 10) 1% low, p99 \(.p99_ms * 100 | round / 100) ms | game \(.game_ms * 100 | round / 100) render \(.render_ms * 100 | round / 100) gpu \(.gpu_ms * 100 | round / 100) ms")' "$REPORT"
echo "Report: $REPORT"
echo "Samples: $VRAM_CSV"
