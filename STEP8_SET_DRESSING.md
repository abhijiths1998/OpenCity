# OpenCity — M4a Step 8: Set Dressing

**Goal:** Three layers per street — hero focal point per block, secondary street furniture, tertiary clutter. PCG rules with seeds and exclusion zones; doorways, roads and gameplay paths stay clear. Nothing floats, nothing clips, everything touches the ground (contact decals / AO).

---

## Three layers

| Layer | Examples | Per block | Placement |
|---|---|---|---|
| Hero | market stall row, bus stop, construction site, temple-style shrine, food cart | 1 focal point | PCG rule, one per block |
| Secondary | streetlights, signs, power poles with cable sag, bins, benches, parked cars/bikes, barriers | several | PCG rules, along sidewalks |
| Tertiary | posters, trash, crates, plastic chairs, hanging laundry, potted plants, AC drip stains | many | PCG rules, near buildings/curbs |

## PCG rules

- Seeds per district for deterministic placement.
- Exclusion zones: doorways, roads, gameplay paths, chase routes stay clear.
- Snap to ground with contact decals / ambient occlusion at contact points.
- No floating, no clipping, no repeated identical buildings in view.
- Per-instance variation: color tint, wear amount, window lights, signage, balcony clutter.

## Assets

Streetlights, bins, benches, scooters, market stalls from `AUDIT.md` section 8 hero models (Poly Haven). Signs and graffiti: original designs only (no real brands).

## Done by hand (editor)

1. Import hero model FBXes (Step 6 pipeline or hand import).
2. Build PCG scatter graphs per layer with exclusion zones.
3. Place hero focal points per block manually first, then PCG.
4. Verify: nothing floats, nothing clips, ground contact everywhere.
5. Commit to git (with LFS).

## Next step on approval

Step 9 — Lighting and atmosphere: Sky Atmosphere, sun, Sky Light capture, fog, exposure, LUT, night sodium streetlights.
