# Tools

Offline tools that run outside the editor.

- `octool/` (Rust): fetches the CC0 textures, decals and models listed in `SourceArt/manifest.json`
  (adding their `SourceArt/LICENSES.csv` rows) and merges each downloaded prop into one glTF mesh.
  Run with `scripts/octool.sh fetch-textures [--dry-run] | fetch-decals | fetch-models | combine-gltf <in> <out> [keep-regex]`.
- `kit/KIT_SPEC.md`: the modular building kit contract (pieces, sizes, pivots, slots, collision).

Editor-side tools are C++ commandlets in `Source/OpenCityEditor` (`scripts/run_commandlet.sh <Name>`),
including the kit generator (`OCBuildArt -steps=kit`) and its previews (`OCKitPreview`).
