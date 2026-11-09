# OpenCity — ART_BIBLE.md (M4a Step 2)

**Setting:** Marevia, an original fictional coastal port town in a fictional Italian region (Ligurian and Campanian mood, no real place copied); right-hand traffic, Italian road rules (`INFRA_DESIGN.md`). Present day. Fully original — no names, assets, logos, story, characters, map or UI from any existing game, and no real brands (see CLAUDE.md).

The playable area is 3 km × 3 km: the city by the sea plus a mountain band (Monte Ferrale) to the north. Distant peaks and Isola Ventura (offshore island) are scenery, unreachable in Part 1. Districts, gates and landmarks follow `STORY.md`.

---

## Mood and palette per time of day

| Time of day | Mood | Sky / light | Key colours |
|---|---|---|---|
| Noon | Bright, harsh, hot | Deep blue sky, strong sun, short dark shadows | Bleached plaster white, ochre, deep terracotta, shadow blue-grey |
| Late afternoon / golden hour | Warm, long shadows, rich | Low gold sun, long directional shadows | Gold, amber, burnt orange, long violet shadows |
| Sunset | Transition, mood | Pink/orange/purple band at horizon, sun low | Rose, salmon, lavender, streetlights just switching on |
| Night | Quiet, sodium-lit | Deep navy, stars faint, no moon emphasis | Warm sodium orange (streetlights), cool blue shadow, neon shop signs, window light pools |
| Dawn | Cool, soft | Pale blue-grey, soft light | Steel blue, pale pink at horizon, lingering streetlights |

Art direction: warm Mediterranean light, high contrast at noon, layered fog in valleys at night, visible light falloff from streetlamps. Frame rate wins over visuals on the RTX 3050 6 GB (5,000 MiB game VRAM budget).

---

## Material palette

| Material | Where used | Notes |
|---|---|---|
| Weathered plaster / stucco | Old Town, hillside building walls | Tiling, vertex-painted wear at edges |
| Painted concrete | Downtown, hillside roads, foundations | Colour variants per district |
| Laterite / red stone | Hillside foundations, quay walls, retaining walls | Warm red-brown, grainy |
| Red clay tile roofs | Pitched roofs across districts | Terracotta, slight colour variation per roof |
| Tin sheet / corrugated metal | Port sheds, shacks, utility structures | Rusty at the port, newer downtown |
| Glass towers | Downtown glazing | Dark reflective glass, Clear Coat on frames |
| Rusted metal | Port cranes, containers, shutters, Darsena | ORM-driven rust variation |
| Asphalt | Roads | Patches, cracks, tire marks, puddles via decals |
| Concrete pavement slabs | Sidewalks, plazas | 60 cm slabs, slight wear |
| Cobblestone / setts | Old Town narrow streets | Historic feel, uneven |
| Sampietrini, porfido, lastricato, travertine kerbs | Centro storico streets and piazzas, 19th-century kerbs | Paving per segment (`paving` in the road data, INFRA_DESIGN.md); sources still to find (Phase 4) |
| White gravel (strade bianche) | Le Colline farm roads | Lined with cypresses |

Every final texture is a real scanned/authored PBR source (Poly Haven / ambientCG / CC0 / Fab free / owned Megascans), imported at 2K max for hero, 1K for props, tiling for large surfaces. Track every asset in `ASSET_SOURCES.md` (name, source URL, license, where used) — supersedes the existing `SourceArt/LICENSES.csv` habit; keep both in sync.

---

## The five districts

### 1. Città Alta — Old Town / market / hospital
- **Building types:** narrow plastered townhouses, small shops with shutters, hospital block, small chapel
- **Height range:** 2–4 storeys (6.4–12.8 m)
- **Density:** high, tight street grid, small blocks (80–120 m)
- **Signage:** painted timber signs, hanging shop blades, handwritten menus
- **Props:** market stalls, crates, baskets, laundry lines, scooters parked tight
- **Vegetation:** potted citrus, window boxes, small courtyard trees
- **Wear level:** high — layered age, peeling paint, worn steps under windows
- **Color palette:** warm white, ochre, faded green shutters, terracotta tiles, deep shadow

### 2. Porto Vecchio — Port & docks / industrial
- **Building types:** warehouses, fish market, customs office, port sheds, containers
- **Height range:** 1–3 storeys (3.2–9.6 m), long low footprints
- **Density:** medium, wide yards and quays between blocks
- **Signage:** stencilled industrial lettering, rusted metal plates, cargo symbols
- **Props:** crates, barrels, nets, bollards, mooring rings, forklifts, fishing boats
- **Vegetation:** sparse, hardy coastal shrubs, salt-tolerant grasses
- **Wear level:** very high — rust, salt staining, oil stains, chipped paint
- **Color palette:** rust orange, weathered grey, corrugated tin, dark water blue-grey

### 3. Lungomare — Beach Road / hotels / clubs
- **Building types:** hotels, restaurants, clubs, beachfront villas, promenade shelters
- **Height range:** 3–5 storeys (9.6–16 m) along the road, taller hotel blocks
- **Density:** medium-high along the sea, open toward the beach
- **Signage:** neon, backlit signs, awnings, cursive restaurant scripts
- **Props:** sun loungers, umbrellas, scooters, bicycles, planters, bins
- **Vegetation:** palms, bougainvillea, street trees every 8–12 m
- **Wear level:** medium — sea salt on lower walls, fresh paint upstairs, nightlife clutter
- **Color palette:** white, cream, sea-glass green, neon pink/blue at night, sand

### 4. Le Fornaci — Kilns / station / industrial
- **Building types:** kiln sheds, railway station, goods warehouse, workers' cottages
- **Height range:** 1–2 storeys (3.2–6.4 m), tall kiln chimneys
- **Density:** low-medium, wide roads and rail sidings
- **Signage:** railway signage, kiln numbers, painted destination boards
- **Props:** rail sleepers, coal bunkers, brick stacks, wagons, water tower
- **Vegetation:** weeds at track edges, hardy trees, dry grass
- **Wear level:** high — soot, brick dust, oil, weathered timber
- **Color palette:** brick red, charcoal grey, rust, dusty ochre, soot black

### 5. Le Colline — Hillside residential / villas
- **Building types:** villas, lookout points, estate walls, small hermitage, quarry buildings
- **Height range:** 1–3 storeys, terraced into the slope
- **Density:** low, large plots, winding roads, wide views
- **Signage:** estate nameplates, small directional signs, gate pillars
- **Props:** olive terraces, stone walls, cypress, parked cars, garden furniture
- **Vegetation:** olive groves, cypress, pines, terraced plantings
- **Wear level:** low-medium — weathered stone, moss on walls, cultivated look
- **Color palette:** warm stone, olive green, terracotta, dusty blue distance

---

## Landmarks (visible from far away, for navigation)

1. **Faro (lighthouse)** — Molu Salvi breakwater, port entrance; rotating light at night, white tower by day
2. **Radio mast** — Monte Ferrale summit; tallest structure in the region, visible citywide
3. **Eremo delle Aquiles (hill hermitage)** — Monte Ferrale slope, stone tower on the skyline
4. **Diga del Ferrale (dam)** — reservoir in the northern hills, long concrete wall
5. **Campanile (bell tower)** — Città Alta, rises above the old-town rooftops

---

## Believability rules

- **Layered age:** every surface shows more than one era — original material, a repair, a coat of paint, weathering on top. Nothing looks made yesterday.
- **Wear in the right places:** edges, ground contact, under windows, beneath sills, around doors, along baseboards. Wear is directional (rain streaks down, foot traffic wears paths).
- **Clutter that tells a story:** each prop has a reason to be there — laundry drying, crates awaiting delivery, bins full, a parked scooter with a helmet. Avoid "decoration for decoration's sake."
- **Nothing floats, nothing clips:** every object touches the ground convincingly (contact decals / ambient occlusion where it meets the surface).
- **Consistent scale:** doors 220×110 cm, floor-to-floor 320 cm, player 180 cm (see `WORLD_METRICS.md`). A mannequin and a car are the scale reference in every test map.
- **No two neighbouring buildings identical:** per-instance variation — color tint, wear amount, window lights, signage, balcony clutter.
- **Ground floors most detailed:** shops, shutters, signs, lights; upper floors simpler; rooftops always dressed (water tanks, AC units, dishes).
- **Macro variation on large surfaces:** tiling textures plus vertex-painted wear so repetition never reads as a pattern.

---

## Next step on approval

Create `WORLD_METRICS.md` (player/door/road/block/texel-density numbers) and lock it into all scripts, then wait for approval before building final art.
