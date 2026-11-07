"""Check M2 assets after a fresh load. Logs OCVERIFY lines; failures are errors.
Run headless:  scripts/run_python.sh Content/Python/oc_m2_verify.py
"""
import unreal

fails = []


def check(label, ok, detail=""):
    (unreal.log if ok else unreal.log_error)(f"OCVERIFY {'OK  ' if ok else 'FAIL'} {label} {detail}")
    if not ok:
        fails.append(label)


for name in ("Coupe", "Offroad", "Bike"):
    bp = unreal.load_asset(f"/Game/Vehicles/Blueprints/BP_OCVehicle_{name}")
    cdo = unreal.get_default_object(bp.generated_class())
    data = cdo.get_editor_property("vehicle_data")
    check(f"BP_OCVehicle_{name} vehicle_data", data is not None, data.get_name() if data else "None")
    check(f"BP_OCVehicle_{name} input_config", cdo.get_editor_property("input_config") is not None)
    if data:
        check(f"DA_Vehicle_{name} mesh", data.get_editor_property("mesh") is not None)
        check(f"DA_Vehicle_{name} 4 wheels", len(data.get_editor_property("wheel_setups")) == 4)
        check(f"DA_Vehicle_{name} torque curve", len(data.get_editor_property("torque_curve_points")) > 0)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level("/Game/Maps/Dev/L_DevDriving")
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(a, unreal.OCVehicle):
        d = a.get_editor_property("vehicle_data")
        check(f"placed {a.get_actor_label()} vehicle_data", d is not None, d.get_name() if d else "None")

unreal.log(f"OCVERIFY DONE, {len(fails)} failure(s)")
