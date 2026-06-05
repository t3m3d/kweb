#!/usr/bin/env python3
"""Auto-trim the white/transparent border off a sponsor-ad image.

Finds the solid art block by purple-density (works even when the art is a
small box floating in a big white/transparent canvas), then shaves a few
extra pixels so no light anti-alias rim survives on the dark site.

Usage: trim_ad.py <src.png> <out.png> [inset]
"""
import sys
from PIL import Image

def main():
    if len(sys.argv) < 3:
        sys.exit("usage: trim_ad.py <src.png> <out.png> [inset]")
    src, out = sys.argv[1], sys.argv[2]
    inset = int(sys.argv[3]) if len(sys.argv) > 3 else 9

    im = Image.open(src).convert("RGBA")
    W, H = im.size
    px = im.load()

    def light(x, y):
        r, g, b, a = px[x, y]
        return a < 24 or min(r, g, b) >= 205

    def dark(x, y):
        return not light(x, y)

    # pass 1: rough bbox of any dark (art) pixel
    xs = [x for x in range(W) if any(dark(x, y) for y in range(H))]
    ys = [y for y in range(H) if any(dark(x, y) for x in range(W))]
    if not xs or not ys:
        sys.exit("trim_ad: no art found (image all light/transparent?)")
    l0, r0, t0, b0 = min(xs), max(xs), min(ys), max(ys)

    # pass 2: keep only cols/rows that are mostly art within the rough box,
    # dropping stray edge pixels and uneven baked-in frames
    TH = 0.5
    cols = [x for x in range(l0, r0 + 1)
            if sum(dark(x, y) for y in range(t0, b0 + 1)) / (b0 - t0 + 1) > TH]
    rows = [y for y in range(t0, b0 + 1)
            if sum(dark(x, y) for x in range(l0, r0 + 1)) / (r0 - l0 + 1) > TH]
    if not cols or not rows:
        sys.exit("trim_ad: density pass found no solid art band")
    l, r, t, b = min(cols), max(cols), min(rows), max(rows)

    # shave the anti-alias rim
    l += inset; t += inset; r -= inset; b -= inset
    if r <= l or b <= t:
        sys.exit("trim_ad: inset too large for this image")

    crop = im.crop((l, t, r + 1, b + 1))
    crop.save(out)
    print(f"trim_ad: {W}x{H} -> {crop.size[0]}x{crop.size[1]} (inset {inset})")

if __name__ == "__main__":
    main()
