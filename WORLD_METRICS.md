# OpenCity — WORLD_METRICS.md (M4a Step 3)

**1 Unreal Unit (UU) = 1 cm.** All measurements below are in centimetres unless noted. These numbers are locked for the project and enforced in all world-building scripts (`oc_m3_world.py`, `OCWorldBuilderLibrary`, PCG, Blender kitbash import, and any kit/trim-sheet tooling).

---

## Human scale

| Item | Value | Notes |
|---|---|---|
| Player height | 180 cm | Eye line ~160 cm; camera height follows |
| Door opening | 220 × 110 cm | Height × width, clear opening |
| Floor-to-floor height | 320 cm | Storey-to-storey |
| Interior ceiling | 280 cm | Finished ceiling below structure |
| Stair riser | 17 cm | Max comfortable step |
| Stair tread | 28 cm | Min going |

**Scale reference:** every test map contains a 180 cm mannequin and a reference car (the player's motorbike or a coupe) so scale can be checked at a glance.

---

## Roads and streets

| Item | Value | Notes |
|---|---|---|
| Road lane width | 250-375 cm by class | Per road profile (D.M. 5/11/2001): autostrada 375, urban district 300, local 275, centro storico 250; full table in `INFRA_DESIGN.md` section 4 and `SourceArt/Infra/road_profiles.json` |
| Sidewalk width | 80-600 cm by class | Minimum 150 clear (200 with kiosks or bins); centro storico 60-100, viale porticoes 400, lungomare promenade 600 |
| Carriageway cross slope | 2.5% | D.M. 5/11/2001 section 5.2.7 |
| Parking bay | 200 × 500 parallel, 230 × 500 perpendicular | Lines 12 cm: blue paid, white free, yellow reserved |
| Curb height | 15 cm | Road to sidewalk |
| City block | 80–150 m | Block dimension |
| Streetlight spacing | 25–30 m | Along boulevards and main streets |
| Boulevard tree spacing | 8–12 m | Trees every 8–12 m |
| Intersection corner radius | 600–900 cm | Curb return |
| Manhole / drain spacing | ~40–60 m | Along road centreline |

---

## Building grid (modular kits)

| Item | Value | Notes |
|---|---|---|
| Module increments | 100 / 200 / 400 cm | Wall straight, corner, inner corner, window, door |
| Pivot | Bottom corner | Kit pieces pivot at bottom corner, snap to grid |
| Snap | Grid snap | All kit pieces snap to the 100 cm grid |
| Floor-to-floor | 320 cm | Matches human scale |
| Ground-floor detail | Highest | Shops, shutters, signs, lights; upper floors simpler |
| Rooftops | Always dressed | Water tanks, AC units, dish antennas, vents |

---

## Texel density

| Surface class | Texel density | Max texture size | Notes |
|---|---|---|---|
| Hero / near-player surfaces | 512 px/m | 2K | Player-touch surfaces, hero props |
| Environment | 256 px/m | 2K | Building walls, roads, ground |
| Distant | 128 px/m | 1K | Far buildings, distant terrain |
| Props | 256 px/m | 1K | Benches, bins, street furniture |

No 4K/8K textures in-game. Tiling textures for large surfaces; hero textures reused via instances.

---

## Enforcement in scripts

### `oc_m3_world.py` / `OCWorldBuilderLibrary` (C++)
- Building blockouts and final kit pieces are placed on the 100/200/400 cm grid with bottom-corner pivots.
- Road lanes are 350 cm wide; sidewalks 250–400 cm; curbs 15 cm.
- City blocks generated from the road graph stay within 80–150 m.
- Streetlights placed every 25–30 m; boulevard trees every 8–12 m.

### Texture import (`oc_import_textures.py`, planned)
- Hero textures capped at 2K (512 px/m basis), props at 1K, tiling reused heavily.
- Import naming follows CLAUDE.md: `T_<name>_BC / _N / _ORM / _E / _M`.
- Compression: Normalmap for `_N`, Masks for `_ORM`, sRGB on `_BC`/`_E`.

### Blender kitbash pipeline (Step 6)
- Modular pieces built at exact WORLD_METRICS sizes, beveled edges, UVs to trim sheet, second UV channel for lightmaps.
- LODs and `UCX_*` collision meshes generated.
- Exported FBX with correct units/axes; UE import script applies naming, materials, settings.

### Validation
- Editor validators in `OpenCityEditor` reject wrongly named assets, wrong texture settings, and materials not using approved masters.
- A metrics check (planned as an editor validator) flags meshes whose bounds are not on 100 cm increments, door openings not 220×110 cm, and floor-to-floor not 320 cm.

---

## Next step on approval

With `AUDIT.md`, `ART_BIBLE.md` and `WORLD_METRICS.md` in hand, wait for approval before building final art (master materials, texture import, kit replacement of blockout geometry).
