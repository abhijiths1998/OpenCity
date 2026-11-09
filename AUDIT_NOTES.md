## 7. Playtest review (2026-11-09)

Source: your recording `~/Videos/Screencasts/Screencast_20261109_103339.webm` (76 s of the autodrive test in `L_Marevia`, Medium), plus the scripted drive tests (`oc.dev.route`) run the same day.

| # | What the recording shows | Cause | Fix |
|---|---|---|---|
| 1 | Everything reads as cheap: plain boxes with painted-on windows, flat green ground right up to the walls, no sidewalks in alleys, no props, trees, parked cars or people | Nothing is final art yet. Buildings, roads, terrain and water are M3 grey-box (`M_Dev_*`, engine cubes) | The M4a/M4b pipeline below. No downloads have happened yet (`SourceArt/Downloads/` is empty) |
| 2 | Windows smear into streaks at speed | Engine-default motion blur, no Post Process Volume in `L_Marevia` | P0-1 |
| 3 | Pink "…HLOD…" text floating over the mountains | Not in the log. Probably an on-screen engine warning about HLODs. To check in the editor | P0-2 |
| 4 | Distant buildings with flat dark-blue roofs | HLOD1 (merged mesh) drops the per-building colour (per-instance custom data) | P0-2 |
| 5 | Sidewalk skirts stick up like ramps at some junction corners | Road-mesh skirts at junction polygons | P0-3 |
| 6 | One black frame with the camera under the ground | The autopilot's recovery teleport (test tool, not gameplay): the camera lagged through terrain | P0-5 |
| 7 | Buildings stand on bare slopes with no plinth or terrace | The blockout sinks each box 1 m; nothing grades or terraces the plot | Modular kit plinths (step 5); grade plots in the generator |

Found and fixed by the drive tests the same day (committed):

- Buildings 1.6 km wide burying the map: a unit bug in M4a step 4 (8f5f4da)
- 15 landmarks standing on roads; the hospital and station now take a whole block
- Bridge parapets standing across the quay junctions
- Dev materials missing the instanced and Nanite usage flags (default grey material on HLODs and roads)

Still open from the drive tests: the autopilot stops at the west end of the Passerella footbridge, and once in Le Fornaci near (1493, 1050). Streaming kept up everywhere: no NOGROUND at up to 149 km/h.

## 8. Prioritized fix list

**P0: gameplay feel and obvious faults (no downloads needed)**

1. `L_Marevia` Post Process Volume (unbound): manual exposure range for daylight, motion blur 0.2 (off on Low), LUT slot ready for `T_LUT_Marevia`. Cheap Volumetric Cloud on High and above.
2. HLOD look: find the pink HLOD message, and keep the building colours at distance (bake materials in HLOD1, or stretch HLOD0's instanced range).
3. Junction skirts: clip skirt ramps at junction polygons. Plinths under buildings on slopes.
4. A scale-reference mannequin and car in every test map (WORLD_METRICS.md rule; missing in `L_Marevia`).
5. Autopilot: the footbridge and Le Fornaci stops, and camera snap on teleport.
6. M3 leftovers: a perf lap on Medium (`scripts/perf_run.sh L_Marevia 1 1`), and a top-speed streaming run.

**P1: art pipeline, M4a (needs your downloads, section 9)**

7. Import the first tiling sets with `oc_import_textures.py` and build `MI_*` instances from the 8 masters (plaster, painted concrete, asphalt, sidewalk slabs, clay roof tiles, cobble setts, sand, rock, grass).
8. Replace `M_Dev_*` on roads, sidewalks, terrain and water with those instances. Keep `M_Dev_Building` on the blockout cubes until the kit exists.
9. Landscape auto-material (slope and height: grass, dirt, rock, beach sand) with macro variation and distance blending.
10. `MPC_World`, the day/night loop and the first colour-grade LUT (the remaining M4a acceptance items).

**P2: kits and dressing, M4b and later (ART_PIPELINE steps 5-8)**

11. Per-district modular kit and trim sheet. The Blender pipeline is blocked until Blender is repaired (`sudo pacman -Syu`, see OPENCITY_PROJECT.md).
12. Streetlights every 25-30 m, trees every 8-12 m on boulevards, then hero/secondary/tertiary dressing by PCG.

Low priority: engine template content (mannequin and vehicle textures) doesn't follow the `T_*_BC/_N/_ORM` naming. Leave it until those assets are replaced.

## 9. Downloads

Every slug below was checked against the Poly Haven API on 2026-11-09. Download at **2K** for tiling sets and **1K** for props. Put them in `SourceArt/Downloads/<slug>/` and add a row to `ASSET_SOURCES.md` and `SourceArt/LICENSES.csv`. `Tools/fetch_assets.py` can fetch the textures itself from `SourceArt/manifest.json`.

**Tiling textures (Poly Haven, CC0): `https://polyhaven.com/a/<slug>`**

| Use | Slugs (pick one or two per row) |
|---|---|
| Building plaster, Città Alta and Le Colline | `plastered_wall`, `white_stucco`, `white_stucco_02`, `clay_plaster`, `blue_plaster_weathered` |
| Painted concrete, Le Fornaci, Lungomare | `beige_wall_001`, `beige_wall_002`, `chipped_concrete`, `concrete_block_wall` |
| Roof tiles | `clay_roof_tiles`, `clay_roof_tiles_02`, `clay_roof_tiles_03` |
| Asphalt | `asphalt_02`, `asphalt_04`, `asphalt_07` (plus `clean_asphalt` already in the manifest) |
| Sidewalk slabs and plazas | `concrete_pavers_02`, `hexagonal_concrete_paving`, `worn_concrete_floor` |
| Old-town setts | `cobblestone_01`, `cobblestone_floor_01`, `cobblestone_embedded_asphalt` |
| Port metal | `corrugated_iron`, `rusty_corrugated_iron`, `rusty_metal_shutter`, `container_side`, `painted_metal_shutter` |
| Terrain | `coast_sand_01` (beach), `grass_path_2`, `brown_mud_rocks_01`, `cliff_side`, `red_laterite_soil_stones` (hill foundations) |

**Models (Poly Haven, CC0, 1K): `https://polyhaven.com/a/<slug>`**

`street_lamp_01`, `street_lamp_02`, `modular_street_seating`, `metal_trash_can`, `trashbag`, `fire_hydrant`, `water_manhole_cover`, `modular_electricity_poles`, `outdoor_table_chair_set_01`, `plastic_crate_01`, `wooden_crate_01`, `Barrel_01`, `ceramic_pot`, `modular_chainlink_fence`, `island_tree_01`, `island_tree_02`, `fern_02`, `grass_medium_01`, `dutch_ship_medium` (Darsena).

**HDRIs (Poly Haven, CC0, 2K):** `sunny_country_road` (day), `cobblestone_street_night` (night). Use them for reference and look-dev only, because the game sky is Sky Atmosphere.

**Not on Poly Haven, so needs Fab free/monthly-free or Blender** (license check on each one): palm trees, parked scooters, a market stall, traffic cones and bollards, shop signage boards. ambientCG has more tiling sets if a row above doesn't look right: `https://ambientcg.com/list?q=<term>&type=Material`.

## 10. Decisions needed before building art

1. **Setting.** The new brief offers a South Asian coastal city "or your choice". `STORY.md` and `ART_BIBLE.md` already define Marevia as Mediterranean, with the brief's five district types mapped onto it (Città Alta = old town and market, Porto Vecchio = port, Lungomare = beach road, Le Fornaci = industrial and station, Le Colline = hillside). Recommendation: keep Marevia.
2. **Fab free and Megascans.** The new brief allows them with a license check each time, but `CLAUDE.md` says "No Fab, Bridge or Megascans dependency". If you approve, I'll change CLAUDE.md to allow them per asset with a ledger row.
3. **VRAM.** The brief says 4 GB. The log reports an RTX 3050 **6 GB** Laptop GPU, and ARCHITECTURE.md budgets 5,000 MiB. Keep the 5,000 MiB budget, or plan to 4 GB for headroom?
4. **Map size.** The brief says 2 × 2 km. Marevia is 2 × 2.8 km including the mountain band. Recommendation: keep it.
5. **Lightmap UVs.** The brief asks for a second UV channel. The project is fully dynamic (Lumen, `r.AllowStaticLighting=False`), so lightmap UVs are never used. Recommendation: skip them in the Blender export.
