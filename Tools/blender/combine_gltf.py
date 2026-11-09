"""Merge every mesh of a downloaded glTF prop into one object and export it as GLB.

  ~/Applications/blender-4.2/blender -b --factory-startup -P Tools/blender/combine_gltf.py -- <in.gltf> <out.glb> [keep-regex]

keep-regex picks one variant from packs that hold several side by side (object names must match).

Unreal's Interchange importer turns each glTF node into its own static mesh; a prop must be one
mesh to be placed and instanced. Materials and textures are kept (embedded in the GLB).
"""

import re
import sys

import bpy

args = sys.argv[sys.argv.index("--") + 1:]
src, dst = args[:2]
keep = re.compile(args[2]) if len(args) > 2 else None
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and (keep is None or keep.fullmatch(o.name))]
for o in [o for o in bpy.context.scene.objects if o.type == "MESH" and o not in meshes]:
    bpy.data.objects.remove(o, do_unlink=True)
for o in bpy.context.scene.objects:
    o.select_set(o in meshes)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
obj.parent = None
# Pivot at the bottom centre, standing on the ground.
bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")
lo = min((obj.matrix_world @ v.co).z for v in obj.data.vertices)
obj.location.z -= lo
obj.location.x = obj.location.y = 0.0
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for o in list(bpy.context.scene.objects):
    if o is not obj:
        bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=False)
print(f"COMBINED {len(meshes)} mesh(es) -> {dst}, size {tuple(round(d, 2) for d in obj.dimensions)} m")
