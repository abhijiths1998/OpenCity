"""Download CC0 Poly Haven models (glTF, 1K textures) into SourceArt/Downloads/Models/<slug>/.

    python3 -I Tools/fetch_models.py

Reads the "models" list in SourceArt/manifest.json, verifies every file's MD5 against Poly Haven's
API, and adds a ledger row to SourceArt/LICENSES.csv for each model on disk. Downloads are
git-ignored; re-run to fetch them again. Import with Content/Python/oc_import_props.py.
"""

import csv
import datetime
import hashlib
import json
import os
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "SourceArt/manifest.json")
LICENSES = os.path.join(ROOT, "SourceArt/LICENSES.csv")
OUT = os.path.join(ROOT, "SourceArt/Downloads/Models")
UA = {"User-Agent": "OpenCity-asset-fetch"}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return r.read()


def main():
    manifest = json.load(open(MANIFEST))
    models = manifest.get("models", [])
    with open(LICENSES, newline="") as f:
        ledger = {row[0] for row in csv.reader(f) if row}
    new_rows = []
    for m in models:
        slug, res = m["slug"], m.get("res", "1k")
        files = json.loads(get(f"https://api.polyhaven.com/files/{slug}"))
        entry = files["gltf"][res]["gltf"]
        dest = os.path.join(OUT, slug)
        os.makedirs(dest, exist_ok=True)
        todo = [(os.path.basename(entry["url"]), entry)] + list(entry.get("include", {}).items())
        for rel, info in todo:
            path = os.path.join(dest, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            if os.path.exists(path) and hashlib.md5(open(path, "rb").read()).hexdigest() == info.get("md5"):
                continue
            data = get(info["url"])
            if info.get("md5") and hashlib.md5(data).hexdigest() != info["md5"]:
                sys.exit(f"MD5 mismatch for {slug}/{rel}")
            with open(path, "wb") as f:
                f.write(data)
        print(f"{slug}: {len(todo)} files")
        key = f"SourceArt/Downloads/Models/{slug}"
        if key not in ledger:
            new_rows.append([key, f"https://polyhaven.com/a/{slug}", "Poly Haven", "CC0",
                             datetime.date.today().isoformat(), m.get("notes", "")])
    if new_rows:
        with open(LICENSES, "a", newline="") as f:
            csv.writer(f).writerows(new_rows)
        print(f"ledger: {len(new_rows)} new row(s)")


if __name__ == "__main__":
    main()
