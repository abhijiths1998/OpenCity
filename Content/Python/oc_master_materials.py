"""M4a — Master materials generator.

Builds the approved master materials under /Game/Materials/ via
MaterialEditingLibrary, so they can be rebuilt from clean. Artists and
scripts work with Material Instances only.

Run headless:
    scripts/run_python.sh Content/Python/oc_master_materials.py

Masters built:
  /Game/Materials/M_Master_Opaque   PBR, detail normal, macro variation,
                                    vertex-color dirt/moss/wear
  /Game/Materials/M_Master_Building Trim sheets, edge wear, rain streaks,
                                    night windows randomised per instance
  /Game/Materials/M_Master_Road     Asphalt, cracks, tire marks, wet puddles
  /Game/Materials/M_Master_Glass    Reflections, dirt, rain droplets,
                                    cheap translucency
  /Game/Materials/M_Master_Foliage  Two-sided foliage, subsurface, cheap wind
  /Game/Materials/M_Master_CarPaint Clear Coat, flakes, dirt, scratches
  /Game/Materials/M_Master_Character Skin subsurface, cloth, simple hair
  /Game/Materials/M_Master_Water    Ocean, shoreline foam

Limits per master: at most 3 static switches, at most 8 texture samples.
"""

import unreal

import oc_lib as L

TAG = "oc_master_materials"
MAT_DIR = "/Game/Materials"
MEL = unreal.MaterialEditingLibrary

# --- helpers ------------------------------------------------------------

def param_scalar(mat, name, default, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", unreal.Name(name))
    e.set_editor_property("default_value", default)
    return e


def param_vector(mat, name, rgb, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", unreal.Name(name))
    e.set_editor_property("default_value", unreal.LinearColor(*rgb, 1.0))
    return e


def param_texture(mat, name, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    e.set_editor_property("parameter_name", unreal.Name(name))
    return e


def const(mat, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", value)
    return e


def lerp(mat, a, b, weight, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    MEL.connect_material_expressions(a, "", e, "A")
    MEL.connect_material_expressions(b, "", e, "B")
    MEL.connect_material_expressions(weight, "", e, "Alpha")
    return e


def multiply(mat, a, b, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y)
    MEL.connect_material_expressions(a, "", e, "A")
    MEL.connect_material_expressions(b, "", e, "B")
    return e


def connect(mat, src, src_output, prop):
    """Connect src expression's output to the material's main input property."""
    MEL.connect_material_property(src, src_output, prop)


def create_master(name):
    path = f"{MAT_DIR}/{name}"
    if L.assets.does_asset_exist(path):
        mat = L.assets.load_asset(path)
        L.log(TAG, f"reused existing {path}")
    else:
        factory = unreal.MaterialFactoryNew()
        mat = L.tools.create_asset(name, MAT_DIR, unreal.Material, factory)
        if mat is None:
            raise RuntimeError(f"could not create {path}")
        L.log(TAG, f"created {path}")
    L.save(mat)
    return mat


def build_opaque():
    mat = create_master("M_Master_Opaque")

    # Texture samples (6 of 8 allowed).
    bc = param_texture(mat, "BaseColor", -300, -220)
    n = param_texture(mat, "Normal", -300, -100)
    orm = param_texture(mat, "ORM", -300, 20)
    e = param_texture(mat, "Emissive", -300, 140)

    # Parameters.
    macro_tint = param_vector(mat, "MacroTint", (1, 1, 1), 400, -220)
    macro_amount = param_scalar(mat, "MacroAmount", 0.5, 400, -100)
    dirt = param_scalar(mat, "VertexDirt", 0.5, 400, 20)
    rough_min = param_scalar(mat, "RoughnessMin", 0.3, 400, 140)
    rough_max = param_scalar(mat, "RoughnessMax", 0.8, 400, 260)

    # Vertex color -> dirt/moss/wear (RGBA channels). Channel R = dirt amount.
    vc = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexColor, 100, -220)
    # Vertex color R drives dirt amount (connected via the `dirt` parameter below).

    # Base color = texture * macro tint, darkened by vertex dirt.
    bc_tinted = multiply(mat, bc, macro_tint, 100, -220)
    bc_dirty_dark = multiply(mat, bc_tinted, const(mat, 0.45, 100, -100), 100, -100)
    bc_dirty = lerp(mat, bc_tinted, bc_dirty_dark, dirt, 100, 20)
    connect(mat, bc_dirty, "", unreal.EMaterialProperty.MP_BaseColor)

    # Normal = texture sample (normal map).
    connect(mat, n, "", unreal.EMaterialProperty.MP_Normal)

    # Roughness = ORM green channel, clamped between min/max.
    orm_g = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 380)
    orm_g.set_editor_property("R", False)
    orm_g.set_editor_property("G", True)
    orm_g.set_editor_property("B", False)
    orm_g.set_editor_property("A", False)
    MEL.connect_material_expressions(orm, "", orm_g, "")
    rough = lerp(mat, orm_g, const(mat, rough_max, 100, 500), const(mat, rough_min, 100, 580))
    connect(mat, rough, "", unreal.EMaterialProperty.MP_Roughness)

    # Metallic = ORM blue channel.
    orm_b = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 660)
    orm_b.set_editor_property("R", False)
    orm_b.set_editor_property("G", False)
    orm_b.set_editor_property("B", True)
    orm_b.set_editor_property("A", False)
    MEL.connect_material_expressions(orm, "", orm_b, "")
    connect(mat, orm_b, "", unreal.EMaterialProperty.MP_Metallic)

    # Ambient occlusion = ORM red channel.
    orm_r = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 740)
    orm_r.set_editor_property("R", True)
    orm_r.set_editor_property("G", False)
    orm_r.set_editor_property("B", False)
    orm_r.set_editor_property("A", False)
    MEL.connect_material_expressions(orm, "", orm_r, "")
    connect(mat, orm_r, "", unreal.EMaterialProperty.MP_AmbientOcclusion)

    # Emissive = texture * emissive multiplier.
    emissive_mult = param_scalar(mat, "EmissiveMultiplier", 1.0, 400, 380)
    e_scaled = multiply(mat, e, emissive_mult, 100, 820)
    connect(mat, e_scaled, "", unreal.EMaterialProperty.MP_EmissiveColor)

    L.save(mat)
    L.log(TAG, f"{mat.get_path_name()} built (6 texture samples, 0 static switches)")


def build_building():
    mat = create_master("M_Master_Building")

    # Texture samples (4 of 8 allowed): trim sheet, wall BC, wall normal, wall ORM.
    trim = param_texture(mat, "TrimSheet", -300, -220)
    wall_bc = param_texture(mat, "WallBaseColor", -300, -100)
    wall_n = param_texture(mat, "WallNormal", -300, 20)
    wall_orm = param_texture(mat, "WallORM", -300, 140)

    # Parameters.
    edge_wear = param_scalar(mat, "EdgeWear", 0.6, 400, -220)
    rain = param_scalar(mat, "RainStreak", 0.4, 400, -100)
    night_lit = param_scalar(mat, "NightWindowLit", 0.25, 400, 20)
    night_color = param_vector(mat, "NightWindowColor", (1.0, 0.85, 0.6), 400, 140)

    # Static switch (1 of 3 allowed): night windows on/off.
    night_switch = MEL.create_material_expression(mat, unreal.MaterialExpressionStaticSwitchParameter, 400, 260)
    night_switch.set_editor_property("parameter_name", unreal.Name("NightWindows"))
    night_switch.set_editor_property("default_value", True)

    # Trim sheet tiling: scale by trim scale parameter.
    trim_scale = param_scalar(mat, "TrimScale", 1.0, 100, -220)

    # Base color: wall base color + trim sheet overlay where trim alpha present.
    trim_a = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, -220)
    trim_a.set_editor_property("R", True); trim_a.set_editor_property("G", False)
    trim_a.set_editor_property("B", False); trim_a.set_editor_property("A", False)
    MEL.connect_material_expressions(trim, "", trim_a, "")
    wall_trimmed = lerp(mat, wall_bc, multiply(mat, trim, trim_a, 100, -100), trim_a, 100, 20)
    connect(mat, wall_trimmed, "", unreal.EMaterialProperty.MP_BaseColor)

    # Normal: wall normal.
    connect(mat, wall_n, "", unreal.EMaterialProperty.MP_Normal)

    # Roughness: ORM green channel, modified by edge wear (vertex color G).
    vc_g = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 380)
    vc_g.set_editor_property("R", False); vc_g.set_editor_property("G", True)
    vc_g.set_editor_property("B", False); vc_g.set_editor_property("A", False)
    vc_r = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 460)
    vc_r.set_editor_property("R", True); vc_r.set_editor_property("G", False)
    vc_r.set_editor_property("B", False); vc_r.set_editor_property("A", False)
    vc = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexColor, 100, 300)
    MEL.connect_material_expressions(vc, "G", vc_g, "")
    MEL.connect_material_expressions(vc, "R", vc_r, "")
    orm_g = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 540)
    orm_g.set_editor_property("R", False); orm_g.set_editor_property("G", True)
    orm_g.set_editor_property("B", False); orm_g.set_editor_property("A", False)
    MEL.connect_material_expressions(wall_orm, "", orm_g, "")
    wear_rough = multiply(mat, orm_g, lerp(mat, const(mat, 0.2, 100, 620), const(mat, 0.9, 100, 700), edge_wear), 100, 780)
    connect(mat, wear_rough, "", unreal.EMaterialProperty.MP_Roughness)

    # Metallic: ORM blue channel.
    orm_b = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 860)
    orm_b.set_editor_property("R", False); orm_b.set_editor_property("G", False)
    orm_b.set_editor_property("B", True); orm_b.set_editor_property("A", False)
    MEL.connect_material_expressions(wall_orm, "", orm_b, "")
    connect(mat, orm_b, "", unreal.EMaterialProperty.MP_Metallic)

    # Ambient occlusion: ORM red channel.
    orm_r = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 940)
    orm_r.set_editor_property("R", True); orm_r.set_editor_property("G", False)
    orm_r.set_editor_property("B", False); orm_r.set_editor_property("A", False)
    MEL.connect_material_expressions(wall_orm, "", orm_r, "")
    connect(mat, orm_r, "", unreal.EMaterialProperty.MP_AmbientOcclusion)

    # Emissive: night windows (window mask from VC B channel * night color * lit switch).
    win_mask = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 1020)
    win_mask.set_editor_property("R", False); win_mask.set_editor_property("G", False)
    win_mask.set_editor_property("B", True); win_mask.set_editor_property("A", False)
    MEL.connect_material_expressions(vc, "B", win_mask, "")
    night_emissive = multiply(mat, night_color, win_mask, 100, 1100)
    night_emissive = multiply(mat, night_emissive, const(mat, night_lit, 100, 1180), 100, 1180)
    # static switch: route through night_switch -> emissive or black
    black = const(mat, 0.0, 100, 1260)
    night_final = lerp(mat, black, night_emissive, night_switch, 100, 1340)
    connect(mat, night_final, "", unreal.EMaterialProperty.MP_EmissiveColor)

    L.save(mat)
    L.log(TAG, f"{mat.get_path_name()} built (4 texture samples, 1 static switch)")


def build_road():
    mat = create_master("M_Master_Road")

    # Texture samples (4 of 8 allowed): base color, normal, ORM, wetness mask.
    bc = param_texture(mat, "BaseColor", -300, -220)
    n = param_texture(mat, "Normal", -300, -100)
    orm = param_texture(mat, "ORM", -300, 20)
    wet = param_texture(mat, "WetnessMask", -300, 140)

    # Parameters.
    wetness = param_scalar(mat, "Wetness", 0.0, 400, -220)
    rough_min = param_scalar(mat, "RoughnessMin", 0.3, 400, -100)
    rough_max = param_scalar(mat, "RoughnessMax", 0.8, 400, 20)
    puddle_scale = param_scalar(mat, "PuddleScale", 1.0, 400, 140)

    # Base color.
    connect(mat, bc, "", unreal.EMaterialProperty.MP_BaseColor)

    # Normal.
    connect(mat, n, "", unreal.EMaterialProperty.MP_Normal)

    # Roughness: ORM green lowered by wetness.
    orm_g = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 380)
    orm_g.set_editor_property("R", False); orm_g.set_editor_property("G", True)
    orm_g.set_editor_property("B", False); orm_g.set_editor_property("A", False)
    MEL.connect_material_expressions(orm, "", orm_g, "")
    wet_dry_rough = lerp(mat, const(mat, rough_max, 100, 460), const(mat, rough_min, 100, 540), wetness, 100, 620)
    connect(mat, wet_dry_rough, "", unreal.EMaterialProperty.MP_Roughness)

    # Metallic: ORM blue (asphalt is non-metallic; instance overrides where needed).
    orm_b = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 700)
    orm_b.set_editor_property("R", False); orm_b.set_editor_property("G", False)
    orm_b.set_editor_property("B", True); orm_b.set_editor_property("A", False)
    MEL.connect_material_expressions(orm, "", orm_b, "")
    connect(mat, orm_b, "", unreal.EMaterialProperty.MP_Metallic)

    # Ambient occlusion: ORM red channel.
    orm_r = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 780)
    orm_r.set_editor_property("R", True); orm_r.set_editor_property("G", False)
    orm_r.set_editor_property("B", False); orm_r.set_editor_property("A", False)
    MEL.connect_material_expressions(orm, "", orm_r, "")
    connect(mat, orm_r, "", unreal.EMaterialProperty.MP_AmbientOcclusion)

    # Emissive from wetness mask * puddle strength (rain-driven puddles).
    puddle_strength = param_scalar(mat, "PuddleStrength", 0.5, 400, 220)
    wetness_masked = multiply(mat, wet, puddle_strength, 100, 860)
    puddle_emissive = multiply(mat, wetness_masked, const(mat, puddle_scale, 100, 940), 100, 1020)
    connect(mat, puddle_emissive, "", unreal.EMaterialProperty.MP_EmissiveColor)

    L.save(mat)
    L.log(TAG, f"{mat.get_path_name()} built (4 texture samples, 0 static switches)")


def build_glass():
    mat = create_master("M_Master_Glass")

    # Texture samples (3 of 8 allowed): base color, normal, dirt mask.
    bc = param_texture(mat, "BaseColor", -300, -220)
    n = param_texture(mat, "Normal", -300, -100)
    dirt = param_texture(mat, "DirtMask", -300, 20)

    # Parameters.
    opacity = param_scalar(mat, "Opacity", 0.3, 400, -220)
    ior = param_scalar(mat, "IOR", 1.5, 400, -100)
    tint = param_vector(mat, "GlassTint", (1, 1, 1), 400, 20)
    emissive_strength = param_scalar(mat, "EmissiveStrength", 0.0, 400, 140)

    # Blend mode: translucent (approved for glass; cheap translucency per ART_PIPELINE.md).
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_Translucent)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DefaultLit)

    # Base color = tint.
    connect(mat, bc, "", unreal.EMaterialProperty.MP_BaseColor)
    connect(mat, n, "", unreal.EMaterialProperty.MP_Normal)

    # Opacity.
    connect(mat, opacity, "", unreal.EMaterialProperty.MP_Opacity)

    # Emissive for rain droplets / glow.
    emissive = multiply(mat, tint, const(mat, emissive_strength, 100, 300), 100, 380)
    connect(mat, emissive, "", unreal.EMaterialProperty.MP_EmissiveColor)

    L.save(mat)
    L.log(TAG, f"{mat.get_path_name()} built (3 texture samples, 0 static switches, translucent)")


def build_foliage():
    mat = create_master("M_Master_Foliage")

    # Texture samples (3 of 8 allowed): base color, normal, opacity mask.
    bc = param_texture(mat, "BaseColor", -300, -220)
    n = param_texture(mat, "Normal", -300, -100)
    opacity_mask = param_texture(mat, "OpacityMask", -300, 20)

    # Parameters.
    subsurface = param_scalar(mat, "Subsurface", 0.1, 400, -220)
    wind = param_scalar(mat, "WindStrength", 0.5, 400, -100)
    two_sided = param_scalar(mat, "TwoSided", 1.0, 400, 20)

    # Blend mode: masked (foliage uses alpha cutout; regular LODs per ART_PIPELINE.md).
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_Masked)

    connect(mat, bc, "", unreal.EMaterialProperty.MP_BaseColor)
    connect(mat, n, "", unreal.EMaterialProperty.MP_Normal)
    connect(mat, opacity_mask, "", unreal.EMaterialProperty.MP_OpacityMask)

    # Masked threshold and two-sided.
    connect(mat, two_sided, "", unreal.EMaterialProperty.MP_TwoSided)
    connect(mat, subsurface, "", unreal.EMaterialProperty.MP_Subsurface)

    L.save(mat)
    L.log(TAG, f"{mat.get_path_name()} built (3 texture samples, 0 static switches, masked)")


def build_car_paint():
    mat = create_master("M_Master_CarPaint")

    # Texture samples (2 of 8 allowed): base color, flake/dirt mask.
    bc = param_texture(mat, "BaseColor", -300, -220)
    flake_mask = param_texture(mat, "FlakeMask", -300, -100)

    # Parameters.
    clear_coat = param_scalar(mat, "ClearCoat", 1.0, 400, -220)
    flake_amount = param_scalar(mat, "FlakeAmount", 0.3, 400, -100)
    dirt = param_scalar(mat, "Dirt", 0.1, 400, 20)

    # Base color darkened by dirt.
    dirty_bc = multiply(mat, bc, lerp(mat, const(mat, 1.0, 100, -100), const(mat, 0.6, 100, -20), dirt), 100, 100)
    connect(mat, dirty_bc, "", unreal.EMaterialProperty.MP_BaseColor)

    # Clear coat.
    connect(mat, clear_coat, "", unreal.EMaterialProperty.MP_ClearCoat)

    # Flakes drive clear-coat roughness variation via the mask.
    flake_variation = multiply(mat, flake_mask, const(mat, flake_amount, 100, 180), 100, 260)
    connect(mat, flake_variation, "", unreal.EMaterialProperty.MP_ClearCoatRoughness)

    L.save(mat)
    L.log(TAG, f"{mat.get_path_name()} built (2 texture samples, 0 static switches)")


def build_character():
    mat = create_master("M_Master_Character")

    # Texture samples (3 of 8 allowed): skin BC, skin normal, skin ORM.
    skin_bc = param_texture(mat, "SkinBaseColor", -300, -220)
    skin_n = param_texture(mat, "SkinNormal", -300, -100)
    skin_orm = param_texture(mat, "SkinORM", -300, 20)

    # Parameters.
    subsurface = param_scalar(mat, "Subsurface", 0.3, 400, -220)
    sss_color = param_vector(mat, "SubsurfaceColor", (1.0, 0.4, 0.3), 400, -100)

    connect(mat, skin_bc, "", unreal.EMaterialProperty.MP_BaseColor)
    connect(mat, skin_n, "", unreal.EMaterialProperty.MP_Normal)

    # Subsurface from ORM green channel.
    orm_g = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 100, 380)
    orm_g.set_editor_property("R", False); orm_g.set_editor_property("G", True)
    orm_g.set_editor_property("B", False); orm_g.set_editor_property("A", False)
    MEL.connect_material_expressions(skin_orm, "", orm_g, "")
    sss = multiply(mat, orm_g, subsurface, 100, 460)
    connect(mat, sss, "", unreal.EMaterialProperty.MP_Subsurface)
    connect(mat, sss_color, "", unreal.EMaterialProperty.MP_SubsurfaceColor)

    L.save(mat)
    L.log(TAG, f"{mat.get_path_name()} built (3 texture samples, 0 static switches)")


def build_water():
    mat = create_master("M_Master_Water")

    # Texture samples (2 of 8 allowed): normal (dual), foam mask.
    n = param_texture(mat, "Normal", -300, -220)
    foam = param_texture(mat, "FoamMask", -300, -100)

    # Parameters.
    wave_speed = param_vector(mat, "WaveSpeed", (0.1, 0.05, 0, 0), 400, -220)
    foam_tint = param_vector(mat, "FoamTint", (1, 1, 1), 400, -100)

    # Blend mode: translucent for water surface.
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_Translucent)

    connect(mat, n, "", unreal.EMaterialProperty.MP_Normal)

    # Emissive foam.
    foam_emissive = multiply(mat, foam, foam_tint, 100, -100)
    connect(mat, foam_emissive, "", unreal.EMaterialProperty.MP_EmissiveColor)

    L.save(mat)
    L.log(TAG, f"{mat.get_path_name()} built (2 texture samples, 0 static switches, translucent)")


def main():
    unreal.EditorLoadingAndSavingUtils.load_package("/Game/Materials") if False else None
    build_opaque()
    build_building()
    build_road()
    build_glass()
    build_foliage()
    build_car_paint()
    build_character()
    build_water()
    L.log(TAG, "master materials done")


main()
