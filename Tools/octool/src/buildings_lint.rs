//! `octool buildings-lint [layout.json]`: checks the building footprints in the Marevia layout without the
//! editor. Footprints are oriented rectangles (pos = centre, size = width x depth, yaw in degrees, counter-
//! clockwise, x east / y north, metres). Fails on:
//! - overlaps deeper than 5 cm (two buildings in the same volume z-fight and clip),
//! - gaps between 5 cm and 2.5 m (a dark slit between two houses: either share the wall or leave an alley).
//! Reports touching pairs (shared walls) and the gap histogram per district style.

use serde_json::Value;
use std::collections::HashMap;
use std::path::Path;

const TOUCH: f64 = 0.05;
const ALLEY: f64 = 2.5;
const CELL: f64 = 25.0;

struct Rect {
    c: [f64; 2],
    corners: [[f64; 2]; 4],
    radius: f64,
    style: usize,
    landmark: bool,
}

fn rect(b: &Value) -> Option<Rect> {
    if b.get("on").is_some() {
        return None; // a recessed top storey stands on its own building, not on the ground
    }
    let pos = b.get("pos")?.as_array()?;
    let size = b.get("size")?.as_array()?;
    let (x, y) = (pos.first()?.as_f64()?, pos.get(1)?.as_f64()?);
    let (w, d) = (size.first()?.as_f64()?, size.get(1)?.as_f64()?);
    let yaw = b.get("yaw")?.as_f64()?.to_radians();
    let (u, v) = ([yaw.cos(), yaw.sin()], [-yaw.sin(), yaw.cos()]);
    let mut corners = [[0.0; 2]; 4];
    for (i, (su, sv)) in [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)].iter().enumerate() {
        corners[i] = [x + u[0] * su * w / 2.0 + v[0] * sv * d / 2.0, y + u[1] * su * w / 2.0 + v[1] * sv * d / 2.0];
    }
    Some(Rect {
        c: [x, y],
        corners,
        radius: 0.5 * (w * w + d * d).sqrt(),
        style: b.get("style").and_then(Value::as_u64).unwrap_or(0) as usize,
        landmark: b.get("landmark").is_some(),
    })
}

fn sub(a: [f64; 2], b: [f64; 2]) -> [f64; 2] {
    [a[0] - b[0], a[1] - b[1]]
}
fn dot(a: [f64; 2], b: [f64; 2]) -> f64 {
    a[0] * b[0] + a[1] * b[1]
}

fn point_segment(p: [f64; 2], a: [f64; 2], b: [f64; 2]) -> f64 {
    let ab = sub(b, a);
    let t = (dot(sub(p, a), ab) / dot(ab, ab).max(1e-12)).clamp(0.0, 1.0);
    let q = [a[0] + ab[0] * t, a[1] + ab[1] * t];
    dot(sub(p, q), sub(p, q)).sqrt()
}

/// Separation (> 0, the gap) or penetration (< 0, the smallest push apart) between two rectangles.
fn separation(a: &Rect, b: &Rect) -> f64 {
    let mut best_sep = f64::NEG_INFINITY; // largest separating gap along an axis
    for r in [a, b] {
        for i in 0..2 {
            let e = sub(r.corners[i + 1], r.corners[i]);
            let len = dot(e, e).sqrt();
            let n = [-e[1] / len, e[0] / len];
            let (mut a0, mut a1, mut b0, mut b1) = (f64::MAX, f64::MIN, f64::MAX, f64::MIN);
            for p in a.corners {
                a0 = a0.min(dot(p, n));
                a1 = a1.max(dot(p, n));
            }
            for p in b.corners {
                b0 = b0.min(dot(p, n));
                b1 = b1.max(dot(p, n));
            }
            best_sep = best_sep.max((b0 - a1).max(a0 - b1));
        }
    }
    if best_sep < 0.0 {
        return best_sep;
    }
    let mut d = f64::MAX;
    for (p, q) in [(a, b), (b, a)] {
        for c in p.corners {
            for i in 0..4 {
                d = d.min(point_segment(c, q.corners[i], q.corners[(i + 1) % 4]));
            }
        }
    }
    d
}

pub fn lint(path: &Path) -> Result<(), String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let root: Value = serde_json::from_str(&text).map_err(|e| e.to_string())?;
    let styles: Vec<String> = root["building_styles"].as_array().map(|a| a.iter().filter_map(|s| s.as_str().map(String::from)).collect()).unwrap_or_default();
    let rects: Vec<Rect> = root["buildings"].as_array().ok_or("no buildings")?.iter().filter_map(rect).collect();
    let mut grid: HashMap<(i64, i64), Vec<usize>> = HashMap::new();
    for (i, r) in rects.iter().enumerate() {
        let (x0, x1) = (((r.c[0] - r.radius) / CELL).floor() as i64, ((r.c[0] + r.radius) / CELL).floor() as i64);
        let (y0, y1) = (((r.c[1] - r.radius) / CELL).floor() as i64, ((r.c[1] + r.radius) / CELL).floor() as i64);
        for gx in x0..=x1 {
            for gy in y0..=y1 {
                grid.entry((gx, gy)).or_default().push(i);
            }
        }
    }
    let n_styles = styles.len().max(1);
    let (mut overlaps, mut slivers, mut touching) = (Vec::new(), Vec::new(), 0usize);
    let mut with_sliver = vec![false; rects.len()];
    let mut per_style = vec![(0usize, 0usize); n_styles]; // (buildings, with a sliver neighbour)
    let mut seen = std::collections::HashSet::new();
    for cell in grid.values() {
        for (k, &i) in cell.iter().enumerate() {
            for &j in &cell[k + 1..] {
                let (i, j) = (i.min(j), i.max(j));
                let (a, b) = (&rects[i], &rects[j]);
                if dot(sub(a.c, b.c), sub(a.c, b.c)).sqrt() > a.radius + b.radius + ALLEY || !seen.insert((i, j)) {
                    continue;
                }
                let s = separation(a, b);
                if s < -TOUCH {
                    overlaps.push((i, j, -s));
                } else if s <= TOUCH {
                    touching += 1;
                } else if s < ALLEY {
                    slivers.push((i, j, s));
                    with_sliver[i] = true;
                    with_sliver[j] = true;
                }
            }
        }
    }
    for (i, r) in rects.iter().enumerate() {
        let st = r.style.min(n_styles - 1);
        per_style[st].0 += 1;
        per_style[st].1 += with_sliver[i] as usize;
    }
    let mut hist = [0usize; 5]; // gap: <0.5, <1, <1.5, <2, <2.5 m
    for &(_, _, s) in &slivers {
        hist[((s / 0.5) as usize).min(4)] += 1;
    }
    println!("buildings: {} ({} landmarks)", rects.len(), rects.iter().filter(|r| r.landmark).count());
    println!("shared walls (touching within {TOUCH} m): {touching} pairs");
    println!("overlaps deeper than {TOUCH} m: {}", overlaps.len());
    println!("gaps between {TOUCH} and {ALLEY} m: {} pairs; buildings with one: {}", slivers.len(), with_sliver.iter().filter(|&&b| b).count());
    println!("  gap histogram  <0.5 m {}  <1 m {}  <1.5 m {}  <2 m {}  <2.5 m {}", hist[0], hist[1], hist[2], hist[3], hist[4]);
    for (s, (n, bad)) in per_style.iter().enumerate() {
        if *n > 0 {
            println!("  {:<9} {:>5} buildings, {:>5} with a gap ({:.0}%)", styles.get(s).map(String::as_str).unwrap_or("?"), n, bad, 100.0 * *bad as f64 / *n as f64);
        }
    }
    overlaps.sort_by(|a, b| b.2.total_cmp(&a.2));
    for &(i, j, d) in overlaps.iter().take(10) {
        println!("  overlap {d:.2} m: #{i} at ({:.1}, {:.1}) and #{j} at ({:.1}, {:.1})", rects[i].c[0], rects[i].c[1], rects[j].c[0], rects[j].c[1]);
    }
    if !overlaps.is_empty() || !slivers.is_empty() {
        return Err(format!("{} overlaps, {} gaps under {ALLEY} m", overlaps.len(), slivers.len()));
    }
    println!("BUILDINGS LINT OK");
    Ok(())
}
