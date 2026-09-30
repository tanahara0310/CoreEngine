"""1 ノッチ岩（キノコ岩） / 2 磯の岩（琉球石灰岩）

ノッチ岩: 波の浸食で根元がくびれた沖縄特有の石灰岩。
磯の岩: 潮だまりのできる、平たく尖った凹凸の石灰岩。
"""
import math
import random

import numpy as np
from mathutils import Vector

from .. import common as C
from .. import geo
from ..materials import limestone

PREVIEW = dict(cam_dir=(0.35, -1.0, 0.4), lens=40)


def _profile(h, pts):
    hs, rs = zip(*pts)
    return float(np.interp(h, hs, rs))


def notch_rock(name, height, radius, notch_h, notch_r, stem_r, seed, subdiv=6, target_tris=12000,
               lean=(0.0, 0.0), lobes=0.18):
    """きのこ型の岩。高さ方向の半径プロファイルに沿って球を変形させる"""
    rnd = random.Random(seed)
    off = Vector((rnd.uniform(-50, 50), rnd.uniform(-50, 50), rnd.uniform(-50, 50)))
    bury = 0.5
    prof = [
        (-bury, stem_r * 1.15), (0.0, stem_r * 1.1), (notch_h * 0.55, notch_r * 1.05),
        (notch_h, notch_r), (notch_h + 0.35, notch_r * 1.05 + 0.2),
        (notch_h + 0.9, radius * 0.72), (height * 0.55, radius * 0.95), (height * 0.75, radius),
        (height * 0.9, radius * 0.9), (height * 0.96, radius * 0.6), (height, 0.0),
    ]
    bm = geo.icosphere_bm(subdiv)
    for v in bm.verts:
        d = v.co.normalized()
        h = -bury + (d.z + 1.0) * 0.5 * (height + bury)
        r = _profile(h, prof)
        # 天辺は丸く閉じる
        if h > height * 0.96:
            t = (h - height * 0.96) / (height * 0.04)
            r = radius * 0.55 * math.sqrt(max(0.0, 1.0 - t * t))
        ang = math.atan2(d.y, d.x)
        lobe = 1.0 + lobes * geo.fbm(Vector((math.cos(ang) * 1.3, math.sin(ang) * 1.3, h * 0.25)) + off, 3)
        xy = Vector((d.x, d.y))
        if xy.length > 1e-6:
            xy.normalize()
        k = (max(h, 0.0) / height) ** 2
        v.co = Vector((xy.x * r * lobe + lean[0] * k, xy.y * r * lobe + lean[1] * k, h))

    def disp(co, n):
        p = co * 0.5 + off
        big = geo.fbm(p, 3) * 0.16
        karst = (geo.ridged(co * Vector((0.9, 0.9, 1.6)) + off, 5) - 1.0) * 0.16
        strata = 0.035 * math.sin(co.z * 7.0 + geo.fbm(co * 0.7 + off, 2) * 3.0)
        f1, f2 = geo.cell(co * 2.2 + off)
        hole = -0.10 * max(0.0, 0.22 - f1) / 0.22
        fine = geo.fbm(co * 6.0 + off, 3) * 0.025
        # くびれ部分は波で磨かれて滑らか
        notch_w = math.exp(-((co.z - notch_h) / 0.45) ** 2)
        # 地面付近は崩さない
        g = min(1.0, max(0.0, (co.z + 0.2) / 0.6))
        return (big + (karst + hole + strata) * (1.0 - 0.7 * notch_w) + fine) * g + big * (1 - g) * 0.3

    geo.displace_along_normals(bm, disp)
    obj = C.from_bmesh(name, bm)
    geo.decimate(obj, target_tris=target_tris)
    C.set_smooth(obj, True, angle=50)
    return obj


def reef_rock(name, size, height, seed, subdiv=5, target_tris=5000, pool=True):
    """平たい磯の岩。上面が鋭く溶食されている"""
    rnd = random.Random(seed)
    off = Vector((rnd.uniform(-50, 50), rnd.uniform(-50, 50), rnd.uniform(-50, 50)))
    bm = geo.icosphere_bm(subdiv)
    sx, sy = size
    for v in bm.verts:
        d = v.co.normalized()
        ang = math.atan2(d.y, d.x)
        lobe = 1.0 + 0.25 * geo.fbm(Vector((math.cos(ang), math.sin(ang), 0)) * 1.4 + off, 3)
        z = d.z
        # 上側は平たく、下は埋まる
        zz = height * (0.35 + 0.65 * z) if z > 0 else height * 0.35 * (1 + z) - 0.35 * (-z)
        spread = math.sqrt(max(0.0, 1.0 - z * z)) ** 0.6
        v.co = Vector((d.x / max(1e-6, math.hypot(d.x, d.y)) * spread * sx * lobe if (d.x or d.y) else 0,
                       d.y / max(1e-6, math.hypot(d.x, d.y)) * spread * sy * lobe if (d.x or d.y) else 0,
                       zz))

    def disp(co, n):
        up = max(0.0, n.z)
        karst = (geo.ridged(co * 1.8 + off, 5) - 1.0) * 0.16 * (0.4 + up)
        f1, _ = geo.cell(co * 2.8 + off)
        hole = -0.08 * max(0.0, 0.25 - f1) / 0.25
        big = geo.fbm(co * 0.8 + off, 3) * 0.12
        # 潮だまりのくぼみ
        dent = 0.0
        if pool:
            dent = -0.22 * math.exp(-((co.x - sx * 0.15) ** 2 + (co.y + sy * 0.1) ** 2) / (0.18 * sx * sy)) * up
        g = min(1.0, max(0.0, (co.z + 0.15) / 0.35))
        return (karst + hole + big + dent) * g

    geo.displace_along_normals(bm, disp)
    obj = C.from_bmesh(name, bm)
    geo.decimate(obj, target_tris=target_tris)
    C.set_smooth(obj, True, angle=50)
    return obj


def build():
    # 岩ごとに別マテリアル（それぞれ固有のテクスチャ）
    out = {}
    a = notch_rock("NotchRock_A", height=5.2, radius=3.3, notch_h=1.2, notch_r=1.05, stem_r=1.6, seed=11,
                   lean=(0.5, 0.2))
    C.assign(a, limestone("NotchRockA", res=2048))
    out["NotchRock_A"] = [a]
    b = notch_rock("NotchRock_B", height=3.4, radius=2.2, notch_h=0.9, notch_r=0.7, stem_r=1.0, seed=23,
                   target_tris=8000, lean=(-0.3, 0.1), lobes=0.25)
    C.assign(b, limestone("NotchRockB", res=2048))
    out["NotchRock_B"] = [b]
    for i, (size, h, pool) in enumerate([((1.6, 1.1), 0.7, True), ((1.0, 0.7), 0.5, False),
                                          ((0.55, 0.45), 0.35, False)]):
        n = f"ReefRock_{'ABC'[i]}"
        r = reef_rock(n, size, h, seed=40 + i, target_tris=[6000, 4000, 2500][i], pool=pool)
        C.assign(r, limestone(f"ReefRock{'ABC'[i]}", res=[2048, 1024, 1024][i], tide_top=0.9, dark_top=0.5))
        out[n] = [r]
    return out
