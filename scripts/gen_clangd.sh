#!/usr/bin/env bash
# Generate compile_commands.json in the project root for clangd (Rider or VS Code).
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
oc_require_engine
"$UE_BUILD" -mode=GenerateClangDatabase -Project="$UPROJECT" \
  "${PROJECT_NAME}Editor" Linux Development -OutputDir="$PROJECT_ROOT"
ls -l "$PROJECT_ROOT/compile_commands.json"
