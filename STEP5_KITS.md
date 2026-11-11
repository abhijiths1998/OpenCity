# OpenCity — M4a Step 5: Modular Kits and Trim Sheets

**Goal:** For each district, define a modular building kit and trim sheet. Buildings assembled by Blueprint/PCG from kit pieces with per-instance variation. Ground floors most detailed; upper floors simpler; rooftops always dressed. No two neighbouring buildings identical.

**Prerequisites:** `the OCImportTextures commandlet` (done), master materials (M4a, not yet built), your texture downloads from `AUDIT.md` section 8.

---

## Kit definition per district

Each district kit: wall straight / corner / inner corner, window variants, door / shopfront, balcony, cornice, roof edge, roof props (water tanks, AC units, dish antennas), drainpipes, signage boards. Pieces snap to the 100/200/400 cm grid, bottom-corner pivot, per `WORLD_METRICS.md`.

| District | Kit pieces | Trim sheet theme | Wear |
|---|---|---|---|
| Città Alta | plaster walls, timber shutters, market awnings, flower boxes | painted wood + stone sill | peeling paint, worn steps |
| Porto Vecchio | corrugated tin, rusted shutters, cargo doors, bollards | rusted metal + stone | salt staining, oil, chipped paint |
| Lungomare | hotel façades, balcony rails, neon sign backboards | glass + painted wood | sea salt, fresh paint upstairs |
| Le Fornaci | brick kiln walls, rail sidings, goods doors, water tower | brick + rust | soot, brick dust |
| Le Colline | stone villa walls, cypress planters, estate gates | stone + terracotta | moss, weathered stone |

## Trim sheets

One 2K texture set covering strips: frames, panels, cornices, signage boards. Pack `painted wood trim` + `metal trim` + `stone sill` into one atlas. Vertex-painted wear on large tiling surfaces so repetition never reads as a pattern.

## Naming

`SM_Kit_<District>_<Piece>` (e.g. `SM_Kit_CittaAlta_WallStraight_200x320`). Materials: `MI_Kit_<District>_<Piece>` from `M_Master_Building`.

## Assembly

- Blueprint/PCG assembles kit pieces per block with per-instance variation: color tint, wear amount, window lights, signage, balcony clutter.
- Ground floors most detailed (shops, shutters, signs, lights); upper floors simpler; rooftops always dressed (water tanks, AC units, dishes).
- Replace the world build's `BuildBuildings` engine-cube instances with kit pieces as kits land. Keep blockout chunks `BLOCKOUT_*` until replaced.

## Done by hand (editor)

1. Download kit textures (trim sheet + district wall/roof textures) into `SourceArt/Downloads/`.
2. Add rows to `SourceArt/LICENSES.csv` and `ASSET_SOURCES.md`.
3. Run `the OCImportTextures commandlet` for each texture set.
4. Build master materials (`M_Master_Building`, `M_Master_Opaque`) via `MaterialEditingLibrary` Python.
5. Import kit meshes (Blender pipeline, Step 6) or place kit pieces by hand in a test map.
6. Commit to git (with LFS) after each working step.

## Next step on approval

Step 6 — Blender kitbash pipeline (headless Blender Python: modular pieces at exact WORLD_METRICS sizes, beveled edges, trim-sheet UVs, lightmap UV channel 2, LODs, `UCX_*` collision, FBX export). Blocked: Blender is broken by a partial upgrade (needs `openjph 0.33`); project avoids Blender for now.
