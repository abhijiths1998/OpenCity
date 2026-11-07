"""M2 asset setup: vehicle input, radio stations, wheels, vehicle data, Blueprints, test map.

Safe to re-run: assets are updated in place; the driving map is rebuilt.

  /Game/Core/Input/IA_Throttle ... IMC_Vehicle, DA_OCVehicleInputConfig
  /Game/Vehicles/Radio/DA_Radio_*               three original stations (stub, no audio)
  /Game/Vehicles/Bike/SKM_BikeChassis           imported from SourceArt (Tools/make_bike_chassis.py)
  /Game/Vehicles/Wheels/BP_Wheel_*              Chaos wheel classes per axle
  /Game/Vehicles/Data/DA_Vehicle_*              UOCVehicleData: Coupe, Offroad, Bike
  /Game/Vehicles/Blueprints/BP_OCVehicle_*      thin Blueprints that point at the data
  /Game/Maps/Dev/L_DevDriving                   400 m test pad with ramps, walls and the vehicles

Run headless:  scripts/run_python.sh Content/Python/oc_m2_setup.py
"""

import os

import unreal

import oc_lib as L

TAG = "oc_m2"
INPUT_DIR = "/Game/Core/Input"
VEH_DIR = "/Game/Vehicles"
MAP_PATH = "/Game/Maps/Dev/L_DevDriving"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
BIKE_GLTF = os.path.join(PROJECT_DIR, "SourceArt/Vehicles/Bike/SKM_BikeChassis.gltf")


def log(msg):
    L.log(TAG, msg)


# --- Input ------------------------------------------------------------------

def make_vehicle_input():
    A1, A2, B = (unreal.InputActionValueType.AXIS1D, unreal.InputActionValueType.AXIS2D,
                 unreal.InputActionValueType.BOOLEAN)
    a = {n: L.input_action(n, INPUT_DIR, t, TAG) for n, t in {
        "IA_Throttle": A1, "IA_Brake": A1, "IA_Steer": A1, "IA_Handbrake": B, "IA_VehicleLook": A2,
        "IA_ExitVehicle": B, "IA_RadioNext": B, "IA_RadioPrevious": B, "IA_ResetVehicle": B}.items()}

    def rows(imc):
        return [
            (a["IA_Throttle"], "W", []),
            (a["IA_Throttle"], "Gamepad_RightTriggerAxis", []),
            (a["IA_Brake"], "S", []),
            (a["IA_Brake"], "Gamepad_LeftTriggerAxis", []),
            (a["IA_Steer"], "A", [L.negate(imc)]),
            (a["IA_Steer"], "D", []),
            (a["IA_Steer"], "Gamepad_LeftX", [L.dead_zone(imc, 0.15, radial=False)]),
            (a["IA_Handbrake"], "SpaceBar", []),
            (a["IA_Handbrake"], "Gamepad_RightShoulder", []),
            # Free look adds degrees to the camera directly (no controller scales here):
            # mouse delta x 0.25 deg; stick 150 deg/s yaw, 90 deg/s pitch.
            (a["IA_VehicleLook"], "Mouse2D", [L.scalar(imc, 0.25, 0.25)]),
            (a["IA_VehicleLook"], "Gamepad_Right2D", [L.dead_zone(imc), L.scalar(imc, 150.0, 90.0), L.scale_by_dt(imc)]),
            (a["IA_ExitVehicle"], "F", []),
            (a["IA_ExitVehicle"], "Gamepad_FaceButton_Top", []),
            (a["IA_RadioNext"], "E", []),
            (a["IA_RadioNext"], "Gamepad_DPad_Right", []),
            (a["IA_RadioPrevious"], "Q", []),
            (a["IA_RadioPrevious"], "Gamepad_DPad_Left", []),
            (a["IA_ResetVehicle"], "R", []),
            (a["IA_ResetVehicle"], "Gamepad_DPad_Down", []),
        ]
    imc = L.mapping_context("IMC_Vehicle", INPUT_DIR, rows, TAG)

    cfg = L.data_asset("DA_OCVehicleInputConfig", INPUT_DIR, unreal.OCVehicleInputConfig, TAG)
    L.set_props(cfg, vehicle_context=imc, throttle=a["IA_Throttle"], brake=a["IA_Brake"], steer=a["IA_Steer"],
                handbrake=a["IA_Handbrake"], look=a["IA_VehicleLook"], exit=a["IA_ExitVehicle"],
                radio_next=a["IA_RadioNext"], radio_previous=a["IA_RadioPrevious"], reset_vehicle=a["IA_ResetVehicle"])
    L.save(cfg)
    return cfg


# --- Radio --------------------------------------------------------------------

STATIONS = [  # Original stations for Marevia (no real brands or music).
    ("DA_Radio_HarborBreeze", "Harbor Breeze FM", "Coastal pop", 1500.0),
    ("DA_Radio_Underground", "Marevia Underground", "Electronic", 1800.0),
    ("DA_Radio_OldTown", "Old Town Classics", "Classics", 2100.0),
]


def make_radio():
    out = []
    for asset_name, display, genre, length in STATIONS:
        st = L.data_asset(asset_name, f"{VEH_DIR}/Radio", unreal.OCRadioStationData, TAG)
        L.set_props(st, display_name=unreal.Text(display), genre=unreal.Text(genre), length_seconds=length)
        L.save(st)
        out.append(st)
    return out


# --- Bike chassis import ----------------------------------------------------------

def import_bike_chassis():
    folder = f"{VEH_DIR}/Bike"
    if not os.path.exists(BIKE_GLTF):
        raise RuntimeError(f"missing {BIKE_GLTF}: run python3 -I Tools/make_bike_chassis.py")
    task = unreal.AssetImportTask()
    L.set_props(task, filename=BIKE_GLTF, destination_path=folder, automated=True, replace_existing=True, save=True)
    L.tools.import_asset_tasks([task])
    imported = list(task.get_editor_property("imported_object_paths"))
    log(f"bike chassis import: {imported}")
    mesh = next((unreal.load_asset(p) for p in imported if isinstance(unreal.load_asset(p), unreal.SkeletalMesh)), None)
    if mesh is None:
        raise RuntimeError(f"bike chassis import produced no skeletal mesh: {imported}")
    pa = mesh.get_editor_property("physics_asset")
    log(f"bike chassis mesh {mesh.get_path_name()}, physics asset {pa.get_path_name() if pa else 'NONE'}")
    if pa is None:
        raise RuntimeError("bike chassis has no physics asset; Chaos needs one")
    return mesh


# --- Wheels -------------------------------------------------------------------

def wheel_size(static_mesh_path):
    """(radius, width) in cm from a wheel mesh's bounds: largest extent is the diameter."""
    ext = unreal.load_asset(static_mesh_path).get_bounds().box_extent
    dims = sorted([ext.x * 2, ext.y * 2, ext.z * 2])
    return dims[2] / 2.0, dims[0]


def make_wheel(name, axle, radius, width, steer_deg, engine, handbrake, friction=3.0, spring_rate=250.0,
               raise_cm=10.0, drop_cm=10.0, brake_torque=1500.0):
    bp, w = L.blueprint(name, f"{VEH_DIR}/Wheels", unreal.ChaosVehicleWheel.static_class(), TAG)
    L.set_props(w, axle_type=axle, wheel_radius=radius, wheel_width=width, max_steer_angle=steer_deg,
                affected_by_steering=steer_deg > 0.0, affected_by_engine=engine, affected_by_handbrake=handbrake,
                affected_by_brake=True, friction_force_multiplier=friction, spring_rate=spring_rate,
                suspension_max_raise=raise_cm, suspension_max_drop=drop_cm, max_brake_torque=brake_torque,
                max_hand_brake_torque=brake_torque * 2.0 if handbrake else 0.0)
    L.save(bp)
    return bp.generated_class()


# --- Vehicle data ------------------------------------------------------------------

def wheel_setups(classes_and_bones):
    out = []
    for wheel_class, bone in classes_and_bones:
        s = unreal.ChaosWheelSetup()
        L.set_props(s, wheel_class=wheel_class, bone_name=bone)
        out.append(s)
    return out


def engine(max_torque, max_rpm, idle_rpm):
    e = unreal.VehicleEngineConfig()
    L.set_props(e, max_torque=max_torque, max_rpm=max_rpm, engine_idle_rpm=idle_rpm)
    return e


def transmission(forward, final_ratio, change_up, change_down):
    t = unreal.VehicleTransmissionConfig()
    L.set_props(t, use_automatic_gears=True, use_auto_reverse=True, forward_gear_ratios=forward,
                reverse_gear_ratios=[3.0], final_ratio=final_ratio, change_up_rpm=change_up,
                change_down_rpm=change_down, gear_change_time=0.3)
    return t


def differential(kind, split=0.5):
    d = unreal.VehicleDifferentialConfig()
    L.set_props(d, differential_type=kind, front_rear_split=split)
    return d


def camera(arm, height, fov_rest=85.0, fov_speed=100.0, full_kmh=180.0):
    c = unreal.OCVehicleCameraSettings()
    L.set_props(c, arm_length=arm, socket_offset=unreal.Vector(0, 0, height), pitch=-10.0,
                fov_at_rest=fov_rest, fov_at_speed=fov_speed, fov_full_speed_kmh=full_kmh)
    return c


def vec2s(points):
    return [unreal.Vector2D(x, y) for x, y in points]


def make_vehicle_data(name, display, **props):
    d = L.data_asset(name, f"{VEH_DIR}/Data", unreal.OCVehicleData, TAG)
    L.set_props(d, display_name=unreal.Text(display), **props)
    L.save(d)
    return d


def main():
    vin = make_vehicle_input()
    radio = make_radio()
    bike_mesh = import_bike_chassis()

    FRONT, REAR = unreal.AxleType.FRONT, unreal.AxleType.REAR
    RWD, AWD = unreal.VehicleDifferential.REAR_WHEEL_DRIVE, unreal.VehicleDifferential.ALL_WHEEL_DRIVE

    r, w = wheel_size(f"{VEH_DIR}/SportsCar/SM_SportsCar_Wheel")
    log(f"coupe wheel radius {r:.1f} cm, width {w:.1f} cm")
    coupe_f = make_wheel("BP_Wheel_Coupe_Front", FRONT, r, w, 40.0, False, False)
    coupe_r = make_wheel("BP_Wheel_Coupe_Rear", REAR, r, w, 0.0, True, True)

    r, w = wheel_size(f"{VEH_DIR}/OffroadCar/SM_Offroad_Tire")
    log(f"offroad wheel radius {r:.1f} cm, width {w:.1f} cm")
    off_f = make_wheel("BP_Wheel_Offroad_Front", FRONT, r, w, 35.0, True, False, friction=3.5,
                       spring_rate=200.0, raise_cm=20.0, drop_cm=20.0)
    off_r = make_wheel("BP_Wheel_Offroad_Rear", REAR, r, w, 0.0, True, True, friction=3.5,
                       spring_rate=200.0, raise_cm=20.0, drop_cm=20.0)

    bike_f = make_wheel("BP_Wheel_Bike_Front", FRONT, 32.0, 12.0, 30.0, False, False, friction=3.0,
                        spring_rate=120.0, raise_cm=8.0, drop_cm=8.0, brake_torque=600.0)
    bike_r = make_wheel("BP_Wheel_Bike_Rear", REAR, 32.0, 12.0, 0.0, True, True, friction=3.0,
                        spring_rate=120.0, raise_cm=8.0, drop_cm=8.0, brake_torque=600.0)

    coupe = make_vehicle_data(
        "DA_Vehicle_Coupe", "Strada Coupe",
        mesh=unreal.load_asset(f"{VEH_DIR}/SportsCar/SKM_SportsCar"),
        anim_class=unreal.load_asset(f"{VEH_DIR}/SportsCar/ABP_SportsCar").generated_class(),
        mass=1400.0, drag_coefficient=0.3, chassis_width=180.0, chassis_height=120.0,
        wheel_setups=wheel_setups([(coupe_f, "Phys_Wheel_FL"), (coupe_f, "Phys_Wheel_FR"),
                                   (coupe_r, "Phys_Wheel_BL"), (coupe_r, "Phys_Wheel_BR")]),
        torque_curve_points=vec2s([(0, 0.5), (2500, 0.8), (5000, 1.0), (7000, 0.85)]),
        engine_setup=engine(650.0, 7000.0, 900.0),
        transmission_setup=transmission([4.0, 2.6, 1.8, 1.4, 1.1, 0.9], 3.4, 6200.0, 3000.0),
        differential_setup=differential(RWD),
        steering_curve_points=vec2s([(0, 1.0), (60, 0.7), (120, 0.45), (200, 0.3)]),
        camera=camera(650.0, 150.0), radio_stations=radio)

    offroad = make_vehicle_data(
        "DA_Vehicle_Offroad", "Terra 4x4",
        mesh=unreal.load_asset(f"{VEH_DIR}/OffroadCar/SKM_Offroad"),
        anim_class=unreal.load_asset(f"{VEH_DIR}/OffroadCar/Offroad_AnimBP").generated_class(),
        mass=2000.0, drag_coefficient=0.45, chassis_width=200.0, chassis_height=170.0,
        wheel_setups=wheel_setups([(off_f, "PhysWheel_FL"), (off_f, "PhysWheel_FR"),
                                   (off_r, "PhysWheel_BL"), (off_r, "PhysWheel_BR")]),
        torque_curve_points=vec2s([(0, 0.7), (1500, 1.0), (3500, 0.95), (5500, 0.7)]),
        engine_setup=engine(800.0, 5500.0, 800.0),
        transmission_setup=transmission([4.5, 2.9, 1.9, 1.3, 1.0], 3.9, 4800.0, 2200.0),
        differential_setup=differential(AWD, 0.5),
        steering_curve_points=vec2s([(0, 1.0), (50, 0.7), (100, 0.45), (160, 0.3)]),
        camera=camera(750.0, 200.0, 85.0, 96.0, 140.0), radio_stations=radio)

    bike = make_vehicle_data(
        "DA_Vehicle_Bike", "Marevia Scrambler",
        mesh=bike_mesh, mass=230.0, drag_coefficient=0.6, chassis_width=40.0, chassis_height=110.0,
        override_center_of_mass=True, center_of_mass_offset=unreal.Vector(0.0, 0.0, -25.0),
        wheel_setups=wheel_setups([(bike_f, "PhysWheel_FL"), (bike_f, "PhysWheel_FR"),
                                   (bike_r, "PhysWheel_RL"), (bike_r, "PhysWheel_RR")]),
        torque_curve_points=vec2s([(0, 0.6), (4000, 0.9), (8000, 1.0), (10000, 0.8)]),
        engine_setup=engine(140.0, 10000.0, 1400.0),
        transmission_setup=transmission([3.2, 2.2, 1.7, 1.35, 1.1], 3.0, 9000.0, 5000.0),
        differential_setup=differential(RWD),
        steering_curve_points=vec2s([(0, 1.0), (20, 0.6), (40, 0.35), (80, 0.2), (140, 0.12)]),
        camera=camera(450.0, 110.0, 85.0, 104.0, 150.0),
        driver_offset=unreal.Vector(0.0, 0.0, 100.0),
        exit_offsets=[unreal.Vector(0, -110, 60), unreal.Vector(0, 110, 60), unreal.Vector(-200, 0, 60)],
        max_exit_speed_kmh=20.0, radio_stations=[])

    # --- Blueprints ---------------------------------------------------------------
    datas = {}
    for name, parent, data in (("BP_OCVehicle_Coupe", unreal.OCVehicle, coupe),
                               ("BP_OCVehicle_Offroad", unreal.OCVehicle, offroad),
                               ("BP_OCVehicle_Bike", unreal.OCMotorbike, bike)):
        bp, cdo = L.blueprint(name, f"{VEH_DIR}/Blueprints", parent.static_class(), TAG)
        L.set_props(cdo, vehicle_data=data, input_config=vin)
        if parent is unreal.OCMotorbike:
            L.set_props(cdo, body_mesh=unreal.load_asset("/Engine/BasicShapes/Cube"),
                        wheel_mesh=unreal.load_asset("/Engine/BasicShapes/Cylinder"))
        L.save(bp)
        datas[name] = data

    build_driving_map(datas)
    log("done")


def build_driving_map(datas):
    L.new_level(MAP_PATH)
    L.daylight()
    L.block("Floor", (0, 0, -50), (40000, 40000, 100), folder="Ground")
    L.spawn(unreal.PlayerStart, (0, 0, 100), (0, 0, 0), "PlayerStart")

    # Vehicles parked side by side in front of the player, facing +X. The data asset is set on
    # each instance explicitly: right after a Blueprint save in the same session, the class can
    # still carry stale (empty) defaults, and placed actors would be saved without data.
    for i, (name, data) in enumerate(datas.items()):
        cls = unreal.EditorAssetLibrary.load_blueprint_class(f"{VEH_DIR}/Blueprints/{name}")
        actor = L.spawn(cls, (600, -500 + i * 500, 60), (0, 0, 0), name.replace("BP_OCVehicle_", "Vehicle_"), "Vehicles")
        actor.set_editor_property("vehicle_data", data)
        if actor.get_editor_property("vehicle_data") is None:
            raise RuntimeError(f"{name}: vehicle_data did not stick on the placed actor")

    # Layout: a clear runway along +X (y -1500..1500) from the start to a crash wall at 60 m.
    # Everything else sits beside it, so scripted runs don't clip props by accident.
    L.block("CrashWall", (6000, 0, 150), (100, 3000, 300))
    # Crash blocks in their own lane at y = -7000.
    for i in range(6):
        L.block(f"CrashBlock_{i}", (2000 + i * 800, -7000, 75), (150, 150, 150))
    # Jump ramps at 10 and 18 degrees.
    L.block("Ramp_10deg", (2000, 3000, 60), (1400, 600, 20), (0, -10, 0))
    L.block("Ramp_18deg", (2000, 4500, 110), (1400, 600, 20), (0, -18, 0))
    # Slalom posts every 15 m in the lane at y = -4000.
    for i in range(8):
        L.block(f"Slalom_{i}", (1500 + i * 1500, -4000 + (i % 2) * 400, 100), (40, 40, 200))
    # A narrow alley (3 m) behind the start for exit-spot testing.
    L.block("Alley_Left", (-3000, -150 - 25, 150), (2000, 50, 300))
    L.block("Alley_Right", (-3000, 150 + 25, 150), (2000, 50, 300))
    L.save_level(MAP_PATH, TAG)


main()
