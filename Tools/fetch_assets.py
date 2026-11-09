"""Tools/fetch_assets.py

Download CC0 PBR textures referenced in SourceArt/manifest.json into
SourceArt/Downloads/ and register them in SourceArt/LICENSES.csv.

Usage:
    python3 -I Tools/fetch_assets.py [--dry-run]

Each manifest entry needs:
  slug        - Poly Haven asset slug (e.g. "plastered_wall")
  maps        - list of maps to download: bc, n, orm, e (m suffix optional)
  size        - max resolution: 512, 1024, 2048
  author / license / source_url - for the LICENSES.csv ledger

Files are saved as:
    SourceArt/Downloads/<slug>/T_<slug>_BC.png
    SourceArt/Downloads/<slug>/T_<slug>_N.png
    SourceArt/Downloads/<slug>/T_<slug>_ORM.png
    SourceArt/Downloads/<slug>/T_<slug>_E.png

SourceArt/Downloads/ is git-ignored and re-fetched from the manifest.
After downloading, run:
    scripts/run_python.sh Content/Python/oc_import_textures.py \
        --src SourceArt/Downloads/<slug> \
        --dest /Game/Environment/Materials/<Slug> \
        --parent /Game/Materials/M_Master_Opaque
"""

import argparse
import datetime
import csv
import json
import os
import sys
import urllib.request

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCEART = os.path.join(PROJECT_ROOT, "SourceArt")
MANIFEST_PATH = os.path.join(SOURCEART, "manifest.json")
LICENSES_PATH = os.path.join(SOURCEART, "LICENSES.csv")
DOWNLOADS_DIR = os.path.join(SOURCEART, "Downloads")

USER_AGENT = "OpenCity-fetch/1.0 (contact: opencity@localhost)"
MAP_EXT = {"bc": "png", "n": "png", "orm": "png", "e": "png", "m": "png"}

POLYHAVEN_API = "https://api.polyhaven.com/assets"
POLYHAVEN_FILES = "https://api.polyhaven.com/files"

# Map our suffix -> Poly Haven files-endpoint key and output suffix.
# Keys observed from https://api.polyhaven.com/files/<slug>: Diffuse, nor_dx,
# nor_gl, arm (packed AO+Roughness+Metal), AO, Rough, Displacement, mtlx, gltf.
MAP_INFO = {
    "bc": {"ph": "Diffuse",   "suffix": "BC"},
    "n":  {"ph": "nor_dx",    "suffix": "N"},   # DirectX normal map, Y-down (Unreal expects this)
    "orm": {"ph": "arm",      "suffix": "ORM"},  # packed AO+Roughness+Metal
    "e":  {"ph": "Emission",  "suffix": "E"},
    "m":  {"ph": "AO",        "suffix": "M"},
}


def http_get_json(url, timeout=20):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def download_file(url, dest):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as r, open(dest, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)


def size_token(size):
    return {512: "512", 1024: "1k", 2048: "2k", 4096: "4k"}[size]


def discover_urls(slug, size):
    """Query Poly Haven's files endpoint and return {map: download_url}."""
    files = http_get_json(f"{POLYHAVEN_FILES}/{slug}")
    chosen = {}
    size_t = size_token(size)
    for m, info_ in MAP_INFO.items():
        ph = info_["ph"]
        url = None
        entry = files.get(ph) if isinstance(files, dict) else None
        if isinstance(entry, dict):
            # files[<map>][<resolution>][<format>] = {"url": ...}. Take the wanted size, else the
            # next smaller one (never larger: 2K is the in-game cap). Normals as PNG (JPEG blocks
            # show up in lighting); colour and packed ORM as JPEG.
            order = ("2k", "1k", "512") if size_t == "2k" else ("1k", "512") if size_t == "1k" else (size_t,)
            for res in order:
                level = entry.get(res)
                if isinstance(level, dict):
                    fmt = (level.get("png") if m == "n" else None) or level.get("jpg") or level.get("png")
                    if isinstance(fmt, dict) and fmt.get("url"):
                        url = fmt["url"]
                        break
        if url:
            chosen[m] = url
    return chosen


def load_manifest():
    with open(MANIFEST_PATH) as f:
        return json.load(f)


def save_manifest(manifest):
    with open(MANIFEST_PATH, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")


def append_license(rows):
    existing = set()
    if os.path.exists(LICENSES_PATH):
        with open(LICENSES_PATH, newline="") as f:
            for row in csv.reader(f):
                if row:
                    existing.add(row[0])
    new_rows = [r for r in rows if r[0] not in existing]
    with open(LICENSES_PATH, "a", newline="") as f:
        writer = csv.writer(f)
        for row in new_rows:
            writer.writerow(row)
    return new_rows


def main():
    ap = argparse.ArgumentParser(
        description="Download CC0 textures from Poly Haven into SourceArt/Downloads/"
    )
    ap.add_argument("--dry-run", action="store_true",
                    help="plan downloads without writing files")
    args = ap.parse_args()

    manifest = load_manifest()
    assets = manifest.get("assets", [])
    if not assets:
        print("No assets in SourceArt/manifest.json. Add entries, then re-run.")
        return 0

    license_rows = []
    planned = 0

    for entry in assets:
        slug = entry["slug"]
        size = entry.get("size", 1024)
        maps = entry.get("maps", ["bc", "n", "orm", "e"])
        dest_dir = os.path.join(DOWNLOADS_DIR, slug)
        os.makedirs(dest_dir, exist_ok=True)

        try:
            urls = discover_urls(slug, size)
        except Exception as e:
            print(f"WARNING: could not discover URLs for {slug}: {e}")
            continue

        for m in maps:
            if m not in urls:
                print(f"WARNING: no URL for {slug}/{m}; skipping")
                continue
            url = urls[m]
            ext = url.rsplit(".", 1)[-1].lower()  # the format discover_urls chose
            suffix = MAP_INFO[m]["suffix"]
            out_name = f"T_{slug}_{suffix}.{ext}"
            out_path = os.path.join(dest_dir, out_name)
            planned += 1
            if args.dry_run:
                print(f"[dry-run] {slug}/{m} -> {out_name}  ({url})")
                continue
            if os.path.exists(out_path):
                print(f"skip (exists) {out_path}")
                continue
            try:
                download_file(url, out_path)
                print(f"downloaded {out_path}")
            except Exception as e:
                print(f"WARNING: failed {slug}/{m}: {e}")

        if not any(f.startswith(f"T_{slug}_") for f in os.listdir(dest_dir)):
            continue  # nothing on disk: no ledger row
        license_rows.append([
            f"SourceArt/Downloads/{slug}",
            entry.get("source_url", f"https://polyhaven.com/{slug}"),
            entry.get("author", "Poly Haven / CC0"),
            entry.get("license", "CC0"),
            datetime.date.today().isoformat(),
            entry.get("notes", ""),
        ])

    added = append_license(license_rows) if not args.dry_run else []
    if added:
        print(f"appended {len(added)} row(s) to SourceArt/LICENSES.csv")
    print(f"planned/downloaded {planned} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
