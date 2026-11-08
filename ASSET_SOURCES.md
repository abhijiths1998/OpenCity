# OpenCity — ASSET_SOURCES.md (asset ledger)

**Purpose:** Track every external asset used in the project: name, source URL, author, license, where it's used. Required by hard rule 3 — no asset enters the game without a row here and a matching row in `SourceArt/LICENSES.csv`.

**Hard rule:** final art comes only from curated sources you download:
- Poly Haven (models, textures, HDRIs, CC0)
- ambientCG (textures, CC0)
- Fab free and monthly-free assets, Megascans you own (check license each time)
- Your own Blender kitbash pieces

CC0 originals are **not committed**. `SourceArt/manifest.json` lists them and `Tools/fetch_assets.py` re-downloads them, saving LFS quota.

---

## Downloaded assets (to fill in as you download)

| Asset | Source | URL | Author | License | Where used | Date |
|---|---|---|---|---|---|---|
| | | | | | | |
| | | | | | | |

*(Rows are added when you download each texture/model/HDRI from the AUDIT.md download list.)*

---

## Existing engine content (placeholders)

| Asset | Source | License | Where used | Notes |
|---|---|---|---|---|
| `Cube` (BasicShapes) | Unreal Engine | Epic EULA (template content) | M3 blockout buildings | Temporary; replaced by modular kit |
| UE Mannequin (Manny / Quinn) | Unreal Engine | Epic EULA | Character placeholder | See ARCHITECTURE.md — custom characters decided after M6 |

---

## Engine template content in use

| Asset | License | Where used | Notes |
|---|---|---|---|
| Vehicle template | Epic EULA (template content, for use in Unreal projects) | `AOCVehicle` visual parts | Placeholder until Blender replacements |

---

## How to add a row

1. Download the asset into `SourceArt/Downloads/` (not committed).
2. Add a row above (name, source URL, author, license, where used, date).
3. Add a matching row to `SourceArt/LICENSES.csv` (name, source URL, author, license, where used).
4. Run the import script (`oc_import_textures.py` for textures; the Blender UE import script for meshes — Step 6).
5. Run the asset validator.
6. Check in the test map under day and night.
7. Commit to git (with LFS).
