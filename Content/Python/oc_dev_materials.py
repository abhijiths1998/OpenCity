"""Grey-box debug materials, generated so they can be rebuilt from clean. Replaced in M4a.

  /Game/Dev/Materials/M_Dev_Terrain    colour by height (sand, green hills, grey rock) with soft variation
  /Game/Dev/Materials/M_Dev_Road       asphalt with lane markings, from the road meshes' UVs
                                       (UV0 = lateral m, along m; UV1 = half width m, marked lanes per direction)
  /Game/Dev/Materials/M_Dev_Sidewalk   60 cm paving slabs (world-planar UV0 in metres)
  /Game/Dev/Materials/M_Dev_Bridge     flat concrete
  /Game/Dev/Materials/M_Dev_Water      opaque grey-box water: sea, river and reservoir surfaces

Run headless:  scripts/run_python.sh Content/Python/oc_dev_materials.py
"""

import unreal

import oc_lib as L

PATH = "/Game/Dev/Materials"
MEL = unreal.MaterialEditingLibrary


def const(mat, value, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", value)
    return e


def color(mat, rgb, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    e.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return e


# Hash and smooth value noise for Custom nodes (no textures in grey-box). Float-precision safe for
# world positions in metres (sin()-based hashes band at large inputs).
HLSL_NOISE = """
#define OC_HASH(c) frac((frac((c).x * 0.1031 + (c).y * 0.11369) + 33.33) * frac((c).y * 0.13787 + (c).x * 0.0973) * 43.7585)
"""


def value_noise_hlsl(var, p):
    """HLSL lines setting float `var` to smooth value noise (0..1) at float2 expression `p`."""
    return f"""
float2 {var}_i = floor({p}); float2 {var}_f = frac({p}); {var}_f = {var}_f * {var}_f * (3.0 - 2.0 * {var}_f);
float {var} = lerp(lerp(OC_HASH({var}_i), OC_HASH({var}_i + float2(1, 0)), {var}_f.x),
                   lerp(OC_HASH({var}_i + float2(0, 1)), OC_HASH({var}_i + float2(1, 1)), {var}_f.x), {var}_f.y);
"""


def custom(mat, code, inputs, x, y, output=None):
    """A Custom HLSL node returning float3; inputs is [(name, expression)]."""
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property("code", code)
    e.set_editor_property("output_type", output or unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for name, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        ins.append(ci)
    e.set_editor_property("inputs", ins)
    for name, src in inputs:
        MEL.connect_material_expressions(src, "", e, name)
    return e


def uv(mat, index, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, x, y)
    e.set_editor_property("coordinate_index", index)
    return e


def world_pos(mat, x, y):
    return MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, x, y)


def ramp(mat, z, start_cm, span_cm, x, y):
    """saturate((z - start) / span)"""
    sub = MEL.create_material_expression(mat, unreal.MaterialExpressionSubtract, x, y)
    MEL.connect_material_expressions(z, "", sub, "A")
    MEL.connect_material_expressions(const(mat, start_cm, x - 150, y + 60), "", sub, "B")
    div = MEL.create_material_expression(mat, unreal.MaterialExpressionDivide, x + 150, y)
    MEL.connect_material_expressions(sub, "", div, "A")
    MEL.connect_material_expressions(const(mat, span_cm, x, y + 120), "", div, "B")
    sat = MEL.create_material_expression(mat, unreal.MaterialExpressionSaturate, x + 300, y)
    MEL.connect_material_expressions(div, "", sat, "")
    return sat


def make_terrain():
    mat = L.get_or_create("M_Dev_Terrain", PATH, unreal.Material, unreal.MaterialFactoryNew(), "oc_dev")
    MEL.delete_all_material_expressions(mat)

    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1200, 0)
    z = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -1000, 0)
    L.set_props(z, r=False, g=False, b=True, a=False)
    MEL.connect_material_expressions(wp, "", z, "")

    sand, grass, rock = (0.62, 0.55, 0.40), (0.30, 0.38, 0.18), (0.42, 0.40, 0.38)
    shore = ramp(mat, z, 30.0, 120.0, -800, -200)       # sand below ~0.3 m (beach), green by 1.5 m
    high = ramp(mat, z, 15000.0, 15000.0, -800, 200)    # green to rock from 150 m to 300 m

    lerp1 = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -200, -100)
    MEL.connect_material_expressions(color(mat, sand, -400, -300), "", lerp1, "A")
    MEL.connect_material_expressions(color(mat, grass, -400, -150), "", lerp1, "B")
    MEL.connect_material_expressions(shore, "", lerp1, "Alpha")
    lerp2 = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, 0, 0)
    MEL.connect_material_expressions(lerp1, "", lerp2, "A")
    MEL.connect_material_expressions(color(mat, rock, -400, 100), "", lerp2, "B")
    MEL.connect_material_expressions(high, "", lerp2, "Alpha")

    # Break up the flat colours: broad patches (40 m) and fine mottling (3 m).
    vary = custom(mat, HLSL_NOISE + value_noise_hlsl("big", "WP.xy / 4000.0") + value_noise_hlsl("fine", "WP.xy / 300.0")
                  + "return Col * (0.78 + 0.32 * big) * (0.92 + 0.14 * fine);",
                  [("Col", lerp2), ("WP", wp)], 200, 0)
    MEL.connect_material_property(vary, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(mat, 0.9, 0, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    L.save(mat)
    L.log("oc_dev", f"built {mat.get_path_name()}")
    return mat


def make_flat(name, rgb, roughness=0.8):
    """Single-colour two-sided grey-box material."""
    mat = L.get_or_create(name, PATH, unreal.Material, unreal.MaterialFactoryNew(), "oc_dev")
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)  # instanced HLODs
    mat.set_editor_property("used_with_nanite", True)  # road chunks are Nanite; set explicitly so a standalone rebuild keeps it
    MEL.connect_material_property(color(mat, rgb, -300, 0), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(mat, roughness, -300, 150), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    L.save(mat)
    L.log("oc_dev", f"built {mat.get_path_name()}")
    return mat


ROAD_HLSL = HLSL_NOISE + value_noise_hlsl("patch", "WP.xy / 350.0") + value_noise_hlsl("grain", "WP.xy / 12.0") + """
float3 asphalt = float3(0.055, 0.055, 0.058) * (0.85 + 0.3 * patch) * (0.9 + 0.2 * grain);
float hw = Road1.x;            // half width, m (0 on skirts)
float lanes = Road1.y;         // marked lanes per direction (0 = no markings)
float3 surf = asphalt;
if (lanes < 0.5 && hw > 0.01 && hw < 3.5)
{
    // Alleys: grey stone setts, 25 cm, rows offset by half a stone.
    float2 q = WP.xy / 25.0;
    q.x += 0.5 * fmod(floor(q.y), 2.0);
    float2 g = frac(q);
    float joint = max(step(g.x, 0.08), step(g.y, 0.08));
    float3 stone = float3(0.20, 0.19, 0.175) * (0.8 + 0.4 * OC_HASH(floor(q))) * (0.9 + 0.2 * patch);
    surf = lerp(stone, stone * 0.45, joint);
}
else if (lanes < 0.5 && hw > 4.9)
{
    // Piers: 3 m concrete slabs.
    float2 g = frac(WP.xy / 300.0);
    float joint = max(step(g.x, 0.008), step(g.y, 0.008));
    float3 conc = float3(0.33, 0.32, 0.30) * (0.88 + 0.2 * OC_HASH(floor(WP.xy / 300.0))) * (0.9 + 0.15 * grain);
    surf = lerp(conc, conc * 0.6, joint);
}
float au = abs(Road0.x);       // distance from the centre line, m
float mk = 0.0;
if (lanes > 0.5)
{
    float dash = step(frac(Road0.y / 12.0), 0.375);         // 4.5 m line, 7.5 m gap
    mk += step(hw - 0.55, au) * step(au, hw - 0.40);         // solid edge lines
    if (lanes > 1.5)
    {
        mk += step(0.06, au) * step(au, 0.18);               // double centre line
        float laneW = (hw - 0.5) / lanes;
        mk += step(abs(au - laneW), 0.07) * dash;            // dashed lane dividers
    }
    else
    {
        mk += step(au, 0.07) * dash;                         // dashed centre line
    }
}
return lerp(surf, float3(0.70, 0.69, 0.64), saturate(mk));
"""

SIDEWALK_HLSL = HLSL_NOISE + """
float2 cell = UV.xy / 0.6;                                   // 60 cm slabs
float2 g = frac(cell);
float joint = max(step(g.x, 0.04), step(g.y, 0.04));
float n = OC_HASH(floor(cell));
float3 stone = float3(0.50, 0.47, 0.42) * (0.9 + 0.16 * n);
return lerp(stone, stone * 0.62, joint);
"""


def make_custom_surface(name, code, inputs_fn, roughness):
    """Two-sided material whose base colour is one Custom HLSL node."""
    mat = L.get_or_create(name, PATH, unreal.Material, unreal.MaterialFactoryNew(), "oc_dev")
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)  # instanced HLODs
    mat.set_editor_property("used_with_nanite", True)  # road chunks are Nanite; set explicitly so a standalone rebuild keeps it
    node = custom(mat, code, inputs_fn(mat), -300, 0)
    MEL.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(mat, roughness, -300, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    L.save(mat)
    L.log("oc_dev", f"built {mat.get_path_name()}")
    return mat


BUILDING_HLSL = HLSL_NOISE + """
// Grey-box buildings (instanced cubes). Style = building_styles index from the layout:
// 0 oldtown, 1 grid90, 2 grid120, 3 beach, 4 village, 5 terminal, 6 hills. Rnd = 0-1 per building.
float3 n = normalize(N);
float s = round(Style);
float3 pal[6] = { float3(0.80, 0.73, 0.60), float3(0.79, 0.58, 0.33), float3(0.72, 0.42, 0.30),
                  float3(0.83, 0.67, 0.60), float3(0.86, 0.85, 0.80), float3(0.86, 0.78, 0.52) };
float3 wall = pal[min((int)floor(Rnd * 6.0), 5)];
bool industrial = (s == 2.0 || s == 5.0);
if (industrial)
{
    wall = lerp(float3(0.46, 0.48, 0.49), float3(0.58, 0.52, 0.44), Rnd);
}
if (n.z > 0.6)
{
    bool tiled = (s == 0.0 || s == 4.0 || s == 6.0);
    return tiled ? float3(0.55, 0.27, 0.18) * (0.85 + 0.3 * Rnd) : float3(0.42, 0.41, 0.39);
}
float u = dot(WP.xy, float2(-n.y, n.x)) / 100.0;    // metres along the wall
float v = WP.z / 100.0;
float storey = industrial ? 6.0 : 3.2;
float win = step(abs(frac(u / 3.0) - 0.5), industrial ? 0.42 : 0.19) * step(abs(frac(v / storey) - 0.6), industrial ? 0.08 : 0.22);
float shutter = industrial ? 0.0 : step(abs(abs(frac(u / 3.0) - 0.5) - 0.25), 0.05) * step(abs(frac(v / storey) - 0.6), 0.22);
float3 shut = (Rnd > 0.5) ? float3(0.20, 0.33, 0.24) : float3(0.30, 0.38, 0.45);
float3 c = lerp(wall, float3(0.06, 0.07, 0.08), win);
return lerp(c, shut, shutter);
"""

# Grey-box water (sea, river, reservoir): deep blue-green with slow broad patches; low roughness,
# so it reflects the sky. Opaque on purpose: translucent water and shoreline foam come with
# M_Master_Water in M4b.
WATER_HLSL = HLSL_NOISE + value_noise_hlsl("big", "WP.xy / 9000.0") + value_noise_hlsl("ripple", "WP.xy / 700.0") + """
float3 deep = float3(0.010, 0.045, 0.060);
float3 teal = float3(0.020, 0.085, 0.090);
return lerp(deep, teal, 0.6 * big + 0.25 * ripple);
"""

STRIPES_HLSL = """
float t = frac((WP.x + WP.y + WP.z) / 100.0);
return t < 0.5 ? float3(0.75, 0.08, 0.06) : float3(0.85, 0.85, 0.82);
"""


def make_building():
    mat = L.get_or_create("M_Dev_Building", PATH, unreal.Material, unreal.MaterialFactoryNew(), "oc_dev")
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    style = MEL.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -700, 100)
    style.set_editor_property("data_index", 0)
    rnd = MEL.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -700, 250)
    rnd.set_editor_property("data_index", 1)
    normal = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -700, -100)
    node = custom(mat, BUILDING_HLSL, [("WP", world_pos(mat, -700, -250)), ("N", normal), ("Style", style), ("Rnd", rnd)], -300, 0)
    MEL.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(mat, 0.85, -300, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    L.save(mat)
    L.log("oc_dev", f"built {mat.get_path_name()}")


make_terrain()
make_building()
make_custom_surface("M_Dev_Barrier", STRIPES_HLSL, lambda m: [("WP", world_pos(m, -700, 0))], 0.6)
make_flat("M_Dev_Landmark", (0.78, 0.74, 0.66), 0.8)       # pale stone: story landmarks stand out
make_flat("M_Dev_LandmarkAccent", (0.55, 0.26, 0.17), 0.7)  # terracotta roofs, domes
make_flat("M_Dev_Burnt", (0.07, 0.06, 0.055), 0.95)        # La Cala, the burned house
make_flat("M_Dev_Metal", (0.30, 0.32, 0.34), 0.5)          # masts, cranes, rails
make_flat("M_Dev_Glass", (0.25, 0.42, 0.55), 0.2)          # lantern, pool, windows on landmarks
make_flat("M_Dev_Police", (0.10, 0.18, 0.45), 0.5)         # cordon cars
make_flat("M_Dev_Rock", (0.20, 0.17, 0.14), 0.95)          # rockfall
make_flat("M_Dev_Train", (0.38, 0.14, 0.10), 0.7)          # parked freight wagons
make_flat("M_Dev_Concrete", (0.58, 0.57, 0.54), 0.9)       # the dam
make_custom_surface("M_Dev_Road", ROAD_HLSL,
                    lambda m: [("Road0", uv(m, 0, -700, -100)), ("Road1", uv(m, 1, -700, 50)), ("WP", world_pos(m, -700, 200))], 0.8)
make_custom_surface("M_Dev_Sidewalk", SIDEWALK_HLSL, lambda m: [("UV", uv(m, 0, -700, 0))], 0.85)
make_flat("M_Dev_Bridge", (0.45, 0.44, 0.42), 0.8)
make_custom_surface("M_Dev_Water", WATER_HLSL, lambda m: [("WP", world_pos(m, -700, 0))], 0.12)
