"""Shared helpers for OpenCity editor scripts (Content/Python is on Unreal's sys.path)."""

import unreal

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(tag, msg):
    unreal.log(f"[{tag}] {msg}")


def get_or_create(name, folder, asset_class, factory, tag="oc"):
    path = f"{folder}/{name}"
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    asset = tools.create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise RuntimeError(f"could not create {path}")
    log(tag, f"created {path}")
    return asset


def save(asset):
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"could not save {asset.get_path_name()}")


def data_asset(name, folder, cls, tag="oc"):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls.static_class())
    return get_or_create(name, folder, cls, factory, tag)


def blueprint(name, folder, parent_class, tag="oc"):
    """Get or create a Blueprint subclass, compiled. Returns (blueprint, class default object)."""
    path = f"{folder}/{name}"
    if assets.does_asset_exist(path):
        bp = unreal.load_asset(path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)
        bp = tools.create_asset(name, folder, unreal.Blueprint, factory)
        if bp is None:
            raise RuntimeError(f"could not create {path}")
        log(tag, f"created {path}")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    return bp, unreal.get_default_object(bp.generated_class())


def set_props(obj, **props):
    for k, v in props.items():
        obj.set_editor_property(k, v)
    return obj


# --- Enhanced Input -------------------------------------------------------------

def input_action(name, folder, value_type, tag="oc"):
    action = get_or_create(name, folder, unreal.InputAction, unreal.InputAction_Factory(), tag)
    action.set_editor_property("value_type", value_type)
    save(action)
    return action


def key(name):
    k = unreal.Key()
    k.set_editor_property("key_name", name)
    return k


def negate(outer, x=True, y=True, z=True):
    return set_props(unreal.new_object(unreal.InputModifierNegate, outer=outer), x=x, y=y, z=z)


def swizzle(outer):
    return set_props(unreal.new_object(unreal.InputModifierSwizzleAxis, outer=outer), order=unreal.InputAxisSwizzle.YXZ)


def dead_zone(outer, lower=0.2, radial=True):
    t = unreal.DeadZoneType.RADIAL if radial else unreal.DeadZoneType.AXIAL
    return set_props(unreal.new_object(unreal.InputModifierDeadZone, outer=outer), type=t, lower_threshold=lower)


def scalar(outer, x, y=1.0, z=1.0):
    return set_props(unreal.new_object(unreal.InputModifierScalar, outer=outer), scalar=unreal.Vector(x, y, z))


def scale_by_dt(outer):
    return unreal.new_object(unreal.InputModifierScaleByDeltaTime, outer=outer)


def down_trigger(outer, threshold):
    return set_props(unreal.new_object(unreal.InputTriggerDown, outer=outer), actuation_threshold=threshold)


def mapping_context(name, folder, rows, tag="oc"):
    """rows: (action, key_name, [modifiers]) or (action, key_name, [modifiers], [triggers]).
    Modifiers/triggers are created with outer=the context, so build rows with a lambda(imc)."""
    imc = get_or_create(name, folder, unreal.InputMappingContext, unreal.InputMappingContext_Factory(), tag)
    mappings = []
    for row in rows(imc):
        action, key_name, modifiers = row[:3]
        triggers = row[3] if len(row) > 3 else []
        m = unreal.EnhancedActionKeyMapping()
        set_props(m, action=action, key=key(key_name), modifiers=modifiers, triggers=triggers)
        mappings.append(m)
    data = imc.get_editor_property("default_key_mappings")
    data.set_editor_property("mappings", mappings)
    imc.set_editor_property("default_key_mappings", data)
    save(imc)
    log(tag, f"{name}: {len(mappings)} mappings")
    return imc


# --- Levels -------------------------------------------------------------------

def spawn(cls, location=(0, 0, 0), rotation=(0, 0, 0), label=None, folder=None):
    """rotation is (roll, pitch, yaw): the positional order of unreal.Rotator in Python.
    (Not pitch/yaw/roll! A sun written as (-40, 35, 0) ends up below the horizon.)"""
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = sub.spawn_actor_from_class(cls, unreal.Vector(*location), unreal.Rotator(*rotation))
    if actor is None:
        raise RuntimeError(f"failed to spawn {cls}")
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


def block(label, center, size, rotation=(0, 0, 0), folder="Course"):
    """Box from a center and a size in cm (engine cube is 100 cm)."""
    actor = spawn(unreal.StaticMeshActor, center, rotation, label, folder)
    actor.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    return actor


def daylight(folder="Lighting"):
    """Movable sun, sky atmosphere, fog and real-time sky light (no volumetric clouds: see M0 perf notes)."""
    sun = spawn(unreal.DirectionalLight, (0, 0, 1000), (0, -40, 35), "Sun", folder)  # sun 40 deg up
    set_props(sun.get_component_by_class(unreal.DirectionalLightComponent),
              mobility=unreal.ComponentMobility.MOVABLE, atmosphere_sun_light=True, intensity=10.0)
    spawn(unreal.SkyAtmosphere, label="SkyAtmosphere", folder=folder)
    fog = spawn(unreal.ExponentialHeightFog, label="HeightFog", folder=folder)
    # Light haze only: the default density washes distant grey-box terrain into the sky. M4 tunes this.
    set_props(fog.get_component_by_class(unreal.ExponentialHeightFogComponent), fog_density=0.004, fog_height_falloff=0.05)
    sky = spawn(unreal.SkyLight, (0, 0, 500), label="SkyLight", folder=folder)
    set_props(sky.get_component_by_class(unreal.SkyLightComponent),
              mobility=unreal.ComponentMobility.MOVABLE, real_time_capture=True)


def new_level(path):
    """Create a level, or empty an existing one so it can be rebuilt in place.
    (Deleting a map the editor has referenced, e.g. the startup map, fails in a commandlet.)"""
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if assets.does_asset_exist(path):
        if not level.load_level(path):
            raise RuntimeError(f"could not load {path}")
        doomed = [a for a in actors.get_all_level_actors() if not isinstance(a, (unreal.WorldSettings, unreal.Brush))]
        if doomed:
            actors.destroy_actors(doomed)
        left = [a for a in actors.get_all_level_actors() if not isinstance(a, (unreal.WorldSettings, unreal.Brush))]
        if left:
            raise RuntimeError(f"could not clear {path}: {len(left)} actors left")
        return
    if not level.new_level(path):
        raise RuntimeError(f"new_level failed for {path}")


def save_level(path, tag="oc"):
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError(f"save failed for {path}")
    log(tag, f"saved {path}")
