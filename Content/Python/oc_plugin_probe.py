"""Report whether the given plugins are enabled and their modules loaded.

Used by scripts/plugin_smoke.sh. Prints one OCPROBE line per plugin.
Usage (via commandlet): oc_plugin_probe.py Plugin,Plugin,...
"""

import sys

import unreal

names = [n for n in (sys.argv[1] if len(sys.argv) > 1 else "").split(",") if n]
enabled = set(unreal.PluginBlueprintLibrary.get_enabled_plugin_names())
failed = False
for name in names:
    if name in enabled:
        unreal.log(f"OCPROBE OK {name}")
    else:
        unreal.log_warning(f"OCPROBE FAIL {name} is not enabled")
        failed = True
unreal.log("OCPROBE DONE" + (" with failures" if failed else ""))
