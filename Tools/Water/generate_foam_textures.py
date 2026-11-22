"""Generate the foam pattern textures of the water surface (Engine/Assets/Textures/Water).

Two tileable BC5 textures are written:

  FoamNet.dds    1024 x 1024, one tile = 4 m. Holes of 1 cm to 1 m in a foam raft.
  FoamGrain.dds  512 x 512, one tile = 0.5 m. Holes of 1 mm to 3 cm (the bubbles inside the foam).

Both are made the same way. Holes open at random times in the foam and grow, each at its own speed;
several scales of holes are mixed (more small holes than large ones). The value of a point is the time
when a hole reaches it, plus a little more where two holes meet (the film between them lasts longer).
So when the coverage of foam goes down, small and large holes open, merge, and leave a lace of films.

  R = the order in which the surface turns into foam (the dissolve threshold field).
      Every mip level is replaced by its ranks, so its values are spread uniformly over [0, 1]:
      a coverage c is exactly the area where R > 1 - c, at any distance.
  G = brightness of the foam (thick foam far from the holes is brighter), spread uniformly over [0, 1] on mip 0.

The mips are made here (box filter, blur, then ranks again) and stored in the file, because the engine keeps
the mips of block-compressed DDS files as they are.

The script also prints the constants of WaterFoamAppearance.hlsli that depend on the textures:
the spread of bilinear reads of each mip (to map trilinear reads back to uniform values), the mean change
of R between neighbouring texels of each mip (the width of the dissolve), and the dissolve mask table of
the clustered foam (whitecaps and the shore), whose pattern is the uniform texture value combined with
the cluster field of the shader. It checks the foam area of trilinear reads at quarter lods too.

usage: python generate_foam_textures.py [--out DIR] [--preview DIR] [--samples N]
Run it again and paste the printed constants when the textures change.
"""
import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import generate_foam_coverage_table as coverage  # noqa: E402

F32 = np.float32
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_OUT = os.path.join(REPO, "Project", "Engine", "Assets", "Textures", "Water")

# Holes of each texture: (tile [m], resolution, seed, spacings of the scales [m])
NET = dict(tile=4.0, size=1024, seed=20261122, spacings=[1.0, 0.5, 0.25, 0.12, 0.06, 0.03])
GRAIN = dict(tile=0.5, size=512, seed=20261123, spacings=[0.04, 0.02, 0.01, 0.005])
SPEED_SPREAD = 0.45      # log-normal spread of the growth speed of the holes
OPEN_TIME_SPREAD = 1.4   # holes open at a uniform time in [0, this] (a hole crosses its spacing in about 1)
WARP_FRACTION = 0.06     # the positions are bent by this fraction of the largest spacing, to make the holes uneven
FILM_BONUS = 0.35        # extra time where two holes meet (the film between them lasts longer)
FILM_WIDTH = 0.05        # width of that film, in the time between the two holes reaching the point
FILM_REACH_SCALES = 3    # the coarsest scales look this many buckets around (the rest look 2)
MIP_BLUR_TEXELS = 1.5    # blur of each level before its ranks
AREA_CHECK_COVERAGES = (0.03, 0.1, 0.3, 0.6)

# ---------------------------------------------------------------- periodic helpers


def wrap_delta(d):
    """Shortest difference on the unit torus."""
    return d - np.round(d)


class PeriodicNoise:
    """Value noise on a lattice that repeats every `cells` cells (one tile)."""

    def __init__(self, rng, cells):
        self.cells = cells
        self.values = rng.random((cells, cells)).astype(F32)

    def __call__(self, u, v):
        """u, v in tiles (1 = one tile)."""
        x = u * self.cells
        y = v * self.cells
        ix = np.floor(x).astype(np.int64)
        iy = np.floor(y).astype(np.int64)
        fx = (x - ix).astype(F32)
        fy = (y - iy).astype(F32)
        fx = fx * fx * (3.0 - 2.0 * fx)
        fy = fy * fy * (3.0 - 2.0 * fy)
        c = self.cells
        x0 = ix % c
        x1 = (ix + 1) % c
        y0 = iy % c
        y1 = (iy + 1) % c
        a = self.values[y0, x0]
        b = self.values[y0, x1]
        d = self.values[y1, x0]
        e = self.values[y1, x1]
        top = a + (b - a) * fx
        bottom = d + (e - d) * fx
        return top + (bottom - top) * fy


def periodic_fbm(rng, u, v, base_cells, octaves, gain=0.5):
    total = np.zeros(u.shape, F32)
    norm = 0.0
    amplitude = 1.0
    cells = base_cells
    for _ in range(octaves):
        total += F32(amplitude) * PeriodicNoise(rng, cells)(u, v)
        norm += amplitude
        amplitude *= gain
        cells *= 2
    return total / F32(norm)


# ---------------------------------------------------------------- growing holes


def arrival_two(u, v, centers, open_time, speed, bucket_cells, reach):
    """The two earliest times when a hole reaches each point: open_time + distance / speed (unit torus)."""
    g = bucket_cells
    bucket = np.floor(centers * g).astype(np.int64) % g
    counts = np.zeros((g, g), np.int64)
    np.add.at(counts, (bucket[:, 1], bucket[:, 0]), 1)
    slots = int(counts.max())
    table = np.full((g, g, slots), -1, np.int64)
    fill = np.zeros((g, g), np.int64)
    for index in range(len(centers)):
        bx, by = bucket[index]
        table[by, bx, fill[by, bx]] = index
        fill[by, bx] += 1

    t1 = np.full(u.shape, np.inf)
    t2 = np.full(u.shape, np.inf)
    qx = np.floor(u * g).astype(np.int64)
    qy = np.floor(v * g).astype(np.int64)
    # each bucket once (with few buckets the reach wraps around the tile, and a hole seen twice would make a film)
    offsets = sorted({o % g for o in range(-reach, reach + 1)})
    for oy in offsets:
        for ox in offsets:
            candidates = table[(qy + oy) % g, (qx + ox) % g]
            for s in range(slots):
                index = candidates[..., s]
                valid = index >= 0
                safe = np.where(valid, index, 0)
                dx = wrap_delta(u - centers[safe, 0])
                dy = wrap_delta(v - centers[safe, 1])
                t = np.where(valid, open_time[safe] + np.sqrt(dx * dx + dy * dy) / speed[safe], np.inf)
                closer = t < t1
                second = (~closer) & (t < t2)
                t2 = np.where(closer, t1, np.where(second, t, t2))
                t1 = np.where(closer, t, t1)
    return t1, t2


def make_holes(spec):
    """Returns (order, brightness) of one texture before equalisation."""
    rng = np.random.default_rng(spec["seed"])
    n = spec["size"]
    tile = spec["tile"]
    i = (np.arange(n) + 0.5) / n
    u0, v0 = np.meshgrid(i, i)

    warp = WARP_FRACTION * spec["spacings"][0] / tile
    u = (u0 + warp * (periodic_fbm(rng, u0, v0, base_cells=6, octaves=2) - 0.5) * 2.0) % 1.0
    v = (v0 + warp * (periodic_fbm(rng, u0, v0, base_cells=6, octaves=2) - 0.5) * 2.0) % 1.0

    first = np.full((n, n), np.inf)
    second = np.full((n, n), np.inf)
    for k, spacing in enumerate(spec["spacings"]):
        cell = spacing / tile
        count = max(4, int(round(1.0 / (cell * cell))))
        centers = rng.random((count, 2))
        speed = cell * np.exp(rng.normal(0.0, SPEED_SPREAD, count))
        open_time = rng.uniform(0.0, OPEN_TIME_SPREAD, count)
        reach = 3 if k < FILM_REACH_SCALES else 2
        t1, t2 = arrival_two(u, v, centers, open_time, speed, max(1, int(1.0 / cell)), reach)
        merged = np.stack([first, second, t1, t2])
        merged.sort(axis=0)
        first, second = merged[0], merged[1]

    film = np.exp(-((second - first) / FILM_WIDTH) ** 2)
    film_strength = 0.4 + 0.6 * periodic_fbm(rng, u0, v0, base_cells=8, octaves=2)
    order = first + FILM_BONUS * film * film_strength
    return order.astype(F32), first.astype(F32)


# ---------------------------------------------------------------- mips, equalisation, BC5


def equalize(values):
    flat = values.ravel()
    ranks = np.empty(flat.size, np.float64)
    ranks[np.argsort(flat, kind="stable")] = np.arange(flat.size)
    return ((ranks + 0.5) / flat.size).reshape(values.shape).astype(F32)


def downsample(values):
    return 0.25 * (values[0::2, 0::2] + values[1::2, 0::2] + values[0::2, 1::2] + values[1::2, 1::2])


def blur(values, sigma):
    """Periodic Gaussian blur, sigma in texels of this level."""
    n = values.shape[0]
    f = np.fft.fftfreq(n)
    kernel = np.exp(-2.0 * (np.pi * sigma) ** 2 * (f[:, None] ** 2 + f[None, :] ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(values) * kernel)).astype(F32)


def build_mips(order, brightness):
    """Each level: R = ranks of the blurred box-filtered level above, G = box filter.

    The blur keeps bilinear reads between the texels of a level close to uniform too
    (without it the interpolated values crowd towards 0.5 and the foam area shrinks).
    The top level is not blurred, to keep the thin films between the holes."""
    levels = [(equalize(order), equalize(brightness))]
    while levels[-1][0].shape[0] > 1:
        r, g = levels[-1]
        levels.append((equalize(blur(downsample(r), MIP_BLUR_TEXELS)), downsample(g)))
    return levels


def neighbour_change(values):
    if values.shape[0] < 2:
        return 0.0
    dx = np.abs(np.roll(values, -1, axis=1) - values)
    dy = np.abs(np.roll(values, -1, axis=0) - values)
    return float(0.5 * (dx.mean() + dy.mean()))


def bilinear(values, u, v):
    """Bilinear read with wrapping, u and v in tiles."""
    s = values.shape[0]
    x = u * s - 0.5
    y = v * s - 0.5
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = x - x0
    fy = y - y0
    a = values[y0 % s, x0 % s]
    b = values[y0 % s, (x0 + 1) % s]
    c = values[(y0 + 1) % s, x0 % s]
    d = values[(y0 + 1) % s, (x0 + 1) % s]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def mip_spread(decoded, samples, seed):
    """Per level: (variance of a bilinear read, covariance with the bilinear read of the next level), times 12.

    A uniform value has the variance 1/12, so 1 = uniform. The shader blends the two levels of a trilinear read
    with these and maps the value back to uniform (EqualizeFoamMipRead)."""
    rng = np.random.default_rng(seed)
    u = rng.random(samples)
    v = rng.random(samples)
    reads = [bilinear(level, u, v) for level in decoded]
    spread = []
    for k, read in enumerate(reads):
        variance = 12.0 * float(read.var())
        if k + 1 < len(reads):
            covariance = 12.0 * float(np.mean((read - read.mean()) * (reads[k + 1] - reads[k + 1].mean())))
        else:
            covariance = variance
        spread.append((variance, covariance))
    return spread, reads


def equalize_uniform_sum(x, a, b):
    """Same as EqualizeUniformSum of WaterFoamAppearance.hlsli (a + b = 1)."""
    lo = min(a, b)
    hi = max(a, b)
    if lo < 1.0e-3:
        return x
    y = 1.0 - x
    return np.where(x < lo, x * x / (2.0 * a * b),
                    np.where(x > hi, 1.0 - y * y / (2.0 * a * b), (x - 0.5 * lo) / hi))


def equalize_mip_read(value, lod, spread):
    """Same as EqualizeFoamMipRead of WaterFoamAppearance.hlsli."""
    last = len(spread) - 2
    level = min(int(np.floor(lod)), last)
    f = min(lod - level, 1.0)
    variance = ((1 - f) ** 2 * spread[level][0] + f * f * spread[level + 1][0]
                + 2 * f * (1 - f) * spread[level][1])
    b = 0.5 * (1.0 - np.sqrt(max(2.0 * variance - 1.0, 0.0)))
    if b < 1.0e-3:
        return value
    return equalize_uniform_sum(value, 1.0 - b, b)


def area_error(reads, spread, levels_checked):
    """Foam area of a trilinear read (after EqualizeFoamMipRead) at quarter lods, for a few coverages."""
    rows = []
    for quarter in range(0, levels_checked * 4 + 1):
        lod = quarter / 4.0
        level = min(int(lod), len(reads) - 2)
        f = lod - level
        value = reads[level] * (1 - f) + reads[level + 1] * f
        value = equalize_mip_read(value, lod, spread)
        areas = []
        for c in AREA_CHECK_COVERAGES:
            center = 1.0 - c
            t = np.clip((value - (center - 0.04)) / 0.08, 0.0, 1.0)
            areas.append(float((t * t * (3.0 - 2.0 * t)).mean()))
        rows.append((lod, areas))
    return rows


def encode_bc4(channel):
    """BC4 UNORM blocks (8-value mode: e0 = max > e1 = min) of a 2D channel in [0, 1]."""
    h, w = channel.shape
    bh = max(1, (h + 3) // 4)
    bw = max(1, (w + 3) // 4)
    padded = np.pad(channel, ((0, bh * 4 - h), (0, bw * 4 - w)), mode="edge")
    blocks = padded.reshape(bh, 4, bw, 4).transpose(0, 2, 1, 3).reshape(bh, bw, 16).astype(np.float64)
    e0 = np.clip(np.round(blocks.max(axis=2) * 255.0), 0, 255)
    e1 = np.clip(np.round(blocks.min(axis=2) * 255.0), 0, 255)
    same = e0 <= e1
    e1 = np.where(same & (e0 > 0), e0 - 1, e1)
    e0 = np.where(same & (e0 == 0), 1, e0)
    lo = e1[..., None] / 255.0
    hi = e0[..., None] / 255.0
    t = np.clip((blocks - lo) / (hi - lo), 0.0, 1.0)
    q = np.round(t * 7.0).astype(np.uint64)  # 7 = e0, 0 = e1
    index = np.where(q == 7, 0, np.where(q == 0, 1, 8 - q)).astype(np.uint64)
    bits = np.zeros((bh, bw), np.uint64)
    for k in range(16):
        bits |= index[..., k] << np.uint64(3 * k)
    out = np.zeros((bh, bw, 8), np.uint8)
    out[..., 0] = e0.astype(np.uint8)
    out[..., 1] = e1.astype(np.uint8)
    for k in range(6):
        out[..., 2 + k] = ((bits >> np.uint64(8 * k)) & np.uint64(0xFF)).astype(np.uint8)
    return out


def decode_bc4(blocks, h, w):
    e0 = blocks[..., 0].astype(np.float64)
    e1 = blocks[..., 1].astype(np.float64)
    bits = np.zeros(blocks.shape[:2], np.uint64)
    for k in range(6):
        bits |= blocks[..., 2 + k].astype(np.uint64) << np.uint64(8 * k)
    palette = [e0, e1] + [((7 - j) * e0 + j * e1) / 7.0 for j in range(1, 7)]
    palette = np.stack(palette, axis=-1) / 255.0
    texels = np.zeros(blocks.shape[:2] + (16,))
    for k in range(16):
        idx = ((bits >> np.uint64(3 * k)) & np.uint64(7)).astype(np.int64)
        texels[..., k] = np.take_along_axis(palette, idx[..., None], axis=-1)[..., 0]
    bh, bw = blocks.shape[:2]
    return texels.reshape(bh, bw, 4, 4).transpose(0, 2, 1, 3).reshape(bh * 4, bw * 4)[:h, :w]


def write_bc5_dds(path, levels):
    data = bytearray()
    for r, g in levels:
        data += np.concatenate([encode_bc4(r), encode_bc4(g)], axis=2).tobytes()
    h, w = levels[0][0].shape
    top_size = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000  # CAPS HEIGHT WIDTH PIXELFORMAT MIPMAPCOUNT LINEARSIZE
    header = struct.pack("<4s7I44x", b"DDS ", 124, flags, h, w, top_size, 0, len(levels))
    pixel_format = struct.pack("<2I4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    caps = struct.pack("<4I4x", 0x1000 | 0x400000 | 0x8, 0, 0, 0)  # TEXTURE MIPMAP COMPLEX
    dx10 = struct.pack("<5I", 83, 3, 0, 1, 0)  # DXGI_FORMAT_BC5_UNORM, TEXTURE2D
    with open(path, "wb") as f:
        f.write(header + pixel_format + caps + dx10 + bytes(data))


def read_bc5_dds(path):
    """Mip 0 of a file written by write_bc5_dds, decoded to (R, G)."""
    raw = open(path, "rb").read()
    h, w = struct.unpack_from("<2I", raw, 12)
    bh, bw = (h + 3) // 4, (w + 3) // 4
    blocks = np.frombuffer(raw, np.uint8, bh * bw * 16, 148).reshape(bh, bw, 16)
    return decode_bc4(blocks[..., :8], h, w), decode_bc4(blocks[..., 8:], h, w)


def save_preview(path, image):
    from PIL import Image
    Image.fromarray(np.clip(image * 255.0 + 0.5, 0, 255).astype(np.uint8)).save(path)


def preview(directory, name, levels):
    r, g = levels[0]
    tiled = np.tile(r, (2, 2))
    save_preview(os.path.join(directory, name + "_R.png"), tiled)
    save_preview(os.path.join(directory, name + "_G.png"), np.tile(g, (2, 2)))
    for c in (0.1, 0.3, 0.6, 0.9):
        lace = np.clip((tiled - (1.0 - c)) / 0.04 + 0.5, 0.0, 1.0)
        save_preview(os.path.join(directory, "%s_c%02d.png" % (name, int(c * 100))), lace)


# ---------------------------------------------------------------- constants of the shader


def cluster_mask_table(samples, entries):
    """Dissolve mask for a coverage c when the pattern is the clustered uniform value (one row)."""
    rng = np.random.default_rng(20261122)
    x = rng.uniform(-3000.0, 3000.0, samples).astype(F32)
    z = rng.uniform(-3000.0, 3000.0, samples).astype(F32)
    uniform = rng.random(samples).astype(F32)
    cluster = coverage.cluster_field(x, z)
    pattern = np.clip(cluster * (F32(0.3) + F32(0.7) * uniform) + F32(0.2) * uniform, 0.0, 1.0).astype(F32)
    return coverage.inverse_table(pattern, entries)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--preview", default="")
    ap.add_argument("--samples", type=int, default=2_000_000)
    ap.add_argument("--entries", type=int, default=33)
    ap.add_argument("--spread-samples", type=int, default=400_000)
    ap.add_argument("--skip-table", action="store_true")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    spreads = []
    changes = []
    for name, spec in (("FoamNet", NET), ("FoamGrain", GRAIN)):
        order, brightness = make_holes(spec)
        levels = build_mips(order, brightness)
        path = os.path.join(a.out, name + ".dds")
        write_bc5_dds(path, levels)
        if a.preview:
            os.makedirs(a.preview, exist_ok=True)
            preview(a.preview, name, levels)
        top, _ = read_bc5_dds(path)
        error = np.abs(top - levels[0][0])
        print("// %s: BC5 error of R (mean / max) %.4f / %.4f" % (name, error.mean(), error.max()))
        decoded = [decode_bc4(encode_bc4(r), r.shape[0], r.shape[1]) for r, _ in levels]
        spread, reads = mip_spread(decoded, a.spread_samples, spec["seed"])
        spreads.append(spread)
        changes.append([neighbour_change(r) for r in decoded])
        print("// %s: foam area of a trilinear read at coverage %s" % (
            name, " / ".join("%.2f" % c for c in AREA_CHECK_COVERAGES)))
        for lod, areas in area_error(reads, spread, len(levels) - 3):
            print("//   lod %5.2f  %s" % (lod, " / ".join("%.3f" % x for x in areas)))

    count = max(len(s) for s in spreads)
    print("/// @brief 泡の模様の画像のミップごとの、双一次で読んだ R の分散と次のミップとの共分散（一様な値の分散 1/12 を 1 とする）")
    print("/// @details [0] = 網目・[1] = 気泡の粒。足りない段は最後の段と同じ")
    print("static const float2 kFoamTextureMipSpread[2][%d] = {" % count)
    for spread in spreads:
        padded = spread + [spread[-1]] * (count - len(spread))
        print("    {")
        for i in range(0, count, 4):
            print("        " + ", ".join("float2(%.4ff, %.4ff)" % s for s in padded[i:i + 4]) + ",")
        print("    },")
    print("};")
    print("/// @brief 泡の模様の画像のミップごとの、隣の画素との R の差の平均")
    print("static const float kFoamTextureMipChange[2][%d] = {" % count)
    for change in changes:
        padded = change + [change[-1]] * (count - len(change))
        print("    { " + ", ".join("%.3ff" % c for c in padded) + " },")
    print("};")

    if not a.skip_table:
        table = cluster_mask_table(a.samples, a.entries)
        print("/// @brief 塊の場を掛けた泡の画像の模様で、被覆率 c に対する dissolve のマスク（列は √c の等間隔・c = (k / 32)²）")
        print("static const float kFoamTextureClusterMask[%d] = {" % a.entries)
        for i in range(0, a.entries, 8):
            print("    " + ", ".join("%.4ff" % v for v in table[i:i + 8]) + ",")
        print("};")


if __name__ == "__main__":
    main()
