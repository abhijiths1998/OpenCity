# Marevia building kit: piece contract

This file is the interface between the kit generator (C++, `Source/OpenCityEditor/Private/Kit/`, run by
`scripts/run_commandlet.sh OCBuildArt -steps=kit`, which writes the `SM_Kit_*` meshes directly) and the
building assembler (`UOCWorldBuilderLibrary::BuildKitBuildings`). Change it only together with both.
Previews: `scripts/run_commandlet.sh OCKitPreview` -> `SourceArt/Kit/Previews/`.

## Units, axes, pivot

- The generator works in metres, Z up; the meshes are written in Unreal cm = 100 x those numbers, same axes.
- **Facade pieces**: span X from 0 to the piece width, Z from 0 to the piece height. The **outside face is at Y = 0 and faces -Y**. Wall thickness goes into +Y (0.30 m). Openings are recessed into +Y. Things that stick out (sills, cornices, balconies, shutters, signs, pilasters) go into -Y.
- **Pivot**: the bottom-left-outside corner (0, 0, 0).
- **Roof pieces and props**: pivot at the bottom centre, sized as listed.

## Storey and bay grid (WORLD_METRICS.md)

- Storey (floor to floor): **3.20 m**. Ground floor: **3.20 m** as well.
- Bay widths: **2.00 m** and **4.00 m**. Building widths and depths are whole multiples of 2 m.
- Window: 1.10 m wide × 1.50 m tall, sill at 0.90 m. French door: 1.10 × 2.40 m. Street door: 1.10 × 2.20 m.
- Shopfront opening: 3.40 × 2.60 m in a 4 m bay.

## Material slots (slot names = the MI_Kit_<slot> material they get)

| Slot | What | UE material |
|---|---|---|
| `Wall` | plaster / painted render | `MI_Kit_Wall` (tiling plaster, tinted per building) |
| `Trim` | stone sills, lintels, cornices, quoins, plinth, door surrounds | `MI_Kit_Trim` (pale limestone) |
| `Wood` | shutters, doors, window frames | `MI_Kit_Wood` (painted wood, tinted per building) |
| `Glass` | window and door glass | `MI_Kit_Glass` (dark glass, lit at night) |
| `Metal` | railings, rolling shutters, drainpipes, AC units, tanks | `MI_Kit_Metal` |
| `Roof` | clay roof tiles | `MI_Kit_Roof` |
| `Concrete` | flat roof slabs, parapet copings, balcony slabs | `MI_Kit_Concrete` |
| `PaintRed`, `PaintWhite`, `PaintBlack`, `PaintBlue` | sign faces and lettering, mirror rims | `MI_Kit_Paint*` (`M_Master_Paint`, retroreflective at night) |
| `Paint` | bins and other fixtures coloured per instance | `MI_Kit_Paint` (colour from instance custom data 0-2) |
| `Brick` | brick warehouse walls, arches, corbels, gable ends | `MI_Kit_Brick` (red_brick_03, natural colour) |
| `Corrugated` | port shed sheeting | `MI_Kit_Corrugated` (corrugated_iron_02, wall tint) |
| `Fabric` | awnings, laundry | `MI_Kit_Fabric` (rough_linen, shutter colour) |

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
| `Sign_Speed30`, `Sign_Speed50`, `Sign_Stop`, `Sign_GiveWay`, `Sign_NoEntry`, `Sign_ZTL` | post 2.9 m, plate 0.6 m (ZTL panel 0.8 × 1.0) | Codice della Strada shapes and colours on a galvanised post; pivot at the post foot; the plate faces -Y |
| `Prop_ConvexMirror` | post 2.6 m, mirror 0.66 m | blind-corner mirror, red rim; faces -Y |
| `Prop_BollardStone`, `Prop_BollardIron` | 0.3 × 0.58, 0.18 × 0.85 | centro storico and ZTL bollards |
| `Prop_WallLantern` | 0.34 × 0.74 × 0.64 | wall lantern on a bracket; pivot at the wall plate's foot, reaches out along -Y |
| `Prop_LedPole` | 8 m, 1.85 m arm | galvanised LED street light (periphery, extra-urban); arm along -Y |
| `Prop_RecyclingBin` | 1.36 × 1.0 × 1.3 | wheeled kerbside bin, coloured per instance |
| `Prop_Fountain` | 0.6 × 0.62 × 1.14 | cast-iron drinking fountain (original design) |
| `Prop_TrafficLight` | 0.52 × 0.42 × 3.23 | three-aspect signal on a post with a pedestrian push button (original design); pivot at the post foot, faces -Y; lenses unlit until traffic (Phase 11) |
| `Prop_CableLamp` | 0.64 × 0.64 × 0.62 | lamp hung on a cable across a centro storico street; pivot at the bowl's bottom, hook 0.62 m above |
| `Prop_Cable` | 1 × 0.016 × 0.014 | 8 mm cable along +X from the origin, stretched per instance (scale X = span) |

Street fixtures (kind `fixture`) keep their pivot at the post foot or wall attachment (no recentring).

## Quality bar

- Every visible edge bevelled (2-3 cm, 2 segments); no razor-sharp 90° edges.
- Openings are real recesses (glass 15-20 cm behind the wall face), not painted.
- Shutters, sills, railings and cornices have real depth and silhouette.
- Triangle budget per facade piece: up to ~3k (pieces are Nanite in UE).
- Collision: one simple box per facade piece (`UCX_<Piece>_00` in FBX) covering the wall slab only.

## Assembly rules (BuildKitBuildings + OCFacadePlan)

- A facade is planned before anything is placed (`OCPlanFacade`): a row of 2 m and 4 m columns, each with
  one ground use (door, shop, barred windows, wall) and one upper use (windows or blank) kept all the way
  up; balconies are runs of storeys on 4 m columns, laid out by pattern (centre, ends, alternate, all).
- Street fronts are mirror-symmetric by default (style chance), with the door on the axis when the facade
  has an odd number of 2 m units, else in a 2 m corner bay or beside the axis. One door per building, on
  the street facade nearest a road.
- Party walls: each column is probed 0.5 m outside at a quarter and three quarters of its width. Covered
  at both, its cells up to the neighbour's top are not placed; covered at one, they are plain wall. Quoins,
  downpipes and cornice mitres start above whatever stands against their corner.
- Style gates: quoins on old town, village, hills and grid90 up to 5 storeys; no cornice on grid120 and
  terminal sheds; pitched (hip) roofs only where the short side is 14 m or less and storeys 5 or fewer.
- Downhill bays whose ground falls more than 2.8 m below the floor get plain lower storeys and the plinth
  under the lowest; a door or shop over a drop of more than 1.2 m becomes barred windows or wall.
- Choices use hashed sub-seeds per building and purpose (bays, colours, roof, props, lit windows).

## Piece families (BUILDINGS_PLAN.md phase 3, 2026-11-13)

`_200` pieces have one opening centred in the bay, `_400` two on the 2 m pitch. All 3.2 m storeys, facade frame.

| Piece | Notes |
|---|---|
| `Wall_100`, `Plinth_100` | 1 m fillers |
| `StringCourse_200/400` | moulded band, placed on the first-floor line of street fronts (pivot on the line) |
| `WindowTall_*` | 1.10 x 1.95 m, sill 0.55, shutters, keystone: first floors |
| `WindowArch_*` | round-headed 1.10 x 1.85 m, no shutters, keystone |
| `WindowSmall_*` | 0.80 x 0.80 m attic window |
| `WindowClosed_*`, `WindowHalf_*` | shutters shut in the reveal / one leaf shut |
| `WindowBlind_*` | walled-up window: surround and sill on a 4 cm recess |
| `Juliet_400` | French door, railing flush with the wall |
| `BalconyLong_400` | slab and front rail across the whole bay, so neighbours join |
| `Portone_400` | arched carriage door 2.4 x 3.0 m, studded leaves (ground) |
| `Garage_400`, `ShopClosed_400`, `ShopAwning_400` | roller door; shopfront with the shutter down; with a canvas awning |
| `CorniceBracket_200/400` | deep eave on wooden brackets (crown) |
| `Wall_283`, `Window_283`, `WindowPlain_283`, `Plinth_283`, `Cornice_283`, `Parapet_283` | the 45 degree cut of a corner building (2 m legs) |
| `Shed_Wall_200/400`, `Shed_WindowStrip_400`, `Shed_RollerDoor_400` | corrugated sheeting on a concrete kerb |
| `Brick_Wall_200/400`, `Brick_WindowArch_200/400`, `Brick_GoodsDoor_400`, `Brick_Corbel_200/400` | brick warehouse |
| `Hotel_Window_400`, `Hotel_Balcony_400`, `Hotel_Entrance_400` | aluminium glazing, continuous glass-rail balcony, canopy entrance |
| `Church_Portal_400` (6.4 m), `Church_Rose_400`, `Pediment` (unit triangle), `Civic_Entrance_400`, `Pilaster_320`, `Canopy_400` | landmark fronts |
| `RoofHipEnd` (plan x -0.5..0, apex at x 0), `RoofRidge` (x 0..1, capped ends) | hip roofs: two ends scaled uniformly by the roof width + the ridge stretched between; gables: the ridge alone |
| `GableEnd`, `GableEndBrick`, `GableEndCorrugated` | unit triangle in the facade plane |
| `Gutter_200/400` | eaves gutter 0.4-0.55 m out, pivot at the eave line |
| `ShopSign_00`..`_11` | lettering for the shopfront board (invented trade names) |
| `Prop_ShopBlade`, `Prop_WallAC`, `Prop_Laundry`, `Prop_FlowerBox` | wall dressing, pivot on the wall face, reaching -Y |

## Archetypes (DA_BuildingStyles)

The layout writes an `archetype` per building (townhouse, palazzo, tower, courtyard, tenement, office, corner,
shed, warehouse, annex, slabhotel, steppedhotel, villa, kiosk, farmhouse, cottage, villatower, farm, church,
civic, station) plus `age`, `wealth`, `upkeep` (0-1), `street` (character seed), `lot` (volumes of one
building), `on` (a recessed top: the volume it stands on) and `chamfer` (corner building). Each archetype in
`/Game/Environment/Buildings/DA_BuildingStyles` sets the material (Plaster, Brick, Shed), palette, trim,
roof (pitched share, gable share, pitch), crown, door, and weighted piece families for upper, first-floor,
top-storey, balcony, shop and other ground bays. A building draws one of each; every volume of a lot shares it.
Custom data on kit instances: 0-2 wall tint, 3-5 wood tint, 6 wear, 7 unused, 8 ground-floor piece, 9 trim palette.

## Section 4 pieces: Italian character (BUILDINGS_PLAN.md section 4)

| Piece | Notes |
|---|---|
| `EavesRafters_200/400`, `EavesCoved_200/400` | 0.75 m eaves under pitched roofs: stone-coloured rafter ends under terracotta boards, or a plastered cove; pivot at the wall top |
| `EavesRow_200/400` | half-round pan tiles along a clay eave (fixture, placed at the roof base) |
| `WindowNobile_*` | piano nobile: 1.1 x 2.1 m French window under a pediment on consoles |
| `SillBand_200/400` | marcadavanzale, at sill height of each storey (trim) |
| `RusticWall_200/400`, `RusticWindow_400` | bugnato ground floor: channelled joints every 0.4 m (Trim material) |
| `BalconyBracket_400`, `BalconyBracketLong_400` | thin stone slab on scrolled brackets; the long one spans the bay |
| `Edicola` | wall shrine niche with a tiled hood (fixture) |
| `ShopArch_400` | round-headed glazed shop arch |
| `Scarpa_200/400` | battered base, 1.4 m high, 0.25 m out at the foot |
| `Arcade_400` | portico bay: arch on the street line, 3 m walk, shop on the back wall; collision on piers and back wall only |
| `Loggia_400` | open arch at an upper storey, balustrade, 1.6 m deep, French door at the back |
| `Archetto` | brace arch across an alley; unit span along -Y from the wall, scaled to the gap |
| `Sottoportico` | bridge room spanning an alley on the first-floor line; unit 1 m along X (scaled to the gap), 4 m deep |
| `ExtStair_400` | external stair along the wall to a first-floor landing |
| `RoofMono` | lean-to: unit along the wall, -Y away from it, rise 1 at the wall |
| `RoofVault` | rendered barrel vault (Campanian), unit footprint, rise 1 |
| `RoofHipEndSlate`, `RoofRidgeSlate` | Ligurian slate (`Slate` slot, `MI_Kit_Slate`, roof_slates_02) |
| `Prop_ChimneyPot`, `Dormer`, `Prop_Aerial`, `Altana`, `Pergola`, `BellGable` | roofscape |
| `GardenWall_400`, `GatePier` | villa gardens: wall with a tiled coping, gate piers |

Storey heights: pieces are built at 3.2 m and scaled vertically per storey (2.9, 3.2, 3.5 m upper storeys,
3.2-4.2 m ground floors, from `storey_h` and `ground_h` in the layout); glass and openings scale with them.
Trim palette (custom data 9): 0 limestone, 1 grey stone, 2 painted white, 3 brick red, 4 painted cream,
5 painted in a darker shade of the wall colour.

## Tuscan landmarks and the inhabited bridge (2026-11-14)

Original designs in the manner of the references (domed cathedral, round arcaded campanile, octagonal
banded baptistery, shops on a bridge); no replica of a real building.

| Piece | Notes |
|---|---|
| `LM_Dome` | unit drum radius 1 m, scaled uniformly: octagonal drum with oculi, pointed eight-sided tiled dome with white ribs, lantern |
| `LM_TowerRound` | 3.6 m radius, 30 m: blind-arcaded base, six open galleries, belfry; placed with a 2.5 degree lean |
| `LM_Baptistery` | octagon, circumradius 4 m: dark banded pilasters, panelled lower order, blind arches above, white pyramid roof, portal on local +X |
| `LM_Apse` | unit half-round apse (bulges to -Y), banded, half-cone roof |
| `MarbleWall_*`, `MarbleWindow_*`, `MarbleArcade_400`, `MarblePortal_400` | cathedral cladding: inlay panels, framed arched windows, blind-arcade galleries, the portal in marble |
| `BridgeShop_400` | two-storey shop house 4 x 3 m hung outside a bridge parapet on corbels; shop opens to the deck |

Slots `Marble` and `MarbleDark` (`MI_Kit_Marble`, `MI_Kit_MarbleDark`: white_marble, box-projected; dark is the
same marble tinted green-black). Pieces with NaN vertices fail the kit build.
