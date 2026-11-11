//! OpenCity offline asset tools (formerly Tools/fetch_assets.py, fetch_ambientcg.py and fetch_models.py).
//!
//!   octool fetch-textures [--dry-run]   Poly Haven PBR textures ("assets") -> SourceArt/Downloads/<slug>/T_<slug>_<BC|N|ORM|E|M>.<ext>
//!   octool fetch-decals                 ambientCG decal sets ("ambientcg")  -> SourceArt/Downloads/Decals/<id>/
//!   octool fetch-models                 Poly Haven glTF models ("models"), MD5-checked -> SourceArt/Downloads/Models/<slug>/
//!   octool combine-gltf <in> <out> [re] one mesh per prop (nodes fully matching re), pivot at the bottom centre, as GLB
//!
//! Everything is listed in SourceArt/manifest.json. Each asset on disk gets a row in SourceArt/LICENSES.csv.
//! Downloads are git-ignored; re-run to fetch them again (files already present are skipped).
//! Build and run with scripts/octool.sh <command>.

use std::collections::HashSet;
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use serde_json::Value;

mod gltf_combine;

const USER_AGENT: &str = "OpenCity-asset-fetch";

type Result<T> = std::result::Result<T, String>;

fn project_root() -> PathBuf {
    // Tools/octool/ -> project root
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..").canonicalize().expect("project root")
}

fn get(url: &str) -> Result<ureq::http::Response<ureq::Body>> {
    ureq::get(url).header("User-Agent", USER_AGENT).call().map_err(|e| format!("{url}: {e}"))
}

fn get_bytes(url: &str) -> Result<Vec<u8>> {
    let mut data = Vec::new();
    get(url)?.body_mut().as_reader().read_to_end(&mut data).map_err(|e| format!("{url}: {e}"))?;
    Ok(data)
}

fn get_json(url: &str) -> Result<Value> {
    serde_json::from_slice(&get_bytes(url)?).map_err(|e| format!("{url}: {e}"))
}

fn read_manifest(root: &Path) -> Result<Value> {
    let path = root.join("SourceArt/manifest.json");
    let text = fs::read_to_string(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))
}

fn list<'a>(manifest: &'a Value, key: &str) -> &'a [Value] {
    manifest.get(key).and_then(Value::as_array).map(Vec::as_slice).unwrap_or(&[])
}

fn str_or<'a>(v: &'a Value, key: &str, default: &'a str) -> &'a str {
    v.get(key).and_then(Value::as_str).unwrap_or(default)
}

/// Today's date (UTC) as YYYY-MM-DD, from days since 1970 (Howard Hinnant's civil_from_days).
fn today() -> String {
    let secs = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    let z = (secs / 86400) as i64 + 719468;
    let era = z.div_euclid(146097);
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + if m <= 2 { 1 } else { 0 };
    format!("{y:04}-{m:02}-{d:02}")
}

/// The asset paths already in the ledger (first column).
fn ledger_keys(root: &Path) -> HashSet<String> {
    fs::read_to_string(root.join("SourceArt/LICENSES.csv"))
        .unwrap_or_default()
        .lines()
        .filter_map(|l| l.split(',').next())
        .map(|k| k.trim_matches('"').to_string())
        .filter(|k| !k.is_empty())
        .collect()
}

/// Append rows the way Python's csv.writer does (minimal quoting, \r\n line ends).
fn append_ledger(root: &Path, rows: &[Vec<String>]) -> Result<()> {
    if rows.is_empty() {
        return Ok(());
    }
    let quote = |f: &str| {
        if f.contains([',', '"', '\r', '\n']) { format!("\"{}\"", f.replace('"', "\"\"")) } else { f.to_string() }
    };
    let path = root.join("SourceArt/LICENSES.csv");
    let mut file = fs::OpenOptions::new().append(true).create(true).open(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    for row in rows {
        let line = row.iter().map(|f| quote(f)).collect::<Vec<_>>().join(",");
        write!(file, "{line}\r\n").map_err(|e| e.to_string())?;
    }
    println!("ledger: {} new row(s)", rows.len());
    Ok(())
}

// ---- Textures (Poly Haven) ----------------------------------------------------------------------

/// Our map key -> (Poly Haven files key, output suffix).
fn map_info(m: &str) -> Option<(&'static str, &'static str)> {
    match m {
        "bc" => Some(("Diffuse", "BC")),
        "n" => Some(("nor_dx", "N")), // DirectX normal map, Y down (what Unreal expects)
        "orm" => Some(("arm", "ORM")), // packed AO + roughness + metal
        "e" => Some(("Emission", "E")),
        "m" => Some(("AO", "M")),
        _ => None,
    }
}

/// The download URL per map: the wanted size or the next smaller one (never larger: 2K is the in-game cap).
/// Normals as PNG (JPEG blocks show in lighting); colour and packed ORM as JPEG.
fn texture_urls(files: &Value, size: u64, map: &str) -> Option<String> {
    let (ph, _) = map_info(map)?;
    let entry = files.get(ph)?;
    let order: &[&str] = match size {
        2048 => &["2k", "1k", "512"],
        1024 => &["1k", "512"],
        4096 => &["4k"],
        _ => &["512"],
    };
    for res in order {
        if let Some(level) = entry.get(*res) {
            let fmt = (if map == "n" { level.get("png") } else { None }).or_else(|| level.get("jpg")).or_else(|| level.get("png"));
            if let Some(url) = fmt.and_then(|f| f.get("url")).and_then(Value::as_str) {
                return Some(url.to_string());
            }
        }
    }
    None
}

fn fetch_textures(root: &Path, dry_run: bool) -> Result<()> {
    let manifest = read_manifest(root)?;
    let assets = list(&manifest, "assets");
    if assets.is_empty() {
        println!("No assets in SourceArt/manifest.json. Add entries, then re-run.");
        return Ok(());
    }
    let ledger = ledger_keys(root);
    let mut rows = Vec::new();
    let mut planned = 0;
    for entry in assets {
        let slug = str_or(entry, "slug", "");
        let size = entry.get("size").and_then(Value::as_u64).unwrap_or(1024);
        let maps: Vec<&str> = entry.get("maps").and_then(Value::as_array)
            .map(|a| a.iter().filter_map(Value::as_str).collect()).unwrap_or_else(|| vec!["bc", "n", "orm", "e"]);
        let dest = root.join("SourceArt/Downloads").join(slug);
        fs::create_dir_all(&dest).map_err(|e| e.to_string())?;
        let files = match get_json(&format!("https://api.polyhaven.com/files/{slug}")) {
            Ok(f) => f,
            Err(e) => {
                println!("WARNING: could not discover URLs for {slug}: {e}");
                continue;
            }
        };
        for m in maps {
            let Some(url) = texture_urls(&files, size, m) else {
                println!("WARNING: no URL for {slug}/{m}; skipping");
                continue;
            };
            let ext = url.rsplit('.').next().unwrap_or("png").to_lowercase();
            let out = dest.join(format!("T_{slug}_{}.{ext}", map_info(m).unwrap().1));
            planned += 1;
            if dry_run {
                println!("[dry-run] {slug}/{m} -> {}  ({url})", out.file_name().unwrap().to_string_lossy());
                continue;
            }
            if out.exists() {
                println!("skip (exists) {}", out.display());
                continue;
            }
            match get_bytes(&url).and_then(|d| fs::write(&out, d).map_err(|e| e.to_string())) {
                Ok(()) => println!("downloaded {}", out.display()),
                Err(e) => println!("WARNING: failed {slug}/{m}: {e}"),
            }
        }
        let on_disk = fs::read_dir(&dest).map(|d| d.flatten().any(|f| f.file_name().to_string_lossy().starts_with(&format!("T_{slug}_")))).unwrap_or(false);
        let key = format!("SourceArt/Downloads/{slug}");
        if on_disk && !ledger.contains(&key) {
            rows.push(vec![key, str_or(entry, "source_url", &format!("https://polyhaven.com/{slug}")).to_string(),
                str_or(entry, "author", "Poly Haven / CC0").to_string(), str_or(entry, "license", "CC0").to_string(),
                today(), str_or(entry, "notes", "").to_string()]);
        }
    }
    if !dry_run {
        append_ledger(root, &rows)?;
    }
    println!("planned/downloaded {planned} file(s)");
    Ok(())
}

// ---- Decals (ambientCG) -------------------------------------------------------------------------

fn fetch_decals(root: &Path) -> Result<()> {
    const KEEP: [&str; 4] = ["_Color.", "_NormalDX.", "_Roughness.", "_Opacity."];
    let manifest = read_manifest(root)?;
    let ledger = ledger_keys(root);
    let mut rows = Vec::new();
    for it in list(&manifest, "ambientcg") {
        let id = str_or(it, "id", "");
        let res = str_or(it, "res", "1K-PNG");
        let dest = root.join("SourceArt/Downloads/Decals").join(id);
        let have = fs::read_dir(&dest).map(|d| d.flatten().any(|f| KEEP.iter().any(|k| f.file_name().to_string_lossy().contains(k)))).unwrap_or(false);
        if have {
            println!("{id}: already here");
        } else {
            let data = get_bytes(&format!("https://ambientcg.com/get?file={id}_{res}.zip"))?;
            fs::create_dir_all(&dest).map_err(|e| e.to_string())?;
            let mut zip = zip::ZipArchive::new(std::io::Cursor::new(data)).map_err(|e| format!("{id}: {e}"))?;
            let mut kept = 0;
            for i in 0..zip.len() {
                let mut f = zip.by_index(i).map_err(|e| e.to_string())?;
                let name = f.name().map_err(|e| e.to_string())?.to_string();
                if !KEEP.iter().any(|k| name.contains(k)) {
                    continue;
                }
                let base = Path::new(&name).file_name().map(|n| n.to_owned()).ok_or("bad zip entry")?;
                let mut bytes = Vec::new();
                f.read_to_end(&mut bytes).map_err(|e| e.to_string())?;
                fs::write(dest.join(base), bytes).map_err(|e| e.to_string())?;
                kept += 1;
            }
            println!("{id}: {kept} maps");
        }
        let key = format!("SourceArt/Downloads/Decals/{id}");
        if !ledger.contains(&key) {
            rows.push(vec![key, format!("https://ambientcg.com/view?id={id}"), "ambientCG".into(), "CC0".into(), today(),
                str_or(it, "notes", "").to_string()]);
        }
    }
    append_ledger(root, &rows)
}

// ---- Models (Poly Haven glTF) -------------------------------------------------------------------

fn fetch_models(root: &Path) -> Result<()> {
    let manifest = read_manifest(root)?;
    let ledger = ledger_keys(root);
    let mut rows = Vec::new();
    for m in list(&manifest, "models") {
        let slug = str_or(m, "slug", "");
        let res = str_or(m, "res", "1k");
        let files = get_json(&format!("https://api.polyhaven.com/files/{slug}"))?;
        let entry = files.pointer(&format!("/gltf/{res}/gltf")).ok_or(format!("{slug}: no {res} glTF"))?;
        let dest = root.join("SourceArt/Downloads/Models").join(slug);
        let main_url = str_or(entry, "url", "");
        let mut todo: Vec<(String, &Value)> = vec![(main_url.rsplit('/').next().unwrap_or("").to_string(), entry)];
        if let Some(inc) = entry.get("include").and_then(Value::as_object) {
            todo.extend(inc.iter().map(|(k, v)| (k.clone(), v)));
        }
        for (rel, info) in &todo {
            let path = dest.join(rel);
            fs::create_dir_all(path.parent().unwrap()).map_err(|e| e.to_string())?;
            let want = info.get("md5").and_then(Value::as_str);
            if let (Ok(have), Some(want)) = (fs::read(&path), want) {
                if format!("{:x}", md5::compute(&have)) == want {
                    continue;
                }
            }
            let data = get_bytes(str_or(info, "url", ""))?;
            if let Some(want) = want {
                if format!("{:x}", md5::compute(&data)) != want {
                    return Err(format!("MD5 mismatch for {slug}/{rel}"));
                }
            }
            fs::write(&path, data).map_err(|e| e.to_string())?;
        }
        println!("{slug}: {} files", todo.len());
        let key = format!("SourceArt/Downloads/Models/{slug}");
        if !ledger.contains(&key) {
            rows.push(vec![key, format!("https://polyhaven.com/a/{slug}"), "Poly Haven".into(), "CC0".into(), today(),
                str_or(m, "notes", "").to_string()]);
        }
    }
    append_ledger(root, &rows)
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let root = project_root();
    let result = match args.first().map(String::as_str) {
        Some("fetch-textures") => fetch_textures(&root, args.iter().any(|a| a == "--dry-run")),
        Some("fetch-decals") => fetch_decals(&root),
        Some("fetch-models") => fetch_models(&root),
        Some("combine-gltf") if args.len() >= 3 => {
            gltf_combine::combine(Path::new(&args[1]), Path::new(&args[2]), args.get(3).map(String::as_str)).map(|msg| println!("{msg}"))
        }
        _ => Err("usage: octool fetch-textures [--dry-run] | fetch-decals | fetch-models | combine-gltf <in.gltf> <out.glb> [keep-regex]".into()),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}
