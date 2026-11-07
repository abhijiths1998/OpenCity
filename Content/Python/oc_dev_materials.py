"""Grey-box debug materials, generated so they can be rebuilt from clean. Replaced in M4.

  /Game/Dev/Materials/M_Dev_Terrain   colour by height: sand at the shore, green hills, grey rock

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

    MEL.connect_material_property(lerp2, "", unreal.MaterialProperty.MP_BASE_COLOR)
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
    MEL.connect_material_property(color(mat, rgb, -300, 0), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(mat, roughness, -300, 150), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    L.save(mat)
    L.log("oc_dev", f"built {mat.get_path_name()}")
    return mat


make_terrain()
make_flat("M_Dev_Road", (0.06, 0.06, 0.065), 0.7)
make_flat("M_Dev_Sidewalk", (0.32, 0.31, 0.29), 0.85)
make_flat("M_Dev_Bridge", (0.45, 0.44, 0.42), 0.8)
