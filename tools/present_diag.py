#!/usr/bin/env python3
"""Turn f117run's F117_PRESENT_DIAG output into side-by-side PNGs and class tables.

  F117_PRESENT_DIAG=DIR f117run ... --present interp --present-scale 4 --shots EVERY:PREFIX
  py tools/present_diag.py NAME... --frames DIR --shots PREFIXDIR [--rect x0,y0,x1,y1] [--n 4] [--out DIR]

NAME is a shot's base name (SHOTS/NAME.ppm, FRAMES/NAME.tag, .diag.ppm, .txt). The rectangle is the
3-D window in original (320x200) pixels, x1 and y1 exclusive (the view is 0,0,320,91). Prints each
class's share of the window and the colour changes that lie on the coarse cell grid (about 1 in N for
a refined edge, nearly all of them for scaled pixels), and writes NAME.png: the presented picture beside
the origin picture, and NAME_win.png: the window only. Origins are named in src/present/diagtag.h.
"""
import re, sys, os, json
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ENUM = open(os.path.join(HERE, "..", "src", "present", "diagtag.h")).read()
FRAMES = SHOTS = "."
body = ENUM[ENUM.index("enum {"):ENUM.index("DT_COUNT")]
NAMES = re.findall(r"\b(DT_[A-Z_0-9]+)\b(?=\s*[,/])", body)
NAMES = [n for n in NAMES]
assert NAMES[0] == "DT_SEED"
CLASS = {}
for n in NAMES:
    c = "g"
    if n in ("DT_MODEL_SOLID", "DT_MODEL_PATTERN", "DT_MODEL_NEAR"): c = "a"
    elif n == "DT_SPANPOLY_FINE": c = "a2"
    elif n == "DT_SEED": c = "e0"
    elif n.startswith("DT_FB_") or n in ("DT_SEAM", "DT_GUARD"): c = "b"
    elif n in ("DT_SPAN_REJECT", "DT_SPAN_BEYOND"): c = "b2"
    elif n in ("DT_SPAN_THIN", "DT_HUD_FINE", "DT_OUTLINE_FINE", "DT_TICKS_FINE"): c = "c"
    elif n == "DT_TEXT": c = "d"
    elif n in ("DT_SPRITE", "DT_SPRITE_RLE", "DT_BLIT_UNHELD"): c = "e"
    elif n in ("DT_BLIT_COARSE", "DT_PAGECOPY"): c = "f"
    CLASS[n] = c
CLASS_TITLE = {
    "a": "(a) refined model polygon, fine grid", "a2": "(a2) refined span-rasteriser polygon (sky/ground/map regions)",
    "b": "(b) model polygon kept as scaled copy (+ seam/guard repairs)", "b2": "(b2) span-rasteriser polygon kept scaled",
    "c": "(c) fine stroke (HUD, outline, ticks, thin strip)", "d": "(d) scaled text",
    "e0": "(e0) static cockpit art left from the seed (scaled copy, never redrawn)", "e": "(e) scaled sprite / cockpit & map artwork", "f": "(f) page copy / coarse blit",
    "g": "(g) anything else"}


def read_ppm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        return np.frombuffer(f.read(), np.uint8).reshape(h, w, 3)


def load(name, n):
    w, h = 320 * n, 200 * n
    tag = np.fromfile(f"{FRAMES}/{name}.tag", np.uint8).reshape(h, w)
    img = read_ppm(f"{SHOTS}/{name}.ppm")
    dg = read_ppm(f"{FRAMES}/{name}.diag.ppm")
    info = open(f"{FRAMES}/{name}.txt").read().split("\n")
    return tag, img, dg, info


def stats(tag, rect, n):
    x0, y0, x1, y1 = rect
    t = tag[y0 * n:y1 * n, x0 * n:x1 * n]
    total = t.size
    per = np.bincount(t.ravel(), minlength=len(NAMES))
    cls = {}
    for i, nm in enumerate(NAMES):
        cls[CLASS[nm]] = cls.get(CLASS[nm], 0) + int(per[i])
    return total, per, cls


def stairs(tag, img, rect, n):
    """Boundaries between different colours that lie exactly on a coarse cell line.
    A boundary that runs along a cell line over a whole cell length is what a
    stair step is made of; a refined slanted edge touches the grid only 1 in n."""
    x0, y0, x1, y1 = rect
    I = img[y0 * n:y1 * n, x0 * n:x1 * n].astype(np.int32)
    I = (I[..., 0] << 16) | (I[..., 1] << 8) | I[..., 2]
    T = tag[y0 * n:y1 * n, x0 * n:x1 * n]
    H, W = I.shape
    res = {}
    tot = 0
    aligned = 0
    # vertical boundaries between x and x+1: pixel pairs (left, right)
    dv = I[:, :-1] != I[:, 1:]
    xs = (np.arange(W - 1) + 1) % n == 0
    # horizontal boundaries
    dh = I[:-1, :] != I[1:, :]
    ys = (np.arange(H - 1) + 1) % n == 0
    for d, onx, tl, tr in ((dv, xs[None, :], T[:, :-1], T[:, 1:]), (dh, ys[:, None], T[:-1, :], T[1:, :])):
        tot += int(d.sum())
        a = d & onx
        aligned += int(a.sum())
        for side in (tl, tr):
            cnt_all = np.bincount(side[d], minlength=len(NAMES))
            cnt_al = np.bincount(side[a], minlength=len(NAMES))
            for i in range(len(NAMES)):
                if cnt_all[i]:
                    r = res.setdefault(NAMES[i], [0, 0])
                    r[0] += int(cnt_all[i]); r[1] += int(cnt_al[i])
    return tot, aligned, res


def main():
    global FRAMES, SHOTS
    args = sys.argv[1:]
    n = 4
    rect = None
    out = "png"
    names = []
    i = 0
    while i < len(args):
        if args[i] == "--rect": rect = tuple(map(int, args[i + 1].split(","))); i += 2
        elif args[i] == "--n": n = int(args[i + 1]); i += 2
        elif args[i] == "--out": out = args[i + 1]; i += 2
        elif args[i] == "--frames": FRAMES = args[i + 1]; i += 2
        elif args[i] == "--shots": SHOTS = args[i + 1]; i += 2
        else: names.append(args[i]); i += 1
    os.makedirs(out, exist_ok=True)
    allres = {}
    for nm in names:
        tag, img, dg, info = load(nm, n)
        r = rect or (0, 0, 320, 200)
        total, per, cls = stats(tag, r, n)
        tot, al, st = stairs(tag, img, r, n)
        side = np.concatenate([img, dg], axis=1)
        Image.fromarray(side).save(f"{out}/{nm}.png")
        # a zoom of the window's centre third, at 3x of N=4 -> readable crops
        x0, y0, x1, y1 = r
        crop = lambda a: a[y0 * n:y1 * n, x0 * n:x1 * n]
        Image.fromarray(np.concatenate([crop(img), crop(dg)], axis=1)).save(f"{out}/{nm}_win.png")
        allres[nm] = dict(info=info[:2], total=total, per={NAMES[k]: int(per[k]) for k in range(len(NAMES)) if per[k]},
                          cls=cls, boundary_pixels=tot, aligned=al,
                          stair={k: v for k, v in st.items()})
        print(nm, info[1])
        for c in ("a", "a2", "b", "b2", "c", "d", "e0", "e", "f", "g"):
            if cls.get(c): print(f"   {CLASS_TITLE[c]:75s} {100.0 * cls[c] / total:6.2f}%")
        print(f"   boundary pixels {tot}, on a cell line {al} ({100.0 * al / max(tot, 1):.1f}%; refined edges would give about {100.0 / n:.0f}%)")
    json.dump(allres, open(f"{out}/stats.json", "w"), indent=1)


if __name__ == "__main__":
    main()


SCENE = {"a", "a2", "b", "b2"}


def runs_of(mask):
    """(start, length) of True runs along the last axis, for every row."""
    out = []
    for r in range(mask.shape[0]):
        row = mask[r]
        if not row.any(): continue
        d = np.diff(np.concatenate([[0], row.view(np.int8), [0]]))
        s = np.nonzero(d == 1)[0]; e = np.nonzero(d == -1)[0]
        for a, b in zip(s, e): out.append((r, a, b - a))
    return out


def cell_stairs(tag, img, rect, n):
    """Edges between two scene polygons that sit exactly on the coarse cell grid: a run of
    colour change along a cell line that starts on a cell corner and is a whole number of cells
    long. A refined slanted edge has no such runs; a scaled copy is made of nothing else.
    Returns (scene boundary pixels, stair pixels, {pair: stair pixels})."""
    x0, y0, x1, y1 = rect
    I = img[y0 * n:y1 * n, x0 * n:x1 * n].astype(np.int32)
    I = (I[..., 0] << 16) | (I[..., 1] << 8) | I[..., 2]
    T = tag[y0 * n:y1 * n, x0 * n:x1 * n]
    cl = np.array([CLASS[nm] in SCENE for nm in NAMES])
    sc = cl[T]
    H, W = I.shape
    pairs = {}
    scene_b = 0
    stair = 0
    # horizontal boundary between row y and y+1 (a cell line when (y+1)%n==0), runs along x
    dh = (I[:-1] != I[1:]) & sc[:-1] & sc[1:]
    scene_b += int(dh.sum())
    al = dh & (((np.arange(H - 1) + 1) % n == 0)[:, None])
    for r, a, L in runs_of(al):
        if a % n == 0 and L % n == 0:
            stair += L
            k = tuple(sorted((NAMES[T[r, a]], NAMES[T[r + 1, a]])))
            pairs[k] = pairs.get(k, 0) + L
    dv = (I[:, :-1] != I[:, 1:]) & sc[:, :-1] & sc[:, 1:]
    scene_b += int(dv.sum())
    al = dv & (((np.arange(W - 1) + 1) % n == 0)[None, :])
    for c, a, L in runs_of(al.T):
        if a % n == 0 and L % n == 0:
            stair += L
            k = tuple(sorted((NAMES[T[a, c]], NAMES[T[a, c + 1]])))
            pairs[k] = pairs.get(k, 0) + L
    return scene_b, stair, pairs
