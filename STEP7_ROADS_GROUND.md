# OpenCity — M4a Step 7: Roads, Ground and Landscape

**Goal:** Roads as splines (PCG/spline meshes) with intersections, curbs, sidewalks, manholes, drains, lane markings as decals. Ground layers: asphalt with patches/cracks/tire marks/puddles, dirt at edges, leaves and trash near curbs, oil stains near parking. Landscape material with auto-material by slope/height (grass, dirt, rock, sand near beach), macro variation and distance blending. Foliage by PCG rules.

**Prerequisites:** WORLD_METRICS.md (lane 350 cm, sidewalk 250–400 cm, curb 15 cm), `the OCImportTextures commandlet` (done), road textures from `AUDIT.md`.

---

## Current state

`UOCBuildWorldCommandlet` + `OCWorldBuilderLibrary::BuildRoadMeshes` already build road chunks from `marevia_layout.json`:
- Road strips, junction discs, sidewalks, curbs (15 cm)
- UV0 = (lateral m, along m), UV1 = (half width m, marked lanes per direction) drive markings
- Nanite static mesh chunks, `M_Dev_Road` / `M_Dev_Sidewalk`
- Collision disabled (drive on flattened landscape)

## Step 7 changes

- Replace `M_Dev_Road` / `M_Dev_Sidewalk` with `MI_*` instances from `M_Master_Road` once master + textures exist.
- Lane markings as decals (`M_Decal_LaneMarking`) instead of UV-driven colour where art allows.
- Ground material layering: asphalt + patches/cracks/tire marks/puddles decals; dirt at edges; leaves/trash near curbs; oil stains near parking.
- Landscape auto-material by slope/height: grass, dirt, rock, sand near beach; macro variation + distance blending to kill tiling.
- Foliage by PCG: palms, tropical trees, shrubs, grass clumps; not on roads; denser near water/parks; sparse downtown.

## WORLD_METRICS enforcement

- Lane 350 cm, sidewalk 250–400 cm, curb 15 cm (already in the layout generator's SidewalkW = 2.5 m; check lane widths against 350 cm).
- Streetlights every 25–30 m; trees every 8–12 m on boulevards.

## Done by hand (editor)

1. Import road textures (asphalt, concrete slabs) via `the OCImportTextures commandlet`.
2. Build `M_Master_Road` + `MI_*` instances.
3. Replace dev materials on road chunks.
4. Add decal lane markings (pooled DBuffer decals).
5. Paint landscape auto-material layers (slope/height).
6. Place foliage via PCG rules with seeds and exclusion zones.
7. Commit to git (with LFS).

## Next step on approval

Step 8 — Set dressing: three layers per street (hero focal point, secondary street furniture, tertiary clutter).
