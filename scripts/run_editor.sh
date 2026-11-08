#!/usr/bin/env bash
# Open the project in the editor (through XWayland).
# Caps the editor at 60 FPS to limit laptop heat: the M0 stress gate ran the
# CPU at 92 C average / 100 C peak uncapped. Override with OC_EDITOR_MAX_FPS
# (0 = uncapped). Perf runs and packaged builds are never capped.
# Starts at the game's Medium preset: the editor ignores DefaultGameUserSettings and
# defaults to Epic, which ran out of the 6 GB VRAM on Play in L_Marevia (Vulkan OOM).
# Override with OC_EDITOR_SCALABILITY (0 Low .. 3 Epic).
# Usage: scripts/run_editor.sh [extra editor args...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine
MAX_FPS="${OC_EDITOR_MAX_FPS:-60}"
QUALITY="${OC_EDITOR_SCALABILITY:-1}"
exec env "${OC_ENV[@]}" "$UE_EDITOR" "$UPROJECT" -ExecCmds="t.MaxFPS $MAX_FPS, scalability $QUALITY" "$@"
