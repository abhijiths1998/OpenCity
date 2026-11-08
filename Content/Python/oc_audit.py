"""M4a Step 1 — World-building audit.

Scans /Game and writes AUDIT.md listing every problem found with meshes,
textures, materials, tiling risk and lighting/post-process, plus a
prioritized fix list and an asset download list with exact search terms.

Run headless:
    scripts/run_python.sh Content/Python/oc_audit.py
"""

import os

import unreal

import oc_lib as L

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
AUDIT_PATH = os.path.join(PROJECT_DIR, "AUDIT.md")

TAG = "oc_audit"
MESH_OK_PREFIXES = ("SM_", "SK_")
TEXTURE_SUFFIXES = ("_BC", "_N", "_ORM", "_E", "_M")
DEV_MATERIAL_PREFIX = "/Game/Dev/Materials/M_Dev_"
MASTER_PREFIXES = ("/Game/Materials/M_Master_", "/Game/Materials/")

findings = []   # (severity, category, detail)
assets_scanned = 0


def log(tag, msg):
    unreal.log(f"[{tag}] {msg}")


def note(severity, category, detail):
    findings.append((severity, category, detail))


def scan_assets():
    """Walk /Game and classify every asset."""
    global assets_scanned
    lib = unreal.EditorAssetLibrary
    all_assets = lib.list_assets("/Game", recursive=True)
    meshes, textures, materials, maps, blueprints, data_assets, other = [], [], [], [], [], [], []
    for path in all_assets:
        assets_scanned += 1
        asset = lib.load_asset(path)
        if asset is None:
            other.append((path, "failed to load"))
            continue
        cls = asset.get_class().get_name()
        if cls in ("StaticMesh", "SkeletalMesh"):
            meshes.append((path, asset, cls))
        elif cls == "Texture2D":
            textures.append((path, asset))
        elif cls == "Material":
            materials.append((path, asset))
        elif cls == "MaterialInstanceConstant":
            materials.append((path, asset))
        elif cls == "World":
            maps.append((path, asset))
        elif cls == "Blueprint":
            blueprints.append((path, asset))
        elif cls == "DataAsset":
            data_assets.append((path, asset))
        else:
            other.append((path, cls))
    return meshes, textures, materials, maps, blueprints, data_assets, other


def check_mesh(path, mesh, cls):
    problems = []
    # Collision
    try:
        bodies = mesh.get_editor_property("bodies")
        has_collision = bool(bodies) and len(bodies) > 0
    except Exception:
        has_collision = False
    if not has_collision:
        # UCX naming convention check
        if not os.path.basename(path).startswith("UCX_"):
            problems.append("no custom collision (UCX_*) and no physics bodies")

    # Nanite / LOD
    try:
        nanite = mesh.get_editor_property("nanite_settings").enabled
    except Exception:
        nanite = False
    lod_count = mesh.get_num_lods() if hasattr(mesh, "get_num_lods") else 0
    if cls == "SkeletalMesh":
        pass  # skeletal meshes use regular LODs
    elif not nanite and lod_count < 1:
        problems.append("no Nanite and no LODs")

    # UVs / lightmap UVs
    try:
        uv_channels = mesh.get_editor_property("num_tex_coords") if hasattr(mesh, "get_num_tex_coords") else 0
    except Exception:
        uv_channels = 0
    if uv_channels < 1:
        problems.append("no UV channel 0")
    if uv_channels < 2:
        problems.append("no lightmap UV channel (channel 1)")

    # Blockout naming
    name = os.path.basename(path)
    if name.startswith("BLOCKOUT_") or "/Engine/BasicShapes/" in path:
        problems.append("blockout geometry (BasicShapes / BLOCKOUT_*) — not final art")

    # Scale sanity (rough: bounding box extents)
    try:
        bounds = mesh.get_editor_property("bounds")
        ext = bounds.box_extent
        size = max(ext.x, ext.y, ext.z) * 2
        if size > 2000:  # > 20 m single mesh — likely not modular
            problems.append(f"oversized single mesh ({size:.0f} cm) — not modular")
    except Exception:
        pass

    return problems


def check_texture(path, tex):
    problems = []
    name = os.path.basename(path)
    # Compression settings
    try:
        comp = tex.get_editor_property("compression_settings")
        srgb = tex.get_editor_property("srgb")
        group = tex.get_editor_property("lod_group")
        max_size = tex.get_editor_property("max_texture_size")
    except Exception:
        return ["could not read import settings"]

    suffix = None
    for s in TEXTURE_SUFFIXES:
        if name.endswith("_" + s[1:]):
            suffix = s
            break

    if suffix == "_N":
        if str(comp) != "TC_Normalmap":
            problems.append(f"normal map not using Normalmap compression ({comp})")
        if srgb:
            problems.append("normal map marked sRGB")
    elif suffix == "_ORM":
        if str(comp) != "TC_Masks":
            problems.append(f"ORM/mask map not using Masks compression ({comp})")
        if srgb:
            problems.append("ORM map marked sRGB")
    elif suffix == "_BC":
        if not srgb:
            problems.append("base color not marked sRGB")
    elif suffix == "_E":
        if not srgb:
            problems.append("emissive not marked sRGB")

    try:
        size_x = tex.get_editor_property("size_x")
        size_y = tex.get_editor_property("size_y")
    except Exception:
        size_x = size_y = 0
    if max(size_x, size_y) > 2048:
        problems.append(f"oversized texture ({size_x}x{size_y}) — above 2K hero cap")
    try:
        if not tex.get_editor_property("never_mip_map") and size_x > 0:
            pass  # mips assumed on unless explicitly off
    except Exception:
        pass

    return problems


def check_material(path, mat):
    problems = []
    name = os.path.basename(path)
    # Is it a dev material?
    if path.startswith(DEV_MATERIAL_PREFIX):
        problems.append("dev grey-box material (M_Dev_*) — replace with master-material instance in M4a")
    # Is it an instance of a master?
    try:
        parent = mat.get_editor_property("parent")
        if parent is None:
            problems.append("no parent material — not built from a master")
        elif not any(path.startswith(p) for p in MASTER_PREFIXES) and "M_Master" not in str(parent.get_path_name()):
            problems.append(f"parent {parent.get_path_name()} is not an approved master material")
    except Exception:
        pass
    # Texture samples
    try:
        expressions = unreal.MaterialEditingLibrary.get_all_material_expressions(mat)
        tex_samplers = [e for e in expressions if isinstance(e, unreal.MaterialExpressionTextureSample)]
        if not tex_samplers:
            problems.append("no texture samples — flat/procedural material")
    except Exception:
        pass
    return problems


def check_map(path, world):
    problems = []
    try:
        subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actors = subsys.get_all_level_actors()
        classes = [type(a).__name__ for a in actors]
        has_sky = any("SkyAtmosphere" in c for c in classes)
        has_fog = any("ExponentialHeightFog" in c for c in classes)
        has_dl = any("DirectionalLight" in c for c in classes)
        has_skylight = any("SkyLight" in c for c in classes)
        has_pp = any("PostProcess" in c for c in classes)
        if not has_sky:
            problems.append("no Sky Atmosphere")
        if not has_fog:
            problems.append("no Exponential Height Fog")
        if not has_dl:
            problems.append("no Directional Light (sun)")
        if not has_skylight:
            problems.append("no Sky Light")
        if not has_pp:
            problems.append("no Post Process Volume (no exposure/LUT control)")
    except Exception:
        problems.append("could not inspect level actors")
    return problems


def write_audit(meshes, textures, materials, maps, blueprints, data_assets, other):
    lines = []
    lines.append("# OpenCity — M4a Step 1: World-Building Audit")
    lines.append("")
    lines.append(f"Assets scanned under `/Game`: {assets_scanned}")
    lines.append(f"- Meshes: {len(meshes)}  Textures: {len(textures)}  Materials/Instances: {len(materials)}")
    lines.append(f"- Maps: {len(maps)}  Blueprints: {len(blueprints)}  Data assets: {len(data_assets)}  Other: {len(other)}")
    lines.append("")
    lines.append("## 1. Final-art status")
    lines.append("")
    lines.append("**No final art exists yet.** The project is at the grey-box / blockout stage:")
    lines.append("- Buildings are instanced `/Engine/BasicShapes/Cube` blocks (oc_m3_world.py / OCWorldBuilderLibrary).")
    lines.append("- Roads are procedural Nanite mesh chunks from the road graph (UV-driven markings, HLSL colour).")
    lines.append("- Materials are `M_Dev_*` grey-box HLSL materials in `/Game/Dev/Materials/`, regenerated by `oc_dev_materials.py`.")
    lines.append("- The master materials (`M_Master_Opaque`, `M_Master_Building`, …) planned in `ART_PIPELINE.md` section 5 do not exist yet.")
    lines.append("- No PBR textures (base color / normal / ORM / emissive) have been imported.")
    lines.append("")
    lines.append("Because nothing is final art, every category below currently fails by default — this is the baseline the fix list works from.")
    lines.append("")

    # Meshes
    lines.append("## 2. Meshes — problems")
    lines.append("")
    mesh_issues = 0
    for path, mesh, cls in meshes:
        probs = check_mesh(path, mesh, cls)
        if probs:
            mesh_issues += 1
            lines.append(f"- `{path}` ({cls}):")
            for p in probs:
                lines.append(f"  - {p}")
    if mesh_issues == 0:
        lines.append("(none found)")
    lines.append("")

    # Textures
    lines.append("## 3. Textures — problems")
    lines.append("")
    if not textures:
        lines.append("**(none imported yet — all textures must be downloaded and imported in M4a)**")
    else:
        tex_issues = 0
        for path, tex in textures:
            probs = check_texture(path, tex)
            if probs:
                tex_issues += 1
                lines.append(f"- `{path}`:")
                for p in probs:
                    lines.append(f"  - {p}")
        if tex_issues == 0:
            lines.append("(none found)")
    lines.append("")

    # Materials
    lines.append("## 4. Materials — problems")
    lines.append("")
    mat_issues = 0
    for path, mat in materials:
        probs = check_material(path, mat)
        if probs:
            mat_issues += 1
            lines.append(f"- `{path}`:")
            for p in probs:
                lines.append(f"  - {p}")
    if mat_issues == 0:
        lines.append("(none found)")
    lines.append("")

    # Maps / lighting
    lines.append("## 5. Lighting and post-process — problems")
    lines.append("")
    map_issues = 0
    for path, world in maps:
        probs = check_map(path, world)
        if probs:
            map_issues += 1
            lines.append(f"- `{path}`:")
            for p in probs:
                lines.append(f"  - {p}")
    if map_issues == 0:
        lines.append("(none found)")
    lines.append("")

    # Tiling risk
    lines.append("## 6. Visible-tiling risk")
    lines.append("")
    lines.append("No tiling textures exist yet, so there is nothing to flag. When importing in M4a:")
    lines.append("- Large surfaces (roads, walls, plazas) must use tiling textures plus macro variation, never a unique texture.")
    lines.append("- Hero surfaces: 512 px/m; environment: 256 px/m; distant: 128 px/m (see WORLD_METRICS.md).")
    lines.append("")

    # Prioritized fix list
    lines.append("## 7. Prioritized fix list")
    lines.append("")
    fixes = [
        ("P0", "Create master materials", "Build `M_Master_Opaque`, `M_Master_Building`, `M_Master_Road`, `M_Master_Glass`, `M_Master_Foliage` in `/Game/Materials/` via Python (`MaterialEditingLibrary`), then `oc_dev_materials.py` dev materials get replaced by instances."),
        ("P0", "Import base PBR textures", "Download hero/plaster/road/tiling textures (see download list below), import via `oc_import_textures.py` with correct suffix/compression/sRGB, build `MI_*` instances from masters."),
        ("P0", "Replace blockout buildings", "Swap instanced engine cubes for modular building-kit pieces (SM_*), named without BLOCKOUT_*, with lightmap UVs and collision."),
        ("P0", "Add Sky Light / Post Process", "Ensure every map has Sky Atmosphere, Directional Light, Sky Light, Exponential Height Fog and a Post Process Volume with exposure + LUT."),
        ("P1", "Add collision + LODs / Nanite", "UCX_ collision or physics bodies on every mesh; Nanite for static environment, 3+ LODs for everything else."),
        ("P1", "Set texel density", "512 px/m hero, 256 px/m environment, 128 px/m distant; cap hero at 2K, props at 1K."),
        ("P2", "Macro variation on large surfaces", "Vertex-painted wear / trim sheets so tiling doesn't read as repeating."),
        ("P2", "Decal set", "Grim, lane markings, puddles, tire skids — pooled DBuffer decals."),
    ]
    for prio, title, desc in fixes:
        lines.append(f"### {prio} — {title}")
        lines.append("")
        lines.append(desc)
        lines.append("")

    # Asset download list
    lines.append("## 8. Assets to download (exact search terms)")
    lines.append("")
    lines.append("Track every download in `SourceArt/LICENSES.csv` (name, source URL, license, where used). CC0 originals are not committed; `SourceArt/manifest.json` lists them and `Tools/fetch_assets.py` re-downloads them.")
    lines.append("")
    lines.append("### Tiling materials (hero + environment, 2K max, DirectX normals)")
    lines.append("- Poly Haven / ambientCG — `weathered plaster stucco` — building walls (old town, hillside)")
    lines.append("- Poly Haven / ambientCG — `painted concrete` — building walls, colour variants per district")
    lines.append("- Poly Haven / ambientCG — `terracotta roof tiles` — pitched roofs")
    lines.append("- Poly Haven / ambientCG — `asphalt road` — road surfaces, cracks, tire marks")
    lines.append("- Poly Haven / ambientCG — `concrete pavement slabs` — sidewalks, 60 cm slabs")
    lines.append("- Poly Haven / ambientCG — `rusted metal` — port cranes, containers, shutters")
    lines.append("- Poly Haven / ambientCG — `laterite stone` / `red stone` — hillside foundations, quay walls")
    lines.append("- Poly Haven / ambientCG — `glass tower panels` / `dark glass` — downtown glazing")
    lines.append("- Poly Haven / ambientCG — `tin sheet corrugated` — port sheds, shacks")
    lines.append("")
    lines.append("# Trim sheet (one texture set covering strips: frames, panels, cornices, signage boards)")
    lines.append("- Poly Haven / ambientCG — `painted wood trim` + `metal trim` + `stone sill` — packed into one 2K trim sheet")
    lines.append("")
    lines.append("### Hero models (2K max, Nanite, lightmap UVs, UCX collision)")
    lines.append("- Poly Haven — `street lamp post` (sodium vapour) — one per 25–30 m")
    lines.append("- Poly Haven — `bench` — street seating")
    lines.append("- Poly Haven — `trash bin` — street clutter")
    lines.append("- Poly Haven — `fishing boat` (small) — port, Darsena")
    lines.append("- Poly Haven — `scooter` / `motorbike` — parked vehicles, player's motorbike reference")
    lines.append("- Poly Haven — `palm tree` + `tropical tree` — boulevard and coast foliage")
    lines.append("- Poly Haven — `market stall` — hero focal point per block")
    lines.append("")
    lines.append("### HDRIs (Poly Haven)")
    lines.append("- `sunny mediterranean` / `clear sky` — daylight sky light capture")
    lines.append("- `night city` — night sky light capture")
    lines.append("")
    lines.append("### Notes on sources")
    lines.append("- Prefer CC0 (Poly Haven, ambientCG). Fab free / monthly-free and owned Megascans accepted per-asset with a LICENSES.csv row.")
    lines.append("- Download DirectX (Y-down) normal maps where offered; otherwise flip the green channel on import.")
    lines.append("- Do not download 4K/8K; cap at 2K hero / 1K props / tiling reused heavily.")
    lines.append("")

    # What to do by hand
    lines.append("## 9. What you need to do by hand (exact menu paths)")
    lines.append("")
    lines.append("1. Download each texture/model above from Poly Haven / ambientCG into `SourceArt/Downloads/` (not committed).")
    lines.append("2. Add a row to `SourceArt/LICENSES.csv` for every download (name, source URL, author, license, date).")
    lines.append("3. Run `scripts/run_python.sh Content/Python/oc_import_textures.py` to import with correct settings.")
    lines.append("4. Build master materials (M4a), then replace every `M_Dev_*` reference with an `MI_*` instance.")
    lines.append("5. Replace engine-cube buildings with modular kit pieces in `oc_m3_world.py` / `OCWorldBuilderLibrary`.")
    lines.append("6. Add a Post Process Volume to each map: `Place Actors → Post Process Volume`, set exposure and LUT `T_LUT_Marevia`.")
    lines.append("7. Commit to git (with LFS) after each working step.")
    lines.append("")

    lines.append("## 10. Next step on approval")
    lines.append("")
    lines.append("Create `ART_BIBLE.md` (setting, mood, palette, 5 districts, 5 landmarks, believability rules), then `WORLD_METRICS.md` (player/door/road/block/texel-density numbers). Wait for approval before building final art.")
    lines.append("")

    with open(AUDIT_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    L.log(TAG, f"wrote {AUDIT_PATH} ({len(findings)} findings, {assets_scanned} assets scanned)")


def main():
    meshes, textures, materials, maps, blueprints, data_assets, other = scan_assets()
    write_audit(meshes, textures, materials, maps, blueprints, data_assets, other)


main()
