# Marevia buildings: making them good

Work order from the buildings review of commit `fbdd928` (2026-11-13). Variety is designed in at five
scales (city, street, lot, facade, detail); generated at build time by seeded commandlets, never at
runtime; everything stays instanced. After every phase: `scripts/build.sh`, `scripts/test.sh`,
`scripts/build_world.sh` (`--regen` when the layout changed), `scripts/build_hlod.sh`,
`scripts/qa_shots.sh`, `scripts/perf_run.sh` (Medium 60 FPS, VRAM < 5,000 MiB).

Status 2026-11-13: phases 0-6 implemented (commits aa93ca592, 13ffe8c75, c4042eedb and the rebuild after).
Tuning lives in `/Game/Environment/Buildings/DA_BuildingStyles`; contract in `Tools/kit/KIT_SPEC.md`.

## Findings (review) and status

| # | Finding | Status |
|---|---|---|
| 1 | Bay type rolled per cell, per storey | Fixed: columns chosen once, stacked (OCFacadePlan) |
| 2 | No facade composition, door anywhere | Fixed: symmetric fronts, door on the axis, 4 m centre column for portone and entrances |
| 3 | Slits between 79% of buildings; full facades against neighbours | Fixed: shared wall or alley >= 2.5 m (lint); walls behind neighbours not built |
| 4 | One kit for seven districts | Fixed: 21 archetypes; plaster, brick (warehouses) and corrugated (sheds); hotel fronts |
| 5 | One window for every building | Fixed: 8 window families + closed, half, blind states per window |
| 6 | No base / middle / top | Fixed: ground-floor band and splash (material), string course, tall first-floor windows, attic windows |
| 7 | Every building one box | Fixed: L/U wings, rear extensions, villa towers, recessed hotel tops, chamfered corner buildings |
| 8 | Hip roof wrong on long buildings | Fixed: hip ends + stretched ridge (equal pitch); gables for terraces and sheds |
| 9 | Chimneys ignore the roof | Fixed: on the roof surface (hip or gable) |
| 10 | Landmarks are ordinary houses | Fixed for church (portal, rose, pediment, gable), civic and station (entrance, pilasters, canopy) |
| 11 | Glass flat and dark | Fixed: lit state, warm/cool colour and day blinds per window (GLASS_HLSL) |
| 12 | Grime repeats per bay | Fixed: stain offset per instance on both axes |
| 13 | Trim never varies | Fixed: trim palette per archetype (limestone, grey stone, painted white, brick red) |
| 14 | No signage | Fixed: 12 shop sign letterings, blade signs, awnings |
| 15 | Plinth shallower than the slope | Fixed: downhill bays get plain lower storeys |
| 16 | Overlap check on a 1 m raster | Fixed: exact oriented-rectangle test, `octool buildings-lint` in `make_layout.sh` |

## Phases

- [x] **0.** `OCFacadePlan` (OpenCityCore, 3 tests), hashed sub-seeds, styles in `DA_BuildingStyles`
  (`UOCBuildingStyleSet`, created with defaults on first build), `make_layout.sh -seed=N`,
  `build_world.sh -kitseed=N`.
- [x] **1.** Columns, symmetry, balcony patterns, party walls, street test at 1/4, 1/2, 3/4, style gates,
  landmarks, roof guard, chimney Z, roof clutter grid.
- [x] **2.** Zero-or-alley gaps, exact overlap test, terrace building line, height runs round a street level,
  massing, city fields (age, wealth, upkeep), street character seeds, archetypes per lot, corner buildings,
  variety reports (signature repeats within 60 m, neighbour contrast, piece share, street contrast).
- [x] **3.** 60 new pieces (KIT_SPEC "Piece families"). CC0 corrugated iron, red brick, linen added.
- [x] **4.** Hip ends + ridge, gables (plaster, brick, corrugated ends), metal ridges on sheds, gutters.
- [x] **5.** Per-window glass, grime per bay, trim palette, ground-floor band and splash.
- [x] **6.** Shop signs, blade signs, awnings, wall AC units, cables, laundry, flower boxes, door lanterns.

## Open

- Variety report of the 2026-11-13 build: hills `Wall_400` 35% of facade cells (villa side walls);
  28 street fronts still repeat within 60 m after one re-plan.
- `WindowTall_400` is 3.5k triangles (KIT_SPEC budget ~3.3k): the louvres dominate.
- Railing balusters are 7 mm: check for shimmer at distance in the editor.
- Santa Lucia's kit volume reads dark-topped from the Città Alta elevated QA camera (seen before this work).
- Gable end walls leave a thin wedge under the eaves at the wall corners (hidden by the crown on most).
