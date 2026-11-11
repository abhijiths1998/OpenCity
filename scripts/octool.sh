#!/usr/bin/env bash
# Offline asset tools (Rust, Tools/octool): builds on first use, then runs.
# Usage: scripts/octool.sh fetch-textures [--dry-run] | fetch-decals | fetch-models
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec cargo run --quiet --release --manifest-path "$ROOT/Tools/octool/Cargo.toml" -- "$@"
