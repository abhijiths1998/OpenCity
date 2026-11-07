"""M1 asset setup: input, player Blueprints, game mode and the dev playground map.

Safe to re-run: assets are updated in place, the playground map is rebuilt.

  /Game/Core/Input/IA_*                Input actions
  /Game/Core/Input/IMC_OnFoot          Keyboard/mouse + gamepad, always on while on foot
  /Game/Core/Input/IMC_Aiming          Extra mappings only while aiming (shoulder swap)
  /Game/Core/Input/DA_OCInputConfig    UOCInputConfig pointing at all of the above
  /Game/Core/Player/BP_OCPlayerCharacter  AOCCharacter + Quinn mesh + ABP_Unarmed
  /Game/Core/BP_OCGameMode             AOCGameMode with the player character as pawn
  /Game/Maps/Dev/L_DevPlayground       Movement/camera test course

Run headless:  scripts/run_python.sh Content/Python/oc_m1_setup.py
"""

import unreal

INPUT_DIR = "/Game/Core/Input"
PLAYER_DIR = "/Game/Core/Player"
CORE_DIR = "/Game/Core"
MAP_PATH = "/Game/Maps/Dev/L_DevPlayground"

BODY_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"
ANIM_BP = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"
CUBE = "/Engine/BasicShapes/Cube"

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(msg):
    unreal.log(f"[oc_m1] {msg}")


def get_or_create(name, folder, asset_class, factory):
    path = f"{folder}/{name}"
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    asset = tools.create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise RuntimeError(f"could not create {path}")
    log(f"created {path}")
    return asset


def save(asset):
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"could not save {asset.get_path_name()}")


# --- Input ------------------------------------------------------------------

AXIS2D = unreal.InputActionValueType.AXIS2D
BOOL = unreal.InputActionValueType.BOOLEAN

ACTIONS = {
    "IA_Move": AXIS2D,
    "IA_Look": AXIS2D,
    "IA_Jump": BOOL,
    "IA_Sprint": BOOL,
    "IA_SprintToggle": BOOL,
    "IA_Crouch": BOOL,
    "IA_Aim": BOOL,
    "IA_SwapShoulder": BOOL,
}


def make_actions():
    out = {}
    for name, value_type in ACTIONS.items():
        action = get_or_create(name, INPUT_DIR, unreal.InputAction, unreal.InputAction_Factory())
        action.set_editor_property("value_type", value_type)
        save(action)
        out[name] = action
    return out


def key(name):
    k = unreal.Key()
    k.set_editor_property("key_name", name)
    return k


def negate(imc, x=True, y=True, z=True):
    m = unreal.new_object(unreal.InputModifierNegate, outer=imc)
    m.set_editor_property("x", x)
    m.set_editor_property("y", y)
    m.set_editor_property("z", z)
    return m


def swizzle(imc):
    m = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=imc)
    m.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
    return m


def dead_zone(imc, lower=0.2):
    m = unreal.new_object(unreal.InputModifierDeadZone, outer=imc)
    m.set_editor_property("type", unreal.DeadZoneType.RADIAL)
    m.set_editor_property("lower_threshold", lower)
    return m


def scalar(imc, x, y):
    m = unreal.new_object(unreal.InputModifierScalar, outer=imc)
    m.set_editor_property("scalar", unreal.Vector(x, y, 1.0))
    return m


def scale_by_dt(imc):
    return unreal.new_object(unreal.InputModifierScaleByDeltaTime, outer=imc)


def build_context(imc, rows):
    """rows: list of (action, key_name, [modifiers])."""
    mappings = []
    for action, key_name, modifiers in rows:
        m = unreal.EnhancedActionKeyMapping()
        m.set_editor_property("action", action)
        m.set_editor_property("key", key(key_name))
        m.set_editor_property("modifiers", modifiers)
        mappings.append(m)
    data = imc.get_editor_property("default_key_mappings")
    data.set_editor_property("mappings", mappings)
    imc.set_editor_property("default_key_mappings", data)
    save(imc)
    log(f"{imc.get_name()}: {len(mappings)} mappings")


def make_contexts(a):
    on_foot = get_or_create("IMC_OnFoot", INPUT_DIR, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    imc = on_foot
    build_context(imc, [
        # Move: X = right, Y = forward
        (a["IA_Move"], "W", [swizzle(imc)]),
        (a["IA_Move"], "S", [swizzle(imc), negate(imc)]),
        (a["IA_Move"], "A", [negate(imc)]),
        (a["IA_Move"], "D", []),
        (a["IA_Move"], "Gamepad_Left2D", [dead_zone(imc)]),
        # Look: mouse is per-frame delta; stick is a rate, so scale by delta time
        (a["IA_Look"], "Mouse2D", [negate(imc, x=False, y=True, z=False)]),
        (a["IA_Look"], "Gamepad_Right2D", [dead_zone(imc), negate(imc, x=False, y=True, z=False),
                                            scalar(imc, 60.0, 40.0), scale_by_dt(imc)]),
        (a["IA_Jump"], "SpaceBar", []),
        (a["IA_Jump"], "Gamepad_FaceButton_Bottom", []),
        (a["IA_Sprint"], "LeftShift", []),
        (a["IA_SprintToggle"], "Gamepad_LeftThumbstick", []),
        (a["IA_Crouch"], "C", []),
        (a["IA_Crouch"], "Gamepad_FaceButton_Right", []),
        (a["IA_Aim"], "RightMouseButton", []),
        (a["IA_Aim"], "Gamepad_LeftTrigger", []),
    ])

    aiming = get_or_create("IMC_Aiming", INPUT_DIR, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    build_context(aiming, [
        (a["IA_SwapShoulder"], "Q", []),
        (a["IA_SwapShoulder"], "Gamepad_RightThumbstick", []),
    ])
    return on_foot, aiming


def make_input_config(a, on_foot, aiming):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.OCInputConfig.static_class())
    cfg = get_or_create("DA_OCInputConfig", INPUT_DIR, unreal.OCInputConfig, factory)
    cfg.set_editor_property("on_foot_context", on_foot)
    cfg.set_editor_property("aiming_context", aiming)
    for prop, name in (("move", "IA_Move"), ("look", "IA_Look"), ("jump", "IA_Jump"),
                       ("sprint", "IA_Sprint"), ("sprint_toggle", "IA_SprintToggle"),
                       ("crouch", "IA_Crouch"), ("aim", "IA_Aim"), ("swap_shoulder", "IA_SwapShoulder")):
        cfg.set_editor_property(prop, a[name])
    save(cfg)
    return cfg


# --- Blueprints ---------------------------------------------------------------

def make_blueprint(name, folder, parent_class):
    path = f"{folder}/{name}"
    if assets.does_asset_exist(path):
        bp = unreal.load_asset(path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)
        bp = tools.create_asset(name, folder, unreal.Blueprint, factory)
        if bp is None:
            raise RuntimeError(f"could not create {path}")
        log(f"created {path}")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    return bp


def reparent_anim_bp():
    """Give ABP_Unarmed our C++ base so its graph can read crouch/aim/sprint state."""
    abp = unreal.load_asset(ANIM_BP)
    target = unreal.OCCharacterAnimInstance.static_class()
    current = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(abp)
    if current != target:
        unreal.BlueprintEditorLibrary.reparent_blueprint(abp, target)
        log(f"reparented ABP_Unarmed: {current.get_name()} -> {target.get_name()}")
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    save(abp)
    return abp


def make_player(cfg, abp):
    bp = make_blueprint("BP_OCPlayerCharacter", PLAYER_DIR, unreal.OCCharacter.static_class())
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("input_config", cfg)
    cdo.set_editor_property("body_mesh", unreal.load_asset(BODY_MESH))
    cdo.set_editor_property("body_anim_class", abp.generated_class())
    save(bp)
    return bp


def make_game_mode(player_bp):
    bp = make_blueprint("BP_OCGameMode", CORE_DIR, unreal.OCGameMode.static_class())
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("default_pawn_class", player_bp.generated_class())
    save(bp)
    return bp


# --- Playground map -------------------------------------------------------------

def spawn(cls, location=(0, 0, 0), rotation=(0, 0, 0), label=None, folder=None):
    actor = actor_subsystem.spawn_actor_from_class(cls, unreal.Vector(*location), unreal.Rotator(*rotation))
    if actor is None:
        raise RuntimeError(f"failed to spawn {cls}")
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


def block(label, center, size, rotation=(0, 0, 0), folder="Course"):
    """Axis-aligned box from a center and a size in cm (cube mesh is 100 cm)."""
    actor = spawn(unreal.StaticMeshActor, center, rotation, label, folder)
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    comp.set_static_mesh(unreal.load_asset(CUBE))
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    return actor


def build_playground():
    if assets.does_asset_exist(MAP_PATH):
        assets.delete_asset(MAP_PATH)
    if not level_subsystem.new_level(MAP_PATH):
        raise RuntimeError(f"new_level failed for {MAP_PATH}")

    sun = spawn(unreal.DirectionalLight, (0, 0, 1000), (-40, 35, 0), "Sun", "Lighting")
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sc.set_editor_property("atmosphere_sun_light", True)
    sc.set_editor_property("intensity", 10.0)
    spawn(unreal.SkyAtmosphere, label="SkyAtmosphere", folder="Lighting")
    spawn(unreal.ExponentialHeightFog, label="HeightFog", folder="Lighting")
    sky = spawn(unreal.SkyLight, (0, 0, 500), label="SkyLight", folder="Lighting")
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    skc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    skc.set_editor_property("real_time_capture", True)

    block("Floor", (0, 0, -50), (10000, 10000, 100), folder="Ground")
    spawn(unreal.PlayerStart, (0, 0, 100), (0, 0, 0), "PlayerStart")

    # Stairs: 10 steps of 20 cm rise, 35 cm run (character step height is 45 cm).
    # Each step is a box from its front edge to the top of the flight, so there are no gaps.
    for i in range(10):
        length, height = 35 * (10 - i), 20 * (i + 1)
        block(f"Step_{i:02d}", (800 + i * 35 + length / 2, -600, height / 2), (length, 300, height))
    # Ramps at 20 and 40 degrees (walkable limit is about 44.8).
    block("Ramp_20deg", (800, 0, 120), (800, 300, 20), (0, -20, 0))
    block("Ramp_40deg", (800, 600, 220), (800, 300, 20), (0, -40, 0))
    # Crouch tunnel: 120 cm clearance; standing capsule is 192 cm, crouched 120 cm.
    block("Tunnel_Left", (-800, -175, 100), (800, 50, 200))
    block("Tunnel_Right", (-800, 175, 100), (800, 50, 200))
    block("Tunnel_Roof", (-800, 0, 175), (800, 400, 30))
    # Jump test: boxes 50 / 100 / 150 cm tall, and a 300 cm gap between platforms.
    for i, h in enumerate((50, 100, 150)):
        block(f"JumpBox_{h}cm", (0, 800 + i * 300, h / 2), (200, 200, h))
    block("Gap_PlatformA", (-800, 800, 100), (400, 400, 200))
    block("Gap_PlatformB", (-800, 1500, 100), (400, 400, 200))
    # Camera collision test: a corridor 200 cm wide (walls at y -1200 and -1450, 50 cm thick).
    block("Corridor_Left", (0, -1200, 200), (1500, 50, 400))
    block("Corridor_Right", (0, -1450, 200), (1500, 50, 400))

    if not level_subsystem.save_current_level():
        raise RuntimeError(f"save failed for {MAP_PATH}")
    log(f"saved {MAP_PATH}")


def main():
    actions = make_actions()
    on_foot, aiming = make_contexts(actions)
    cfg = make_input_config(actions, on_foot, aiming)
    abp = reparent_anim_bp()
    player = make_player(cfg, abp)
    make_game_mode(player)
    build_playground()
    log("done")


main()
