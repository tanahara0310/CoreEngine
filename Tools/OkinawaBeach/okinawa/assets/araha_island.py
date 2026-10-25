"""アラハ島（アラハビーチの沖にある琉球石灰岩の小島）

浜と平行に並ぶ 3 つの岩の塊が、水中の土台でつながっている。
どの塊も平均水面のすぐ上が波で削られてくびれ（ノッチ）、その上が張り出す。

座標: z=0 が平均水面、原点は島の中心の真下（海底は z=-2.2）。長手方向が X（約 22 m）。
"""
import math
import random

import numpy as np
from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb

PREVIEW = dict(cam_dir=(0.25, -1.0, 0.32), lens=40)

SEABED = -2.2

# (中心 x, 中心 y, 天辺の高さ, 張り出しの半径, くびれの半径, 乱数)
LUMPS = [
    (0.0, 0.0, 4.4, 4.1, 2.9, 3),
    (-7.6, 1.1, 3.0, 2.9, 1.9, 7),
    (6.7, -0.9, 3.6, 3.3, 2.2, 13),
]


def _profile(h, pts):
    hs, rs = zip(*pts)
    return float(np.interp(h, hs, rs))


def lump(name, cx, cy, top, radius, notch_r, seed, subdiv=6):
    """1 つの塊。海底から立ち上がり、水面の上でくびれてから張り出す"""
    rnd = random.Random(seed)
    off = Vector((rnd.uniform(-50, 50), rnd.uniform(-50, 50), rnd.uniform(-50, 50)))
    base_r = notch_r * 1.35
    prof = [
        (SEABED - 0.3, base_r * 1.25), (SEABED, base_r * 1.2), (-1.0, base_r), (-0.2, notch_r * 1.08),
        (0.45, notch_r), (1.0, notch_r * 1.04 + 0.15), (1.6, radius * 0.8), (top * 0.55 + 0.4, radius * 0.97),
        (top * 0.75, radius), (top * 0.9, radius * 0.86), (top * 0.97, radius * 0.55), (top, 0.0),
    ]
    bm = geo.icosphere_bm(subdiv)
    lean = (rnd.uniform(-0.4, 0.4), rnd.uniform(-0.6, 0.2))
    for v in bm.verts:
        d = v.co.normalized()
        h = SEABED - 0.3 + (d.z + 1.0) * 0.5 * (top - SEABED + 0.3)
        r = _profile(h, prof)
        if h > top * 0.97:
            t = (h - top * 0.97) / (top * 0.03)
            r = radius * 0.5 * math.sqrt(max(0.0, 1.0 - t * t))
        ang = math.atan2(d.y, d.x)
        lobe = 1.0 + 0.22 * geo.fbm(Vector((math.cos(ang) * 1.2, math.sin(ang) * 1.2, h * 0.22)) + off, 3)
        xy = Vector((d.x, d.y))
        if xy.length > 1e-6:
            xy.normalize()
        k = (max(h, 0.0) / top) ** 2
        # 浜と平行（X）にやや長い楕円
        v.co = Vector((cx + xy.x * r * lobe * 1.12 + lean[0] * k, cy + xy.y * r * lobe * 0.88 + lean[1] * k, h))
    obj = C.from_bmesh(name, bm)
    return obj, off


def base_mound(name):
    """塊どうしをつなぐ水中の土台"""
    bm = geo.icosphere_bm(5)
    for v in bm.verts:
        d = v.co.normalized()
        ang = math.atan2(d.y, d.x)
        lobe = 1.0 + 0.18 * geo.fbm(Vector((math.cos(ang), math.sin(ang), 0.0)) * 1.5 + Vector((4, 9, 2)), 3)
        z = SEABED - 0.4 + max(d.z, 0.0) * 1.5 + min(d.z, 0.0) * 0.4
        s = math.sqrt(max(0.0, 1.0 - d.z * d.z)) ** 0.5
        v.co = Vector((d.x / max(1e-6, math.hypot(d.x, d.y)) * s * 11.5 * lobe if (d.x or d.y) else 0.0,
                       d.y / max(1e-6, math.hypot(d.x, d.y)) * s * 5.2 * lobe if (d.x or d.y) else 0.0, z))
    return C.from_bmesh(name, bm)


def island_mesh(name, target_tris=40000):
    parts = []
    for i, (cx, cy, top, radius, notch_r, seed) in enumerate(LUMPS):
        o, _ = lump(f"_lump{i}", cx, cy, top, radius, notch_r, seed)
        parts.append(o)
    parts.append(base_mound("_base"))
    obj = C.join(parts, name)
    # 重なった塊を 1 つの面にまとめる
    m = obj.modifiers.new("Remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = 0.09
    m.adaptivity = 0.0
    C.apply_modifiers(obj)
    geo.smooth_mod(obj, 0.5, 3)

    off = Vector((17.0, -31.0, 5.0))

    def disp(co, n):
        big = geo.fbm(co * 0.3 + off, 3) * 0.32
        karst = (geo.ridged(co * Vector((0.4, 0.4, 0.75)) + off, 4) - 1.0) * 0.2
        strata = 0.05 * math.sin(co.z * 3.5 + geo.fbm(co * 0.4 + off, 2) * 3.0)
        f1, _ = geo.cell(co * 0.7 + off)
        hole = -0.14 * max(0.0, 0.25 - f1) / 0.25
        # ノッチは波で磨かれて滑らか
        notch_w = math.exp(-((co.z - 0.45) / 0.6) ** 2)
        up = max(0.0, n.z)
        # 上面は尖った溶食（カレン）が強い
        return big + (karst * (0.7 + 0.6 * up) + hole + strata) * (1.0 - 0.75 * notch_w)

    import bmesh
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    geo.displace_along_normals(bm, disp)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    geo.smooth_mod(obj, 0.4, 2)
    geo.decimate(obj, target_tris=target_tris)
    C.set_smooth(obj, True, angle=75)
    return obj


def island_material(name="ArahaIsland", res=2048):
    """琉球石灰岩。水面より上は灰ベージュに黒い地衣類、潮間帯は黒く濡れ、水中は藻で緑褐色"""

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        cs = nb.mapping(co, scale=(0.45, 0.45, 0.45))
        big = nb.noise(cs, 0.7, 5, 0.6)
        mid = nb.noise(cs, 3.5, 6, 0.62)
        pits_a = nb.voronoi(cs, 9.0, rand=1.0)
        pits_b = nb.voronoi(nb.mapping(cs, loc=(3.1, 1.7, 0.4)), 23.0, rand=1.0)
        pits = nb.math("MAXIMUM", nb.mul(nb.smooth(pits_a, 0.16, 0.0), 0.9),
                       nb.mul(nb.smooth(pits_b, 0.18, 0.0), 0.6))
        ridge = nb.noise(nb.mapping(cs, scale=(1, 1, 2.2)), 6.0, 8, 0.7, distortion=0.4)
        fine = nb.noise(co, 25.0, 4, 0.6)

        base = nb.ramp(big, [(0.30, srgb("#6f675a")), (0.55, srgb("#857c6b")), (0.75, srgb("#9a907c"))])
        grey = nb.mix(base, srgb("#66625a"), nb.smooth(mid, 0.45, 0.7))
        # 風雨で黒ずんだ上面と地衣類
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        lichen = nb.smooth(nb.noise(cs, 11.0, 4, 0.6), 0.55, 0.68)
        grey = nb.mix(grey, srgb("#3b3a35"), nb.mul(lichen, 0.45))
        grey = nb.mix(grey, srgb("#55524a"), nb.mul(nb.smooth(nz, 0.4, 0.9), nb.mul(nb.smooth(big, 0.4, 0.7), 0.5)))
        # 張り出しの下は雨だれの黒い筋
        streak = nb.noise(nb.mapping(co, scale=(9, 9, 0.6)), 1.0, 4, 0.6)
        grey = nb.mix(grey, srgb("#45423b"), nb.mul(nb.smooth(streak, 0.55, 0.75), 0.4))

        edge = nb.add(z, nb.mul(nb.sub(big, 0.5), 0.8))
        # 潮間帯（しぶきのかかる黒い帯）
        tide = nb.smooth(edge, 1.5, 0.9)
        algae = nb.mix(srgb("#2e2c25"), srgb("#4a4733"), nb.smooth(fine, 0.4, 0.7))
        col = nb.mix(grey, algae, nb.mul(tide, 0.88))
        # 水中は緑褐色の藻
        under = nb.smooth(edge, -0.1, -0.7)
        weed = nb.mix(srgb("#2c3420"), srgb("#4f5a32"), nb.smooth(nb.noise(co, 6.0, 5, 0.6), 0.35, 0.7))
        col = nb.mix(col, weed, nb.mul(under, 0.9))
        # 穴の奥は暗く
        col = nb.mix(col, srgb("#3f3b33"), nb.mul(pits, 0.7))
        # 海底際の砂
        col = nb.mix(col, srgb("#c9bfa2"), nb.mul(nb.smooth(z, SEABED + 0.5, SEABED + 0.1), 0.7))

        wet = nb.smooth(edge, 1.2, 0.3)
        rough = nb.mixf(nb.maprange(fine, 0.3, 0.7, 0.82, 0.95), 0.45, wet)
        height = nb.add(nb.add(nb.mul(pits, -0.6), nb.mul(ridge, 0.5)), nb.mul(fine, 0.15))
        cavity = nb.maprange(pits, 0.0, 1.0, 1.0, 0.55)
        return dict(color=col, rough=rough, height=height, height_scale=0.05, cavity=cavity)

    return pbr_material(name, fn, res=res, ao_samples=64, ao_distance=3.0)


def build():
    obj = island_mesh("ArahaIsland")
    C.assign(obj, island_material())
    return {"ArahaIsland": [obj]}
