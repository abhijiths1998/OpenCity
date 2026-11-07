#!/usr/bin/env bash
# Open the project in the editor (through XWayland).
# Usage: scripts/run_editor.sh [extra editor args...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine
exec env "${OC_ENV[@]}" "$UE_EDITOR" "$UPROJECT" "$@"
