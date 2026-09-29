"""ビーチの家具（3 バリエーション）

BeachParasol_A: 直径 2.2m の八角形パラソル。青白の布（縁は波形のフラップ）、アルミの骨と支柱
BeachParasol_B: 茅葺きパラソル（沖縄のリゾートでよく見る、枯れたヤシの葉を段々に葺いたもの）
DeckChair_A   : チーク材のデッキチェア（寝椅子）。背もたれは角度調整式で少し起こした状態

原点は接地点（支柱の中心 / チェアの中心）。支柱は砂に 0.3m 埋まる。チェアは長さ方向 = X（頭側が -X）。
布・茅は薄い板なので double_sided（エンジンは常に裏面カリング）。フラップと茅の毛先はアルファで抜く。
"""
import math
import random

from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from . import _timber as T

PREVIEW = dict(cam_dir=(0.35, -1.0, 0.42), lens=40, spacing=1.2)

# パラソル A
R_TIP = 1.1        # 骨の先端の半径（八角形の外接円）
Z_APEX = 2.32
Z_TIP = 1.98
N_RIB = 8
VAL_H = 0.14       # フラップの丈

# パラソル B（茅葺き）
TB_APEX = 2.72
TB_RIM_R = 1.25
TB_RIM_Z = 2.0


def tube_wood_attrs(obj, rnd, flag=0.0):
    """tube_along の Proc UV（u=周, v=長さ）から木目座標を作る（木目 = 管の長さ方向）"""
    me = obj.data
    uv = me.uv_layers["Proc"].data
    import numpy as np
    arr = np.zeros((len(me.loops), 4), np.float32)
    off = rnd.uniform(0, 50)
    r1, r2 = rnd.random(), rnd.random()
    for li in range(len(me.loops)):
        u, v = uv[li].uv
        a = u * math.tau
        arr[li] = (v + off, 0.08 * math.cos(a), 0.08 * math.sin(a), 1.0)
    at = me.color_attributes.new("WoodCo", "FLOAT_COLOR", "CORNER")
    at.data.foreach_set("color", arr.ravel())
    T.set_attr(obj, "WoodId", lambda co, n: (r1, r2, flag))


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def fabric_material():
    """青白の帆布（パネルごとに交互）。縁のフラップは波形に抜く"""
    def fn(nb):
        fab = nb.sep(nb.attr("Fab", "Vector"))      # (縁に沿った長さ m, 縁から下 / 中心からの距離, 1=フラップ)
        pan = nb.sep(nb.attr("Panel", "Vector"))    # (パネル番号, パネル内の横位置 0..1, 0)
        U, V, flap = fab
        pid, pa = pan[0], pan[1]
        oc = nb.coord("Object")
        blue = nb.math("LESS_THAN", nb.math("MODULO", nb.add(pid, 0.25), 2.0), 1.0)
        col = nb.mix(srgb("#ecebe4"), srgb("#1f5fa8"), blue)
        # 日焼けで上面の青は少し褪せる
        fade = nb.smooth(nb.noise(oc, 1.5, 3, 0.5), 0.3, 0.8)
        col = nb.mix(col, nb.mix(srgb("#e3e0d6"), srgb("#4a7cb4"), blue), nb.mul(fade, 0.35))
        # 骨に沿った縫い目、縁の白いパイピング
        seam = nb.smooth(nb.math("MINIMUM", pa, nb.sub(1.0, pa)), 0.012, 0.004)
        stitch = nb.mul(nb.math("LESS_THAN", nb.math("FRACT", nb.mul(U, 180.0)), 0.55),
                        nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(V, 0.04)), 0.0025, 0.001), flap))
        col = nb.mix(col, srgb("#c9c6bc"), nb.mul(stitch, 0.7))
        # 帆布の張りムラ（織り目はテクセルより細かいので描かない）
        weave = nb.noise(oc, 18.0, 3, 0.5)
        dirt = nb.smooth(nb.noise(oc, 4.0, 5, 0.6), 0.55, 0.8)
        col = nb.mix(col, srgb("#8d8778"), nb.mul(dirt, 0.12))
        # 波形のフラップ（パネル 1 枚に 3 山）
        ph = nb.math("FRACT", nb.mul(pa, 3.0))
        arc = nb.math("SQRT", nb.math("MAXIMUM", nb.sub(1.0, nb.pow(nb.sub(nb.mul(ph, 2.0), 1.0), 2.0)), 0.0))
        depth = nb.add(0.085, nb.mul(arc, 0.05))
        # 波形の縁の縁取り（青には白、白には青）
        pipe = nb.mul(flap, nb.smooth(V, nb.sub(depth, 0.02), nb.sub(depth, 0.016)))
        col = nb.mix(col, nb.mix(srgb("#1f5fa8"), srgb("#f2f1ec"), blue), pipe)
        alpha = nb.math("MAXIMUM", nb.sub(1.0, flap), nb.smooth(V, nb.add(depth, 0.002), nb.sub(depth, 0.002)))
        h = nb.add(nb.mul(weave, 0.5), nb.add(nb.mul(seam, -0.6), nb.mul(pipe, 0.8)))
        return dict(color=col, rough=0.78, height=h, height_scale=0.0015, alpha=alpha,
                    cavity=nb.maprange(seam, 0, 1, 1.0, 0.8))

    return pbr_material("ParasolFabric", fn, res=1024, uv="smart", double_sided=True, ao_distance=0.3)


def alu_material():
    """アルミ（ヘアライン）。MetalId=1 の部品は白い樹脂"""
    def fn(nb):
        oc = nb.coord("Object")
        flag = nb.sep(nb.attr("WoodId", "Vector"))[2]
        brushed = nb.noise(nb.mapping(oc, scale=(60, 60, 0.6)), 3.0, 4, 0.6)
        scratch = nb.smooth(nb.noise(nb.mapping(oc, scale=(2, 2, 40)), 4.0, 3, 0.5), 0.72, 0.76)
        alu = nb.mix(srgb("#b9bcbf"), srgb("#d2d5d7"), brushed)
        alu = nb.mix(alu, srgb("#8e9194"), nb.mul(scratch, 0.5))
        plastic = nb.mix(srgb("#e8e6df"), srgb("#d4d1c7"), nb.noise(oc, 30.0, 3, 0.5))
        col = nb.mix(alu, plastic, flag)
        rough = nb.mixf(nb.maprange(brushed, 0.3, 0.7, 0.22, 0.34), 0.45, flag)
        metal = nb.sub(1.0, flag)
        # 砂に埋まる根元は曇る
        z = nb.sep(oc)[2]
        sand = nb.smooth(z, 0.08, -0.05)
        col = nb.mix(col, srgb("#b3a88e"), nb.mul(sand, 0.6))
        rough = nb.mixf(rough, 0.7, sand)
        return dict(color=col, rough=rough, metal=metal, height=brushed, height_scale=0.0003)

    return pbr_material("ParasolMetal", fn, res=512, ao_distance=0.3)


def thatch_material():
    """枯れたヤシの葉の束（アトラス）。u = 横、v = 0 が毛先 → 1 が上端"""
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        out_a = None
        col = None
        hgt = None
        # 2 層の葉の帯（奥は暗く密、手前は明るく疎）
        for layer, (n, shift, dark) in enumerate(((46.0, 0.0, 0.75), (34.0, 0.37, 1.0))):
            x = nb.add(nb.mul(nb.add(U, nb.mul(V, 0.06 + 0.05 * layer)), n), shift)
            sid = nb.add(nb.math("FLOOR", x), 100.0 * layer)
            r1 = T.white(nb, sid)
            r2 = T.white(nb, nb.add(sid, 31.7))
            r3 = T.white(nb, nb.add(sid, 57.1))
            f = nb.sub(nb.math("FRACT", x), 0.5)
            width = nb.mul(nb.maprange(r1, 0, 1, 0.55, 0.95), nb.maprange(V, 0.0, 0.3, 0.55, 1.0))
            core = nb.smooth(nb.math("ABSOLUTE", f), nb.mul(width, 0.5), nb.sub(nb.mul(width, 0.5), 0.06))
            # 毛先の長さはばらばら（葉先は細く尖る）
            end = nb.mul(r2, 0.22)
            tip = nb.smooth(V, end, nb.add(end, 0.05))
            strand = nb.mul(core, tip)
            c = nb.ramp(r3, [(0.0, srgb("#6a553a")), (0.35, srgb("#937a55")), (0.7, srgb("#b49a6b")),
                             (1.0, srgb("#cbb68a"))])
            # 葉脈と、先端ほど灰色に風化
            rib = nb.smooth(nb.math("ABSOLUTE", f), 0.05, 0.0)
            c = nb.mix(c, srgb("#5a4a35"), nb.mul(rib, 0.4))
            c = nb.mix(c, srgb("#8d877a"), nb.mul(nb.smooth(V, 0.5, 0.0), nb.mul(r1, 0.5)))
            streak = nb.noise(nb.comb(nb.mul(U, 400.0), nb.mul(V, 6.0), float(layer)), 1.0, 3, 0.5)
            c = nb.hsv(c, 0.5, 1.0, nb.mul(nb.maprange(streak, 0.3, 0.7, 0.8, 1.1), dark))
            hl = nb.mul(strand, nb.sub(1.0, nb.mul(nb.math("ABSOLUTE", f), 1.6)))
            if out_a is None:
                out_a, col, hgt = strand, c, hl
            else:
                col = nb.mix(col, c, strand)
                out_a = nb.math("MAXIMUM", out_a, strand)
                hgt = nb.math("MAXIMUM", hgt, nb.add(hl, 0.3))
        # 上の方は葉が重なって隙間が無い
        dense = nb.smooth(V, 0.3, 0.5)
        alpha = nb.math("MAXIMUM", out_a, dense)
        col = nb.mix(srgb("#4a3b28"), col, nb.math("MAXIMUM", out_a, nb.mul(dense, 0.3)))
        cavity = nb.mul(nb.maprange(out_a, 0, 1, 0.6, 1.0), nb.maprange(V, 0.35, 1.0, 1.0, 0.55))
        return dict(color=col, rough=0.88, alpha=alpha, height=hgt, height_scale=0.004, cavity=cavity)

    return pbr_material("ThatchLeaf", fn, res=1024, uv="atlas", double_sided=True, atlas_size=(1.0, 0.8))


def log_material(name="ThatchWood"):
    """皮をむいた丸太・垂木。WoodId.z=1 はシュロ縄"""
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        r1, r2, flag = nb.sep(nb.attr("WoodId", "Vector"))
        g = T.wood_grain(nb, co, rings=20.0, knots=0.4, crack_amt=1.4)
        tone = nb.add(nb.mul(g["var"], 0.6), nb.mul(r1, 0.4))
        col = nb.ramp(tone, [(0.2, srgb("#6d5a43")), (0.55, srgb("#8f7a5c")), (0.85, srgb("#a8977a"))])
        col = nb.mix(col, srgb("#4a3c2c"), nb.mul(g["ring"], 0.4))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(g["fib"], 0.25, 0.75, 0.85, 1.1))
        col = nb.mix(col, srgb("#3a2e22"), nb.mul(g["knot"], 0.8))
        col = nb.mix(col, srgb("#1c1712"), nb.mul(g["crack"], 0.9))
        # 皮の残り
        bark = nb.smooth(nb.noise(nb.mapping(co, scale=(0.8, 6, 6)), 1.5, 4, 0.6), 0.62, 0.68)
        col = nb.mix(col, srgb("#4b3a2a"), nb.mul(bark, 0.8))
        h = nb.add(nb.add(nb.mul(g["fib"], 0.4), nb.mul(g["ring"], 0.3)), nb.add(nb.mul(g["crack"], -1.5), nb.mul(bark, 0.5)))
        # シュロ縄（撚り目）
        x = nb.sep(co)[0]
        twist = nb.math("FRACT", nb.add(nb.mul(x, 70.0), nb.mul(nb.math("ARCTAN2", nb.sep(co)[2], nb.sep(co)[1]), 0.32)))
        rope = nb.smooth(nb.math("ABSOLUTE", nb.sub(twist, 0.5)), 0.5, 0.2)
        rcol = nb.mix(srgb("#3b2a1c"), srgb("#6b5238"), rope)
        col = nb.mix(col, rcol, flag)
        h = nb.mixf(h, rope, flag)
        return dict(color=col, rough=0.85, height=h, height_scale=0.004,
                    cavity=nb.maprange(nb.math("MAXIMUM", g["crack"], nb.mul(flag, nb.sub(1.0, rope))), 0, 1, 1.0, 0.55))

    return pbr_material(name, fn, res=1024, ao_distance=0.4)


def teak_material():
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        r1, r2, _ = nb.sep(nb.attr("WoodId", "Vector"))
        g = T.wood_grain(nb, co, rings=40.0, knots=0.1, crack_amt=0.6)
        tone = nb.add(nb.mul(g["var"], 0.6), nb.mul(r1, 0.4))
        col = nb.ramp(tone, [(0.15, srgb("#7c5130")), (0.5, srgb("#a0703f")), (0.85, srgb("#b98a55"))])
        col = nb.mix(col, srgb("#5a381d"), nb.mul(g["ring"], 0.45))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(g["fib"], 0.25, 0.75, 0.88, 1.08))
        col = nb.mix(col, srgb("#3f2815"), nb.mul(g["knot"], 0.7))
        col = nb.mix(col, srgb("#24170d"), nb.mul(g["crack"], 0.8))
        # 上向きの面は日焼けして銀色がかる
        nz = nb.sep(nb.coord("Normal"))[2]
        sun = nb.mul(nb.smooth(nz, 0.3, 0.95), nb.maprange(r2, 0, 1, 0.25, 0.55))
        grey = nb.mix(srgb("#9b9082"), srgb("#b3a896"), g["fib"])
        col = nb.mix(col, grey, sun)
        h = nb.add(nb.add(nb.mul(g["fib"], 0.4), nb.mul(g["ring"], 0.3)), nb.mul(g["crack"], -1.2))
        rough = nb.maprange(sun, 0, 0.55, 0.5, 0.75)
        return dict(color=col, rough=rough, height=h, height_scale=0.0015,
                    cavity=nb.maprange(g["crack"], 0, 1, 1.0, 0.6))

    return pbr_material("ChairTeak", fn, res=1024, ao_distance=0.3)


def steel_material():
    def fn(nb):
        oc = nb.coord("Object")
        n = nb.noise(oc, 40.0, 4, 0.6)
        col = nb.mix(srgb("#a9acad"), srgb("#c8cacb"), n)
        tea = nb.smooth(nb.noise(oc, 12.0, 5, 0.6), 0.62, 0.72)   # 塩で出るもらい錆
        col = nb.mix(col, srgb("#8a5a33"), nb.mul(tea, 0.5))
        return dict(color=col, rough=nb.mixf(0.25, 0.6, tea), metal=nb.sub(1.0, nb.mul(tea, 0.5)))

    return pbr_material("ChairSteel", fn, res=512, ao_distance=0.15)


# ---------------------------------------------------------------------------
# パラソル A
# ---------------------------------------------------------------------------
def _canopy_z(r, a=0.5):
    """中心からの距離比 r (0..1) での布の高さ（骨の間は少したるむ）"""
    return Z_APEX - (Z_APEX - Z_TIP) * (r ** 1.35) - 0.022 * math.sin(math.pi * a) * r


def _tip(k):
    ang = (k + 0.5) / N_RIB * math.tau
    return Vector((math.cos(ang) * R_TIP, math.sin(ang) * R_TIP, 0.0))


def canopy(name):
    mb = T.MeshBuilder()
    NA, NR = 6, 10
    per = N_RIB * NA
    rs = [0.035 + (1 - 0.035) * (i / NR) ** 0.9 for i in range(NR + 1)]
    chord = (_tip(1) - _tip(0)).length

    def plan(r, m):
        p, a = divmod(m % per, NA)
        a /= NA
        return (_tip(p).lerp(_tip(p + 1), a)) * r, a

    rows = []
    for r in rs:
        row = []
        for m in range(per):
            pp, a = plan(r, m)
            row.append(mb.v((pp.x, pp.y, _canopy_z(r, a))))
        rows.append(row)
    top = mb.v((0, 0, Z_APEX + 0.004))

    for i in range(NR):
        for m in range(per):
            p = m // NA
            m1 = m + 1
            a0, a1 = (m % NA) / NA, (m % NA + 1) / NA
            # パネル境界の頂点は a=1 として扱うため、面ごとに属性を持たせる
            ids = (rows[i][m], rows[i][m1 % per], rows[i + 1][m1 % per], rows[i + 1][m])
            rr = (rs[i], rs[i], rs[i + 1], rs[i + 1])
            aa = (a0, a1, a1, a0)
            vals = {ids[k]: (p * chord + (aa[k]) * chord, rr[k] * R_TIP, 0.0) for k in range(4)}
            pvals = {ids[k]: (p, aa[k], 0.0) for k in range(4)}
            mb.f(ids, Fab=lambda vi, d=vals: d[vi], Panel=lambda vi, d=pvals: d[vi])
    for m in range(per):
        p = m // NA
        a0, a1 = (m % NA) / NA, (m % NA + 1) / NA
        ids = (top, rows[0][m], rows[0][(m + 1) % per])
        vals = {top: (p * chord + 0.5 * chord, 0.0, 0.0), ids[1]: (p * chord + a0 * chord, rs[0] * R_TIP, 0.0),
                ids[2]: (p * chord + a1 * chord, rs[0] * R_TIP, 0.0)}
        pvals = {top: (p, 0.5, 0.0), ids[1]: (p, a0, 0.0), ids[2]: (p, a1, 0.0)}
        mb.f(ids, Fab=lambda vi, d=vals: d[vi], Panel=lambda vi, d=pvals: d[vi])
    # フラップ（縁から垂れる帯、わずかに外へ開く）
    edge = rows[-1]
    lows = []
    for m in range(per):
        pp, a = plan(1.0, m)
        out = pp.normalized() * 0.012
        z = _canopy_z(1.0, a)
        lows.append(mb.v((pp.x + out.x, pp.y + out.y, z - VAL_H)))
    for m in range(per):
        p = m // NA
        a0, a1 = (m % NA) / NA, (m % NA + 1) / NA
        m1 = (m + 1) % per
        ids = (edge[m], lows[m], lows[m1], edge[m1])
        vals = {edge[m]: (p * chord + a0 * chord, 0.0, 1.0), lows[m]: (p * chord + a0 * chord, VAL_H, 1.0),
                lows[m1]: (p * chord + a1 * chord, VAL_H, 1.0), edge[m1]: (p * chord + a1 * chord, 0.0, 1.0)}
        pvals = {edge[m]: (p, a0, 0.0), lows[m]: (p, a0, 0.0), lows[m1]: (p, a1, 0.0), edge[m1]: (p, a1, 0.0)}
        mb.f(ids, Fab=lambda vi, d=vals: d[vi], Panel=lambda vi, d=pvals: d[vi])
    obj = mb.build(name, fix_normals=False)
    # 上向き（外向き）に揃える
    T.recalc_normals(obj)
    if obj.data.polygons[0].normal.z < 0:
        obj.data.flip_normals()
    C.set_smooth(obj, True, angle=60)
    return obj


def parasol_a(prefix):
    parts = []
    fab = canopy(f"{prefix}_canopy")
    C.assign(fab, fabric_material())
    parts.append(fab)
    metal = []
    # 支柱（下段は太め、継ぎ目にカラー）
    metal.append(T.rod(f"{prefix}_pole0", (0, 0, -0.3), (0, 0, 1.35), 0.021, sides=12))
    metal.append(T.rod(f"{prefix}_pole1", (0, 0, 1.3), (0, 0, Z_APEX + 0.03), 0.017, sides=12))
    collar = T.lathe(f"{prefix}_collar", [(0, 1.28), (0.027, 1.28), (0.029, 1.3), (0.029, 1.39), (0.024, 1.41), (0, 1.41)], 12)
    metal.append(collar)
    # 上ろくろ・下ろくろ（樹脂）・頂部の飾り
    plastic = []
    plastic.append(T.lathe(f"{prefix}_hub", [(0, Z_APEX - 0.09), (0.03, Z_APEX - 0.09), (0.038, Z_APEX - 0.06),
                                             (0.038, Z_APEX - 0.03), (0, Z_APEX - 0.03)], 12))
    runner_z = 1.74
    plastic.append(T.lathe(f"{prefix}_runner", [(0, runner_z - 0.06), (0.03, runner_z - 0.06), (0.034, runner_z - 0.03),
                                                (0.034, runner_z + 0.04), (0.026, runner_z + 0.06), (0, runner_z + 0.06)], 12))
    plastic.append(T.lathe(f"{prefix}_finial", [(0, Z_APEX), (0.02, Z_APEX), (0.03, Z_APEX + 0.03), (0.026, Z_APEX + 0.06),
                                                (0.012, Z_APEX + 0.08), (0, Z_APEX + 0.085)], 10))
    # 骨（布の下）と受け骨
    for k in range(N_RIB):
        tipd = _tip(k).normalized()
        pts = [tipd * (R_TIP * r) + Vector((0, 0, _canopy_z(r, 0.0) - 0.011)) for r in (0.03, 0.2, 0.4, 0.6, 0.8, 1.0)]
        pts[0] = Vector((tipd.x * 0.035, tipd.y * 0.035, Z_APEX - 0.06))
        rib = geo.tube_along(pts, [0.006] * len(pts), sides=6, name=f"{prefix}_rib{k}")
        metal.append(rib)
        cap = T.lathe(f"{prefix}_tipcap{k}", [(0, -0.015), (0.009, -0.012), (0.009, 0.01), (0, 0.016)], 8)
        T.orient(cap, pts[-1] + tipd * 0.006, tipd)
        plastic.append(cap)
        mid = tipd * (R_TIP * 0.48) + Vector((0, 0, _canopy_z(0.48, 0.0) - 0.013))
        metal.append(T.rod(f"{prefix}_str{k}", (tipd.x * 0.034, tipd.y * 0.034, runner_z + 0.03), mid, 0.005, sides=6))
    for o in metal:
        T.set_attr(o, "WoodId", lambda co, n: (0.0, 0.0, 0.0))
    for o in plastic:
        T.set_attr(o, "WoodId", lambda co, n: (0.0, 0.0, 1.0))
    am = alu_material()
    for o in metal + plastic:
        C.set_smooth(o, True, angle=45)
        C.assign(o, am)
    return parts + metal + plastic


# ---------------------------------------------------------------------------
# パラソル B（茅葺き）
# ---------------------------------------------------------------------------
def _cone_z(r):
    return TB_APEX - (TB_APEX - TB_RIM_Z) * (r / TB_RIM_R)


def thatch_tier(prefix, k, r_in, r_out, lift, droop, rnd, count=None, v_range=(0.0, 1.0)):
    """円錐の帯を葉束カードで葺く。v=0 が毛先（外側の下端）"""
    cards = []
    circ = math.tau * r_out
    n = count or max(4, round(circ / 0.85))
    NU, NV = 4, 4
    ang0 = rnd.uniform(0, math.tau)
    for c in range(n):
        span = math.tau / n * 1.12
        a_c = ang0 + c / n * math.tau
        verts, faces, uvs = [], [], []
        flip = rnd.random() < 0.5
        dz = (c % 2) * 0.012 + rnd.uniform(-0.004, 0.004)
        for j in range(NV + 1):
            t = j / NV                       # 0 = 外側（毛先）→ 1 = 内側
            r = r_out + (r_in - r_out) * t
            for i in range(NU + 1):
                s = i / NU
                a = a_c + (s - 0.5) * span
                rr = r * (1.0 + rnd.uniform(-0.015, 0.015))
                z = _cone_z(rr) + lift + dz + (1 - t) ** 2 * (-droop) + rnd.uniform(-0.01, 0.01)
                # 毛先は下へ垂れて少し広がる
                rr += (1 - t) ** 2 * droop * 0.3
                verts.append((math.cos(a) * rr, math.sin(a) * rr, z))
        for j in range(NV):
            for i in range(NU):
                a = j * (NU + 1) + i
                faces.append((a, a + 1, a + NU + 2, a + NU + 1))
                v0 = v_range[0] + (v_range[1] - v_range[0]) * j / NV
                v1 = v_range[0] + (v_range[1] - v_range[0]) * (j + 1) / NV
                u0, u1 = i / NU, (i + 1) / NU
                if flip:
                    u0, u1 = 1 - u0, 1 - u1
                uvs.append([(u0, v0), (u1, v0), (u1, v1), (u0, v1)])
        o = C.mesh_object(f"{prefix}_t{k}_{c}", verts, faces, uvs)
        # 外向き（上向き）の面にする
        if o.data.polygons[0].normal.z < 0:
            o.data.flip_normals()
        C.set_smooth(o, True)
        cards.append(o)
    return cards


def parasol_b(prefix, rnd):
    wood, leaves = [], []
    # 支柱: 少し曲がった丸太
    pts = geo.bezier_points((0, 0, -0.3), (0.03, 0.0, 0.8), (-0.02, 0.02, 1.8), (0, 0, TB_APEX - 0.02), 14)
    radii = [0.072 - 0.012 * i / 13 for i in range(14)]
    pole = geo.tube_along(pts, radii, sides=14, name=f"{prefix}_pole",
                          ring_fn=lambda i, j, a: 1.0 + 0.03 * math.sin(a * 2 + i) + 0.02 * math.sin(a * 5))
    tube_wood_attrs(pole, rnd)
    wood.append(pole)
    top = Vector((0, 0, TB_APEX - 0.04))
    # 垂木 8 本と外周の輪
    NRAF = 8
    for k in range(NRAF):
        a = k / NRAF * math.tau + 0.2
        d = Vector((math.cos(a), math.sin(a), 0))
        end = d * (TB_RIM_R + 0.12) + Vector((0, 0, _cone_z(TB_RIM_R + 0.12) - 0.01))
        p = [top + d * 0.05 + Vector((0, 0, -0.03)), end]
        p = [p[0].lerp(p[1], t) for t in (0, 0.33, 0.66, 1.0)]
        raf = geo.tube_along(p, [0.026, 0.024, 0.022, 0.02], sides=8, name=f"{prefix}_raf{k}")
        tube_wood_attrs(raf, rnd)
        wood.append(raf)
    ring_pts = []
    for i in range(33):
        a = i / 32 * math.tau
        ring_pts.append(Vector((math.cos(a) * TB_RIM_R, math.sin(a) * TB_RIM_R, _cone_z(TB_RIM_R) - 0.02)))
    ring = geo.tube_along(ring_pts, [0.02] * len(ring_pts), sides=8, name=f"{prefix}_ring")
    tube_wood_attrs(ring, rnd)
    wood.append(ring)
    # 支柱と垂木の結束（シュロ縄）
    for z, r in ((TB_APEX - 0.16, 0.078), (TB_APEX - 0.22, 0.078)):
        rp = [Vector((math.cos(i / 16 * math.tau) * r, math.sin(i / 16 * math.tau) * r, z)) for i in range(17)]
        rope = geo.tube_along(rp, [0.012] * 17, sides=6, name=f"{prefix}_rope{z:.2f}")
        tube_wood_attrs(rope, rnd, flag=1.0)
        wood.append(rope)
    # 茅（下の段から）
    tiers = [(0.86, 1.52, 0.035, 0.12), (0.52, 1.12, 0.07, 0.08), (0.2, 0.76, 0.105, 0.06), (0.0, 0.44, 0.14, 0.04)]
    for k, (ri, ro, lift, droop) in enumerate(tiers):
        leaves += thatch_tier(prefix, k, ri, ro, lift, droop, rnd)
        # 厚みを出すため同じ段をずらしてもう 1 枚
        leaves += thatch_tier(prefix, k + 10, ri + 0.02, ro - 0.05, lift + 0.02, droop * 0.7, rnd, v_range=(0.05, 1.0))
    # 頂部の房: 上へ立ち上がる葉束
    for c in range(5):
        a = c / 5 * math.tau
        d = Vector((math.cos(a), math.sin(a), 0))
        base = Vector((0, 0, TB_APEX + 0.1))
        verts, faces, uvs = [], [], []
        for j in range(4):
            t = j / 3
            w = 0.09 * (1 - 0.3 * t)
            p = base + d * (0.02 + 0.08 * t) + Vector((0, 0, 0.26 * t))
            side = Vector((-d.y, d.x, 0))
            verts += [p - side * w, p + side * w]
            if j:
                i0 = (j - 1) * 2
                faces.append((i0, i0 + 1, i0 + 3, i0 + 2))
                uvs.append([(0, 1 - (t - 1 / 3)), (1, 1 - (t - 1 / 3)), (1, 1 - t), (0, 1 - t)])
        o = C.mesh_object(f"{prefix}_tuft{c}", verts, faces, uvs)
        C.set_smooth(o, True)
        leaves.append(o)
    rope = [Vector((math.cos(i / 16 * math.tau) * 0.085, math.sin(i / 16 * math.tau) * 0.085, TB_APEX + 0.14))
            for i in range(17)]
    r = geo.tube_along(rope, [0.014] * 17, sides=6, name=f"{prefix}_toprope")
    tube_wood_attrs(r, rnd, flag=1.0)
    wood.append(r)
    wm, lm = log_material(), thatch_material()
    for o in wood:
        C.set_smooth(o, True, angle=50)
        C.assign(o, wm)
    for o in leaves:
        C.assign(o, lm)
    return wood + leaves


# ---------------------------------------------------------------------------
# デッキチェア
# ---------------------------------------------------------------------------
def deck_chair(prefix, rnd, back_deg=38.0):
    wood, steel = [], []
    L, W = 1.9, 0.66
    rail_h, rail_t = 0.11, 0.035
    top = 0.33           # 側框の上端
    ys = W / 2 - rail_t / 2
    for sy in (-1, 1):
        wood.append(T.board(f"{prefix}_rail{sy}", (L, rail_t, rail_h), loc=(0, sy * ys, top - rail_h / 2), rnd=rnd,
                            bevel=0.006))
        for x in (0.8, -0.72):
            wood.append(T.board(f"{prefix}_leg{sy}{x}", (0.055, 0.05, top - rail_h + 0.02),
                                loc=(x, sy * (ys - rail_t / 2 - 0.025), (top - rail_h + 0.02) / 2), rnd=rnd, bevel=0.005,
                                axis="Z"))
        # 頭側の車輪
        wh = T.lathe(f"{prefix}_wheel{sy}", [(0, -0.018), (0.07, -0.018), (0.075, -0.012), (0.075, 0.012), (0.07, 0.018),
                                            (0, 0.018)], 18)
        wh.matrix_world = Matrix.Translation((-0.86, sy * (W / 2 + 0.03), 0.075)) @ Matrix.Rotation(math.pi / 2, 4, "X")
        T.wood_attrs(wh, rnd, axis="X")
        wood.append(wh)
        hub = T.lathe(f"{prefix}_hub{sy}", [(0, 0.0), (0.02, 0.0), (0.02, 0.006), (0.012, 0.01), (0, 0.01)], 10)
        T.orient(hub, (-0.86, sy * (W / 2 + 0.048), 0.075), (0, sy, 0))
        steel.append(hub)
        steel.append(T.rod(f"{prefix}_axle{sy}", (-0.86, sy * (ys - 0.02), 0.075), (-0.86, sy * (W / 2 + 0.05), 0.075),
                           0.007, sides=6))
        # 背もたれの受け（歯のついた刻み板）
        for kk in range(4):
            x = -0.62 - kk * 0.07
            wood.append(T.board(f"{prefix}_notch{sy}{kk}", (0.045, 0.025, 0.035),
                                loc=(x, sy * (ys - rail_t / 2 - 0.0125), top - 0.02), rot=(0, math.radians(-25), 0),
                                rnd=rnd, bevel=0.003, axis="X"))
    # 横框（足側・頭側）
    for x in (0.9, -0.9):
        wood.append(T.board(f"{prefix}_cross{x}", (0.04, W - 2 * rail_t, 0.08), loc=(x, 0, top - 0.05), rnd=rnd,
                            bevel=0.005, axis="Y"))
    # 座面のすのこ
    sw, gap = 0.075, 0.018
    x = 0.92 - sw / 2
    hinge_x = -0.28
    while x - sw / 2 > hinge_x:
        wood.append(T.board(f"{prefix}_slat{x:.2f}", (sw, W - 0.01, 0.02), loc=(x, 0, top + 0.01), rnd=rnd, bevel=0.005,
                            axis="Y"))
        x -= sw + gap
    # 背もたれ（蝶番で起こす）
    bl = 0.82
    back = []
    for sy in (-1, 1):
        back.append(T.board(f"{prefix}_bside{sy}", (bl, 0.03, 0.05), loc=(-bl / 2, sy * (ys - rail_t / 2 - 0.02), -0.012),
                            rnd=rnd, bevel=0.005, axis="X"))
    xb = -0.04
    while xb > -bl + 0.03:
        back.append(T.board(f"{prefix}_bslat{xb:.2f}", (0.065, W - 0.13, 0.018), loc=(xb - 0.0325, 0, 0.022), rnd=rnd,
                            bevel=0.004, axis="Y"))
        xb -= 0.065 + 0.018
    back.append(T.board(f"{prefix}_btop", (0.05, W - 0.13, 0.03), loc=(-bl + 0.02, 0, 0.02), rnd=rnd, bevel=0.006,
                        axis="Y"))
    ang = math.radians(back_deg)
    mback = Matrix.Translation((hinge_x, 0, top + 0.012)) @ Matrix.Rotation(ang, 4, "Y")
    for o in back:
        o.matrix_world = mback @ o.matrix_world
    wood += back
    # 支え棒（U 字の丸棒）: 背もたれの中ほどから刻み板へ
    pa = mback @ Vector((-bl * 0.62, 0, -0.03))
    pb = Vector((-0.66, 0, top - 0.005))
    for sy in (-1, 1):
        yy = sy * (ys - rail_t / 2 - 0.045)
        steel.append(T.rod(f"{prefix}_prop{sy}", (pa.x, yy, pa.z), (pb.x, yy, pb.z), 0.009, sides=8))
    steel.append(T.rod(f"{prefix}_propx", (pb.x, -(ys - rail_t / 2 - 0.045), pb.z), (pb.x, ys - rail_t / 2 - 0.045, pb.z),
                       0.009, sides=8))
    # 蝶番とボルト
    for sy in (-1, 1):
        yy = sy * (ys - rail_t / 2 - 0.003)
        steel.append(T.rod(f"{prefix}_pin{sy}", (hinge_x, yy - sy * 0.03, top + 0.012), (hinge_x, yy + sy * 0.012, top + 0.012),
                           0.008, sides=8))
        for x in (0.8, -0.72, 0.9, -0.9):
            b = T.lathe(f"{prefix}_bolt{sy}{x}", [(0, 0), (0.009, 0), (0.009, 0.003), (0.005, 0.005), (0, 0.005)], 8)
            T.orient(b, (x, sy * W / 2, top - rail_h / 2 - 0.02), (0, sy, 0))
            steel.append(b)
    tm, sm = teak_material(), steel_material()
    for o in wood:
        C.assign(o, tm)
    for o in steel:
        C.set_smooth(o, True, angle=45)
        C.assign(o, sm)
    return wood + steel


def build():
    rnd = random.Random(21)
    return {
        "BeachParasol_A": parasol_a("ParasolA"),
        "BeachParasol_B": parasol_b("ParasolB", rnd),
        "DeckChair_A": deck_chair("DeckChair", rnd),
    }
