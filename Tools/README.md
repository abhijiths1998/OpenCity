# Tools

Offline tools that run outside the editor.

- `octool/` (Rust): fetches the CC0 textures, decals and models listed in `SourceArt/manifest.json`
  and adds their `SourceArt/LICENSES.csv` rows. Run with `scripts/octool.sh fetch-textures [--dry-run] | fetch-decals | fetch-models`.
- `blender/`: the modular kit generator and preview renders (Blender 4.2 LTS).

Editor-side tools are C++ commandlets in `Source/OpenCityEditor` (`scripts/run_commandlet.sh <Name>`).
