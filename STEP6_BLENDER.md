# OpenCity — M4a Step 6: Blender Kitbash Pipeline

**Goal:** Headless Blender Python scripts (`blender -b -P script.py`) build modular pieces at exact WORLD_METRICS sizes, beveled edges, trim-sheet UVs, lightmap UV channel 2, LODs, `UCX_*` collision. Then a UE Python import script applies naming, materials, settings automatically.

**Blocker:** Blender is broken on this machine by a partial upgrade (needs `openjph 0.33`, `sudo pacman -Syu`). The project avoids Blender for now (glTF generated in Python). This document is the pipeline spec to run once Blender is fixed.

---

## Blender Python script spec

`Tools/blender/kit_piece.py` — run with `blender -b -P Tools/blender/kit_piece.py -- <piece> <district> <size>`:

1. Set units: metric, 1 unit = 1 m (exports as cm in Unreal).
2. Build the piece at exact WORLD_METRICS size (100/200/400 cm increments).
3. Bevel all edges (no razor-sharp 90°).
4. UV channel 0 mapped to the trim sheet; UV channel 2 for lightmaps.
5. Add `UCX_<Piece>` collision mesh (convex hull or box).
6. Generate LODs (decimate, at least 3 for non-Nanite).
7. Export FBX with correct units/axes (X forward, Z up) into `SourceArt/Blender/Import/<district>/`.

## UE import script spec

`Content/Python/oc_import_kit.py` (to be written):

```bash
scripts/run_python.sh Content/Python/oc_import_kit.py \
    --src SourceArt/Blender/Import/CittaAlta \
    --dest /Game/Environment/Meshes/Kit/CittaAlta \
    --parent /Game/Materials/M_Master_Building
```

1. Import FBX via `AssetImportTask`.
2. Apply naming `SM_Kit_<District>_<Piece>`.
3. Assign `MI_*` material from master by parameter name.
4. Set LODs, collision (`UCX_*`), lightmap UV channel, Nanite flag for static environment.
5. Save and print summary.

## Naming & settings

| Rule | Value |
|---|---|
| Units | Metric, 1 m = 100 cm in Unreal |
| Forward / up | X forward, Z up |
| Pivot | Bottom corner for modular pieces; base centre for props |
| Modular grid | 1 m (100 cm), floor height 3.2 m (320 cm) |
| Collision | `UCX_<Mesh>` |
| LODs | Nanite meshes: no LOD chain. Others: at least 3 |
| UVs | Channel 0 textures, channel 2 lightmaps |
| Naming | `SM_` static, `SK_` skeletal |

## Done by hand (once Blender works)

1. `sudo pacman -Syu` to fix `openjph 0.33` and restore Blender.
2. Write `Tools/blender/kit_piece.py` per district kit piece.
3. Write `Content/Python/oc_import_kit.py`.
4. Import all kit pieces; run the asset validator.
5. Place kit pieces in a test map, day and night.
6. Commit to git (with LFS).

## Next step on approval

Step 7 — Roads, ground and landscape (splines, ground layers, auto-material by slope/height).
