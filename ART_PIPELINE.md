# OpenCity: Art Pipeline

Status: **draft, awaiting approval**. Instruction counts and memory figures here are Targets until measured in M4.

The pipeline runs in ten steps. **Step 1 (audit → AUDIT.md), Step 2 (art bible → ART_BIBLE.md) and Step 3 (world metrics → WORLD_METRICS.md) are done and await your approval.** Steps 4–10 follow on approval.

| Step | Deliverable | Status |
|---|---|---|
| 1 — Audit | `AUDIT.md` + `oc_audit.py` | Done (baseline = 100% grey-box) |
| 2 — Art bible | `ART_BIBLE.md` | Done |
| 3 — World metrics | `WORLD_METRICS.md` | Done |
| 4 — Blockout | grey-box district via road splines + PCG | Pending approval |
| 5 — Modular kits + trim sheets | per-district `SM_*` kit, trim sheets | Pending approval |
| 6 — Blender kitbash pipeline | headless Blender scripts + UE import script | Pending approval |
| 7 — Roads, ground, landscape | splines, ground layers, auto-material | Pending approval |
| 8 — Set dressing | three layers per street | Pending approval |
| 9 — Lighting + atmosphere | sky, sun, fog, exposure, LUT, night | Pending approval |
| 10 — Visual QA loop | camera bookmarks + screenshot script | Pending approval |

See `ASSET_SOURCES.md` for the asset download ledger (hard rule 3).

## 1. Look

**Marevia** is a fictional present-day Mediterranean-style port. It belongs to no real country and copies no real or game city.

| Element | Direction |
|---|---|
| Materials | Limestone, sun-faded plaster, terracotta, painted shutters, worn asphalt, rust and salt stains at the port |
| Palette | Warm stone and ochre, deep sea blue, faded greens, with small accents of red and teal |
| Hero lighting | Late-afternoon golden hour. This is the look to perfect first |
| Grade | Warm highlights, slightly cool shadows, gentle contrast, never heavily saturated |
| Night | Warm sodium street lights against a deep blue sky |
| Weather priority | Clear, then cloudy, then rain as an occasional storm |

### Originality rules

- All names, brands, logos, signage, graffiti and radio content are invented for this game.
- Vehicles are invented designs, not recognisable real models.
- No layouts, landmarks, characters, UI, music or story taken from any existing game.

## 2. Asset sources

| Need | Source | Notes |
|---|---|---|
| Tiling materials, HDRIs | Poly Haven, ambientCG | CC0 |
| Props, foliage | Poly Haven models, Blender | CC0 or original |
| Stylised filler props | Kenney | CC0, only where style fits |
| Buildings | Blender modular kits with trim sheets | Original. **Biggest manual workload** |
| Cars | UE vehicle template first, Blender replacements later | Template content is for use in Unreal projects |
| Motorbike | Blender | Original, from grey-box up |
| Characters | UE Mannequin (Manny and Quinn) | Placeholder for a long time. Custom characters are a separate decision after M6 |
| Animation | Template sets; Mixamo is optional | Mixamo is not CC0, so read its terms first |
| Sound | Freesound CC0 only, and original | Filter by license |

Megascans and Fab are out of scope: free Megascans ended in 2024, and Fab on Linux is unreliable. Avoid AI-generated assets unless you decide otherwise, as their licensing is unclear.

### License ledger

Every external asset gets a row in `SourceArt/LICENSES.csv`: asset, source URL, author, license, download date. A validator flags imported assets missing from the ledger.

CC0 originals are not committed. `SourceArt/manifest.json` lists them and `Tools/fetch_assets.py` re-downloads them, saving LFS quota. Check Poly Haven's API terms before scripting against it. The assets are CC0, but the API may have separate terms. The script uses a generic client name with no personal details.

## 3. Texture specification

### Naming

`T_<Asset>_<Suffix>` with suffix `BC`, `N`, `ORM`, `E` or `M`. Example: `T_PlasterAged_ORM`.

### Import settings

| Suffix | Content | sRGB | Compression | Notes |
|---|---|---|---|---|
| `BC` | Base color (alpha optional) | On | `TC_Default` | No baked lighting or AO |
| `N` | Normal | Off | `TC_Normalmap` (BC5) | **DirectX format, Y down** |
| `ORM` | R occlusion, G roughness, B metallic | Off | `TC_Masks` | Packed by script |
| `E` | Emissive | On | `TC_Default` | Often 512 is enough |
| `M` | Masks | Off | `TC_Masks` | Channel meaning in asset notes |

**Normal map gotcha:** Poly Haven and ambientCG offer OpenGL and DirectX normals. Unreal expects DirectX. Download the DirectX variant where offered. Otherwise the import script flips the green channel.

**Packing:** sources supply separate AO, roughness and metal maps. `Tools/pack_orm.py` packs them offline, and missing maps default to AO white and metal black.

### Resolution limits

| Asset class | Max | Texel density | Texture group |
|---|---|---|---|
| Hero vehicles, characters | 2K | 512 px/m | Vehicle, Character |
| Hero props | 2K | 512 px/m | World |
| Standard props | 1K | 256 px/m | World |
| Environment | 1K | 256 px/m | World |
| Tiling surfaces, trim sheets | 2K, reused heavily | 256 to 512 px/m effective | World |
| Decals | 1K, atlased where possible | n/a | Effects |
| UI | Native size, no mips | n/a | UI |

Textures use power-of-two sizes with mips on, except UI. Large surfaces never get unique textures. They use tiling plus macro variation. `T_*` assets above their class limit fail validation.

### Virtual Texturing and streaming

- **Runtime Virtual Texturing:** Landscape and road blending only. This is where it clearly pays off.
- **Streaming Virtual Texturing:** off for v1. It adds shader variants and sampling cost for little benefit at these texture sizes. Revisit at M11 if the pool is under pressure.
- **Streaming pool:** see `ARCHITECTURE.md` section 8. M4 replaces the Target values with measured ones.

## 4. Import workflow

`Content/Python/oc_import_textures.py`:

```bash
scripts/run_python.sh Content/Python/oc_import_textures.py --src SourceArt/Textures/PlasterAged --dest /Game/Environment/Materials/PlasterAged --parent M_Master_Opaque
```

The script will:

1. Parse the suffix from each file name, and stop on unknown names rather than guess.
2. Import through `AssetImportTask`.
3. Apply sRGB, compression, texture group, LOD bias and max size from one table.
4. Create `MI_<Asset>` from the named master and assign textures by parameter name.
5. Print a summary with any warnings.

Paths are case-sensitive on Linux, so file names must match exactly.

### Blender export

| Rule | Value |
|---|---|
| Units | Metric, 1 unit = 1 m in Blender, exports as cm in Unreal |
| Forward and up | X forward, Z up, checked with a test cube |
| Pivot | Base centre for props, grid-aligned corner for modular pieces |
| Modular grid | 1 m, with 3 m floor height |
| Collision | `UCX_<Mesh>` for custom collision |
| LODs | Nanite meshes need no LOD chain. All others get at least 3 |
| UVs | Channel 0 for textures, channel 1 only when needed |
| Naming | `SM_` static, `SK_` skeletal |

## 5. Master materials

All are generated by Python with `MaterialEditingLibrary` under `Content/Python/materials/`, so they can be rebuilt from clean. Artists and scripts work with **Material Instances only**. Shared logic lives in Material Functions (`MF_Wetness`, `MF_MacroVariation`, `MF_WindCheap`).

Limits per master: at most 3 static switches, at most 8 texture samples. Prefer scalar lerps over static switches. Use Quality Switch nodes for Low.

| Master | Features | Base pass target |
|---|---|---:|
| `M_Master_Opaque` | PBR, detail normal, macro variation, world-aligned option, vertex-color dirt, moss and wear | 180 |
| `M_Master_Building` | Trim sheets, grime, edge wear, rain streaks, night windows randomised per instance | 220 |
| `M_Master_Road` | Asphalt, cracks, tire marks, wet puddles, RVT blending. Lane markings are decals | 200 |
| `M_Master_CarPaint` | Clear Coat, flakes, dirt, scratches and damage | 220 |
| `M_Master_Glass` | Reflections, dirt, rain droplets. Cheap translucency, no refraction on Low | 150 |
| `M_Master_Foliage` | Two-sided foliage model, subsurface, cheap wind | 120 |
| `M_Master_Character` | Skin subsurface, cloth, simple hair | 200 |
| `M_Master_Water` | Ocean, shoreline foam. Depends on the M3 Water decision | 250 |

Implementation notes:

- **Car paint:** color, dirt and damage come through **Custom Primitive Data**, so no dynamic material instances are needed per car.
- **Building windows:** per-instance random drives which windows light up, with a world-position hash for non-instanced meshes.
- **Foliage:** masked materials use regular LODs as you specified. Check Nanite foliage status in 5.8 during M4b.
- **Shading:** standard models, with Substrate off.

### Decals

Materials: `M_Decal_Grime`, `M_Decal_Graffiti` (original designs), `M_Decal_Puddle`, `M_Decal_Blood`, `M_Decal_BulletHole`, `M_Decal_TireSkid`, `M_Decal_LaneMarking`.

Runtime decals are pooled, 64 on Medium, oldest removed first, with fade and distance culling. DBuffer decals are on, so they work with lit surfaces.

## 6. Global world parameters

`MPC_World`, written only by `UOCWorldStateSubsystem`:

| Parameter | Type | Range |
|---|---|---|
| `Wetness` | Scalar | 0 to 1 |
| `PuddleAmount` | Scalar | 0 to 1 |
| `RainIntensity` | Scalar | 0 to 1 |
| `DustAmount` | Scalar | 0 to 1 |
| `SnowAmount` | Scalar | Fixed at 0 in v1 |
| `TimeOfDay` | Scalar | 0 to 24 |
| `NightFactor` | Scalar | 0 to 1 |
| `WindStrength` | Scalar | 0 to 1 |
| `WindDirection` | Vector | Normalised XY |

`MF_Wetness` darkens base color, lowers roughness, and is shared by every opaque master. Wetness rises quickly in rain and dries slowly.

## 7. Lighting and sky

| Element | Setup |
|---|---|
| Sky | Sky Atmosphere, Sky Light with real-time capture at low update rate |
| Sun and moon | Two Directional Lights, with only the sun casting shadows |
| Clouds | Volumetric, low-cost, static sky on Low |
| Fog | Exponential Height Fog, volumetric fog on High and up only |
| Street lights | Pooled local lights. At most 4 cast shadows near the player, the rest use emissive plus no-shadow lights |
| GI and shadows | See preset table in `ARCHITECTURE.md` section 8 |
| Post | Filmic tonemapper, LUT `T_LUT_Marevia`, auto-exposure tuned for day and night, light bloom, subtle vignette, motion blur toggle |

Nanite is used for static environment meshes. Regular LODs are for animated, skeletal or masked content. With VSM, non-Nanite geometry costs more, so check whether Medium should stay on VSM or use cascades.

## 8. Rain

One value, `RainIntensity`, drives:

1. Wet surfaces through `Wetness`
2. Puddles on roads through `PuddleAmount`
3. Niagara rain, attached to the camera, with splashes near the player only
4. Windshield droplets in `M_Master_Glass`
5. Audio and vehicle grip

## 9. Performance review

At each visual milestone, report:

- Shader Complexity and Quad Overdraw hot spots with screenshots
- `stat unit`, `stat gpu`, VRAM used and budget in `stat unit`
- Unreal Insights trace from the fixed test path
- Shader count changes
- Instruction count for each master

Hand-built node graphs are a last resort. If one is needed, I'll give you the exact node list, connections and defaults.

## 10. Adding an asset

1. Add the asset to the manifest and license ledger.
2. Name the files by convention, and export normals in DirectX format.
3. Run the import script.
4. Run the asset validator.
5. Check it in the test map, under day and night.
6. Commit.
