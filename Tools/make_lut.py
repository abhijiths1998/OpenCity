"""Write the Marevia colour-grade LUT (ART_BIBLE.md): SourceArt/LUT/T_LUT_Marevia.png.

    python3 -I Tools/make_lut.py

Unreal's 256 x 16 LUT strip (16 slices of 16 x 16, blue across slices). The grade: a mild
S-curve, shadows a touch cool, highlights warm (Mediterranean sun), saturation +6%. Applied by
the map's post-process volume (oc_m3_world.py) after oc_art_materials.py imports it.
"""

import os

import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "SourceArt/LUT/T_LUT_Marevia.png")


def grade(rgb):
    lum = rgb @ np.array([0.2126, 0.7152, 0.0722])
    s = rgb + (rgb - lum[..., None]) * 0.06                     # saturation +6%
    s = np.clip(s, 0, 1)
    s = s + 0.06 * (s - 0.5) * (1 - np.abs(2 * s - 1))           # gentle S-curve around mid-grey
    w = np.clip(lum, 0, 1)[..., None]
    shadows = np.array([0.985, 1.0, 1.03])                      # cool shadows
    highlights = np.array([1.035, 1.0, 0.95])                   # warm highlights
    s = s * (shadows * (1 - w) + highlights * w)
    return np.clip(s, 0, 1)


def main():
    n = 16
    r, g, b = np.meshgrid(np.arange(n), np.arange(n), np.arange(n), indexing="ij")
    rgb = np.stack([r, g, b], -1) / (n - 1.0)
    out = grade(rgb)
    img = np.zeros((n, n * n, 3), dtype=np.uint8)
    for bi in range(n):
        for gi in range(n):
            for ri in range(n):
                img[gi, bi * n + ri] = np.round(out[ri, gi, bi] * 255)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    Image.fromarray(img).save(OUT)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
