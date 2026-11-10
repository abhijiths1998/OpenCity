# OpenCity: INFRA_DESIGN.md (roads, rail, water and street infrastructure)

Status: **Phase 1 approved (2026-11-10, the user delegated the phase decisions); Phase 2 done; Phase 3 in progress.**

Marevia is a fictional town on a fictional Italian coast. All names are original: towns, streets, the motorway operator, rail and bus companies, utilities, fuel brands, beach clubs and police. The layout follows `STORY.md`. This document adds infrastructure to it and does not replace any district.

The network plan image is `SourceArt/Infra/network_plan.png`, made by `python3 -I Tools/render_network_plan.py`.

---

## 0. Decisions made without asking (2026-11-09; confirm or change)

The brief's question round was skipped on request, so these are defaults:

| Question | Default taken | Why |
|---|---|---|
| Region archetype and reference mood | Ligurian and Campanian coast: a walled hill town above a port, a river town, terraced hills. Mood only; no real layout is copied. | Matches the existing Marevia (`STORY.md`, `ART_BIBLE.md`). |
| Map size | **Keep 3 × 3 km.** The brief suggests a 2 × 2 km city plus countryside. | Approved on 2026-11-09 and already built. |
| Canal district | **No** (the brief marks it optional). | `STORY.md` has no canal district; a Darsena already exists. |
| Rail and tram | **Yes, designed below. Data is empty until you approve it.** | The brief asks for both, and `STORY.md` already has Stazione Centrale. |
| Funicular | Optional, not designed. | Città Alta sits only 20 to 40 m above the port. |
| Engine | UE 5.8.4, Linux, Vulkan (`CLAUDE.md`). | Installed. |
| Asset sources | Poly Haven, ambientCG, Fab free or monthly-free, owned Megascans, and the project's Blender kits (`CLAUDE.md`, approved 2026-11-09). | Existing rule. |
| GPU | The brief says RTX 3050 Mobile **4 GB**. The project measures a **6 GB** card and budgets 5,000 MiB for the game (`CLAUDE.md`). | This doc keeps the 5,000 MiB budget. Tell me if the target machine really has 4 GB; every VRAM figure below would need to shrink. |

## 1. Audit for the Italian setting: keep, rebuild, delete

| Area | What exists | Verdict |
|---|---|---|
| Layout generator (`Tools/make_marevia_layout.py`) | Roads, districts, water, landmarks, terrain, buildings | **Keep.** It is the single source of truth, extended in Phase 2. |
| Road graph and lanes (`FOCRoadGraph`, `FOCLaneGraph`) | Nodes and segments; lanes on the right, so right-hand traffic | **Keep.** Extended with profiles, metadata, junctions and one-way support (Phase 2, done). |
| Road meshes (`OCWorldBuilderLibrary`) | Strips per kind; kerbs and 2.5 m sidewalks on avenues and streets; 40 cm skirts | **Rebuild** in Phase 3/4 to the profile cross-sections: footpaths, parking strips, medians, gutters, travertine kerbs. |
| Road widths | avenue 14 m, street 9 m, alley 5.5 m, coast 8 m, pass 7 m, pier 10 m | **Mostly keep.** They already match the profiles (section 4). The 5.5 m local streets outside the centre are under the DM F minimum of 6.5 m and are flagged. |
| Lane markings | Drawn in the road shader | **Rebuild** to Codice della Strada widths and dashes (section 6). |
| Decals | Manholes, tyre tracks, damage and gum (ambientCG) | **Keep.** Add Italian marking decals and patch types in Phase 4. |
| Paving materials | asphalt_02, cobblestone_floor_01, concrete_pavers_02 (Poly Haven) | **Keep asphalt.** Sampietrini (basalt cobbles), porfido (porphyry cubes), lastricato (stone slabs), travertine kerbs and white gravel need new CC0 or Megascans sources in Phase 4. |
| Terrain | Heightfield with road flattening, slope relax and a 1.6 m/m clamp | **Keep.** Phase 3 adds retaining walls by era and dry-stone terraces. **Known bug:** stacked switchback legs on the pass road touch, leaving 30 to 90 m walls between them. |
| Water | Custom sea, river and reservoir surfaces, not the Water plugin (ARCHITECTURE section 7) | **Keep.** Phase 8 adds edge treatments, weirs, levels and a storm sea. |
| Bridges | Five named bridges, grey-box decks with parapets | **Rebuild** in Phase 6 by era: stone arch, 19th-century iron, concrete. |
| Street props | Lamps every 28 m, boulevard trees, bins, hydrants (Poly Haven) | **Keep as placeholders.** Phase 5 adds Italian sets: wall lanterns, cable-hung lamps, galvanised LED poles, sign clusters. **Delete** the US-style fire hydrants; Italy uses underground or wall-mounted hydrants. |
| Building kit (Blender) | Italian palazzo bays, shutters, cornices, hip roofs | **Keep.** |
| Grey-box materials (`M_Dev_*`) and the old M4a masters (`M_Master_Building/Opaque/Glass/...`) | Unused or placeholder | **Delete** once nothing references them (a later cleanup step). |

## 2. Standards and sources

Verified 2026-11-09 against the D.M. 5/11/2001 text (Gazzetta Ufficiale, consolidated 05.2024, read from https://www.certifico.com/component/attachments/download/39843) and DPR 495/1992 articles 78 and 137-149 (reproduced at leggeinchiaro.it).

- **Cross sections:** D.M. 5/11/2001 Tab. 3.4.a. **Curves:** §5.2.3, 5.2.4 and 5.2.7. **Gradients:** §5.3.1.
- **Markings:** articles 137, 138, 141, 144, 145 and 149. **Sign colours:** article 78.
- **Rail:** 1435 mm gauge and 3 kV DC on conventional lines (RFI).

**Unsure, to verify before final art:**

1. **Hairpins (tornanti):** the decree sets no numbers for them (§5.2.4 exempts mountain hairpins from Rmin and superelevation). Our 12 m radius and 12% grade are game choices.
2. **Tram voltage:** 600 or 750 V DC is common practice, not verified from a primary source.
3. **Yellow for road-works markings:** article 137 lists yellow, but the primary text linking it to temporary works was not read.
4. **DPR 495 text:** the articles were read in a reproduction, not on Normattiva, and two secondary sources disagreed on widths between articles 138 and 141.
5. **Junction geometry and sight distances (D.M. 19/4/2006):** not researched yet. Phase 5 has to.

## 3. Urban history plan

Roads follow the town's history. Each segment's `era` field records it.

| Era | Where in Marevia | Street pattern | Data |
|---|---|---|---|
| **Roman** | Two straight streets crossing at the Santa Lucia piazza: a cardo and a decumanus fragment inside the core | Straight axis through an organic town | Phase 3 straightens two core streets; they are `medieval` until then |
| **Medieval** | Città Alta's walled core: an ellipse of about 800 × 580 m around Santa Lucia (`CORE` in the generator) | Organic streets, alleys, stepped lanes, small piazzas; **ZTL**, Zona 30 | 430 `centro_storico` segments, 161 inside the ZTL |
| **Walls and gates** | Along the core ellipse. Gates: Porta Marina (S), Porta San Rocco (E), Porta dei Colli (N), Porta Ovest (W), Porta Nuova (a 19th-century breach, SE) | Y-forks at the gates; gates are one-lane bottlenecks with lights | 28 `ztl_gate` junctions today; Phase 6 consolidates them to 5 gates and pedestrian passages |
| **19th century** | **Viale delle Mura**, a ring where the walls were pulled down in the 1880s. The corso from Ponte San Rocco to Stazione Centrale (the railway arrived in the 1870s). The Belle Époque Lungomare. | Straight viali and corsi with trees and porticoes; tram | `viale` and `lungomare` profiles; the ring is proposed (dashed on the plan) |
| **Post-war periphery** (1960s-80s) | Inland Porto Vecchio and Le Fornaci's grid | Wide grids, roundabouts added in the 2000s, car parks, supermarkets, petrol stations | `urbana_quartiere`, 4 roundabouts at avenue crossings (planned) |
| **Industrial and logistics** | Le Fornaci east; the autostrada junction and toll plaza at the east edge | Large plots, a trumpet interchange | Proposed |
| **Countryside** | Le Colline and Monte Ferrale: hairpin pass road (built in the 1930s with the dam), coast road, strade bianche, olive terraces, cypress avenues | Contour-following roads, dry-stone walls | `montana_tornanti`, `costiera`, `locale_f2`, `strada_bianca` |

Your six district types, mapped onto `STORY.md`:

| Your district type | Marevia district |
|---|---|
| Centro storico | Città Alta's walled core |
| 19th-century expansion | The ring viale, the corso to the station, and the Lungomare promenade |
| 1960s-80s periphery | Inland Porto Vecchio and Le Fornaci |
| Lungomare and port | Lungomare, Porto Vecchio's quays, Molo Salvi and Borgo Marino |
| Industrial | Le Fornaci east, plus the proposed autostrada |
| Hills and countryside | Le Colline and Monte Ferrale |

## 4. Road classes: cross-sections

Source of truth: `SourceArt/Infra/road_profiles.json`. All widths are in cm. The carriageway is lanes, shoulders, emergency lanes, median and parking, measured between the kerbs. Footpath widths are per side. Every road in the city is right-hand traffic.

| Profile | DM type | Lanes per dir × width | Shoulder L / R | Emergency lane | Median | Parking | Footpath | Carriageway | Max grade | R min | Speed |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `autostrada` | A | 2 × 375 | 70 / 0 | 300 | 260 (New Jersey barrier) | - | - | 2,500 | 5% | 339 m | 130 |
| `extraurbana_principale` | B | 2 × 375 | 50 / 175 | - | 250 | - | - | 2,200 | 6% | 178 m | 110 |
| `extraurbana_c1` / `c2` | C1 / C2 | 1 × 375 / 350 | 0 / 150 / 125 | - | - | - | - | 1,050 / 950 | 7% | 118 m | 90 |
| `urbana_scorrimento` | D | 2 × 325 | 50 / 100 | - | 180 | - | 150 | 1,780 | 6% | 77 m | 70 |
| `urbana_quartiere` | E | 1 × 300 | 0 / 50 | - | - | 200 | 250 | 900 | 8% | 51 m | 50 |
| `urbana_locale` | F (urban) | 1 × 275 | 0 / 50 | - | - | - | 150 | 650 | 10% | 19 m | 50 |
| `locale_f1` / `f2` | F1 / F2 | 1 × 350 / 325 | 0 / 100 | - | - | - | - | 900 / 850 | 10% | 45 m | 90 |
| `viale` | E | 2 × 300 | 0 / 50 | - | 200 (trees) | - | 400 (porticoes) | 1,500 | 8% | 51 m | 50 |
| `lungomare` | E | 2 × 300 | 0 / 50 | - | - | 200 | 600 (promenade, bike lane in it) | 1,500 | 8% | 51 m | 50 |
| `centro_storico` | not classified | 1 × 250 | 0 / 25 (gutter) | - | - | - | 80 | 550 | 15% (game) | - | 30 |
| `vicolo` | not classified | 1 × 110, pedestrian | - | - | - | - | - | 220 | 25%; steps beyond | - | 10 |
| `costiera` | F2 lanes | 1 × 325 | 0 / 50 to a 1 m stone parapet | - | - | - | - | 750 | 10% | 45 m | 70 |
| `montana_tornanti` | F2 (hairpins are not regulated) | 1 × 300 | 0 / 50 | - | - | - | - | 700 | 12% (game) | 12 m (game) | 60 |
| `strada_bianca` | not classified | 1 × 275 | - | - | - | - | - | 550 | 12% | 20 m | 50 |
| `passerella` | not classified; **pedestrian** | 1 × 275 (walkers) | - | - | - | - | - | 550 | 8% | - | 10 |
| `banchina_porto` | not classified | 1 × 350 | 0 / 150 | - | - | - | - | 1,000 | 4% | - | 30 |

Notes from the decree:

- **Bus routes:** where buses run on D or E roads, lanes are 350 cm (footnote to Tab. 3.4.a).
- **Footpaths:** at least 150 cm clear, and 200 cm where kiosks or bins stand (§3.4.6).
- **Parking bays:** 200 × 500 cm parallel, 230 × 500 cm perpendicular (§3.4.7).
- **Kerbs:** 15 cm (WORLD_METRICS). Travertine in the 19th-century districts, granite in the post-war ones.
- **Setbacks:** building frontages sit on the footpath in the centre and the 19th-century districts; post-war blocks sit back 3 to 6 m.

## 5. Geometry rules and junctions

| Rule | Value |
|---|---|
| Cross slope (camber) on straights | **2.5%** (§5.2.7). The brief says 2%; the decree says 2.5%. |
| Max superelevation | 7% extra-urban (A, B, C, F-extra); 5% for D; 3.5% for E, F-urban and urban service roads |
| Max gradient | A 5% extra-urban / 6% urban, B 6%, C 7%, D 6%, E 8%, F 10%. Up to 1 point more where justified. Centro storico 15% and vicoli 25% are game choices; steps (scalinate) beyond that. |
| Curve widening | E = 45 / R metres per lane (R = outer lane radius). Ignored below 20 cm. Runs from 7.5 m before the curve to 7.5 m after. |
| Hairpins | Game rules: R ≥ 12 m on the inner edge, both lanes widened by 45/R, retaining walls on stacked legs, **legs at least (height difference / 1.6) m apart in plan**, otherwise a wall (see the known bug in section 1) |
| Vertical curves | Smooth, no crest or dip sharper than the segment's design speed allows (Phase 3; the D.M. table values to be added) |
| Sight distance at junctions | D.M. 19/4/2006; to be researched in Phase 5 |
| Roundabouts | Counter-clockwise (right-hand traffic); give way to traffic already on the ring |

**Junction catalogue.** Each junction is a node with a type; 547 are typed today. All are `planned`: no signals, roundabouts or gates are built yet.

| Type | Use | Count today |
|---|---|---|
| `priority` | Default urban junction (give-way signs) | 367 |
| `blind_t` | Old town and hills; convex mirror | 137 |
| `ztl_gate` | Where streets enter the ZTL; camera and electronic panel | 28 (to consolidate into 5 gates) |
| `bridge_head` | Bridge ends | 10 |
| `roundabout` | Periphery avenue crossings; landscaped centre (olive tree, sculpture, fountain) | 4 |
| `piazza` | Santa Lucia: cars round the edge, pedestrians in the middle | 1 |
| `signalized` | 4-way signals with pedestrian buttons, flashing amber at night in small towns | 0 (Phase 5 picks the corso junctions) |
| `gate_fork` | Y-forks at the old city gates | proposed |
| `interchange` | Trumpet from the autostrada to the toll plaza | proposed |
| `level_crossing`, `tram_crossing` | Rail and tram crossings | proposed |

## 6. Markings and signs (Codice della Strada and DPR 495/1992)

| Item | Value |
|---|---|
| Lane line width (art. 138) | 15 cm on A and B; 12 cm on C, D and E; 10 cm on F |
| Dash and gap (art. 138) | Above 110 km/h: 4.5 m and 7.5 m. 50 to 110 km/h: 3 m and 4.5 m. 50 km/h or less, or in tunnels: 3 m and 3 m. Warning line before a continuous line: 4.5 m and 1.5 m. |
| Edge line (art. 141) | 25 cm on A and B; 15 cm on C, D and E; 12 cm on F |
| Stop line (art. 144) | At least 50 cm, with STOP lettering |
| Give-way triangles (art. 144) | 60 × 70 cm (C, D), 50 × 60 cm (E), 40 × 50 cm (F) |
| Zebra crossing (art. 145) | 50 cm stripes with 50 cm gaps; 2.5 m long on E and F, 4 m elsewhere; set back 5 m from the stop line |
| Parking bays (art. 149) | 12 cm lines: blue = paid, white = free, yellow = reserved |
| Temporary works | Yellow (to verify; section 2) |
| Direction sign colours (art. 78) | Green = autostrada, blue = extra-urban, white = urban, brown = tourist and cultural, yellow = temporary |

Layering on top of the rules: faded lines with ghost lines from old repainting, asphalt patches over cobbles, new signs on old poles.

## 7. Water bodies

Data lives in the layout's `water_network`. Edge ranges are in metres along the centreline for rivers, or along x for the coast and basins.

| Body | Type | Depth | Flow | Edges |
|---|---|---|---|---|
| Golfo di Marevia | sea | 30 m | still, gentle swell; storm state with weather | Molo Salvi breakwater, port quays, the Ardo delta, sand beach along the Lungomare. Phase 8 adds pebble coves, rock platforms with ladders, and cliffs with sea caves. |
| Darsena | port | 6 m | still | Quays with bollards, fenders, ladders and mooring rings |
| Fiume Ardo | river | 2.5 m | 0.8 m/s; rises with weather | Embankment walls with roads on top through the city; low walkways at water level from 300 to 1,500 m; natural banks in Le Colline; a dry rock gorge and cascades below the dam (no water sheets over 17°) |
| Foci dell'Ardo | estuary | 1 m | 0.3 m/s | Sandy delta, wadeable |
| Lago del Ferrale | reservoir | 25 m | still | The dam to the south, natural banks elsewhere |
| Torrente Rivazza | torrent, seasonal | dry most of the year | - | **Proposed:** a wide gravel bed with braided channels in Le Colline east, culverted under Le Fornaci to a storm outfall at the Lungomare |

Shading stays cheap: colour by depth, a murky green-brown river, shoreline foam, reflections from Lumen and screen space, no caustics.

## 8. Rail and tram (proposed; dashed on the plan)

- **Linea Litoranea** (regional, 1435 mm gauge, 3 kV DC; original operator *Ferrovie Litorali Mareviesi*):
  - **Route:** in from the west in a tunnel under Città Alta, with Marevia Porta Ovest station at its mouth; across the Ardo on its own bridge; to **Marevia Centrale** in Le Fornaci; then south-east on the brick-arch **Viadotto degli Archi** along the coast to the east edge.
  - **Crossings:** one level crossing in Le Fornaci, with barriers, lights and bells.
  - **Grade:** 2.5% at most (game limit for the regional line).
- **Tram 1** (750 V DC, to verify; operator *Tranvie Urbane Marevia*):
  - **Route:** Stazione Centrale to Città Alta over Ponte San Rocco, with rails embedded in the road, island stops and priority at signals.
  - **Data:** the road segments flagged `tram` (23 segments, 2.3 km).
  - **Fix needed:** the corridor is the graph's current shortest path, and it uses 9 alley segments. Phase 7 moves it onto the ring viale.
- Trains and trams run first as moving set dressing, with rideable versions as a later option.

## 9. Data model (Phase 2: done)

One source of truth. `Tools/make_marevia_layout.py` writes the networks into `SourceArt/World/marevia_layout.json`. `Content/Python/oc_networks.py` writes three data assets:

| Asset | C++ | Contents |
|---|---|---|
| `DA_MareviaRoads` | `UOCRoadGraphAsset` → `FOCRoadGraph`, `FOCLaneGraph` | Nodes, segments, `profiles` (`FOCRoadProfile`), `junctions` (`FOCRoadJunction`) |
| `DA_MareviaRail` | `UOCRailNetworkAsset` → `FOCRailNetwork` | Lines (kind, gauge, electrification, points, stations). Empty until approved. |
| `DA_MareviaWater` | `UOCWaterNetworkAsset` → `FOCWaterNetwork` | Bodies (type, depth, flow, level, seasonal, rises with weather, centreline, edges) |

Fields added to each road segment:

| Field | Values |
|---|---|
| `profile` | A key in `road_profiles.json` |
| `one_way` | Lanes and pathfinding drive A to B only |
| `paving` | asphalt, sampietrini, porfido, lastricato, brick, concrete, gravel_white |
| `era` | roman, medieval, c19, postwar, modern |
| `wear` | 0 to 1 |
| `ztl` | Inside the limited-traffic zone |
| `parking` | none, blue, white, yellow |
| `tram` | Tram tracks in the carriageway |

Speeds are clamped to each profile's maximum: urban 50, Zona 30 in the ZTL. `pedestrian` (the `passerella` and `vicolo` profiles) keeps cars off a segment: paths, reach and lanes skip it. The drive test had routed cars over the Passerella footbridge.

**Tools:**

| Task | Command |
|---|---|
| Regenerate the layout | `python3 -I Tools/make_marevia_layout.py` |
| Rebuild the world | `scripts/build_world.sh` (writes all three assets) |
| Rewrite the assets only, when geometry didn't change | `scripts/run_python.sh Content/Python/oc_validate_networks.py --import` |
| Draw the plan | `python3 -I Tools/render_network_plan.py` |

Proposed infrastructure waits in `SourceArt/Infra/proposed_networks.json` until you approve it.

## 10. Validation (Phase 13 data checks: done; geometry and visual checks come later)

`scripts/run_python.sh Content/Python/oc_validate_networks.py` runs `FOCNetworkValidator`, which is C++ with 4 unit tests. It writes `Saved/Validation/networks.md` and fails on errors.

**Checks:**
- **Roads:** profile, width against the profile, lanes, gradient, curve radius at through-nodes, speed, connectivity from the start, one-way traps, ZTL boundaries without gates, tram branches, junction types.
- **Rail:** geometry, gradient, electrification for the line kind, stations.
- **Water:** body type, depth, still water with flow, edge treatments.

**Current result (2026-11-10, after Phase 3 a and c): 74 issues, 5 errors** (was 106 and 27).

| Check | Result | Notes |
|---|---|---|
| Gradient | 2 errors, 5 warnings (was 20 and 23) | `limit_grades` holds road nodes within each road's grade before the terrain is cut and filled to them |
| Radius | 0 errors, 5 warnings (was 7 and 9) | Hairpins tighter than 12 m become light-bulb loops of 13 m (`bulb_hairpins`). The 5 left sit within 25 m of the north map edge or where a loop would hit another leg. |
| Clearance | 3 errors, 15 warnings | Loops pass 9 m from the other leg at a different height. **Retaining walls** (Phase 3b, next) |
| Width | 44 warnings | 5.5 m local streets outside the centre (F minimum 6.5 m); widening needs the road mesh rebuild (Phase 4) |
| Speed, connectivity, ZTL gates, junctions | 0 | |

Not checkable from data yet, so they come with the geometry phases:
- Gaps or overlaps between meshes, and kerb height mismatches
- Markings against lanes
- Floating props, sign facing, light spacing
- Water leaking through terrain, bridge clearances

## 11. Plugins (rule 6), as flagged in the UE 5.8.4 plugin files

| Plugin | Status | Use |
|---|---|---|
| PCG | **Production** (1.0) | Phase 5 street furniture from the networks |
| Landscape splines | Engine core, stable | Not needed: roads are baked meshes from the graph |
| Nanite spline meshes | Enabled by default (`r.Nanite.AllowSplineMeshes=1`), not flagged experimental | Not needed for the same reason |
| Water | **Experimental** (0.1) | **Not used.** Custom surfaces stay (ARCHITECTURE section 7). |
| Landmass | Beta | Not used |
| ZoneGraph | **Experimental** (0.5) | Approved for M5 traffic (ARCHITECTURE section 10). Fallback: our `FOCLaneGraph`, which already has lanes and links. |
| PCG Geometry Script interop | Beta | Avoided |

## 12. Performance budget (Phase 12)

These are **targets**; Unreal Insights measurements at the three bookmarks replace them.

| Bookmark | Draw calls | Triangles (Nanite visible) | Decals | GPU ms (Medium) | Game VRAM |
|---|---|---|---|---|---|
| Centro storico piazza | ≤ 2,500 | Nanite, any | ≤ 300 visible | ≤ 14 | ≤ 5,000 MiB |
| Lungomare | ≤ 2,500 | Nanite, any | ≤ 200 | ≤ 14 | ≤ 5,000 MiB |
| Autostrada viaduct | ≤ 1,500 | Nanite, any | ≤ 100 | ≤ 12 | ≤ 5,000 MiB |

Measured baseline (perf path, Medium, 2026-11-09): 82 to 86 FPS, GPU 11.0 to 11.6 ms, game VRAM about 4,000 MiB.

Rules:
- Props are ISM/HISM; road and coast segments merge per World Partition cell.
- Decal caps and draw distances are set per scalability level.
- HLOD covers roads, bridges, coast and rail.
- Water gets reduced features at Low and Medium.

## 13. Phases and approvals

| Phase | Content | Status |
|---|---|---|
| 1 | History and network design (this document and the plan) | **Approved 2026-11-10** (defaults in section 0 accepted) |
| 2 | Data model and tools | **Done** (not visible in game) |
| 3 | Terrain integration: re-grade the steep roads (**done**), hairpin loops (**done**), retaining walls by era, terraces, RVT edge blending | In progress |
| 4 | Surfaces, markings, wear, physical materials | Waiting |
| 5 | Street infrastructure (PCG) | Waiting |
| 6 | Bridges, tunnels, walls and gates | Waiting |
| 7 | Rail and tram | Waiting (needs this design approved) |
| 8 | Water | Waiting |
| 9 | Roadside services | Waiting |
| 10 | Weather and seasons (MPC_World already drives wetness and rain) | Partly done in M4b |
| 11 | Traffic (M5) | Not started (one milestone at a time, per `CLAUDE.md`) |
| 12 | Performance budget and measurement | Targets set |
| 13 | Validation and visual QA | Data checks done |
