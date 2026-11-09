"""Final-art textures and materials: import the CC0 sets, build the surface masters, make instances.

  scripts/run_python.sh Content/Python/oc_art_materials.py

1. Imports every set in SourceArt/Downloads/<slug>/T_<slug>_{BC,N,ORM}.* (Tools/fetch_assets.py)
   into /Game/Environment/Textures/<slug>/ with WORLD_METRICS / ART_PIPELINE settings:
   BC sRGB, N BC5 normal map, ORM linear masks; World texture groups; capped at 2048.
2. Builds /Game/Materials/M_Master_Surface: one opaque master for kit pieces, props, roads and
   ground. A Custom HLSL node samples BaseColor / Normal / ORM texture objects with:
     ProjectionMode 0  mesh UV0 in metres / TileSizeM (kit pieces, roads, sidewalks)
                    1  roof planes: UVs aligned to each face's eaves from world position, so a
                       roof scaled to any footprint never stretches its tiles
                    2  world top-down (flat ground)
   macro variation from a second, 7x larger sample of the same texture (breaks tiling),
   per-instance tints from PerInstanceCustomData (slot A = data 0-2, slot B = data 3-5),
   roughness remap, normal strength.
3. Builds /Game/Materials/M_Master_KitGlass (dark glass; data 7 = window lit, for night),
   M_Master_Road (asphalt / alley setts / pier concrete with worn lane markings from the road
   meshes' UVs) and M_Master_Landscape (auto-material: sand, grass, dirt, rock by height and slope,
   with distance blending).
4. Material instances in /Game/Environment/Materials/: MI_Kit_* (KIT_SPEC.md slots), MI_Road_*,
   MI_Sidewalk.

Everything is regenerated from clean on each run.
"""

import os

import unreal

import oc_lib as L

TAG = "oc_art"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
DOWNLOADS = os.path.join(PROJECT_DIR, "SourceArt/Downloads")
TEX_ROOT = "/Game/Environment/Textures"
MI_ROOT = "/Game/Environment/Materials"
MAT_ROOT = "/Game/Materials"
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

# Instance name -> (texture slug, tile size m, tint, settings)
INSTANCES = {
    "MI_Kit_Wall": ("plastered_wall", 2.5, (1.0, 0.97, 0.92), dict(TintSlot=0, Macro=0.6, RoughMul=1.0, RoughAdd=0.05)),
    "MI_Kit_WallStucco": ("white_stucco", 2.0, (1.0, 0.98, 0.94), dict(TintSlot=0, Macro=0.5)),
    "MI_Kit_Trim": ("old_sandstone_02", 1.5, (1.0, 0.98, 0.93), dict(TintSlot=-1, Macro=0.3)),
    "MI_Kit_Wood": ("wood_peeling_paint_weathered", 1.2, (1.0, 1.0, 1.0), dict(TintSlot=1, Macro=0.2)),
    "MI_Kit_Metal": ("metal_plate", 1.0, (0.35, 0.36, 0.37), dict(TintSlot=-1, Macro=0.2, RoughMul=0.8)),
    "MI_Kit_Roof": ("clay_roof_tiles", 2.0, (1.0, 1.0, 1.0), dict(TintSlot=-1, Macro=0.5, ProjectionMode=1)),
    "MI_Kit_Concrete": ("brushed_concrete", 2.0, (0.95, 0.94, 0.92), dict(TintSlot=-1, Macro=0.5)),
    "MI_Road_Asphalt": ("asphalt_02", 4.0, (1.0, 1.0, 1.0), dict(TintSlot=-1, Macro=0.7)),
    "MI_Road_Setts": ("cobblestone_floor_01", 2.0, (1.0, 1.0, 1.0), dict(TintSlot=-1, Macro=0.5)),
    "MI_Sidewalk": ("concrete_pavers_02", 1.5, (1.0, 1.0, 1.0), dict(TintSlot=-1, Macro=0.5)),
    # Box-projected (ProjectionMode 3) for grey-box landmarks, bridges and distant scenery.
    "MI_Box_Stucco": ("white_stucco", 2.0, (1.0, 0.95, 0.88), dict(TintSlot=-1, Macro=0.5, ProjectionMode=3)),
    "MI_Box_Stone": ("old_sandstone_02", 2.0, (1.0, 0.97, 0.92), dict(TintSlot=-1, Macro=0.4, ProjectionMode=3)),
    "MI_Box_Concrete": ("brushed_concrete", 2.5, (0.92, 0.91, 0.9), dict(TintSlot=-1, Macro=0.5, ProjectionMode=3)),
    "MI_Box_Metal": ("metal_plate", 1.5, (0.4, 0.42, 0.44), dict(TintSlot=-1, Macro=0.3, ProjectionMode=3)),
    "MI_Box_Roof": ("clay_roof_tiles", 2.0, (1.0, 1.0, 1.0), dict(TintSlot=-1, Macro=0.4, ProjectionMode=3)),
    "MI_Box_Rock": ("cliff_side", 12.0, (0.95, 0.92, 0.88), dict(TintSlot=-1, Macro=0.8, ProjectionMode=3)),
    "MI_Box_Hill": ("aerial_grass_rock", 30.0, (0.8, 0.82, 0.7), dict(TintSlot=-1, Macro=0.8, ProjectionMode=2)),
}

SURFACE_HLSL = r"""
// Projection and tangent frame.
float3 n = normalize(VN);
float3 T, B;
float2 uv;
float tileCm = max(Tile, 0.01) * 100.0;
if (Mode < 0.5)
{
    uv = UV / max(Tile, 0.01);
    T = Parameters.TangentToWorld[0];
    B = Parameters.TangentToWorld[1];
}
else if (Mode < 1.5)
{
    // Roof planes: U along the eave (horizontal), V down the slope; flat parts fall back to X/Y.
    float3 h = float3(n.x, n.y, 0.0);
    T = length(h) > 0.05 ? normalize(cross(float3(0.0, 0.0, 1.0), h)) : float3(1.0, 0.0, 0.0);
    B = normalize(cross(n, T));
    uv = float2(dot(WP, T), dot(WP, B)) / tileCm;
}
else if (Mode < 2.5)
{
    T = float3(1.0, 0.0, 0.0);
    B = float3(0.0, 1.0, 0.0);
    uv = WP.xy / tileCm;
}
else
{
    // World box projection from the dominant axis (grey-box landmarks, bridges, scenery: their
    // engine-cube UVs would stretch one texture over a whole face).
    float3 a = abs(n);
    if (a.z >= a.x && a.z >= a.y) { T = float3(1, 0, 0); B = float3(0, 1, 0); uv = WP.xy / tileCm; }
    else if (a.x >= a.y)          { T = float3(0, 1, 0); B = float3(0, 0, -1); uv = WP.yz * float2(1, -1) / tileCm; }
    else                          { T = float3(1, 0, 0); B = float3(0, 0, -1); uv = WP.xz * float2(1, -1) / tileCm; }
}

// Base colour with macro variation: the same texture 7x larger, relative to its mean colour.
float3 bc = Texture2DSample(BCTex, BCTexSampler, uv).rgb;
float3 big = Texture2DSample(BCTex, BCTexSampler, uv * 0.143 + float2(0.37, 0.71)).rgb;
float3 mean = Texture2DSampleLevel(BCTex, BCTexSampler, float2(0.5, 0.5), 12.0).rgb;
float lumBig = dot(big, float3(0.3, 0.59, 0.11));
float lumMean = max(dot(mean, float3(0.3, 0.59, 0.11)), 0.02);
bc *= lerp(1.0, clamp(lumBig / lumMean, 0.65, 1.35), Macro);

// Tints: material tint x per-instance tint (slot A = custom data 0-2, slot B = 3-5).
float3 inst = Slot < -0.5 ? float3(1.0, 1.0, 1.0) : (Slot < 0.5 ? float3(I0, I1, I2) : float3(I3, I4, I5));
bc *= Tint * inst;

// Normal (DirectX, BC5): rebuild Z, scale, to world.
float2 nxy = (Texture2DSample(NTex, NTexSampler, uv).rg * 2.0 - 1.0) * NStrength;
float3 tn = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
WorldNormal = normalize(T * tn.x + B * tn.y + n * tn.z);

float3 orm = Texture2DSample(ORMTex, ORMTexSampler, uv).rgb;
AO = orm.r;
Rough = saturate(orm.g * RoughMul + RoughAdd);
Metal = orm.b * MetalMul;
return bc;
"""


def log(msg):
    L.log(TAG, msg)


# --- Textures -------------------------------------------------------------------------------

def import_textures():
    imported = {}
    for slug in sorted(os.listdir(DOWNLOADS)):
        folder = os.path.join(DOWNLOADS, slug)
        files = [f for f in os.listdir(folder) if f.startswith("T_")]
        if not files:
            continue
        dest = f"{TEX_ROOT}/{slug}"
        tasks = []
        for f in files:
            t = unreal.AssetImportTask()
            L.set_props(t, filename=os.path.join(folder, f), destination_path=dest, replace_existing=True,
                        automated=True, save=False)
            tasks.append(t)
        TOOLS.import_asset_tasks(tasks)
        maps = {}
        for f in files:
            name = os.path.splitext(f)[0]
            tex = unreal.load_asset(f"{dest}/{name}")
            if tex is None:
                raise RuntimeError(f"import failed: {dest}/{name}")
            role = name.rsplit("_", 1)[-1]
            if role == "N":
                L.set_props(tex, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP,
                            lod_group=unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
            elif role == "ORM":
                L.set_props(tex, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_MASKS,
                            lod_group=unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR)
            else:
                L.set_props(tex, srgb=True, compression_settings=unreal.TextureCompressionSettings.TC_DEFAULT,
                            lod_group=unreal.TextureGroup.TEXTUREGROUP_WORLD)
            tex.set_editor_property("max_texture_size", 2048)
            L.save(tex)
            maps[role] = tex
        imported[slug] = maps
        log(f"textures: {slug} {sorted(maps)}")
    return imported


# --- Masters --------------------------------------------------------------------------------

def fresh_material(name):
    path = f"{MAT_ROOT}/{name}"
    mat = L.get_or_create(name, MAT_ROOT, unreal.Material, unreal.MaterialFactoryNew(), TAG)
    MEL.delete_all_material_expressions(mat)
    L.set_props(mat, used_with_instanced_static_meshes=True, used_with_nanite=True)
    return mat


def scalar(mat, name, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    L.set_props(e, parameter_name=name, default_value=value)
    return e


def vector(mat, name, rgb, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    L.set_props(e, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return e


def tex_object(mat, name, sampler, default, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    L.set_props(e, parameter_name=name, sampler_type=sampler,
                sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    if default:
        e.set_editor_property("texture", default)
    return e


def instance_data(mat, index, default, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, x, y)
    e.set_editor_property("data_index", index)
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, x - 200, y)
    c.set_editor_property("r", default)
    MEL.connect_material_expressions(c, "", e, "DefaultValue")
    return e


def custom(mat, code, inputs, outputs, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property("code", code)
    e.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for name, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        ins.append(ci)
    e.set_editor_property("inputs", ins)
    outs = []
    for name, kind in outputs:
        co = unreal.CustomOutput()
        L.set_props(co, output_name=name, output_type=kind)
        outs.append(co)
    e.set_editor_property("additional_outputs", outs)
    for name, src in inputs:
        MEL.connect_material_expressions(src, "", e, name)
    return e


def build_surface_master(defaults, name="M_Master_Surface", two_sided=False):
    mat = fresh_material(name)
    mat.set_editor_property("two_sided", two_sided)  # two-sided variant: foliage (leaves are cut geometry)
    mat.set_editor_property("tangent_space_normal", False)  # the Custom node outputs a world normal
    bc = tex_object(mat, "BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, defaults.get("BC"), -1400, -400)
    nm = tex_object(mat, "Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, defaults.get("N"), -1400, -250)
    orm = tex_object(mat, "ORM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, defaults.get("ORM"), -1400, -100)
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 50)
    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1400, 150)
    vn = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1400, 250)
    params = [("Tile", scalar(mat, "TileSizeM", 2.0, -1100, -400)),
              ("Mode", scalar(mat, "ProjectionMode", 0.0, -1100, -320)),
              ("Macro", scalar(mat, "Macro", 0.5, -1100, -240)),
              ("Slot", scalar(mat, "TintSlot", -1.0, -1100, -160)),
              ("Tint", vector(mat, "Tint", (1, 1, 1), -1100, -80)),
              ("NStrength", scalar(mat, "NormalStrength", 1.0, -1100, 0)),
              ("RoughMul", scalar(mat, "RoughMul", 1.0, -1100, 80)),
              ("RoughAdd", scalar(mat, "RoughAdd", 0.0, -1100, 160)),
              ("MetalMul", scalar(mat, "MetalMul", 1.0, -1100, 240))]
    data = [(f"I{i}", instance_data(mat, i, 1.0, -800, 350 + 70 * i)) for i in range(6)]
    f1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    node = custom(mat, SURFACE_HLSL,
                  [("BCTex", bc), ("NTex", nm), ("ORMTex", orm), ("UV", uv), ("WP", wp), ("VN", vn)] + params + data,
                  [("WorldNormal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("AO", f1), ("Rough", f1), ("Metal", f1)],
                  -300, 0)
    MEL.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node, "WorldNormal", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(node, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(node, "Metal", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(node, "AO", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    MEL.recompile_material(mat)
    L.save(mat)
    log(f"built {mat.get_path_name()}")
    return mat


GLASS_HLSL = r"""
// Dark glass with a faint sky tint; lit windows glow warm (custom data 7 = lit, NightAmount from the day/night cycle).
Emis = float3(1.0, 0.72, 0.42) * Lit * Night * Strength;
return float3(0.02, 0.025, 0.03);
"""


def build_world_parameters():
    """MPC_World: written only by UOCWorldStateSubsystem (ARCHITECTURE.md)."""
    mpc = L.get_or_create("MPC_World", "/Game/Core", unreal.MaterialParameterCollection,
                          unreal.MaterialParameterCollectionFactoryNew(), TAG)
    params = []
    for name, value in (("NightAmount", 0.0), ("TimeOfDay", 12.0), ("Wetness", 0.0)):
        p = unreal.CollectionScalarParameter()
        L.set_props(p, parameter_name=name, default_value=value)
        params.append(p)
    mpc.set_editor_property("scalar_parameters", params)
    L.save(mpc)
    return mpc


def mpc_scalar(mat, mpc, name, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    L.set_props(e, collection=mpc, parameter_name=name)
    return e


LAMP_HLSL = r"""
// Street lamp glass and bulbs: dark by day, warm sodium glow at night (MPC_World.NightAmount).
Emis = float3(1.0, 0.62, 0.30) * Night * Strength;
return float3(0.35, 0.33, 0.30);
"""


def build_lamp_master(mpc):
    mat = fresh_material("M_Master_LampGlow")
    night = mpc_scalar(mat, mpc, "NightAmount", -800, 0)
    strength = scalar(mat, "Glow", 40.0, -800, 100)
    node = custom(mat, LAMP_HLSL, [("Night", night), ("Strength", strength)],
                  [("Emis", unreal.CustomMaterialOutputType.CMOT_FLOAT3)], -300, 0)
    MEL.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node, "Emis", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    r = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 200)
    r.set_editor_property("r", 0.2)
    MEL.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    L.save(mat)
    log(f"built {mat.get_path_name()}")


def build_glass_master(mpc):
    mat = fresh_material("M_Master_KitGlass")
    lit = instance_data(mat, 7, 0.0, -800, 0)
    night = mpc_scalar(mat, mpc, "NightAmount", -800, 100)
    strength = scalar(mat, "WindowLight", 3.0, -800, 200)
    node = custom(mat, GLASS_HLSL, [("Lit", lit), ("Night", night), ("Strength", strength)],
                  [("Emis", unreal.CustomMaterialOutputType.CMOT_FLOAT3)], -300, 0)
    MEL.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node, "Emis", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    r = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 200)
    r.set_editor_property("r", 0.06)
    MEL.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    s = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 300)
    s.set_editor_property("r", 1.0)
    MEL.connect_material_property(s, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(mat)
    L.save(mat)
    log(f"built {mat.get_path_name()}")
    return mat


ROAD_HLSL = r"""
// Road meshes (OCWorldBuilderLibrary::BuildRoadMeshes): UV0 = (lateral m, along m); UV1 = (half width m,
// marked lanes per direction). Alleys get stone setts, piers concrete slabs, everything else asphalt.
float hw = R1.x;
float lanes = R1.y;
float2 uv0 = R0;
float kind = (lanes < 0.5 && hw > 0.01 && hw < 3.5) ? 1.0 : ((lanes < 0.5 && hw > 4.9) ? 2.0 : 0.0);
float tile = kind > 1.5 ? TileP : (kind > 0.5 ? TileS : TileA);
float2 uv = uv0 / tile;
float2 dx = ddx(uv), dy = ddy(uv);
float3 bc; float2 nxy; float3 orm;
if (kind > 1.5)
{
    bc = Texture2DSampleGrad(PBC, PBCSampler, uv, dx, dy).rgb;
    nxy = Texture2DSampleGrad(PN, PNSampler, uv, dx, dy).rg;
    orm = Texture2DSampleGrad(PORM, PORMSampler, uv, dx, dy).rgb;
}
else if (kind > 0.5)
{
    bc = Texture2DSampleGrad(SBC, SBCSampler, uv, dx, dy).rgb;
    nxy = Texture2DSampleGrad(SN, SNSampler, uv, dx, dy).rg;
    orm = Texture2DSampleGrad(SORM, SORMSampler, uv, dx, dy).rgb;
}
else
{
    bc = Texture2DSampleGrad(ABC, ABCSampler, uv, dx, dy).rgb;
    nxy = Texture2DSampleGrad(AN, ANSampler, uv, dx, dy).rg;
    orm = Texture2DSampleGrad(AORM, AORMSampler, uv, dx, dy).rgb;
}
// Macro variation across the city: a large, slow sample of the asphalt colour.
float big = dot(Texture2DSample(ABC, ABCSampler, WP.xy / 4100.0).rgb, float3(0.3, 0.59, 0.11));
bc *= lerp(0.82, 1.18, saturate(big * 2.2 - 0.3));

// Lane markings (paint), worn by the asphalt's own texture.
float au = abs(uv0.x);
float mk = 0.0;
if (lanes > 0.5)
{
    float dash = step(frac(uv0.y / 12.0), 0.375);
    mk += step(hw - 0.55, au) * step(au, hw - 0.40);
    if (lanes > 1.5)
    {
        mk += step(0.06, au) * step(au, 0.18);
        float laneW = (hw - 0.5) / lanes;
        mk += step(abs(au - laneW), 0.07) * dash;
    }
    else
    {
        mk += step(au, 0.07) * dash;
    }
}
float wear = saturate(dot(bc, float3(0.3, 0.59, 0.11)) * 3.0);
mk = saturate(mk) * lerp(0.55, 1.0, wear);
bc = lerp(bc, float3(0.72, 0.71, 0.66), mk);

float2 n2 = (nxy * 2.0 - 1.0) * (1.0 - mk * 0.8);
float3 tn = float3(n2, sqrt(saturate(1.0 - dot(n2, n2))));
WorldNormal = normalize(mul(tn, (float3x3)Parameters.TangentToWorld));
Rough = lerp(saturate(orm.g), 0.55, mk);
AO = orm.r;
return bc;
"""

LAND_HLSL = r"""
// Landscape auto-material: beach sand near the sea, grass, dirt on slopes, rock on steep or high
// ground. World top-down UVs in metres; transitions broken up by a large noise-free texture sample;
// far away each layer is sampled 4x larger so tiling doesn't read from the hills.
float3 n = normalize(VN);
float h = WP.z / 100.0;
float2 m = WP.xy / 100.0;
float vary = dot(Texture2DSample(GBC, GBCSampler, m / 61.0).rgb, float3(0.3, 0.59, 0.11)) * 2.0 - 0.5;
float wSand = saturate((1.8 - h + vary * 0.8) / 1.2);
float wRock = max(saturate((0.78 - n.z + vary * 0.05) / 0.10), saturate((h - 230.0 + vary * 40.0) / 70.0));
float wDirt = saturate((0.94 - n.z + vary * 0.04) / 0.07) * (1.0 - wRock);
wSand *= (1.0 - wRock);
float wGrass = saturate(1.0 - wSand - wRock - wDirt);
float total = wSand + wRock + wDirt + wGrass + 1e-4;
float4 w = float4(wGrass, wDirt, wRock, wSand) / total;

float dist = length(WP - Cam);
float far = saturate((dist - 4000.0) / 12000.0);
float3 bc = 0; float2 nn = 0; float3 orm = 0;
// Steep rock: project from the side (X or Y) so cliffs don't smear; everything else top-down.
float3 an = abs(n);
float2 mRock = an.z > 0.7 ? m : (an.x > an.y ? float2(WP.y, -WP.z) / 100.0 : float2(WP.x, -WP.z) / 100.0);
#define OC_LAYER(i, T, TN, TO, TILE) if (w[i] > 0.01) { float2 uv = (i == 2 ? mRock : m) / TILE; float2 dx = ddx(uv), dy = ddy(uv); \
    float3 c = lerp(Texture2DSampleGrad(T, T##Sampler, uv, dx, dy).rgb, Texture2DSampleGrad(T, T##Sampler, uv * 0.25, dx * 0.25, dy * 0.25).rgb, far); \
    bc += c * w[i]; nn += (Texture2DSampleGrad(TN, TN##Sampler, uv, dx, dy).rg * 2.0 - 1.0) * w[i] * (1.0 - far * 0.7); \
    orm += Texture2DSampleGrad(TO, TO##Sampler, uv, dx, dy).rgb * w[i]; }
OC_LAYER(0, GBC, GN, GORM, TileG)
OC_LAYER(1, DBC, DN, DORM, TileD)
OC_LAYER(2, RBC, RN, RORM, TileR)
OC_LAYER(3, SBC, SN, SORM, TileS)
float3 tn = float3(nn, sqrt(saturate(1.0 - dot(nn, nn))));
WorldNormal = normalize(mul(tn, (float3x3)Parameters.TangentToWorld));
Rough = saturate(orm.g);
AO = orm.r;
// Dry Mediterranean grass: less saturated, a touch darker than the raw scan.
float g = dot(bc, float3(0.3, 0.59, 0.11));
bc = lerp(bc, lerp(float3(g, g, g), bc, 0.65) * 0.9, w.x);
return bc * lerp(0.9, 1.1, saturate(vary + 0.5));
"""


def textures_for(textures, slug):
    t = textures.get(slug)
    if not t:
        raise RuntimeError(f"textures for {slug} missing: python3 -I Tools/fetch_assets.py")
    return t


def build_textured_master(name, code, sets, tiles, landscape=False):
    """A master whose Custom node samples several texture sets: sets = [(prefix, slug)], tiles = {param: default}."""
    mat = fresh_material(name)
    if landscape:
        L.set_props(mat, used_with_instanced_static_meshes=False, used_with_nanite=False)
    mat.set_editor_property("tangent_space_normal", False)
    inputs = []
    y = -600
    for prefix, t in sets:
        for role, sampler in (("BC", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                              ("N", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                              ("ORM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)):
            inputs.append((f"{prefix}{role}", tex_object(mat, f"{prefix}_{role}", sampler, t[role], -1400, y)))
            y += 120
    inputs.append(("WP", MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1100, -600)))
    inputs.append(("VN", MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1100, -500)))
    inputs.append(("Cam", MEL.create_material_expression(mat, unreal.MaterialExpressionCameraPositionWS, -1100, -400)))
    for i in (0, 1):
        c = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, -300 + 100 * i)
        c.set_editor_property("coordinate_index", i)
        inputs.append((f"R{i}", c))
    yy = 0
    for p, v in tiles.items():
        inputs.append((p, scalar(mat, p, v, -1100, yy)))
        yy += 80
    f1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    node = custom(mat, code, inputs, [("WorldNormal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("AO", f1), ("Rough", f1)], -300, 0)
    MEL.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node, "WorldNormal", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(node, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(node, "AO", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    MEL.recompile_material(mat)
    L.save(mat)
    log(f"built {mat.get_path_name()}")
    return mat


# --- Instances ------------------------------------------------------------------------------

def make_instance(name, parent, textures, tile, tint, settings):
    mi = L.get_or_create(name, MI_ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew(), TAG)
    MEL.set_material_instance_parent(mi, parent)
    MEL.clear_all_material_instance_parameters(mi)
    for param, role in (("BaseColor", "BC"), ("Normal", "N"), ("ORM", "ORM")):
        if role in textures:
            MEL.set_material_instance_texture_parameter_value(mi, param, textures[role])
    MEL.set_material_instance_scalar_parameter_value(mi, "TileSizeM", tile)
    MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    for k, v in settings.items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    MEL.update_material_instance(mi)
    L.save(mi)
    return mi


def main():
    textures = import_textures()
    first = textures.get("plastered_wall", {})
    surface = build_surface_master(first)
    mpc = build_world_parameters()
    glass = build_glass_master(mpc)
    build_lamp_master(mpc)
    build_surface_master(first, "M_Master_SurfaceTwoSided", two_sided=True)
    made = []
    for name, (slug, tile, tint, settings) in INSTANCES.items():
        if slug not in textures:
            log(f"skip {name}: textures for {slug} not downloaded (python3 -I Tools/fetch_assets.py)")
            continue
        make_instance(name, surface, textures[slug], tile, tint, settings)
        made.append(name)
    gmi = L.get_or_create("MI_Kit_Glass", MI_ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew(), TAG)
    MEL.set_material_instance_parent(gmi, glass)
    L.save(gmi)
    made.append("MI_Kit_Glass")
    build_textured_master("M_Master_Road", ROAD_HLSL,
                          [("A", textures_for(textures, "asphalt_02")), ("S", textures_for(textures, "cobblestone_floor_01")),
                           ("P", textures_for(textures, "concrete_pavers_02"))],
                          {"TileA": 4.0, "TileS": 2.0, "TileP": 3.0})
    build_textured_master("M_Master_Landscape", LAND_HLSL,
                          [("G", textures_for(textures, "aerial_grass_rock")), ("D", textures_for(textures, "brown_mud_rocks_01")),
                           ("R", textures_for(textures, "cliff_side")), ("S", textures_for(textures, "coast_sand_01"))],
                          {"TileG": 6.0, "TileD": 4.0, "TileR": 8.0, "TileS": 4.0}, landscape=True)
    log(f"instances: {', '.join(made)}")


main()
