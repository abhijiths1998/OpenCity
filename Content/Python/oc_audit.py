"""World-building audit (M4a step 1): scan /Game and write AUDIT.md.

Every finding comes from the assets themselves; nothing in the scan section is hand-written.
Hand-written parts (playtest observations, the prioritized fix list, the download list) live in
AUDIT_NOTES.md and are appended unchanged, so re-running the scan never loses them.

Checks
  Meshes     code-generated / blockout, no UVs, no collision, no Nanite and no LODs, odd scale
             (lightmap UVs are not checked: the project is fully dynamic, r.AllowStaticLighting=False)
  Textures   compression and sRGB by suffix (_BC _N _ORM _E _M), size above the 2K cap, no mips,
             names without a T_ prefix or a known suffix
  Materials  not from an approved master (/Game/Materials/M_Master_*), no textures (flat or
             procedural), no normal input, constant roughness
  Tiling     large-surface materials (terrain, road, building, sidewalk, water) without a macro
             variation parameter or texture
  Maps       Sky Atmosphere, sun, Sky Light, Height Fog, Post Process Volume (exposure), per map,
             read from World Partition actor descriptors so unloaded actors count too

Run headless:  scripts/run_python.sh Content/Python/oc_audit.py
"""

import datetime
import os

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
AUDIT_PATH = os.path.join(PROJECT_DIR, "AUDIT.md")
NOTES_PATH = os.path.join(PROJECT_DIR, "AUDIT_NOTES.md")
TAG = "oc_audit"

MASTER_PREFIX = "/Game/Materials/M_Master_"
CODE_GENERATED = {  # path prefix -> what made it (hard rule 1: blockout only)
    "/Game/World/Roads/": "road chunks built in C++ from the road graph (OCWorldBuilderLibrary)",
    "/Game/Vehicles/Bike/": "motorbike chassis generated as glTF by Tools/make_bike_chassis.py",
    "/Game/Dev/": "dev/test content",
}
LARGE_SURFACE_WORDS = ("Terrain", "Landscape", "Road", "Building", "Sidewalk", "Water", "Ground")
MACRO_WORDS = ("macro", "variation", "detail", "distance")
TEXTURE_SUFFIXES = {
    "_BC": ("default", True), "_N": ("normal", False), "_ORM": ("masks", False),
    "_M": ("masks", False), "_E": ("default", True),
}

MEL = unreal.MaterialEditingLibrary

findings = {}  # section -> list of (asset, [problems])
counts = {}


def log(msg):
    unreal.log(f"[{TAG}] {msg}")


def add(section, asset, problems):
    if problems:
        findings.setdefault(section, []).append((asset, problems))


def short(path):
    return path.split(".")[0]


# --- Meshes ---------------------------------------------------------------------------------

def check_static_mesh(path, mesh):
    p = []
    for prefix, what in CODE_GENERATED.items():
        if path.startswith(prefix):
            p.append(f"code-generated: {what}; blockout only, needs a BLOCKOUT_ name or final-art replacement")
    # C++ helper: the engine's mesh scripting libraries refuse to run in a commandlet.
    info = unreal.OCAuditLibrary.get_static_mesh_audit_info(mesh)
    if info.num_uv_channels < 1:
        p.append("no UV channel 0")
    if info.simple_collision_count == 0 and not info.complex_as_simple:
        p.append("no simple collision (no UCX_/primitives) and not complex-as-simple")
    if not info.nanite and info.num_lo_ds < 2:
        p.append("no Nanite and only one LOD")
    ext = mesh.get_bounds().box_extent
    size = 2 * max(ext.x, ext.y, ext.z)
    if size > 5000 and not path.startswith("/Game/World/Roads/"):
        p.append(f"very large single mesh ({size / 100:.0f} m): not modular")
    if size < 1:
        p.append("degenerate bounds (empty mesh?)")
    return p


def check_skeletal_mesh(path, mesh):
    p = []
    for prefix, what in CODE_GENERATED.items():
        if path.startswith(prefix):
            p.append(f"code-generated: {what}; blockout only")
    try:
        if mesh.get_editor_property("physics_asset") is None:
            p.append("no physics asset (no collision)")
    except Exception:
        pass
    return p


# --- Textures -------------------------------------------------------------------------------

def check_texture(path, tex):
    p = []
    name = path.split("/")[-1].split(".")[0]
    if not name.startswith("T_"):
        p.append("name lacks the T_ prefix")
    suffix = next((s for s in sorted(TEXTURE_SUFFIXES, key=len, reverse=True) if name.endswith(s)), None)
    comp = tex.get_editor_property("compression_settings")
    srgb = tex.get_editor_property("srgb")
    if suffix is None:
        p.append("no known suffix (_BC _N _ORM _E _M): role unknown, settings not checkable")
    else:
        kind, want_srgb = TEXTURE_SUFFIXES[suffix]
        if kind == "normal" and comp != unreal.TextureCompressionSettings.TC_NORMALMAP:
            p.append(f"normal map not using Normalmap compression ({comp.name})")
        if kind == "masks" and comp != unreal.TextureCompressionSettings.TC_MASKS:
            p.append(f"mask/ORM not using Masks compression ({comp.name})")
        if srgb != want_srgb:
            p.append(f"sRGB is {srgb}, should be {want_srgb}")
    try:
        sx, sy = tex.blueprint_get_size_x(), tex.blueprint_get_size_y()
        if max(sx, sy) > 2048:
            p.append(f"{sx}x{sy}: above the 2K in-game cap")
    except Exception:
        pass
    if tex.get_editor_property("mip_gen_settings") == unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS:
        p.append("no mipmaps")
    return p


# --- Materials ------------------------------------------------------------------------------

def base_material(mat):
    m = mat
    for _ in range(16):
        if isinstance(m, unreal.MaterialInstance):
            parent = m.get_editor_property("parent")
            if parent is None:
                return None
            m = parent
        else:
            return m
    return None


def check_material(path, mat):
    p = []
    base = base_material(mat)
    is_master = path.startswith(MASTER_PREFIX)
    if base is None:
        p.append("material instance with no parent")
        return p
    base_path = base.get_path_name()
    if not is_master and not base_path.startswith(MASTER_PREFIX):
        p.append(f"not built from an approved master (base: {short(base_path)})")
    if isinstance(mat, unreal.Material) and not is_master:
        if not MEL.get_used_textures(mat):
            p.append("no textures: flat or procedural colour")
        if MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_NORMAL) is None:
            p.append("no normal input")
        rough = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_ROUGHNESS)
        if rough is None or isinstance(rough, unreal.MaterialExpressionConstant):
            p.append("constant roughness (looks plastic)")
    name = path.split("/")[-1]
    if any(w.lower() in name.lower() for w in LARGE_SURFACE_WORDS) and not is_master and not path.startswith(("/Game/Vehicles/", "/Game/Characters/")):
        params = [str(n).lower() for n in MEL.get_scalar_parameter_names(base) + MEL.get_texture_parameter_names(base)]
        if not any(any(w in n for w in MACRO_WORDS) for n in params):
            add("tiling", short(path), ["large-surface material with no macro/variation/distance-blend parameter"])
    return p


# --- Maps -----------------------------------------------------------------------------------

def check_map(path):
    p = []
    unreal.EditorLoadingAndSavingUtils.load_map(path.split(".")[0])
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    try:
        descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs() or []  # None for non-partitioned maps
    except Exception:
        descs = []
    classes = [str(d.native_class) for d in descs]
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    classes += [a.get_class().get_path_name() for a in actors]
    need = {"SkyAtmosphere": "no Sky Atmosphere", "DirectionalLight": "no Directional Light (sun)",
            "SkyLight": "no Sky Light", "ExponentialHeightFog": "no Exponential Height Fog",
            "PostProcessVolume": "no Post Process Volume: default exposure, no grading LUT"}
    for key, msg in need.items():
        if not any(key in c for c in classes):
            p.append(msg)
    if not any("VolumetricCloud" in c for c in classes):
        p.append("no Volumetric Cloud (ART_PIPELINE asks for cheap clouds on High+)")
    for a in actors:
        if isinstance(a, unreal.PostProcessVolume):
            s = a.get_editor_property("settings")
            if not s.get_editor_property("override_auto_exposure_min_brightness") and \
                    not s.get_editor_property("override_auto_exposure_bias"):
                p.append(f"{a.get_actor_label()}: exposure not set (engine defaults)")
            if not s.get_editor_property("override_color_grading_lut"):
                p.append(f"{a.get_actor_label()}: no colour-grading LUT")
    counts.setdefault("map_actors", {})[short(path)] = len(descs) or len(actors)
    return p


# --- Report ---------------------------------------------------------------------------------

SECTIONS = [
    ("meshes", "Meshes"),
    ("textures", "Textures"),
    ("materials", "Materials"),
    ("tiling", "Visible-tiling risk"),
    ("maps", "Lighting and post-process (per map)"),
]


def write(totals):
    out = ["# OpenCity: world-building audit", "",
           f"Scanned {datetime.date.today().isoformat()} by `Content/Python/oc_audit.py` (headless). "
           "Sections 1-6 are generated; everything after them comes from `AUDIT_NOTES.md`.", "",
           "## 1. Summary", "",
           "| Assets | Count | With problems |", "|---|---|---|"]
    for key, label in (("meshes", "Static and skeletal meshes"), ("textures", "Textures"),
                       ("materials", "Materials and instances"), ("maps", "Maps")):
        out.append(f"| {label} | {totals.get(key, 0)} | {len(findings.get(key, []))} |")
    masters = totals.get("masters", 0)
    out += ["", f"- Master materials in `/Game/Materials`: {masters} of 8.",
            f"- Materials built from a master: {totals.get('from_master', 0)} (instances `MI_*`: {totals.get('instances', 0)}).",
            f"- Textures following the T_ naming with a role suffix: {totals.get('named_textures', 0)} of {totals.get('textures', 0)}.",
            ""]
    for n, (key, label) in enumerate(SECTIONS, start=2):
        out += [f"## {n}. {label}", ""]
        items = findings.get(key, [])
        if not items:
            out += ["No problems found.", ""]
            continue
        # Five or more assets in one folder with the same problems collapse into one entry.
        groups = {}
        for asset, problems in sorted(items):
            groups.setdefault((asset.rsplit("/", 1)[0], tuple(problems)), []).append(asset)
        for (folder, problems), assets in sorted(groups.items()):
            heads = [f"`{folder}/` ({len(assets)} assets)"] if len(assets) >= 5 else [f"`{a}`" for a in assets]
            for head in heads:
                out.append(f"- {head}")
                out += [f"  - {pr}" for pr in problems]
        out.append("")
    if os.path.exists(NOTES_PATH):
        with open(NOTES_PATH, encoding="utf-8") as f:
            out += ["---", "", f.read()]
    with open(AUDIT_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


def main():
    lib = unreal.EditorAssetLibrary
    totals = {}
    maps = []
    for path in lib.list_assets("/Game", recursive=True):
        data = lib.find_asset_data(path)
        cls = str(data.asset_class_path.asset_name) if data and data.is_valid() else ""
        if cls == "World":
            maps.append(path)
            continue
        if cls not in ("StaticMesh", "SkeletalMesh", "Texture2D", "Material", "MaterialInstanceConstant"):
            continue
        asset = lib.load_asset(path)
        if asset is None:
            continue
        if cls == "StaticMesh":
            totals["meshes"] = totals.get("meshes", 0) + 1
            add("meshes", short(path), check_static_mesh(path, asset))
        elif cls == "SkeletalMesh":
            totals["meshes"] = totals.get("meshes", 0) + 1
            add("meshes", short(path), check_skeletal_mesh(path, asset))
        elif cls == "Texture2D":
            totals["textures"] = totals.get("textures", 0) + 1
            pr = check_texture(path, asset)
            if not any("prefix" in x or "suffix" in x for x in pr):
                totals["named_textures"] = totals.get("named_textures", 0) + 1
            add("textures", short(path), pr)
        else:
            totals["materials"] = totals.get("materials", 0) + 1
            if path.startswith(MASTER_PREFIX):
                totals["masters"] = totals.get("masters", 0) + 1
            if cls == "MaterialInstanceConstant":
                totals["instances"] = totals.get("instances", 0) + 1
            pr = check_material(path, asset)
            if not any("approved master" in x for x in pr) and not path.startswith(MASTER_PREFIX):
                totals["from_master"] = totals.get("from_master", 0) + 1
            add("materials", short(path), pr)
    totals["maps"] = len(maps)
    for path in maps:
        try:
            add("maps", short(path), check_map(path))
        except Exception as e:
            add("maps", short(path), [f"could not inspect ({e})"])
    write(totals)
    n = sum(len(v) for v in findings.values())
    log(f"wrote {AUDIT_PATH}: {n} assets with problems; totals {totals}")


main()
