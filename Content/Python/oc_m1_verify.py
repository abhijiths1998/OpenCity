"""Check the M1 assets after a fresh load: Blueprint defaults, input mappings, anim BP parent.
Logs OCVERIFY OK/FAIL lines; any FAIL is logged as an error so run_python.sh fails.
Run headless:  scripts/run_python.sh Content/Python/oc_m1_verify.py
"""
import unreal

fails = []


def check(label, ok, detail=""):
    if ok:
        unreal.log(f"OCVERIFY OK   {label} {detail}")
    else:
        fails.append(label)
        unreal.log_error(f"OCVERIFY FAIL {label} {detail}")


player = unreal.load_asset("/Game/Core/Player/BP_OCPlayerCharacter")
cdo = unreal.get_default_object(player.generated_class())
check("player input_config", cdo.get_editor_property("input_config") is not None)
mesh = cdo.get_editor_property("body_mesh")
check("player body_mesh", mesh is not None, mesh.get_name() if mesh else "")
anim = cdo.get_editor_property("body_anim_class")
check("player body_anim_class", anim is not None, anim.get_name() if anim else "")

gm = unreal.load_asset("/Game/Core/BP_OCGameMode")
pawn = unreal.get_default_object(gm.generated_class()).get_editor_property("default_pawn_class")
check("game mode pawn", pawn == player.generated_class(), pawn.get_name() if pawn else "None")

abp = unreal.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed")
parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(abp)
check("ABP_Unarmed parent", parent == unreal.OCCharacterAnimInstance.static_class(), parent.get_name())

for name, expected in (("IMC_OnFoot", 17), ("IMC_Aiming", 2)):
    imc = unreal.load_asset(f"/Game/Core/Input/{name}")
    maps = imc.get_editor_property("default_key_mappings").get_editor_property("mappings")
    check(f"{name} mapping count", len(maps) == expected, f"{len(maps)}")
    for m in maps:
        action = m.get_editor_property("action")
        k = m.get_editor_property("key").get_editor_property("key_name")
        mods = [type(x).__name__ for x in m.get_editor_property("modifiers")]
        trigs = [f"{type(x).__name__}({x.get_editor_property('actuation_threshold'):.2f})" for x in m.get_editor_property("triggers")]
        unreal.log(f"OCVERIFY      {name}: {action.get_name() if action else None} <- {k} {mods} {trigs}")
        check(f"{name} {k} has action", action is not None)

legacy = unreal.get_default_object(unreal.InputSettings).get_editor_property("enable_legacy_input_scales")
check("legacy input scales on (look mappings depend on it)", legacy is True, str(legacy))

cfg = unreal.load_asset("/Game/Core/Input/DA_OCInputConfig")
for prop in ("on_foot_context", "aiming_context", "move", "look", "jump", "sprint",
             "sprint_toggle", "crouch", "aim", "swap_shoulder", "interact"):
    check(f"input config {prop}", cfg.get_editor_property(prop) is not None)

unreal.log(f"OCVERIFY DONE, {len(fails)} failure(s)")
