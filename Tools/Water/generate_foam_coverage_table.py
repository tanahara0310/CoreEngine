"""Generate the foam pattern constants of WaterFoamAppearance.hlsli.

The lace of the foam is smoothstep(1 - m, 1 - m + s, FoamPattern(x, t)), where m is the mask
passed to the dissolve. Its area fraction A(m) is not m because FoamPattern is not uniformly
distributed. This script evaluates FoamPattern with the same formula as the shader (float32),
over random positions and pattern times, measures A(m), and prints the inverse table
m = A^-1(c) for c = (k / (N - 1))^2 (k = 0 .. N - 1), so that the shader can pass the table value
for a coverage c and get exactly the area c. The entries are spaced in sqrt(c) to resolve the
small coverages of whitecaps.

Far from the camera the shader replaces the finest octaves of the pattern with their means.
The table has one row per quarter step of the fade level (0 .. 3: the number of octaves replaced,
finest first), and the script also prints the octave means and the mean of the whole pattern.

usage: python generate_foam_coverage_table.py [--samples N] [--entries N]
Run it again and paste the output when FoamPattern or its constants change.
"""
import argparse

import numpy as np

U32 = np.uint32
F32 = np.float32

LACE_SOFTNESS = 0.08  # kFoamLaceSoftness
PATTERN_PERIOD = 600.0  # kFoamPatternPeriodSeconds
CELL_JITTER = 0.62  # kFoamCellJitter
CELL_MOTION = 0.12  # kFoamCellMotion
OCTAVES = [
    # kFoamEdgeCellSizes, kFoamEdgeAngles, kFoamEdgeWidths, kFoamEdgeWeights,
    # kFoamEdgeStrengthMin, kFoamEdgeWarps, kFoamEdgeWarpFrequencies, kFoamEdgeSeeds
    dict(cell=1.3, angle=0.43, width=0.28, weight=0.58, strength_min=0.30, warp=0.7, warp_frequency=0.4, seed=11),
    dict(cell=0.45, angle=1.91, width=0.34, weight=0.28, strength_min=0.55, warp=0.4, warp_frequency=1.2, seed=23),
    dict(cell=0.15, angle=3.07, width=0.42, weight=0.14, strength_min=0.80, warp=0.0, warp_frequency=0.0, seed=37),
]
# The rows of the table: fade levels 0, 0.25, .. 3 (how many octaves are replaced by their means, finest first).
ROW_STEPS_PER_LEVEL = 4
ROW_LEVELS = [i / ROW_STEPS_PER_LEVEL for i in range(3 * ROW_STEPS_PER_LEVEL + 1)]
# The edge width of the dissolve is clamped to this (kFoamMaxEdgeWidth).
MAX_EDGE_WIDTH = 0.1


def fades_for_level(level):
    """Fade of each octave (big, mid, fine) at a fade level."""
    return (min(max(level - 2.0, 0.0), 1.0), min(max(level - 1.0, 0.0), 1.0), min(max(level, 0.0), 1.0))


def to_u32(v):
    """asuint(int(v)) of a float holding an integer."""
    return v.astype(np.int64).astype(U32)


def foam_hash(px, pz):
    """FoamHash: integer hash of the lattice point (same arithmetic as the shader)."""
    with np.errstate(over="ignore"):
        h = (to_u32(px) * U32(0x8DA6B343)) ^ (to_u32(pz) * U32(0xD8163841))
        h ^= h >> U32(16)
        h *= U32(0x7FEB352D)
        h ^= h >> U32(15)
        h *= U32(0x846CA68B)
        h ^= h >> U32(16)
    return ((h >> U32(8)).astype(F32) * F32(1.0 / 16777216.0)).astype(F32)


def value_noise(px, pz):
    """FoamValueNoise."""
    ix = np.floor(px)
    iz = np.floor(pz)
    fx = px - ix
    fz = pz - iz
    fx = fx * fx * (F32(3.0) - F32(2.0) * fx)
    fz = fz * fz * (F32(3.0) - F32(2.0) * fz)
    a = foam_hash(ix, iz)
    b = foam_hash(ix + F32(1.0), iz)
    c = foam_hash(ix, iz + F32(1.0))
    d = foam_hash(ix + F32(1.0), iz + F32(1.0))
    ab = a + (b - a) * fx
    cd = c + (d - c) * fx
    return (ab + (cd - ab) * fz).astype(F32)


def noise_at(x, z, frequency, ox, oz):
    return value_noise((x * F32(frequency) + F32(ox)).astype(F32), (z * F32(frequency) + F32(oz)).astype(F32))


def pcg3(x, y, z):
    """FoamPcg3."""
    with np.errstate(over="ignore"):
        x = x * U32(1664525) + U32(1013904223)
        y = y * U32(1664525) + U32(1013904223)
        z = z * U32(1664525) + U32(1013904223)
        x = x + y * z
        y = y + z * x
        z = z + x * y
        x ^= x >> U32(16)
        y ^= y >> U32(16)
        z ^= z >> U32(16)
        x = x + y * z
        y = y + z * x
        z = z + x * y
    return x, y, z


def to_unit(h):
    return (h >> U32(8)).astype(F32) * F32(1.0 / 16777216.0)


def cell_key(cx, cz):
    """FoamCellKey."""
    with np.errstate(over="ignore"):
        return (to_u32(cx) * U32(0x8DA6B343)) ^ (to_u32(cz) * U32(0xD8163841))


def cell_edge(px, pz, t, seed):
    """FoamCellEdge: (F2 - F1, edge hash)."""
    cx = np.floor(px)
    cz = np.floor(pz)
    fx = (px - cx).astype(F32)
    fz = (pz - cz).astype(F32)
    d1 = np.full(px.shape, 8.0, F32)
    d2 = np.full(px.shape, 8.0, F32)
    k1 = np.zeros(px.shape, U32)
    k2 = np.zeros(px.shape, U32)
    jitter_low = F32(0.5 - 0.5 * CELL_JITTER)
    seeds = np.full(px.shape, seed, U32)
    for j in (-1, 0, 1):
        for i in (-1, 0, 1):
            nx = cx + F32(i)
            nz = cz + F32(j)
            hx, hy, hz = pcg3(to_u32(nx), to_u32(nz), seeds)
            ux, uy, uz = to_unit(hx), to_unit(hy), to_unit(hz)
            speed = np.floor(F32(4.0) + uz * F32(8.0))
            phase = F32(2.0 * np.pi) * (speed * t / F32(PATTERN_PERIOD) + uz)
            ptx = F32(i) + jitter_low + F32(CELL_JITTER) * ux + F32(CELL_MOTION) * np.sin(phase)
            ptz = F32(j) + jitter_low + F32(CELL_JITTER) * uy + F32(CELL_MOTION) * np.sin(phase + F32(1.9))
            d = np.sqrt((ptx - fx) ** 2 + (ptz - fz) ** 2).astype(F32)
            key = cell_key(nx, nz)
            closer = d < d1
            second = (~closer) & (d < d2)
            d2 = np.where(closer, d1, np.where(second, d, d2))
            k2 = np.where(closer, k1, np.where(second, key, k2))
            d1 = np.where(closer, d, d1)
            k1 = np.where(closer, key, k1)
    edge_hash, _, _ = pcg3(np.minimum(k1, k2), np.maximum(k1, k2), seeds + U32(7))
    return (d2 - d1).astype(F32), to_unit(edge_hash)


def edge_line(x, z, t, octave):
    """FoamEdgeLine."""
    o = OCTAVES[octave]
    s = F32(np.sin(F32(o["angle"])))
    c = F32(np.cos(F32(o["angle"])))
    px = (c * x - s * z).astype(F32)
    pz = (s * x + c * z).astype(F32)
    if o["warp"] > 0.0:
        wx = noise_at(x, z, o["warp_frequency"], 17.3, -41.1) * F32(2.0) - F32(1.0)
        wz = noise_at(x, z, o["warp_frequency"], -63.7, 29.9) * F32(2.0) - F32(1.0)
        amount = F32(o["warp"] * o["cell"])
        px = (px + amount * wx).astype(F32)
        pz = (pz + amount * wz).astype(F32)
    cell = F32(o["cell"])
    edge, edge_hash = cell_edge((px / cell).astype(F32), (pz / cell).astype(F32), t, o["seed"])
    line = np.clip(F32(1.0) - edge / F32(o["width"]), 0.0, 1.0).astype(F32)
    line = line * line * (F32(3.0) - F32(2.0) * line)
    strength_min = F32(o["strength_min"])
    return (line * (strength_min + (F32(1.0) - strength_min) * edge_hash)).astype(F32)


def cluster_field(x, z):
    """FoamClusterField."""
    noise = (noise_at(x, z, 0.22, 3.1, 7.7) * F32(0.55)
             + noise_at(x, z, 0.55, -5.3, 1.9) * F32(0.30)
             + noise_at(x, z, 1.4, 9.1, 2.2) * F32(0.15))
    return np.clip((noise - F32(0.2)) / F32(0.6), 0.0, 1.0).astype(F32)


def combine(lines, means, fades, cluster):
    """FoamPattern from the octave lines (lines[k] may be None when fades[k] == 1)."""
    net = np.zeros(cluster.shape, F32)
    for k, o in enumerate(OCTAVES):
        value = F32(means[k]) if fades[k] >= 1.0 else lines[k] + (F32(means[k]) - lines[k]) * F32(fades[k])
        net = net + F32(o["weight"]) * value
    return np.clip(cluster * (F32(0.3) + F32(0.7) * net) + F32(0.2) * net, 0.0, 1.0).astype(F32)


def smoothstep(e0, e1, v):
    t = np.clip((v - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def area_curve(pattern, width):
    """A(m) on a dense grid of m for the dissolve smoothstep(c - w, c + w, p), c = 1 - m + s/2."""
    counts, edges = np.histogram(pattern.astype(np.float64), bins=20000, range=(0.0, 1.0))
    centers = 0.5 * (edges[:-1] + edges[1:])
    weights = counts / counts.sum()
    m_max = 1.0 + LACE_SOFTNESS + width - pattern.min() + 1.0e-3
    m_grid = np.linspace(0.0, m_max, 20001)
    area = np.empty_like(m_grid)
    for i, m in enumerate(m_grid):
        center = 1.0 - m + 0.5 * LACE_SOFTNESS
        area[i] = (weights * smoothstep(center - width, center + width, centers)).sum()
    area = np.maximum.accumulate(area)
    area[-1] = 1.0
    return m_grid, area


def inverse_table(pattern, entries):
    m_grid, area = area_curve(pattern, 0.5 * LACE_SOFTNESS)
    targets = np.linspace(0.0, 1.0, entries) ** 2
    table = np.interp(targets, area, m_grid)
    table[0] = 0.0
    return table


def table_lookup(table_rows, coverage, level):
    entries = table_rows.shape[1]
    k = np.sqrt(coverage) * (entries - 1)
    position = level * ROW_STEPS_PER_LEVEL
    row = min(int(position), table_rows.shape[0] - 2)
    t = position - row
    a = np.interp(k, np.arange(entries), table_rows[row])
    b = np.interp(k, np.arange(entries), table_rows[row + 1])
    return a + (b - a) * t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=2_000_000)
    ap.add_argument("--entries", type=int, default=33)
    ap.add_argument("--extent", type=float, default=3000.0, help="half size of the sampled area [m]")
    a = ap.parse_args()

    rng = np.random.default_rng(20261111)
    x = rng.uniform(-a.extent, a.extent, a.samples).astype(F32)
    z = rng.uniform(-a.extent, a.extent, a.samples).astype(F32)
    t = rng.uniform(0.0, PATTERN_PERIOD, a.samples).astype(F32)

    lines = [edge_line(x, z, t, k) for k in range(len(OCTAVES))]
    means = [float(line.mean()) for line in lines]
    cluster = cluster_field(x, z)
    patterns = [combine(lines, means, fades_for_level(level), cluster) for level in ROW_LEVELS]
    rows = np.array([inverse_table(p, a.entries) for p in patterns])
    pattern_mean = float(patterns[0].mean())

    # Accuracy checks (printed as comments)
    print("// pattern percentiles 1/50/99%%: %.3f / %.3f / %.3f" % tuple(np.percentile(patterns[0], [1, 50, 99])))
    for level in (0.125, 1.125, 2.375, 2.625, 2.875):
        p = combine(lines, means, fades_for_level(level), cluster)
        m_grid, area = area_curve(p, 0.5 * LACE_SOFTNESS)
        errors = []
        for c in (0.01, 0.03, 0.1, 0.3):
            m = table_lookup(rows, c, level)
            errors.append("%.3f->%.4f" % (c, np.interp(m, m_grid, area)))
        print("// level %.2f area: %s" % (level, " ".join(errors)))
    for width in (0.06, MAX_EDGE_WIDTH):
        m_grid, area = area_curve(patterns[0], width)
        errors = []
        for c in (0.01, 0.03, 0.1, 0.3):
            m = table_lookup(rows, c, 0.0)
            errors.append("%.3f->%.4f" % (c, np.interp(m, m_grid, area)))
        print("// edge width %.2f area: %s" % (width, " ".join(errors)))

    print("// 段ごとの値の平均（遠くでその段を平均へ移すときに使う）")
    print("static const float kFoamEdgeMeans[3] = { %s };" % ", ".join("%.4ff" % v for v in means))
    print("// 模様の値の平均（模様を評価しない遠くで、白濁の変調に使う）")
    print("static const float kFoamPatternMean = %.4ff;" % pattern_mean)
    print("/// @brief 被覆率 c に対する dissolve のマスク。レースの面積の期待値がちょうど c になる")
    print("/// @details 行は平均へ移した段の数の 0.25 刻み（0 行目 = どの段も模様のまま・12 行目 = 3 段とも平均）。")
    print("///          列は √c の等間隔（c = (k / 32)²）")
    print("static const float kFoamLaceMaskForCoverage[%d][%d] = {" % (len(ROW_LEVELS), a.entries))
    for row in rows:
        print("    {")
        for i in range(0, a.entries, 8):
            print("        " + ", ".join("%.4ff" % v for v in row[i:i + 8]) + ",")
        print("    },")
    print("};")


if __name__ == "__main__":
    main()
