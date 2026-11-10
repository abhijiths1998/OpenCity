"""Decals (M4b): import the ambientCG decal sets and build the decal master and instances.

  scripts/run_python.sh Content/Python/oc_decals.py

Sources: Tools/fetch_ambientcg.py -> SourceArt/Downloads/Decals/<id>/<id>_1K-PNG_{Color,NormalDX,
Roughness,Opacity}.png (CC0). Textures go to /Game/Environment/Textures/Decals/<id>/.

M_Master_Decal: a DBuffer decal (colour, normal, roughness). Opacity comes from the set's opacity
map, or - for sets without one (tyre tracks, wall leaks) - from how dark the colour map is
(OpacityFromColor = 1). MI_Decal_<id> per set. Road decals are placed by
UOCWorldBuilderLibrary::BuildRoadDecals; wall leaks are also used by M_Master_Surface as a grime
overlay on walls (no decal actors on buildings).
"""

import os

import unreal

import oc_lib as L

TAG = "oc_decals"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJECT_DIR, "SourceArt/Downloads/Decals")
TEX = "/Game/Environment/Textures/Decals"
MAT_ROOT = "/Game/Materials"
MI_ROOT = "/Game/Environment/Materials/Decals"
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

# Per set: (opacity from colour?, strength, roughness override or -1 to use the map[, colour key, gain]).
# Opacity from colour = how far the colour is below the key, times the gain: the wall leaks sit on
# white (key 0.92), the tyre tracks on light grey (0.48 to 0.60 in linear colour, as the shader
# sees it), which at the old fixed key showed the whole decal rectangle as a pale patch.
SETS = {
    "ManholeCover001": (0.0, 1.0, -1.0),
    "ManholeCover004": (0.0, 1.0, -1.0),
    "AsphaltDamage001": (0.0, 1.0, -1.0),
    "ChewingGum001": (0.0, 1.0, -1.0),
    "ManholeCover009": (0.0, 1.0, -1.0),   # gutter gullies (Phase 4)
    "ManholeCover010": (0.0, 1.0, -1.0),   # covers set in cobbles (Phase 4)
    "TireTracks001": (1.0, 0.55, 0.6, 0.42, 4.0),   # linear: background 0.48-0.60, tracks 0.07-0.32
    "Leaking012A": (1.0, 0.8, -1.0),
    "Leaking013A": (1.0, 0.8, -1.0),
    "Leaking016A": (1.0, 0.8, -1.0),
    "Leaking019A": (1.0, 0.8, -1.0),
}

DECAL_HLSL = r"""
float4 c = Texture2DSample(BC, BCSampler, UV);
float lum = dot(c.rgb, float3(0.3, 0.59, 0.11));
float op = FromColor > 0.5 ? saturate((ColorKey - lum) * ColorGain) : Texture2DSample(OP, OPSampler, UV).r;
Opacity = op * Strength;
float2 nxy = Texture2DSample(NM, NMSampler, UV).rg * 2.0 - 1.0;
Normal = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
Rough = RoughOverride >= 0.0 ? RoughOverride : Texture2DSample(RO, ROSampler, UV).r;
return FromColor > 0.5 ? c.rgb * 0.6 : c.rgb;
"""


def log(msg):
    L.log(TAG, msg)


def import_set(aid):
    folder = os.path.join(SRC, aid)
    files = sorted(f for f in os.listdir(folder) if f.endswith(".png"))
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        L.set_props(t, filename=os.path.join(folder, f), destination_path=f"{TEX}/{aid}", replace_existing=True,
                    automated=True, save=False, destination_name="T_" + os.path.splitext(f)[0].replace("_1K-PNG", ""))
        tasks.append(t)
    TOOLS.import_asset_tasks(tasks)
    maps = {}
    for f in files:
        name = "T_" + os.path.splitext(f)[0].replace("_1K-PNG", "")
        tex = unreal.load_asset(f"{TEX}/{aid}/{name}")
        role = name.rsplit("_", 1)[-1]
        if role == "NormalDX":
            L.set_props(tex, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif role in ("Roughness", "Opacity"):
            L.set_props(tex, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_GRAYSCALE)
        else:
            L.set_props(tex, srgb=True, compression_settings=unreal.TextureCompressionSettings.TC_DEFAULT)
        L.set_props(tex, lod_group=unreal.TextureGroup.TEXTUREGROUP_WORLD, max_texture_size=1024)
        L.save(tex)
        maps[role] = tex
    return maps


def tex_object(mat, name, sampler, default, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    L.set_props(e, parameter_name=name, sampler_type=sampler, sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    if default:
        e.set_editor_property("texture", default)
    return e


def scalar(mat, name, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    L.set_props(e, parameter_name=name, default_value=value)
    return e


def build_master(defaults):
    mat = L.get_or_create("M_Master_Decal", MAT_ROOT, unreal.Material, unreal.MaterialFactoryNew(), TAG)
    MEL.delete_all_material_expressions(mat)
    # Translucent deferred decal; with r.DBuffer on, the connected outputs (colour, normal, roughness)
    # make it a DBuffer decal (the decal blend mode is derived in 5.8, not set).
    L.set_props(mat, material_domain=unreal.MaterialDomain.MD_DEFERRED_DECAL, blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT)
    S = unreal.MaterialSamplerType
    inputs = [("BC", tex_object(mat, "Color", S.SAMPLERTYPE_COLOR, defaults.get("Color"), -1100, -300)),
              ("NM", tex_object(mat, "Normal", S.SAMPLERTYPE_NORMAL, defaults.get("NormalDX"), -1100, -150)),
              ("RO", tex_object(mat, "Roughness", S.SAMPLERTYPE_GRAYSCALE, defaults.get("Roughness"), -1100, 0)),
              ("OP", tex_object(mat, "Opacity", S.SAMPLERTYPE_GRAYSCALE, defaults.get("Opacity"), -1100, 150)),
              ("UV", MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 300)),
              ("FromColor", scalar(mat, "OpacityFromColor", 0.0, -1100, 380)),
              ("Strength", scalar(mat, "Strength", 1.0, -1100, 460)),
              ("RoughOverride", scalar(mat, "RoughnessOverride", -1.0, -1100, 540)),
              ("ColorKey", scalar(mat, "ColorKey", 0.92, -1100, 620)),
              ("ColorGain", scalar(mat, "ColorGain", 2.5, -1100, 700))]
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    e.set_editor_property("code", DECAL_HLSL)
    e.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for n, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    e.set_editor_property("inputs", ins)
    outs = []
    for n, k in (("Opacity", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3),
                 ("Rough", unreal.CustomMaterialOutputType.CMOT_FLOAT1)):
        co = unreal.CustomOutput()
        L.set_props(co, output_name=n, output_type=k)
        outs.append(co)
    e.set_editor_property("additional_outputs", outs)
    for n, src in inputs:
        MEL.connect_material_expressions(src, "", e, n)
    MEL.connect_material_property(e, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(e, "Normal", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(e, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(e, "Opacity", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    L.save(mat)
    log(f"built {mat.get_path_name()}")
    return mat


def main():
    sets = {aid: import_set(aid) for aid in SETS if os.path.isdir(os.path.join(SRC, aid))}
    if not sets:
        raise RuntimeError("no decals downloaded: python3 -I Tools/fetch_ambientcg.py")
    master = build_master(sets.get("ManholeCover001", next(iter(sets.values()))))
    for aid, maps in sets.items():
        from_color, strength, rough, *key = SETS[aid]
        key = key or [0.92, 2.5]
        mi = L.get_or_create(f"MI_Decal_{aid}", MI_ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew(), TAG)
        MEL.set_material_instance_parent(mi, master)
        MEL.clear_all_material_instance_parameters(mi)
        for param, role in (("Color", "Color"), ("Normal", "NormalDX"), ("Roughness", "Roughness"), ("Opacity", "Opacity")):
            if role in maps:
                MEL.set_material_instance_texture_parameter_value(mi, param, maps[role])
        for k, v in (("OpacityFromColor", from_color), ("Strength", strength), ("RoughnessOverride", rough),
                     ("ColorKey", key[0]), ("ColorGain", key[1])):
            MEL.set_material_instance_scalar_parameter_value(mi, k, v)
        MEL.update_material_instance(mi)
        L.save(mi)
    log(f"decal instances: {', '.join(sorted(sets))}")


main()
