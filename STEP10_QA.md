# OpenCity — M4a Step 10: Visual QA Loop

**Goal:** Mandatory visual QA. Camera bookmarks (≥3 per district: street level, elevated, driving view) as a DataAsset. Script launches game/editor from command line, moves to each bookmark, captures HighResShot screenshots at noon, sunset and night, saves to `Screenshots/<date>/`. Review each screenshot against the checklist, fix issues, report frame time and VRAM per viewpoint.

---

## Camera bookmark DataAsset

`DA_CameraBookmarks` (`UPrimaryDataAsset`): entries = {district, label, location (cm), rotation (pitch/yaw/roll), FOV}. ≥3 per district (6 districts = ≥18 bookmarks).

## Screenshot script spec

`Content/Python/oc_visual_qa.py` (to be written):

1. Load `DA_CameraBookmarks`.
2. For each bookmark: `oc.dev.viewfrom X Y Z Pitch Yaw [FOV]` (cm; freezes player until `off`).
3. Time of day: noon, sunset, night (set via `UOCWorldStateSubsystem` / console).
4. `HighResShot` → `Screenshots/<date>/<district>_<label>_<time>.png`.
5. Log `stat unit`, `stat gpu`, `r.Streaming` stats per viewpoint.
6. Return `oc.dev.viewfrom off` between bookmarks.

Run headless or from editor (GPU needed for rendering).

## Critique checklist (per screenshot)

- No visible texture tiling or stretched UVs
- Consistent texel density; nothing blurry next to something sharp
- Every surface has roughness variation and a normal map; nothing looks plastic
- Scale looks right next to the mannequin and car
- No floating/clipping objects; no flat empty areas; no repeated identical buildings in view
- Clear silhouette variety on skyline; a landmark visible from most viewpoints
- Lighting has contrast and depth (fog layers, shadows, bounce light)

## Budget check (per viewpoint)

- `stat unit`, `stat gpu`: frame time vs 16.6 ms (60 FPS) target
- VRAM used vs 5,000 MiB budget (texture pool 2,000 MiB)
- Report breaches; cut texture pool first if over budget

## Done by hand (editor)

1. Create `DA_CameraBookmarks` (≥3 per district).
2. Write `oc_visual_qa.py`.
3. Run at noon, sunset, night; capture screenshots to `Screenshots/<date>/`.
4. Review each screenshot against the checklist; fix issues.
5. Report frame time and VRAM per viewpoint.
6. Commit to git (with LFS).

## Next step on approval

M4a complete. Move to M4b (remaining masters: car paint, glass, foliage, character, water; decals; weather presets; final post-process; showcase street block).
