#!/usr/bin/env bash
# M0 stability gate: run the stress map in a standalone window for N minutes
# while logging GPU state every 5 s. Passes if the game is still running at the end.
# Usage: scripts/stability/stress_gate.sh [minutes]     default: 60
# Before running: close browsers, commit your work, install freeze capture.
source "$(dirname "${BASH_SOURCE[0]}")/../common.sh"
oc_require_engine

MINUTES="${1:-60}"
OUT="$PROJECT_ROOT/Saved/Stability"
mkdir -p "$OUT"
TS="$(oc_timestamp)"
GPU_LOG="$OUT/gpu_$TS.csv"

[[ -f /etc/sysctl.d/99-freeze-capture.conf ]] || echo "warning: freeze capture not installed (scripts/stability/install_freeze_capture.sh)" >&2
systemctl --user is-active --quiet templog.service 2>/dev/null || echo "warning: templog.service is not running" >&2
[[ -z "$(git -C "$PROJECT_ROOT" status --porcelain)" ]] || echo "warning: uncommitted changes in the repo" >&2

env "${OC_ENV[@]}" "$UE_EDITOR" "$UPROJECT" /Game/Maps/Test/L_Stress -game \
  -windowed -ResX=1920 -ResY=1080 -WinX=0 -WinY=0 -nosplash \
  -ocperf -ocperfloops=0 -ExecCmds="scalability 1" -log="OCStress.log" &
GAME_PID=$!
cat <<MSG
Stress gate: ${MINUTES}-minute burn-in.
A game window flies a camera over a heavy Nanite + Lumen scene. Nothing to do in it:
leave it running and don't close it. The script ends it and prints the result.
It checks whether this machine stays stable under long Unreal GPU load.
Game pid $GAME_PID, GPU log $GPU_LOG
MSG

echo "time,temp_c,power_w,mem_used_mib,gpu_util,clock_mhz" > "$GPU_LOG"
END=$(( SECONDS + MINUTES * 60 ))
while (( SECONDS < END )); do
  if ! kill -0 "$GAME_PID" 2>/dev/null; then
    wait "$GAME_PID"; CODE=$?
    RAN_MIN=$(( (MINUTES*60 - (END-SECONDS)) / 60 ))
    LOG="$PROJECT_ROOT/Saved/Logs/OCStress.log"
    if [[ $CODE -eq 0 ]] && grep -q "SDL_EVENT_QUIT" "$LOG" 2>/dev/null; then
      echo "GATE NOT COMPLETED: the game window was closed after ${RAN_MIN} min. Run it again and leave the window open." >&2
      exit 3
    fi
    echo "GATE FAILED: game exited early (code $CODE) after ${RAN_MIN} min. Log: $LOG" >&2
    exit 1
  fi
  LINE=$(timeout 4 nvidia-smi --query-gpu=temperature.gpu,power.draw,memory.used,utilization.gpu,clocks.gr --format=csv,noheader,nounits 2>/dev/null) \
    || LINE="NVIDIA-SMI-TIMEOUT"
  echo "$(date +%T),${LINE// /}" >> "$GPU_LOG"
  sync "$GPU_LOG" 2>/dev/null || true
  sleep 5
done

kill -TERM "$GAME_PID" 2>/dev/null || true
wait "$GAME_PID" 2>/dev/null || true
echo "GATE PASSED: ${MINUTES} min without a crash or freeze. GPU log: $GPU_LOG"
