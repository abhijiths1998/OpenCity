# OpenCity — M4a Step 4: Blockout (grey-box district)

**Goal (from the pipeline):** Build the 2×2 km district as a grey-box blockout using road splines and simple volumes, driven by PCG. **Validate gameplay before art:** driving at 120 km/h feels right, sightlines to landmarks, chase routes, alleys, shortcuts, vertical variety (hills, flyovers, rooftops). Show top-down map screenshots and 5 ground-level screenshots per district for approval.

**Status:** The blockout already exists from M3 (`oc_m3_world.py` + `OCWorldBuilderLibrary`). This step **re-aligns it to `WORLD_METRICS.md`** and adds validation. No final art: hard rule 1 — code-generated geometry is blockout only, clearly named `BLOCKOUT_*`.

---

## What the blockout currently produces

| Element | Source | Grey-box state |
|---|---|---|
| Landscape (coast, hills, Monte Ferrale) | `marevia_height.png` → `create_landscape_from_heightmap` | `M_Dev_Terrain` grey-box material; height colours only |
| Road graph + road chunks | `marevia_layout.json` roads → `BuildRoadMeshes` | Nanite road meshes, `M_Dev_Road` / `M_Dev_Sidewalk`, UV-driven markings, 15 cm curbs |
| Buildings | `marevia_layout.json` buildings → `BuildBuildings` | Instanced `/Engine/BasicShapes/Cube`, `M_Dev_Building`, **now snapped to 100 cm grid / 320 cm storeys / `BLOCKOUT_*` labels** |
| Lighting & sky | `L.daylight()` (oc_lib) | Sun, SkyAtmosphere, SkyLight real-time capture, HeightFog, PlayerStart |
| Story layer | `oc_m3_story.py` | Bridges (PonteMobile lift, Molo swing), 20 landmarks, gates, locked-area volumes, deep-water volumes, `DA_MareviaRoads`, `DA_AreaUnlocks` |

The map is 2016 × 3024 m (2 km × 2.8 km playable plus mountain band), seed 1974, 3810 buildings, 448 road nodes, 719 road segments.

---

## What changed for Step 4

`Source/OpenCityEditor/Private/OCWorldBuilderLibrary.cpp` (`BuildBuildings`):

- Building centres snapped to the **100 cm modular grid** (`WORLD_METRICS.md`)
- Widths/depths snapped to **100 cm increments**
- Heights snapped to whole **320 cm storeys** (floor-to-floor)
- Chunk actors labeled **`BLOCKOUT_Buildings_<x>_<y>`** (hard rule 1: blockout clearly named)
- Header doc updated in `OCWorldBuilderLibrary.h`

New script: `Content/Python/oc_verify_blockout.py` — headless (no GPU rendering) validator that loads `L_Marevia` and checks:
- blockout chunk labels are `BLOCKOUT_*`
- instanced mesh is the engine cube (no final-art geometry)
- instance centres on the 100 cm grid
- heights on 320 cm storeys
- road chunks exist
- scale-reference mannequin and car present in test maps

Run: `scripts/run_python.sh Content/Python/oc_verify_blockout.py`

---

## What you need to check in the editor (exact menu paths)

These need the editor (GPU) — I can't run them here because the headless editor launch was blocked. Do them when editor access is approved.

1. **Open the blockout map:** `File → Open Level → /Game/Maps/Marevia/L_Marevia`
2. **Confirm blockout naming:** Outliner → search `BLOCKOUT_` → all building chunks and road chunks should appear. Any building chunk without `BLOCKOUT_` prefix means `oc_m3_world.py` wasn't re-run after the C++ change.
3. **Confirm grid snap:** select a `BLOCKOUT_Buildings_*` actor → Details → Transform → Location X/Y should end in `00` (100 cm grid); Scale Z should be a multiple of `3.2` (320 cm storeys).
4. **Confirm no final art:** search for `SM_` building kits or `MI_*` materials on buildings — should be none. Buildings are still engine cubes with `M_Dev_Building`.
5. **Play the map:** `File → Play Selected Viewport` (or `scripts/play.sh L_Marevia`). Drive at 120 km/h (`oc.dev.drive ...`) — check lane feel, sightlines to the 5 landmarks (Faro, radio mast, Eremo, Diga, Campanile), chase routes, alleys, shortcuts, hill verticality.
6. **Screenshots for approval:**
   - Top-down map screenshot (one, full city) — `HighResShot 4096` from the editor viewport or `oc.dev.viewfrom ...` camera bookmark
   - 5 ground-level screenshots per district (6 districts = 30 screenshots) — street level, looking at landmarks, chase routes, alleys, rooftops/hills

Save screenshots to `Screenshots/2026-11-08/` (or today's date) per Step 10's convention.

---

## Assets you need to download or do by hand

None for pure blockout — it uses engine cubes and dev materials. The blockout is a placeholder; final art replaces it in Steps 5–7.

**By hand (editor):**
- Place a 180 cm mannequin and the reference car in every test map (scale reference per `WORLD_METRICS.md`) — `Place Actors → Character → Mannequin`, `Place Actors → Vehicle`
- Save the map: `File → Save Current Level as...`
- Commit to git (with LFS) after each working step

---

## Gameplay validation checklist (before art)

- [ ] Driving at 120 km/h on the avenue feels right (lane width 350 cm, curb 15 cm)
- [ ] Sightlines to each landmark from its district and from afar
- [ ] Chase routes exist between districts (PonteMobile, Ponte San Rocco gates open/closed)
- [ ] Alleys and shortcuts connect blocks (80–150 m block size)
- [ ] Vertical variety: hills (Le Colline, Monte Ferrale), flyovers, rooftops
- [ ] No floating buildings, no buildings clipped into terrain (they drape on the landscape)
- [ ] Gates lock/unlock correctly per `DA_AreaUnlocks` progression

---

## Next step on approval

With the blockout validated, move to **Step 5 — Modular kits and trim sheets** (per-district `SM_*` kit: wall straight/corner/inner corner, window variants, door/shopfront, balcony, cornice, roof edge, roof props, drainpipes, signage boards; trim sheets + vertex-painted wear). That step needs `oc_import_textures.py` (done) and the master materials (M4a), plus your texture downloads from `AUDIT.md` section 8.
