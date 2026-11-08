"""M4a — Texture import script.

Imports PBR texture sets from SourceArt into /Game with correct settings
and builds MI_* instances from a named master material.

Usage:
    scripts/run_python.sh Content/Python/oc_import_textures.py \
        --src SourceArt/Textures/PlasterAged \
        --dest /Game/Environment/Materials/PlasterAged \
        --parent /Game/Materials/M_Master_Opaque

File names must be T_<Asset>_<SUFFIX>.ext where SUFFIX is one of
BC, N, ORM, E, M. The script stops on unknown names rather than guessing.

After import, run the asset validator and check the result in the test map
under day and night (see ART_PIPELINE.md section 10).
"""

import argparse
import os
import sys

import unreal

import oc_lib as L

TAG = "oc_import_textures"

# --- Texture specification (mirrors ART_PIPELINE.md section 3) -----------

SPEC = {
    "BC": {
        "content": "Base color",
        "srgb": True,
        "compression": unreal.TextureCompressionSettings.TC_Default,
        "group": unreal.TextureGroup.TEXTUREGROUP_World,
        "max_size": 2048,
        "param": "BaseColor",
        "description": "No baked lighting or AO",
    },
    "N": {
        "content": "Normal",
        "srgb": False,
        "compression": unreal.TextureCompressionSettings.TC_Normalmap,
        "group": unreal.TextureGroup.TEXTUREGROUP_World,
        "max_size": 2048,
        "param": "Normal",
        "description": "DirectX format, Y down",
    },
    "ORM": {
        "content": "R occlusion, G roughness, B metallic",
        "srgb": False,
        "compression": unreal.TextureCompressionSettings.TC_Masks,
        "group": unreal.TextureGroup.TEXTUREGROUP_World,
        "max_size": 2048,
        "param": "ORM",
        "description": "Packed offline by Tools/pack_orm.py",
    },
    "E": {
        "content": "Emissive",
        "srgb": True,
        "compression": unreal.TextureCompressionSettings.TC_Default,
        "group": unreal.TextureGroup.TEXTUREGROUP_World,
        "max_size": 512,
        "param": "Emissive",
        "description": "Often 512 is enough",
    },
    "M": {
        "content": "Masks",
        "srgb": False,
        "compression": unreal.TextureCompressionSettings.TC_Masks,
        "group": unreal.TextureGroup.TEXTUREGROUP_World,
        "max_size": 1024,
        "param": "Mask",
        "description": "Channel meaning in asset notes",
    },
}

VALID_SUFFIXES = frozenset(SPEC)


def parse_name(filename):
    """Return (base_name, suffix) from T_<Asset>_<SUFFIX>.ext, or None."""
    stem = os.path.splitext(filename)[0]
    parts = stem.split("_")
    if len(parts) < 3 or parts[0] != "T":
        return None
    suffix = parts[-1]
    if suffix not in VALID_SUFFIXES:
        return None
    base = "_".join(parts[1:-1])
    return base, suffix


def apply_settings(texture, spec):
    """Apply sRGB, compression, group and max size to an imported texture."""
    try:
        texture.set_editor_property("srgb", spec["srgb"])
    except Exception as e:
        L.log(TAG, f"warning: could not set srgb on {texture.get_path_name()}: {e}")
    try:
        texture.set_editor_property("compression_settings", spec["compression"])
    except Exception as e:
        L.log(TAG, f"warning: could not set compression on {texture.get_path_name()}: {e}")
    try:
        texture.set_editor_property("lod_group", spec["group"])
    except Exception as e:
        L.log(TAG, f"warning: could not set lod group on {texture.get_path_name()}: {e}")
    try:
        texture.set_editor_property("max_texture_size", spec["max_size"])
    except Exception as e:
        L.log(TAG, f"warning: could not set max size on {texture.get_path_name()}: {e}")
    try:
        texture.set_editor_property("never_mip_map", False)
    except Exception:
        pass  # mips on unless UI


def create_instance(name, folder, parent):
    """Create (or reuse) a MaterialInstanceConstant child of parent."""
    path = f"{folder}/{name}"
    if L.assets.does_asset_exist(path):
        mi = L.assets.load_asset(path)
        L.log(TAG, f"reused existing {path}")
    else:
        factory = unreal.MaterialInstanceConstantFactoryNew()
        mi = L.tools.create_asset(name, folder, unreal.MaterialInstanceConstant, factory)
        if mi is None:
            raise RuntimeError(f"could not create {path}")
        L.log(TAG, f"created {path}")
    try:
        mi.set_editor_property("parent", parent)
    except Exception as e:
        raise RuntimeError(f"could not set parent {parent.get_path_name()} on {path}: {e}")
    L.save(mi)
    return mi


def set_texture_parameter(mi, parameter_name, texture):
    """Assign a texture to a scalar/texture parameter on the instance."""
    name = unreal.Name(parameter_name)
    tpv = unreal.TextureParameterValue()
    tpv.set_editor_property("parameter_name", name)
    tpv.set_editor_property("value", texture)
    values = list(mi.get_editor_property("texture_parameter_values"))
    values = [v for v in values if v.get_editor_property("parameter_name") != name]
    values.append(tpv)
    mi.set_editor_property("texture_parameter_values", values)


def import_one(abs_path, dest_folder, parent):
    """Import one texture file and return (texture, spec) or raise."""
    filename = os.path.basename(abs_path)
    parsed = parse_name(filename)
    if parsed is None:
        raise ValueError(f"unknown texture name pattern: {filename}")
    base, suffix = parsed
    spec = SPEC[suffix]

    tex_name = f"T_{base}_{suffix}"
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", abs_path)
    task.set_editor_property("destination_path", dest_folder)
    task.set_editor_property("destination_name", tex_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)

    L.tools.import_asset_tasks([task])

    texture = L.assets.load_asset(f"{dest_folder}/{tex_name}")
    if texture is None:
        raise RuntimeError(f"import produced no asset: {abs_path}")
    apply_settings(texture, spec)
    L.save(texture)
    L.log(TAG, f"imported {tex_name} ({spec['content']}, {spec['max_size']})")
    return texture, spec, base


def main():
    ap = argparse.ArgumentParser(description="Import PBR textures and build MI_* instances")
    ap.add_argument("--src", required=True, help="SourceArt folder containing T_*_* files")
    ap.add_argument("--dest", required=True, help="/Game destination folder for textures and instances")
    ap.add_argument("--parent", required=True, help="/Game path to the master material (e.g. /Game/Materials/M_Master_Opaque)")
    args = ap.parse_args()

    src = args.src
    dest = args.dest
    if not dest.startswith("/Game/"):
        raise SystemExit("--dest must be a /Game/ path")

    parent = L.assets.load_asset(args.parent)
    if parent is None:
        raise SystemExit(f"could not load master material: {args.parent}")

    # Collect files by base name so we can group a texture set.
    files = {}
    for fname in sorted(os.listdir(src)):
        if not os.path.isfile(os.path.join(src, fname)):
            continue
        parsed = parse_name(fname)
        if parsed is None:
            raise SystemExit(f"skipping unknown name pattern: {fname}")
        base, suffix = parsed
        files.setdefault(base, {})[suffix] = os.path.join(src, fname)

    imported = {}  # base -> {suffix: texture}
    for base, by_suffix in files.items():
        textures = {}
        for suffix, abs_path in by_suffix.items():
            texture, spec, _ = import_one(abs_path, dest, parent)
            textures[suffix] = texture
        imported[base] = textures

    # Build (or reuse) one MI_<base> per texture set and assign textures.
    for base, textures in imported.items():
        mi = create_instance(f"MI_{base}", dest, parent)
        for suffix, texture in textures.items():
            param = SPEC[suffix]["param"]
            set_texture_parameter(mi, param, texture)
        L.save(mi)
        parts = [f"{s} -> {SPEC[s]['param']}" for s in sorted(textures)]
        L.log(TAG, f"instance MI_{base}: {', '.join(parts)}")

    L.log(TAG, f"done: {len(imported)} texture sets imported into {dest}")


if __name__ == "__main__":
    main()
