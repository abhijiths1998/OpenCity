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

## Section 4: Italian character (2026-11-13)

| Item | Status |
|---|---|
| 4.1 Height steps (storey height per lot 2.9/3.2/3.5 m, tall ground floors) | Done (layout `storey_h`, `ground_h`; pieces scaled) |
| 4.1 Wings, lean-tos, torrette, recessed tops | Done (`lot`, `on`, `feature`) |
| 4.1 Loggia, arcade (portico), external stair, bridge room, brace arches | Done |
| 4.1 Roof terrace cut-out | Altana on pitched roofs and pergolas on flat roofs; the corner-bay cut-out is not built |
| 4.1 Trapezoid lots on bending streets | Not done: needs a wedge filler piece |
| 4.2 Pitched by default, 33% pitch, 0.75 m eaves (rafters or cove), forms by archetype, own roof per volume, eaves tile row | Done |
| 4.2 Chimney pots, dormers, aerials, altane, bell gable on the church | Done |
| 4.2 Ligurian slate, Campanian vaults | Done (roof_slates_02 CC0, `RoofVault`) |
| 4.3 Portone, piano nobile, attic windows, persiane states, marcapiano, marcadavanzale, bugnato, lesene, cornicione on mensole, bracket balconies, edicola, arches, scarpa | Done; mezzanine windows over shops and the Ligurian shutter flap are not separate pieces |
| 4.4 Ligurian palette, painted surrounds and quoins (tint, not 5 mm geometry), two-tone walls, weighted shutter colours, Campanian whites, weathering by upkeep | Done |
| 4.5 Carruggi | Side alleys between houses are 2.5-4 m; the old-town road profile stays 5 m carriageway (M3, parked) |
| 4.5 Piazzas | Done: 10 junction squares, fronted by arcades and palazzi; no special paving |
| 4.5 Stepped lanes (scalinate) | Not done: needs pedestrian paths on the terrain (M5 pedestrians) |
| 4.5 Garden walls, gates | Done; no cypress rows (no CC0 cypress model in the ledger) |
| 4.6 Volumes in the layout JSON | As records sharing `lot` with `on` and `feature` rather than nested lists |

## Reference look (2026-11-14)

From six reference photos (a domed cathedral with campanile, a round arcaded tower and cathedral square, an
octagonal banded baptistery, a cliffside village in strong colours, shops on a bridge):

- Santa Lucia (Città Alta) is a cathedral complex: marble cladding with dark inlay and blind-arcade galleries,
  marble portal on the square, rose window, pediment with a bell gable, a 20 m ribbed dome on an oculus drum
  over the crossing, a banded apse, a round arcaded campanile with a slight lean, an octagonal baptistery. The
  grey-box apse and campanile are retired. The church fronts its short south end and sits on its square.
- The Passerella carries 32 overhanging shop houses on corbels, open at mid-span.
- Old town colours are saturated (ochre, Genoa red, salmon, orange, pale green, rose, terracotta, lemon), and
  steep old-town ground takes tall narrow tower houses (688, was 349).
- `DA_BuildingStyles` carries a defaults version; an older asset is refilled by the world build (logged).
- Medium perf lap after this: 79.1 FPS average, 45.2 1% low, game VRAM 4,080 MiB (browser open).

## Measurements (2026-11-13, after section 4)

- Build: 8,203 volumes, 814,288 kit instances, 130 piece kinds; roofs 2,202 hip, 3,674 gable and lean-to, 2,327 flat or vaulted.
- Variety: 89% of touching pairs differ in 2+ of storeys, colour, windows, crown; 22 street fronts repeat within 60 m.
- Medium perf lap: 84.5 FPS average, 48.4 1% low, game VRAM peak 4,073 MiB (budget 5,000); browser open.

## Open

- Variety report of the 2026-11-13 build: hills `Wall_400` 35% of facade cells (villa side walls);
  28 street fronts still repeat within 60 m after one re-plan.
- `WindowTall_400` (3.5k) and `WindowNobile_400` (3.7k) are over the ~3.3k triangle budget: the louvres dominate.
- Railing balusters are 7 mm: check for shimmer at distance in the editor.
- Fixed 2026-11-13: distant roofs vanished (and boxes read dark-topped, Santa Lucia included) because
  `AOCKitBuildingChunk` culled unit roof meshes by their unscaled 1 m bounds at 120 m.
- Gable end walls leave a thin wedge under the eaves at the wall corners (hidden by the crown on most).
