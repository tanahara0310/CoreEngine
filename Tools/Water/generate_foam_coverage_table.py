"""Generate kFoamLaceMaskForCoverage of WaterFoamAppearance.hlsli.

The lace of the foam is smoothstep(1 - m, 1 - m + s, FoamPattern(x)), where m is the mask
passed to the dissolve. Its area fraction A(m) is not m because FoamPattern is not uniformly
distributed. This script evaluates FoamPattern with the same formula as the shader (float32),
measures A(m), and prints the inverse table m = A^-1(c) for c = (k / (N - 1))^2 (k = 0 .. N - 1),
so that the shader can pass the table value for a coverage c and get exactly the area c.
The entries are spaced in sqrt(c) to resolve the small coverages of whitecaps.

usage: python generate_foam_coverage_table.py [--samples N] [--entries N]
Run it again and paste the output when FoamPattern or kFoamLaceSoftness changes.
"""
import argparse

import numpy as np

LACE_SOFTNESS = np.float32(0.18)  # kFoamLaceSoftness


def foam_hash(px, pz):
    """FoamHash: integer hash of the lattice point (same arithmetic as the shader)."""
    with np.errstate(over="ignore"):
        qx = px.astype(np.int64).astype(np.uint32)
        qz = pz.astype(np.int64).astype(np.uint32)
        h = (qx * np.uint32(0x8DA6B343)) ^ (qz * np.uint32(0xD8163841))
        h ^= h >> np.uint32(16)
        h *= np.uint32(0x7FEB352D)
        h ^= h >> np.uint32(15)
        h *= np.uint32(0x846CA68B)
        h ^= h >> np.uint32(16)
    return ((h >> np.uint32(8)).astype(np.float32) * np.float32(1.0 / 16777216.0)).astype(np.float32)


def value_noise(px, pz):
    ix = np.floor(px)
    iz = np.floor(pz)
    fx = px - ix
    fz = pz - iz
    fx = fx * fx * (np.float32(3.0) - np.float32(2.0) * fx)
    fz = fz * fz * (np.float32(3.0) - np.float32(2.0) * fz)
    a = foam_hash(ix, iz)
    b = foam_hash(ix + np.float32(1.0), iz)
    c = foam_hash(ix, iz + np.float32(1.0))
    d = foam_hash(ix + np.float32(1.0), iz + np.float32(1.0))
    ab = a + (b - a) * fx
    cd = c + (d - c) * fx
    return (ab + (cd - ab) * fz).astype(np.float32)


def foam_pattern(x, z):
    def n(scale):
        s = np.float32(scale)
        return value_noise((x * s).astype(np.float32), (z * s).astype(np.float32))

    fbm = (n(0.65) * np.float32(0.40) + n(1.7) * np.float32(0.25)
           + n(4.1) * np.float32(0.20) + n(9.7) * np.float32(0.15))
    ridge = np.float32(1.0) - np.abs(n(1.1) * np.float32(2.0) - np.float32(1.0))
    ridge = ridge * ridge
    return np.clip(fbm * np.float32(0.65) + ridge * np.float32(0.35), 0.0, 1.0).astype(np.float32)


def smoothstep(e0, e1, v):
    t = np.clip((v - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=4_000_000)
    ap.add_argument("--entries", type=int, default=33)
    ap.add_argument("--extent", type=float, default=2000.0, help="half size of the sampled area [m]")
    a = ap.parse_args()

    rng = np.random.default_rng(20261002)
    x = rng.uniform(-a.extent, a.extent, a.samples).astype(np.float32)
    z = rng.uniform(-a.extent, a.extent, a.samples).astype(np.float32)
    pattern = foam_pattern(x, z).astype(np.float64)

    # Histogram of the pattern, then A(m) on a dense grid of m.
    # The lace reaches 1 everywhere once 1 - m + s <= min(pattern).
    counts, edges = np.histogram(pattern, bins=20000, range=(0.0, 1.0))
    centers = 0.5 * (edges[:-1] + edges[1:])
    weights = counts / counts.sum()
    m_max = 1.0 + float(LACE_SOFTNESS) - pattern.min() + 1.0e-3
    m_grid = np.linspace(0.0, m_max, 20001)
    area = np.array([(weights * smoothstep(1.0 - m, 1.0 - m + float(LACE_SOFTNESS), centers)).sum()
                     for m in m_grid])
    area = np.maximum.accumulate(area)
    area[-1] = 1.0

    targets = np.linspace(0.0, 1.0, a.entries) ** 2
    table = np.interp(targets, area, m_grid)
    table[0] = 0.0

    print("// pattern percentiles 1/50/99%%: %.3f / %.3f / %.3f" % tuple(np.percentile(pattern, [1, 50, 99])))
    for c in (0.001, 0.01, 0.03, 0.07, 0.3):
        k = np.sqrt(c) * (a.entries - 1)
        m = np.interp(k, np.arange(a.entries), table)
        print("// coverage %.3f -> mask %.4f -> area %.4f" % (c, m, np.interp(m, m_grid, area)))
    for m in (0.1, 0.2, 0.3, 0.5):
        print("// old area for mask %.1f: %.4f" % (m, np.interp(m, m_grid, area)))
    print("static const float kFoamLaceMaskForCoverage[%d] = {" % a.entries)
    for i in range(0, a.entries, 8):
        print("    " + ", ".join("%.4ff" % v for v in table[i:i + 8]) + ",")
    print("};")


if __name__ == "__main__":
    main()
