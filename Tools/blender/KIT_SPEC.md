# Marevia building kit: piece contract

This file is the interface between the Blender kit generator (`Tools/blender/make_kit.py`), the
UE import (`Content/Python/oc_import_kit.py`) and the building assembler
(`UOCWorldBuilderLibrary::BuildKitBuildings`). Change it only together with all three.

## Units, axes, pivot

- Blender: metres, Z up. Exported FBX: scale so that 1 Blender m = 100 UE cm (FBX "apply unit scale", "FBX All" scale with `apply_scale_options='FBX_SCALE_ALL'` and unit scale 1.0, or glTF which is metres natively; see the import script).
- **Facade pieces**: span X from 0 to the piece width, Z from 0 to the piece height. The **outside face is at Y = 0 and faces -Y**. Wall thickness goes into +Y (0.30 m). Openings are recessed into +Y. Things that stick out (sills, cornices, balconies, shutters, signs, pilasters) go into -Y.
- **Pivot**: the bottom-left-outside corner (0, 0, 0).
- **Roof pieces and props**: pivot at the bottom centre, sized as listed.

## Storey and bay grid (WORLD_METRICS.md)

- Storey (floor to floor): **3.20 m**. Ground floor: **3.20 m** as well.
- Bay widths: **2.00 m** and **4.00 m**. Building widths and depths are whole multiples of 2 m.
- Window: 1.10 m wide × 1.50 m tall, sill at 0.90 m. French door: 1.10 × 2.40 m. Street door: 1.10 × 2.20 m.
- Shopfront opening: 3.40 × 2.60 m in a 4 m bay.

## Material slots (Blender material names = UE slot names)

| Slot | What | UE material |
|---|---|---|
| `Wall` | plaster / painted render | `MI_Kit_Wall` (tiling plaster, tinted per building) |
| `Trim` | stone sills, lintels, cornices, quoins, plinth, door surrounds | `MI_Kit_Trim` (pale limestone) |
| `Wood` | shutters, doors, window frames | `MI_Kit_Wood` (painted wood, tinted per building) |
| `Glass` | window and door glass | `MI_Kit_Glass` (dark glass, lit at night) |
| `Metal` | railings, rolling shutters, drainpipes, AC units, tanks | `MI_Kit_Metal` |
| `Roof` | clay roof tiles | `MI_Kit_Roof` |
| `Concrete` | flat roof slabs, parapet copings, balcony slabs | `MI_Kit_Concrete` |

UV0 is in **metres**, laid out for tiling materials: on walls 1 UV unit = 1 m of surface (box/cube projection in world-scale), so a texture's real size is set by the material, never baked into a piece. Trims and frames use the same metre scale. No lightmap UV channel (the project is fully dynamic).

## Pieces

Names are the exported mesh names; UE adds the `SM_Kit_` prefix (`SM_Kit_Wall_400`).

| Piece | Size W × H (m) | Notes |
|---|---|---|
| `Wall_200`, `Wall_400` | 2 / 4 × 3.2 | plain wall, a 3 cm render band at the floor line |
| `Window_200`, `Window_400` | 2 / 4 × 3.2 | one centred window (two in `Window_400`): recessed frame + glass, stone sill and lintel, **open louvred shutters** folded against the wall |
| `WindowPlain_200`, `WindowPlain_400` | 2 / 4 × 3.2 | as above without shutters (frame only) |
| `Balcony_400` | 4 × 3.2 | centred French door, 0.8 m deep balcony slab with wrought-iron railing, door shutters |
| `Shopfront_400` | 4 × 3.2 | ground floor: stone-framed opening, glazing, half-raised metal rolling shutter, blank sign board above (0.6 m) |
| `Door_200` | 2 × 3.2 | ground floor: panelled street door with stone surround and a step |
| `GroundWindow_400` | 4 × 3.2 | ground floor: two barred windows (iron grilles), sills |
| `Corner_320` | 0.4 × 0.4 × 3.2 | stone quoin pilaster; pivot at the corner it covers, centred on it |
| `Plinth_200`, `Plinth_400` | 2 / 4 × (0.6 above + 3.0 below ground) | stone base; Z from -3.0 to 0.6 so slopes never show a gap |
| `Cornice_200`, `Cornice_400` | 2 / 4 × 0.35 | moulded projecting cornice, sits on top of the last storey |
| `Parapet_200`, `Parapet_400` | 2 / 4 × 0.9 | low parapet wall with a concrete coping, for flat roofs |
| `RoofHip` | 1 × 1 footprint, 1 m ridge height | hip roof with tiles, overhang included; scaled per building (material is world-aligned so scaling never stretches tiles) |
| `RoofFlat` | 1 × 1 × 0.25 | flat roof slab, scaled per building |
| `Prop_WaterTank`, `Prop_ACUnit`, `Prop_Dish`, `Prop_Chimney`, `Prop_RoofHut` | real sizes | roof clutter |
| `Drainpipe_320` | 0.12 diameter × 3.2 | vertical downpipe segment |
| `RetainingWall_Stone_400`, `RetainingWall_Concrete_400` | 4 × 2 × 0.5 (face at Y = 0) | retaining-wall rows, stacked to height with the lowest row buried; stone (coursed, `Trim`) on old roads and the 1930s pass, concrete (`Concrete`) on post-war roads (INFRA_DESIGN.md Phase 3) |
| `WallCoping_400` | 4 × 0.16 × 0.6 | stone cap on a retaining wall's top row |

## Quality bar

- Every visible edge bevelled (2-3 cm, 2 segments); no razor-sharp 90° edges.
- Openings are real recesses (glass 15-20 cm behind the wall face), not painted.
- Shutters, sills, railings and cornices have real depth and silhouette.
- Triangle budget per facade piece: up to ~3k (pieces are Nanite in UE).
- Collision: one simple box per facade piece (`UCX_<Piece>_00` in FBX) covering the wall slab only.
