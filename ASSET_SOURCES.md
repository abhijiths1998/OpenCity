# OpenCity — ASSET_SOURCES.md (asset ledger)

**Purpose:** Track every external asset used in the project: name, source URL, author, license, where it's used. Required by hard rule 3 — no asset enters the game without a row here and a matching row in `SourceArt/LICENSES.csv`.

**Hard rule:** final art comes only from curated sources you download:
- Poly Haven (models, textures, HDRIs, CC0)
- ambientCG (textures, CC0)
- Fab free and monthly-free assets, Megascans you own (check license each time)
- Your own Blender kitbash pieces

CC0 originals are **not committed**. `SourceArt/manifest.json` lists them and `Tools/fetch_assets.py` re-downloads them, saving LFS quota.

---

## Downloaded assets (to fill in as you download)

| Asset | Source | URL | Author | License | Where used | Date |
|---|---|---|---|---|---|---|
| Marevia building kit (26 pieces, `SM_Kit_*`) | Own work: `Tools/blender/make_kit.py` (Blender 4.2) | n/a | OpenCity | Project-owned | All buildings (`BuildKitBuildings`) | 2026-11-09 |
| `plastered_wall` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/plastered_wall | Poly Haven | CC0 | Kit: building walls (MI_Kit_Wall) | 2026-11-09 |
| `white_stucco` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/white_stucco | Poly Haven | CC0 | Kit: Lungomare and Le Colline walls (alternate plaster) | 2026-11-09 |
| `old_sandstone_02` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/old_sandstone_02 | Poly Haven | CC0 | Kit: stone trims, sills, cornices, quoins, plinths (MI_Kit_Trim) | 2026-11-09 |
| `wood_peeling_paint_weathered` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/wood_peeling_paint_weathered | Poly Haven | CC0 | Kit: shutters, doors, frames, tinted per building (MI_Kit_Wood) | 2026-11-09 |
| `metal_plate` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/metal_plate | Poly Haven | CC0 | Grey-box metal on landmarks (MI_Box_Metal) | 2026-11-09 |
| `painted_metal_shutter` (BC, N, ORM, 1K) | Poly Haven | https://polyhaven.com/a/painted_metal_shutter | Poly Haven | CC0 | Kit: rolling shutters, railings, pipes, roof props (MI_Kit_Metal) | 2026-11-09 |
| `clay_roof_tiles` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/clay_roof_tiles | Poly Haven | CC0 | Kit: hip roofs (MI_Kit_Roof) | 2026-11-09 |
| `brushed_concrete` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/brushed_concrete | Poly Haven | CC0 | Kit: flat roofs, copings, balcony slabs (MI_Kit_Concrete) | 2026-11-09 |
| `asphalt_02` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/asphalt_02 | Poly Haven | CC0 | Roads | 2026-11-09 |
| `concrete_pavers_02` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/concrete_pavers_02 | Poly Haven | CC0 | Sidewalks and plazas | 2026-11-09 |
| `cobblestone_floor_01` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/cobblestone_floor_01 | Poly Haven | CC0 | Old-town alleys (setts) | 2026-11-09 |
| `coast_sand_01` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/coast_sand_01 | Poly Haven | CC0 | Landscape: beach | 2026-11-09 |
| `aerial_grass_rock` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/aerial_grass_rock | Poly Haven | CC0 | Landscape: grass and scrub | 2026-11-09 |
| `brown_mud_rocks_01` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/brown_mud_rocks_01 | Poly Haven | CC0 | Landscape: dirt and slopes | 2026-11-09 |
| `cobblestone_square` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/cobblestone_square | Poly Haven | CC0 | Road paving: sampietrini (centro storico streets) | 2026-11-10 |
| `granite_tile` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/granite_tile | Poly Haven | CC0 | Road paving: lastricato stone slabs (centro storico alleys, Passerella) | 2026-11-10 |
| `gravel_floor` (BC, N, ORM, 1K) | Poly Haven | https://polyhaven.com/a/gravel_floor | Poly Haven | CC0 | Road paving: strade bianche (tinted pale) | 2026-11-10 |
| `cliff_side` (BC, N, ORM, 2K) | Poly Haven | https://polyhaven.com/a/cliff_side | Poly Haven | CC0 | Landscape: rock, cliffs, mountains | 2026-11-09 |
| `street_lamp_01` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/street_lamp_01 | Poly Haven | CC0 | Street lights along streets and avenues | 2026-11-09 |
| `street_lamp_02` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/street_lamp_02 | Poly Haven | CC0 | Street lights, old town variant | 2026-11-09 |
| `modular_street_seating` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/modular_street_seating | Poly Haven | CC0 | Benches at junctions and plazas | 2026-11-09 |
| `metal_trash_can` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/metal_trash_can | Poly Haven | CC0 | Bins at junctions | 2026-11-09 |
| `fire_hydrant` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/fire_hydrant | Poly Haven | CC0 | Street clutter | 2026-11-09 |
| `island_tree_01` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/island_tree_01 | Poly Haven | CC0 | Boulevard and seafront trees | 2026-11-09 |
| `island_tree_02` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/island_tree_02 | Poly Haven | CC0 | Boulevard and seafront trees | 2026-11-09 |
| `fern_02` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/fern_02 | Poly Haven | CC0 | Planters, verges | 2026-11-09 |
| `ceramic_pot` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/ceramic_pot | Poly Haven | CC0 | Potted plants by doors | 2026-11-09 |
| `plastic_crate_01` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/plastic_crate_01 | Poly Haven | CC0 | Shopfront clutter | 2026-11-09 |
| `wooden_crate_01` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/wooden_crate_01 | Poly Haven | CC0 | Port and shopfront clutter | 2026-11-09 |
| `Barrel_01` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/Barrel_01 | Poly Haven | CC0 | Port clutter | 2026-11-09 |
| `outdoor_table_chair_set_01` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/outdoor_table_chair_set_01 | Poly Haven | CC0 | Cafe tables on Lungomare | 2026-11-09 |
| `modular_electricity_poles` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/modular_electricity_poles | Poly Haven | CC0 | Power poles in Le Fornaci and the hills | 2026-11-09 |
| `boulder_01` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/boulder_01 | Poly Haven | CC0 | Distant scenery: east-coast hills, scaled up (oc_m3_water.py) | 2026-11-09 |
| `namaqualand_boulder_03` (model, glTF, 1K textures) | Poly Haven | https://polyhaven.com/a/namaqualand_boulder_03 | Poly Haven | CC0 | Distant scenery: peaks north of Monte Ferrale, scaled up (oc_m3_water.py) | 2026-11-09 |

*(Rows are added when you download each texture/model/HDRI from the AUDIT.md download list.)*

---

## Existing engine content (placeholders)

| Asset | Source | License | Where used | Notes |
|---|---|---|---|---|
| `Cube` (BasicShapes) | Unreal Engine | Epic EULA (template content) | M3 blockout buildings | Temporary; replaced by modular kit |
| UE Mannequin (Manny / Quinn) | Unreal Engine | Epic EULA | Character placeholder | See ARCHITECTURE.md — custom characters decided after M6 |

---

## Engine template content in use

| Asset | License | Where used | Notes |
|---|---|---|---|
| Vehicle template | Epic EULA (template content, for use in Unreal projects) | `AOCVehicle` visual parts | Placeholder until Blender replacements |
| `SM_SportsCar_Body` (from the template's `SM_SportsCar`) | Epic EULA, as above | Coupe body | Same geometry; windows moved to their own `Glass` slot by `Tools/blender/split_car_glass.py` (source FBXs in `SourceArt/Vehicles/SportsCar/`) |

---

## How to add a row

1. Download the asset into `SourceArt/Downloads/` (not committed).
2. Add a row above (name, source URL, author, license, where used, date).
3. Add a matching row to `SourceArt/LICENSES.csv` (name, source URL, author, license, where used).
4. Run the import script (`oc_import_textures.py` for textures; the Blender UE import script for meshes — Step 6).
5. Run the asset validator.
6. Check in the test map under day and night.
7. Commit to git (with LFS).
