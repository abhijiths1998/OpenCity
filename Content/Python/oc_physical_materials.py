"""Physical materials per surface (INFRA_DESIGN.md Phase 4) and their assignment to the paving instances.

  scripts/run_python.sh Content/Python/oc_physical_materials.py

/Game/Core/Physics/PM_<Surface>: friction and restitution per surface. Chaos vehicle tyres scale
their grip by the surface's friction; footstep and tyre audio (M10) and wet grip (wetness from
MPC_World) build on the same assets. Each MI_Paving_* (oc_art_materials.PAVING) gets its surface's
physical material, so road chunks carry it through complex collision.
Values are game tuning (dry): asphalt is the reference grip.
"""

import unreal

import oc_lib as L

TAG = "oc_physmat"
FOLDER = "/Game/Core/Physics"
SURFACES = {  # name: (friction, restitution)
    "Asphalt": (1.0, 0.1),
    "Cobbles": (0.85, 0.1),      # sampietrini, porfido: rougher ride, a little less grip
    "StoneSlab": (0.78, 0.1),    # lastricato: polished by centuries of feet
    "Gravel": (0.62, 0.05),      # strade bianche
    "Concrete": (0.95, 0.1),     # quays
    "Dirt": (0.6, 0.05),
    "Sand": (0.45, 0.02),
    "MetalGrate": (0.7, 0.2),
    "TramRail": (0.55, 0.2),     # slippery for bikes
}
PAVING_SURFACE = {
    "asphalt": "Asphalt", "sampietrini": "Cobbles", "porfido": "Cobbles", "lastricato": "StoneSlab",
    "stone_slab": "StoneSlab", "gravel_white": "Gravel", "concrete": "Concrete",
}


def main():
    made = {}
    for name, (friction, restitution) in SURFACES.items():
        pm = L.get_or_create(f"PM_{name}", FOLDER, unreal.PhysicalMaterial, unreal.PhysicalMaterialFactoryNew(), TAG)
        L.set_props(pm, friction=friction, static_friction=friction, restitution=restitution)
        L.save(pm)
        made[name] = pm
    assigned = []
    for paving, surface in PAVING_SURFACE.items():
        mi = unreal.load_asset(f"/Game/Environment/Materials/Paving/MI_Paving_{paving}")
        if mi is None:
            continue
        mi.set_editor_property("phys_material", made[surface])
        L.save(mi)
        assigned.append(f"{paving}->{surface}")
    L.log(TAG, f"physical materials: {len(made)}; paving: {', '.join(assigned)}")


main()
