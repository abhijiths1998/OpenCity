"""Instruction counts for the generated masters against the ART_PIPELINE.md budgets.

  OC_PY_RENDER=1 scripts/run_python.sh Content/Python/oc_material_stats.py

Logs one MATSTAT line per master (pixel and vertex shader instructions, samplers, budget) from
MaterialEditingLibrary.get_statistics on the SM6 shader platform. Masters without a budget in
ART_PIPELINE.md are listed with '-'.
"""

import unreal

import oc_lib as L

TAG = "oc_matstats"
# Base-pass pixel shader targets (ART_PIPELINE.md, "Master materials": original target + the
# measured ~190 fixed cost). The kit and props share M_Master_Surface (Building role).
BUDGETS = {
    "M_Master_Surface": 410,
    "M_Master_SurfaceTwoSided": 410,
    "M_Master_Road": 390,
    "M_Master_Landscape": None,
    "M_Master_Water": 440,
    "M_Master_KitGlass": 340,
    "M_Master_CarGlass": 340,
    "M_Master_LampGlow": None,
    "M_Master_Decal": None,
    "M_PP_Rain": None,
}

over = []
for name, budget in BUDGETS.items():
    mat = unreal.load_asset(f"/Game/Materials/{name}")
    if mat is None:
        L.log(TAG, f"MATSTAT {name}: missing")
        continue
    unreal.MaterialEditingLibrary.recompile_material(mat)
    st = unreal.MaterialEditingLibrary.get_statistics(mat)
    ps = st.num_pixel_shader_instructions
    L.log(TAG, f"MATSTAT {name}: ps {ps} vs {st.num_vertex_shader_instructions} samplers {st.num_samplers} budget {budget or '-'}")
    if budget and ps > budget:
        over.append(f"{name} {ps}/{budget}")
# Static switches make one shader per instance permutation: report the ones that cover the city.
for name in ("MI_Kit_Wall", "MI_Kit_Roof", "MI_Sidewalk", "MI_Box_Stone"):
    mi = unreal.load_asset(f"/Game/Environment/Materials/{name}")
    if mi:
        st = unreal.MaterialEditingLibrary.get_statistics(mi)
        L.log(TAG, f"MATSTAT   {name}: ps {st.num_pixel_shader_instructions}")
L.log(TAG, f"MATSTAT over budget: {', '.join(over) if over else 'none'}")
