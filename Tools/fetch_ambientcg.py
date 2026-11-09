"""Download CC0 ambientCG assets (decals) into SourceArt/Downloads/Decals/<id>/.

    python3 -I Tools/fetch_ambientcg.py

Reads the "ambientcg" list in SourceArt/manifest.json ({"id", "res", "notes"}), downloads each
asset's zip at the given resolution (default 1K-PNG), keeps the maps the decal material uses
(Color, NormalDX, Roughness, Opacity) and adds a ledger row to SourceArt/LICENSES.csv.
Downloads are git-ignored; re-run to fetch them again. Import with oc_decals.py.
"""

import csv
import datetime
import io
import json
import os
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "SourceArt/manifest.json")
LICENSES = os.path.join(ROOT, "SourceArt/LICENSES.csv")
OUT = os.path.join(ROOT, "SourceArt/Downloads/Decals")
UA = {"User-Agent": "OpenCity-asset-fetch"}
KEEP = ("_Color.", "_NormalDX.", "_Roughness.", "_Opacity.")


def main():
    items = json.load(open(MANIFEST)).get("ambientcg", [])
    with open(LICENSES, newline="") as f:
        ledger = {row[0] for row in csv.reader(f) if row}
    rows = []
    for it in items:
        aid, res = it["id"], it.get("res", "1K-PNG")
        dest = os.path.join(OUT, aid)
        if os.path.isdir(dest) and any(k in f for f in os.listdir(dest) for k in KEEP):
            print(f"{aid}: already here")
        else:
            url = f"https://ambientcg.com/get?file={aid}_{res}.zip"
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
                data = r.read()
            os.makedirs(dest, exist_ok=True)
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                kept = [n for n in z.namelist() if any(k in n for k in KEEP)]
                for n in kept:
                    with open(os.path.join(dest, os.path.basename(n)), "wb") as f:
                        f.write(z.read(n))
            print(f"{aid}: {len(kept)} maps")
        key = f"SourceArt/Downloads/Decals/{aid}"
        if key not in ledger:
            rows.append([key, f"https://ambientcg.com/view?id={aid}", "ambientCG", "CC0",
                         datetime.date.today().isoformat(), it.get("notes", "")])
    if rows:
        with open(LICENSES, "a", newline="") as f:
            csv.writer(f).writerows(rows)
        print(f"ledger: {len(rows)} new row(s)")


if __name__ == "__main__":
    main()
