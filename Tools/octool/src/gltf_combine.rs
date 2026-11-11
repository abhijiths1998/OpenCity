//! Merge the meshes of a downloaded glTF prop into one mesh and write it as GLB (formerly
//! Tools/blender/combine_gltf.py). Unreal's Interchange importer turns each glTF node into its own
//! static mesh; a prop must be one mesh to be placed and instanced. Node transforms are baked in,
//! primitives sharing a material are merged, the pivot goes to the bottom centre (standing on the
//! ground), and the used materials, textures and images are embedded in the GLB.

use std::collections::HashMap;
use std::fs;
use std::path::Path;

use serde_json::{json, Map, Value};

type Result<T> = std::result::Result<T, String>;
type Mat4 = [[f64; 4]; 4]; // column-major, m[col][row]

fn mul(a: &Mat4, b: &Mat4) -> Mat4 {
    let mut r = [[0.0; 4]; 4];
    for c in 0..4 {
        for row in 0..4 {
            r[c][row] = (0..4).map(|k| a[k][row] * b[c][k]).sum();
        }
    }
    r
}

fn identity() -> Mat4 {
    let mut m = [[0.0; 4]; 4];
    for (i, col) in m.iter_mut().enumerate() {
        col[i] = 1.0;
    }
    m
}

fn nums(v: Option<&Value>) -> Option<Vec<f64>> {
    v.and_then(Value::as_array).map(|a| a.iter().filter_map(Value::as_f64).collect())
}

fn local_matrix(node: &Value) -> Mat4 {
    if let Some(m) = nums(node.get("matrix")) {
        let mut r = [[0.0; 4]; 4];
        for c in 0..4 {
            for row in 0..4 {
                r[c][row] = m[c * 4 + row];
            }
        }
        return r;
    }
    let t = nums(node.get("translation")).unwrap_or(vec![0.0, 0.0, 0.0]);
    let q = nums(node.get("rotation")).unwrap_or(vec![0.0, 0.0, 0.0, 1.0]);
    let s = nums(node.get("scale")).unwrap_or(vec![1.0, 1.0, 1.0]);
    let (x, y, z, w) = (q[0], q[1], q[2], q[3]);
    let rot = [
        [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + z * w), 2.0 * (x * z - y * w)],
        [2.0 * (x * y - z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + x * w)],
        [2.0 * (x * z + y * w), 2.0 * (y * z - x * w), 1.0 - 2.0 * (x * x + y * y)],
    ];
    let mut m = identity();
    for c in 0..3 {
        for row in 0..3 {
            m[c][row] = rot[c][row] * s[c];
        }
    }
    m[3][0] = t[0];
    m[3][1] = t[1];
    m[3][2] = t[2];
    m
}

struct Source {
    json: Value,
    buffers: Vec<Vec<u8>>,
    dir: std::path::PathBuf,
}

impl Source {
    fn load(path: &Path) -> Result<Source> {
        let json: Value = serde_json::from_str(&fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?)
            .map_err(|e| format!("{}: {e}", path.display()))?;
        let dir = path.parent().unwrap_or(Path::new(".")).to_path_buf();
        let mut buffers = Vec::new();
        for b in json["buffers"].as_array().cloned().unwrap_or_default() {
            let uri = b["uri"].as_str().ok_or("buffer without uri (GLB input is not supported)")?;
            buffers.push(fs::read(dir.join(uri)).map_err(|e| format!("{uri}: {e}"))?);
        }
        Ok(Source { json, buffers, dir })
    }

    /// An accessor as f64 rows of `n` components (normalized integers are mapped to 0..1).
    fn read(&self, index: usize) -> Result<Vec<Vec<f64>>> {
        let acc = &self.json["accessors"][index];
        let count = acc["count"].as_u64().ok_or("accessor count")? as usize;
        let n = match acc["type"].as_str() {
            Some("SCALAR") => 1,
            Some("VEC2") => 2,
            Some("VEC3") => 3,
            Some("VEC4") => 4,
            t => return Err(format!("accessor type {t:?}")),
        };
        let comp = acc["componentType"].as_u64().ok_or("componentType")?;
        let size = match comp {
            5120 | 5121 => 1,
            5122 | 5123 => 2,
            5125 | 5126 => 4,
            c => return Err(format!("componentType {c}")),
        };
        let normalized = acc["normalized"].as_bool().unwrap_or(false);
        let view = &self.json["bufferViews"][acc["bufferView"].as_u64().ok_or("sparse/empty accessors are not supported")? as usize];
        let data = &self.buffers[view["buffer"].as_u64().unwrap_or(0) as usize];
        let base = view["byteOffset"].as_u64().unwrap_or(0) as usize + acc["byteOffset"].as_u64().unwrap_or(0) as usize;
        let stride = view["byteStride"].as_u64().map(|s| s as usize).unwrap_or(n * size);
        let mut out = Vec::with_capacity(count);
        for i in 0..count {
            let mut row = Vec::with_capacity(n);
            for k in 0..n {
                let o = base + i * stride + k * size;
                let b = &data[o..o + size];
                let v = match comp {
                    5126 => f32::from_le_bytes([b[0], b[1], b[2], b[3]]) as f64,
                    5125 => u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as f64,
                    5123 => {
                        let x = u16::from_le_bytes([b[0], b[1]]) as f64;
                        if normalized { x / 65535.0 } else { x }
                    }
                    5122 => {
                        let x = i16::from_le_bytes([b[0], b[1]]) as f64;
                        if normalized { (x / 32767.0).max(-1.0) } else { x }
                    }
                    5121 => {
                        let x = b[0] as f64;
                        if normalized { x / 255.0 } else { x }
                    }
                    _ => {
                        let x = b[0] as i8 as f64;
                        if normalized { (x / 127.0).max(-1.0) } else { x }
                    }
                };
                row.push(v);
            }
            out.push(row);
        }
        Ok(out)
    }
}

#[derive(Default)]
struct Part {
    pos: Vec<[f64; 3]>,
    nrm: Vec<[f64; 3]>,
    uv: Vec<[f64; 2]>,
    idx: Vec<u32>,
}

/// Merge `src` into `dst` (GLB). `keep` selects mesh nodes by full-name match; None keeps them all.
pub fn combine(src: &Path, dst: &Path, keep: Option<&str>) -> Result<String> {
    let s = Source::load(src)?;
    let re = keep.map(|k| regex::Regex::new(&format!("^(?:{k})$"))).transpose().map_err(|e| e.to_string())?;
    let nodes = s.json["nodes"].as_array().cloned().unwrap_or_default();
    let scene = s.json["scenes"][s.json["scene"].as_u64().unwrap_or(0) as usize]["nodes"].as_array().cloned().unwrap_or_default();

    // World matrices of the scene's nodes.
    let mut world: Vec<Option<Mat4>> = vec![None; nodes.len()];
    let mut stack: Vec<(usize, Mat4)> = scene.iter().filter_map(Value::as_u64).map(|i| (i as usize, identity())).collect();
    while let Some((i, parent)) = stack.pop() {
        let m = mul(&parent, &local_matrix(&nodes[i]));
        for c in nodes[i]["children"].as_array().cloned().unwrap_or_default().iter().filter_map(Value::as_u64) {
            stack.push((c as usize, m));
        }
        world[i] = Some(m);
    }

    // Primitives of the kept mesh nodes, baked into world space and grouped by material (first-use order).
    let mut parts: Vec<(Option<u64>, Part)> = Vec::new();
    let mut kept = 0;
    for (i, node) in nodes.iter().enumerate() {
        let (Some(mesh), Some(m)) = (node["mesh"].as_u64(), world[i]) else { continue };
        let name = node["name"].as_str().unwrap_or("");
        if let Some(re) = &re {
            if !re.is_match(name) {
                continue;
            }
        }
        kept += 1;
        // Normal matrix: inverse transpose of the upper 3x3 (cofactors; the scale is irrelevant after normalising).
        let a = |c: usize, r: usize| m[c][r];
        let cof = |c: usize, r: usize| {
            let (c1, c2) = ((c + 1) % 3, (c + 2) % 3);
            let (r1, r2) = ((r + 1) % 3, (r + 2) % 3);
            a(c1, r1) * a(c2, r2) - a(c1, r2) * a(c2, r1)
        };
        let det: f64 = (0..3).map(|r| a(0, r) * cof(0, r)).sum();
        for prim in s.json["meshes"][mesh as usize]["primitives"].as_array().cloned().unwrap_or_default() {
            if prim["mode"].as_u64().unwrap_or(4) != 4 {
                continue; // triangles only
            }
            let attrs = &prim["attributes"];
            let pos = s.read(attrs["POSITION"].as_u64().ok_or("primitive without POSITION")? as usize)?;
            let nrm = match attrs["NORMAL"].as_u64() { Some(i) => s.read(i as usize)?, None => vec![vec![0.0, 1.0, 0.0]; pos.len()] };
            let uv = match attrs["TEXCOORD_0"].as_u64() { Some(i) => s.read(i as usize)?, None => vec![vec![0.0, 0.0]; pos.len()] };
            let idx: Vec<u32> = match prim["indices"].as_u64() {
                Some(i) => s.read(i as usize)?.iter().map(|r| r[0] as u32).collect(),
                None => (0..pos.len() as u32).collect(),
            };
            let material = prim["material"].as_u64();
            let slot = match parts.iter().position(|(mat, _)| *mat == material) {
                Some(p) => p,
                None => {
                    parts.push((material, Part::default()));
                    parts.len() - 1
                }
            };
            let part = &mut parts[slot].1;
            let base = part.pos.len() as u32;
            for p in &pos {
                part.pos.push([0, 1, 2].map(|r| a(0, r) * p[0] + a(1, r) * p[1] + a(2, r) * p[2] + m[3][r]));
            }
            for n in &nrm {
                let v = [0, 1, 2].map(|r| cof(0, r) * n[0] + cof(1, r) * n[1] + cof(2, r) * n[2]);
                let len = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt().max(1e-12);
                let sign = if det < 0.0 { -1.0 } else { 1.0 };
                part.nrm.push(v.map(|x| sign * x / len));
            }
            part.uv.extend(uv.iter().map(|t| [t[0], t[1]]));
            for tri in idx.chunks(3) {
                if det < 0.0 {
                    part.idx.extend([base + tri[0], base + tri[2], base + tri[1]]); // a mirroring transform flips the winding
                } else {
                    part.idx.extend(tri.iter().map(|i| base + i));
                }
            }
        }
    }
    if kept == 0 {
        return Err(format!("{}: no mesh node matches {keep:?}", src.display()));
    }

    // Pivot at the bottom centre: X and Z (glTF horizontal) centred, lowest Y at 0.
    let (mut lo, mut hi) = ([f64::MAX; 3], [f64::MIN; 3]);
    for (_, p) in &parts {
        for v in &p.pos {
            for k in 0..3 {
                lo[k] = lo[k].min(v[k]);
                hi[k] = hi[k].max(v[k]);
            }
        }
    }
    let shift = [-(lo[0] + hi[0]) / 2.0, -lo[1], -(lo[2] + hi[2]) / 2.0];

    // Output buffer, materials, textures and images (only the ones the kept primitives use).
    let mut bin: Vec<u8> = Vec::new();
    let mut views: Vec<Value> = Vec::new();
    let mut accessors: Vec<Value> = Vec::new();
    let mut add_view = |bin: &mut Vec<u8>, bytes: &[u8], target: Option<u64>| -> usize {
        while bin.len() % 4 != 0 {
            bin.push(0);
        }
        let mut v = json!({"buffer": 0, "byteOffset": bin.len(), "byteLength": bytes.len()});
        if let Some(t) = target {
            v["target"] = json!(t);
        }
        bin.extend_from_slice(bytes);
        views.push(v);
        views.len() - 1
    };
    let mut material_map: HashMap<u64, usize> = HashMap::new();
    let mut texture_map: HashMap<u64, usize> = HashMap::new();
    let mut image_map: HashMap<u64, usize> = HashMap::new();
    let mut sampler_map: HashMap<u64, usize> = HashMap::new();
    let (mut materials, mut textures, mut images, mut samplers) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
    let mut image_bytes: Vec<(usize, Vec<u8>, String)> = Vec::new(); // (image index, data, mime)

    // Remap every texture reference in a material (the values of *Texture keys: baseColorTexture, normalTexture, ...,
    // including extension textures), copying the texture, its image and sampler.
    fn remap(v: &mut Value, f: &mut dyn FnMut(u64) -> usize) {
        match v {
            Value::Object(o) => {
                for (k, child) in o.iter_mut() {
                    if k.ends_with("Texture") {
                        if let Some(n) = child.get("index").and_then(Value::as_u64) {
                            child["index"] = json!(f(n));
                        }
                    }
                    remap(child, f);
                }
            }
            Value::Array(a) => a.iter_mut().for_each(|c| remap(c, f)),
            _ => {}
        }
    }

    let mut primitives = Vec::new();
    for (material, part) in &parts {
        let mut prim = Map::new();
        let pos: Vec<[f64; 3]> = part.pos.iter().map(|v| [v[0] + shift[0], v[1] + shift[1], v[2] + shift[2]]).collect();
        let (mut plo, mut phi) = ([f64::MAX; 3], [f64::MIN; 3]);
        for v in &pos {
            for k in 0..3 {
                plo[k] = plo[k].min(v[k] as f32 as f64);
                phi[k] = phi[k].max(v[k] as f32 as f64);
            }
        }
        let f32s = |rows: &mut dyn Iterator<Item = f64>| rows.flat_map(|x| (x as f32).to_le_bytes()).collect::<Vec<u8>>();
        let n = pos.len();
        let pv = add_view(&mut bin, &f32s(&mut pos.iter().flatten().copied()), Some(34962));
        accessors.push(json!({"bufferView": pv, "componentType": 5126, "count": n, "type": "VEC3", "min": plo, "max": phi}));
        let pa = accessors.len() - 1;
        let nv = add_view(&mut bin, &f32s(&mut part.nrm.iter().flatten().copied()), Some(34962));
        accessors.push(json!({"bufferView": nv, "componentType": 5126, "count": n, "type": "VEC3"}));
        let na = accessors.len() - 1;
        let tv = add_view(&mut bin, &f32s(&mut part.uv.iter().flatten().copied()), Some(34962));
        accessors.push(json!({"bufferView": tv, "componentType": 5126, "count": n, "type": "VEC2"}));
        let ta = accessors.len() - 1;
        let iv = add_view(&mut bin, &part.idx.iter().flat_map(|i| i.to_le_bytes()).collect::<Vec<u8>>(), Some(34963));
        accessors.push(json!({"bufferView": iv, "componentType": 5125, "count": part.idx.len(), "type": "SCALAR"}));
        let ia = accessors.len() - 1;
        prim.insert("attributes".into(), json!({"POSITION": pa, "NORMAL": na, "TEXCOORD_0": ta}));
        prim.insert("indices".into(), json!(ia));
        if let Some(mi) = material {
            let new = match material_map.get(mi) {
                Some(&m) => m,
                None => {
                    let mut mat = s.json["materials"][*mi as usize].clone();
                    let mut copy_texture = |t: u64| -> usize {
                        if let Some(&n) = texture_map.get(&t) {
                            return n;
                        }
                        let mut tex = s.json["textures"][t as usize].clone();
                        if let Some(src) = tex["source"].as_u64() {
                            let img_new = *image_map.entry(src).or_insert_with(|| {
                                let img = &s.json["images"][src as usize];
                                let uri = img["uri"].as_str().unwrap_or("");
                                let data = fs::read(s.dir.join(uri)).unwrap_or_default();
                                let stem = Path::new(uri).file_stem().map(|x| x.to_string_lossy().to_string()).unwrap_or_default();
                                let mime = if uri.to_lowercase().ends_with(".png") { "image/png" } else { "image/jpeg" };
                                images.push(json!({"name": stem}));
                                image_bytes.push((images.len() - 1, data, mime.to_string()));
                                images.len() - 1
                            });
                            tex["source"] = json!(img_new);
                        }
                        if let Some(smp) = tex["sampler"].as_u64() {
                            let smp_new = *sampler_map.entry(smp).or_insert_with(|| {
                                samplers.push(s.json["samplers"][smp as usize].clone());
                                samplers.len() - 1
                            });
                            tex["sampler"] = json!(smp_new);
                        }
                        textures.push(tex);
                        texture_map.insert(t, textures.len() - 1);
                        textures.len() - 1
                    };
                    remap(&mut mat, &mut copy_texture);
                    materials.push(mat);
                    material_map.insert(*mi, materials.len() - 1);
                    materials.len() - 1
                }
            };
            prim.insert("material".into(), json!(new));
        }
        primitives.push(Value::Object(prim));
    }
    for (img, data, mime) in &image_bytes {
        let v = add_view(&mut bin, data, None);
        images[*img]["bufferView"] = json!(v);
        images[*img]["mimeType"] = json!(mime);
    }
    while bin.len() % 4 != 0 {
        bin.push(0);
    }
    let name = dst.file_stem().map(|x| x.to_string_lossy().to_string()).unwrap_or_default();
    let mut doc = json!({
        "asset": {"version": "2.0", "generator": "OpenCity octool combine-gltf"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": primitives}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": bin.len()}],
    });
    for (key, list) in [("materials", materials), ("textures", textures), ("images", images), ("samplers", samplers)] {
        if !list.is_empty() {
            doc[key] = Value::Array(list);
        }
    }
    for key in ["extensionsUsed", "extensionsRequired"] {
        if let Some(v) = s.json.get(key) {
            doc[key] = v.clone();
        }
    }
    let mut text = serde_json::to_vec(&doc).map_err(|e| e.to_string())?;
    while text.len() % 4 != 0 {
        text.push(b' ');
    }
    let total = 12 + 8 + text.len() + 8 + bin.len();
    let mut out = Vec::with_capacity(total);
    out.extend_from_slice(b"glTF");
    out.extend_from_slice(&2u32.to_le_bytes());
    out.extend_from_slice(&(total as u32).to_le_bytes());
    out.extend_from_slice(&(text.len() as u32).to_le_bytes());
    out.extend_from_slice(b"JSON");
    out.extend_from_slice(&text);
    out.extend_from_slice(&(bin.len() as u32).to_le_bytes());
    out.extend_from_slice(b"BIN\0");
    out.extend_from_slice(&bin);
    if let Some(dir) = dst.parent() {
        fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    fs::write(dst, out).map_err(|e| format!("{}: {e}", dst.display()))?;
    let size = [hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]];
    // Report the size in Blender's axis order (x, depth, height), as combine_gltf.py did.
    Ok(format!("COMBINED {kept} mesh(es) -> {}, size ({:.2}, {:.2}, {:.2}) m", dst.display(), size[0], size[2], size[1]))
}
