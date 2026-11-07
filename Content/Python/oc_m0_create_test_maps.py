"""Create the M0 test maps. Safe to re-run: existing maps are rebuilt.

  /Game/Maps/Test/L_PerfBaseline  empty lit level: sky, sun, fog, ground, perf camera
  /Game/Maps/Test/L_Stress        baseline plus a Nanite mesh field and shadowed lights,
                                  used for the M0 stability gate

Run headless:  scripts/run_python.sh Content/Python/oc_m0_create_test_maps.py
"""

import math

import unreal

MAP_DIR = "/Game/Maps/Test"
DEV_MESH_DIR = "/Game/Dev/Meshes"
NANITE_MESH = f"{DEV_MESH_DIR}/SM_Dev_NaniteSphere"
SOURCE_MESH = "/Engine/EngineMeshes/Sphere"  # higher poly than BasicShapes, good for Nanite
GROUND_MESH = "/Engine/BasicShapes/Cube"

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.EditorAssetLibrary


def log(msg):
    unreal.log(f"[oc_m0] {msg}")


def spawn(cls, location=(0, 0, 0), rotation=(0, 0, 0), label=None):
    """rotation is (roll, pitch, yaw), the positional order of unreal.Rotator."""
    actor = actor_subsystem.spawn_actor_from_class(
        cls, unreal.Vector(*location), unreal.Rotator(*rotation)
    )
    if actor is None:
        raise RuntimeError(f"failed to spawn {cls}")
    if label:
        actor.set_actor_label(label)
    return actor


def ensure_nanite_mesh():
    """Copy an engine sphere into /Game and enable Nanite on the copy."""
    if not assets.does_asset_exist(NANITE_MESH):
        if assets.duplicate_asset(SOURCE_MESH, NANITE_MESH) is None:
            raise RuntimeError(f"could not duplicate {SOURCE_MESH}")
    mesh = unreal.load_asset(NANITE_MESH)
    settings = mesh.get_editor_property("nanite_settings")
    if not settings.enabled:
        settings.enabled = True
        mesh.set_editor_property("nanite_settings", settings)
        log("enabled Nanite on SM_Dev_NaniteSphere")
    assets.save_asset(NANITE_MESH, only_if_is_dirty=False)
    return mesh


def build_environment():
    """Sky, sun, fog, clouds, sky light, ground and the perf camera path."""
    sun = spawn(unreal.DirectionalLight, (0, 0, 1000), (0, -35, 40), "Sun")  # (roll, pitch, yaw): sun 35 deg up
    sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun_comp.set_editor_property("atmosphere_sun_light", True)
    sun_comp.set_editor_property("intensity", 10.0)

    spawn(unreal.SkyAtmosphere, label="SkyAtmosphere")
    spawn(unreal.VolumetricCloud, label="VolumetricCloud")
    spawn(unreal.ExponentialHeightFog, label="HeightFog")

    sky = spawn(unreal.SkyLight, (0, 0, 500), label="SkyLight")
    sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_comp.set_editor_property("real_time_capture", True)

    # A 400 m slab of the engine cube (top at z=0). The flat Plane mesh has no collision,
    # so the walking player (default game mode since M1) fell through it.
    ground = spawn(unreal.StaticMeshActor, (0, 0, -50), label="Ground")
    ground_comp = ground.get_component_by_class(unreal.StaticMeshComponent)
    ground_comp.set_static_mesh(unreal.load_asset(GROUND_MESH))
    ground.set_actor_scale3d(unreal.Vector(400, 400, 1))

    perf_cls = unreal.load_class(None, "/Script/OpenCity.OCPerfCameraPath")
    if perf_cls is None:
        raise RuntimeError("OCPerfCameraPath class not found: build the editor target first")
    spawn(perf_cls, label="PerfCameraPath")


def build_stress_content(mesh):
    """A 30 x 30 field of Nanite spheres and 24 shadow-casting point lights."""
    spacing, grid = 600.0, 30
    offset = (grid - 1) * spacing / 2
    for x in range(grid):
        for y in range(grid):
            actor = spawn(
                unreal.StaticMeshActor,
                (x * spacing - offset, y * spacing - offset, 150),
            )
            comp = actor.get_component_by_class(unreal.StaticMeshComponent)
            comp.set_static_mesh(mesh)
            scale = 1.5 + 2.0 * (((x * 7 + y * 13) % 10) / 10.0)
            actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
            actor.set_folder_path("StressMeshes")

    for i in range(24):
        angle = 2 * math.pi * i / 24
        light = spawn(
            unreal.PointLight,
            (4000 * math.cos(angle), 4000 * math.sin(angle), 400),
            label=f"StressLight_{i:02d}",
        )
        comp = light.get_component_by_class(unreal.PointLightComponent)
        comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        comp.set_editor_property("intensity", 5000.0)
        comp.set_editor_property("attenuation_radius", 2500.0)
        comp.set_editor_property("cast_shadows", True)
        light.set_folder_path("StressLights")
    log(f"stress content: {grid * grid} Nanite meshes, 24 shadowed lights")


def make_map(name, with_stress, mesh):
    import oc_lib
    path = f"{MAP_DIR}/{name}"
    oc_lib.new_level(path)
    build_environment()
    if with_stress:
        build_stress_content(mesh)
    if not level_subsystem.save_current_level():
        raise RuntimeError(f"save failed for {path}")
    log(f"saved {path}")


def main():
    mesh = ensure_nanite_mesh()
    make_map("L_PerfBaseline", False, mesh)
    make_map("L_Stress", True, mesh)
    log("done")


main()
