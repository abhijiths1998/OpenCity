"""Instruction counts for the generated masters against the ART_PIPELINE.md budgets.

  OC_PY_RENDER=1 scripts/run_python.sh Content/Python/oc_material_stats.py

Logs one MATSTAT line per master (pixel and vertex shader instructions, samplers, budget) from
MaterialEditingLibrary.get_statistics on the SM6 shader platform. Masters without a budget in
ART_PIPELINE.md are listed with '-'.
"""

import unreal

import oc_lib as L

TAG = "oc_matstats"
# Base-pass pixel shader targets (ART_PIPELINE.md, "Master materials"). The kit and props share
# M_Master_Surface, which covers the Opaque and Building roles.
BUDGETS = {
    "M_Master_Surface": 220,
    "M_Master_SurfaceTwoSided": 220,
    "M_Master_Road": 200,
    "M_Master_Landscape": 200,
    "M_Master_Water": 250,
    "M_Master_KitGlass": 150,
    "M_Master_CarGlass": 150,
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
L.log(TAG, f"MATSTAT over budget: {', '.join(over) if over else 'none'}")
