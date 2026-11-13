# Marevia buildings: making them good

Work order from the buildings review of commit `fbdd928` (2026-11-13). Variety is designed in at five
scales (city, street, lot, facade, detail); generated at build time by seeded commandlets, never at
runtime; everything stays instanced (6,000+ buildings, 5,000 MiB VRAM budget). After every phase:
`scripts/build.sh`, `scripts/test.sh`, `scripts/build_world.sh` (`--regen` when the layout changed),
`scripts/build_hlod.sh`, `scripts/qa_shots.sh`, `scripts/perf_run.sh` (Medium 60 FPS, VRAM < 5,000 MiB).

## Findings (review) and status

| # | Finding | Status |
|---|---|---|
| 1 | Bay type rolled per cell, per storey | Fixed (phase 1): columns chosen once, stacked |
| 2 | No facade composition, door anywhere | Fixed: symmetric street fronts, door on the axis or in a corner bay |
| 3 | Slits of 5 cm-2.5 m between 79% of buildings; full facades against neighbours | Fixed (phase 2): shared wall or alley >= 2.5 m; walls behind neighbours hidden |
| 4 | One kit for seven districts | Partly: style gates (no quoins/cornices on sheds, beach); district pieces are phase 3 |
| 5 | One window for every building | Open (phase 3) |
| 6 | No base / middle / top | Partly: plain-window attic storey; string course needs a piece (phase 3) |
| 7 | Every building one box | Open (phase 2.5 massing) |
| 8 | Hip roofs wrong on long buildings | Guarded: pitched only up to 14 m short side and 5 storeys; real fix phase 4 |
| 9 | Chimneys ignore the roof | Fixed: base on the roof plane, stretched 0.9 m clear |
| 10 | Landmarks are ordinary houses | Partly: symmetric, central door, no shops/balconies; own pieces phase 3 |
| 11 | Glass flat and dark | Open (phase 5) |
| 12 | Grime repeats per bay | Open (phase 5) |
| 13 | Trim never varies | Open (phase 5) |
| 14 | No signage | Open (phase 6) |
| 15 | Plinth shallower than the slope | Fixed: downhill bays get plain lower storeys down to the ground |
| 16 | Overlap check on a 1 m raster | Fixed: exact oriented-rectangle test in the layout, `octool buildings-lint` |

## Phases

- [x] **0. Plan separate from placement.** `OCFacadePlan` (OpenCityCore, 3 tests); hashed sub-seeds per
  purpose (`OCSubSeed`). Still to do: styles into a DataAsset (`DA_BuildingStyles`), `-seed=` on the
  layout and world commandlets.
- [x] **1. Assembler rules, no new meshes.** Columns, symmetry, balcony patterns (centre, ends, alternate,
  all; piano nobile), plain attic storey, party walls (per bay, hidden below the neighbour's top),
  street test at 1/4, 1/2, 3/4, style gates, landmarks, roof guard, chimney Z, roof clutter on a grid.
  The string course waits for its piece.
- [ ] **2. Layout.** Done: zero-or-alley gaps, exact overlap test, terrace building line, height runs,
  `octool buildings-lint` in `make_layout.sh`. Open: massing (L/U, recessed top storeys, rear
  extensions), city fields (age, wealth, upkeep) and street character, archetypes per lot, corner
  buildings, variety checks (signature repeat, neighbour contrast).
- [ ] **3. More pieces.** Wall_100, string course, tall/arched/small/closed/half/blind windows, Juliet,
  long balcony, portone, garage, closed shop, awning, chamfered corner, bracketed eaves; port sheds and
  brick (needs CC0 corrugated metal and brick textures in the ledger first); hotel pieces; landmark
  fronts. Selected per style through weighted lists. Check the 7 mm balusters for shimmer.
- [ ] **4. Roofs.** Hip from end caps + stretched ridge; gables for terraces; gutters; flat-roof clutter.
- [ ] **5. Materials.** Lit state and blinds per window, grime offset per bay plus splash/streaks, trim
  tint per style, ground-floor wall band.
- [ ] **6. Dressing.** Shop signs (invented names), awnings, blades, AC units and cables, laundry,
  flower boxes, house numbers, wall lanterns by doors.
