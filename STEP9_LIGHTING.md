# OpenCity — M4a Step 9: Lighting and Atmosphere

**Goal:** Sky Atmosphere + Directional Light (sun) + Sky Light (real-time capture) + Exponential Height Fog + Volumumetric Clouds (cheap). Manual exposure ranges per time of day. Color-grading LUT `T_LUT_Marevia` matching ART_BIBLE.md. Night: warm sodium streetlights, colored shop signs and window lights (emissive + real lights), light pools on ground. Lumen tuned for low VRAM; Low scalability fallback.

---

## Lighting setup (per map)

| Element | Setup |
|---|---|
| Sky | Sky Atmosphere, Sky Light real-time capture at low update rate |
| Sun / moon | Two Directional Lights; only sun casts shadows |
| Clouds | Volumetric, low-cost; static sky on Low |
| Fog | Exponential Height Fog; volumetric fog on High and up only |
| Street lights | Pooled local lights; at most 4 cast shadows near player, rest emissive + no-shadow |
| GI / shadows | See ARCHITECTURE.md section 8 preset table |
| Post | Filmic tonemapper, LUT `T_LUT_Marevia`, auto-exposure day/night, bloom, subtle vignette, motion blur toggle |

## MPC_World parameters (written only by `UOCWorldStateSubsystem`)

| Parameter | Type | Range |
|---|---|---|
| Wetness | Scalar | 0–1 |
| PuddleAmount | Scalar | 0–1 |
| RainIntensity | Scalar | 0–1 |
| DustAmount | Scalar | 0–1 |
| SnowAmount | Scalar | fixed 0 in v1 |
| TimeOfDay | Scalar | 0–24 |
| NightFactor | Scalar | 0–1 |
| WindStrength | Scalar | 0–1 |
| WindDirection | Vector | normalised XY |

## Night setup

- Warm sodium streetlights (emissive + pooled real lights, ≤4 shadow-casting near player)
- Colored shop signs and window lights (emissive, per-instance random)
- Light pools on ground (spotlights with falloff)
- Deep blue sky, stars faint

## Done by hand (editor)

1. Add Post Process Volume to each map: `Place Actors → Post Process Volume`; set exposure + LUT `T_LUT_Marevia`.
2. Confirm Sky Atmosphere, Directional Light, Sky Light (real-time capture), Exponential Height Fog on every map.
3. Build `T_LUT_Marevia` color grade matching ART_BIBLE.md mood per time of day.
4. Place sodium streetlights every 25–30 m along boulevards.
5. Test day / sunset / night; capture screenshots for each.
6. Commit to git (with LFS).

## Next step on approval

Step 10 — Visual QA loop: camera bookmarks, screenshot script, critique checklist, frame time / VRAM per viewpoint.
