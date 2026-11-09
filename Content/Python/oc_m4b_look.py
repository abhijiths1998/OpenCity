"""Apply oc_look.POST_SETTINGS to L_Marevia's unbound post-process volume without rebuilding the map.

  scripts/run_python.sh Content/Python/oc_m4b_look.py
"""

import unreal

import oc_lib as L
import oc_look

TAG = "oc_look"
MAP = "/Game/Maps/Marevia/L_Marevia"

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
vols = [a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        if isinstance(a, unreal.PostProcessVolume) and a.get_editor_property("unbound")]
if len(vols) != 1:
    raise RuntimeError(f"expected one unbound post-process volume in {MAP}, found {len(vols)}")
ppv = vols[0]
ppv.set_editor_property("settings", oc_look.apply(ppv.get_editor_property("settings")))
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
L.log(TAG, f"{ppv.get_actor_label()}: {len(oc_look.POST_SETTINGS) // 2} post-process values applied")
