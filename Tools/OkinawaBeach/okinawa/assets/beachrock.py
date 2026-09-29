"""ビーチロック（汀線の砂礫が炭酸塩で固まった板状の岩。沖縄の浜に多い）

  BeachRock_A: 約 6 × 2.5 m。4 枚の薄い層が段になって海側へ下がる岩の群れ
  BeachRock_B: 約 4 × 2 m。3 層
  BeachRock_C: 約 2 m。割れて崩れたブロックの山

座標: X = 汀線方向, -Y = 海側。層は海側へ 7〜8° 傾き、節理（割れ目）でほぼ矩形のブロックに割れている。
原点 = 岩群の中心の砂面 (z=0)。下部は砂に埋まる（浜の前浜は約 10° 傾くので海側ほど深く作ってある）。
各ブロックは閉じたメッシュ。
"""
import math
import os
import random

import bpy
import numpy as np
from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb

VOXEL = 0.02   # ボクセル化の格子(m)。割れ目はこれの 2 倍以上あける


# ---------------------------------------------------------------------------
# 2D 多角形
# ---------------------------------------------------------------------------
def _area(poly):
    return 0.5 * sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(poly, poly[1:] + poly[:1]))


def _centroid(poly):
    a = _area(poly)
    cx = sum((p[0] + q[0]) * (p[0] * q[1] - q[0] * p[1]) for p, q in zip(poly, poly[1:] + poly[:1]))
    cy = sum((p[1] + q[1]) * (p[0] * q[1] - q[0] * p[1]) for p, q in zip(poly, poly[1:] + poly[:1]))
    return cx / (6 * a), cy / (6 * a)


def _clip(poly, a, b, c):
    """a·x + b·y + c >= 0 の側を残す（凸多角形）"""
    out = []
    n = len(poly)
    for i in range(n):
        p, q = poly[i], poly[(i + 1) % n]
        dp = a * p[0] + b * p[1] + c
        dq = a * q[0] + b * q[1] + c
        if dp >= 0:
            out.append(p)
        if (dp >= 0) != (dq >= 0):
            t = dp / (dp - dq)
            out.append((p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t))
    return out


def _inset(poly, d):
    """凸多角形（反時計回り）を内側へ d ずらす"""
    n = len(poly)
    lines = []
    for i in range(n):
        p, q = Vector(poly[i]), Vector(poly[(i + 1) % n])
        t = (q - p).normalized()
        nrm = Vector((-t.y, t.x))           # 左 = 内側
        lines.append((nrm, nrm.dot(p) + d))
    out = []
    for i in range(n):
        (n1, c1), (n2, c2) = lines[i - 1], lines[i]
        det = n1.x * n2.y - n1.y * n2.x
        if abs(det) < 1e-9:
            continue
        out.append(((c1 * n2.y - c2 * n1.y) / det, (n1.x * c2 - n2.x * c1) / det))
    return out if len(out) >= 3 and _area(out) > 0 else []


# ---------------------------------------------------------------------------
# ブロック 1 個
# ---------------------------------------------------------------------------
def _prism(bm, poly, ztop, zbot):
    """多角形の柱を bm に足す。ztop/zbot(x, y) は上下の面の高さ"""
    top = [bm.verts.new((x, y, ztop(x, y))) for x, y in poly]
    bot = [bm.verts.new((x, y, zbot(x, y))) for x, y in poly]
    bm.faces.new(top)
    bm.faces.new(bot[::-1])
    n = len(poly)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bot[i], bot[j], top[j], top[i]))


def _edge_dist(poly, xy):
    """2D 点列 xy (N,2) から多角形の辺・角までの距離: (辺, 角)"""
    P = np.array(poly)
    Q = np.roll(P, -1, 0)
    d_side = np.full(len(xy), 1e9)
    for a, b in zip(P, Q):
        ab = b - a
        t = np.clip(((xy - a) @ ab) / max(ab @ ab, 1e-12), 0.0, 1.0)
        d_side = np.minimum(d_side, np.linalg.norm(xy - (a + t[:, None] * ab), axis=1))
    d_corner = np.min(np.linalg.norm(xy[:, None, :] - P[None, :, :], axis=2), axis=1)
    return d_side, d_corner


def _block(name, prisms, xform, rnd, tris, erosion=1.0):
    """柱（多角形, 上面の高さ関数, 下面の高さ関数）の組を 1 個の岩にする

    ボクセル化して角を少し丸め、元の柱の稜線・角からの距離に応じて欠けさせ、面にうねりとくぼみを付ける。
    層理の縞や小さな穴は細かすぎて減らすと崩れるので、マテリアル（法線マップ）で付ける。
    変形（傾き）はその後にかけてから減らす
    """
    import bmesh
    bm = bmesh.new()
    for poly, zt, zb in prisms:
        _prism(bm, poly, zt, zb)
    obj = C.from_bmesh(name, bm)
    m = obj.modifiers.new("Remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = VOXEL
    C.apply_modifiers(obj)
    geo.smooth_mod(obj, factor=0.5, iterations=1)
    me = obj.data
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    nr = np.empty(n * 3)
    me.vertices.foreach_get("normal", nr)
    nr = nr.reshape(-1, 3)
    # 稜線・角への近さ（どれかの柱の上の稜線か縦の角。凹んだ入隅は削らない）
    edge = np.zeros(n)
    for poly, zt, zb in prisms:
        d_side, d_corner = _edge_dist(poly, co[:, :2])
        ztop = np.array([zt(x, y) for x, y in co[:, :2]])
        zbot = np.array([zb(x, y) for x, y in co[:, :2]])
        d_top = np.abs(co[:, 2] - ztop)
        inr = (co[:, 2] > zbot - 0.02) & (co[:, 2] < ztop + 0.03)
        e = np.exp(-(d_side + d_top) / 0.035) + np.exp(-d_corner / 0.06) * inr
        edge = np.maximum(edge, np.minimum(e, 1.0))
    off = Vector((rnd.uniform(-90, 90), rnd.uniform(-90, 90), rnd.uniform(-90, 90)))
    disp = np.empty(n)
    for i in range(n):
        p = Vector(co[i])
        # 稜線はところどころ角張って欠ける（細かい凹凸は法線マップ側）
        chip = geo.fbm(p * 4.0 + off, 3)
        chip = min(1.0, max(0.0, (chip - 0.1) / 0.12))
        d = -edge[i] * (0.004 + 0.035 * chip) * erosion
        # 面の大きなうねりと、上面の浅い溶食のくぼみ
        d += 0.01 * geo.fbm(p * 1.8 + off, 3) * erosion
        f1, _ = geo.cell(p * 2.2 + off)
        d -= 0.016 * max(0.0, nr[i, 2]) * max(0.0, 0.35 - f1) / 0.35
        disp[i] = d
    co += nr * disp[:, None]
    me.vertices.foreach_set("co", co.ravel())
    me.update()
    me.transform(xform)
    me.update()
    geo.decimate(obj, target_tris=tris)
    C.set_smooth(obj, True, angle=45)
    return obj


# ---------------------------------------------------------------------------
# 層になった岩の群れ
# ---------------------------------------------------------------------------
def slab_group(name, length, width, beds, dip, seed, tris, col_step=(1.2, 2.0), row_step=(0.8, 1.3),
               retreat=0.32, crack=(0.03, 0.07), split=0.3):
    """beds: [(下面, 上面), ...]（層理面からの高さ、下の層から）"""
    rnd = random.Random(seed)
    tdip = math.tan(math.radians(dip))
    hx, hy = length / 2, width / 2
    # 節理: 汀線に直交する割れ目（x 位置）と平行な割れ目（y 位置）。少し斜め・不揃い
    xs = [-hx - 0.4]
    while xs[-1] < hx + 0.4:
        xs.append(xs[-1] + rnd.uniform(*col_step))
    ys = [-hy - 0.3]
    while ys[-1] < hy + 0.3:
        ys.append(ys[-1] + rnd.uniform(*row_step))
    jit = {}

    def corner(i, j):
        if (i, j) not in jit:
            jit[(i, j)] = (xs[i] + rnd.uniform(-0.18, 0.18) + ys[j] * 0.08, ys[j] + rnd.uniform(-0.12, 0.12))
        return jit[(i, j)]

    # 層ごとの輪郭: 上の層ほど海側・陸側・両端が後退して段になる
    ph = [rnd.uniform(0, 10) for _ in range(8)]

    def sea_edge(k, x):
        return -hy + k * retreat + 0.14 * math.sin(x * 1.3 + ph[k]) + 0.09 * math.sin(x * 3.3 + ph[k + 4])

    def land_edge(k, x):
        return hy - k * 0.14 - 0.12 * math.sin(x * 1.1 + ph[k + 1])

    blocks = []
    for k, (b0, b1) in enumerate(beds):
        ex = (-hx + k * rnd.uniform(0.2, 0.4) + rnd.uniform(0, 0.2), hx - k * rnd.uniform(0.2, 0.4) - rnd.uniform(0, 0.2))
        for i in range(len(xs) - 1):
            for j in range(len(ys) - 1):
                cell = [corner(i, j), corner(i + 1, j), corner(i + 1, j + 1), corner(i, j + 1)]
                parts = [cell]
                if k >= 1 and rnd.random() < split * k:
                    # 上の層は細かく割れる（汀線に直交する割れ目を 1 本足す。少し斜め）
                    ccx, ccy = _centroid(cell)
                    mx = ccx + rnd.uniform(-0.15, 0.15) * (cell[1][0] - cell[0][0])
                    b = rnd.uniform(-0.25, 0.25)
                    parts = [_clip(cell, -1.0, b, mx - b * ccy), _clip(cell, 1.0, -b, -(mx - b * ccy))]
                for poly in parts:
                    if len(poly) < 3:
                        continue
                    cx, cy = _centroid(poly)
                    # 輪郭で切る（海側は 2 本の斜めの割れ口でぎざぎざに。陸側・両端）
                    se = sea_edge(k, cx) + rnd.uniform(-0.08, 0.08)
                    a1, a2 = rnd.uniform(-0.35, 0.0), rnd.uniform(0.0, 0.35)
                    cuts = [(a1, 1.0, -a1 * cx - se), (a2, 1.0, -a2 * cx - se), (0.0, -1.0, land_edge(k, cx)),
                            (1.0, rnd.uniform(-0.2, 0.2), -ex[0]), (-1.0, rnd.uniform(-0.2, 0.2), ex[1])]
                    # 角が欠けたブロック
                    if rnd.random() < 0.3:
                        ang = rnd.uniform(0, math.tau)
                        u = Vector((math.cos(ang), math.sin(ang)))
                        r = max(u.dot(Vector(p) - Vector((cx, cy))) for p in poly)
                        cuts.append((-u.x, -u.y, u.dot(Vector((cx, cy))) + r - rnd.uniform(0.15, 0.3)))
                    for ca, cb, cc in cuts:
                        if len(poly) >= 3:
                            poly = _clip(poly, ca, cb, cc)
                    if len(poly) < 3 or _area(poly) < 0.08:
                        continue
                    poly = _inset(poly, rnd.uniform(*crack) / 2)
                    if not poly or _area(poly) < 0.06:
                        continue
                    blocks.append((k, poly))

    parts = []
    weights = [_area(p) * (0.45 if k == 0 else 1.0) for k, p in blocks]
    for n, ((k, poly), w) in enumerate(zip(blocks, weights)):
        b0, b1 = beds[k]
        cx, cy = _centroid(poly)
        # 沈下・傾き: 海側の縁のブロックほど大きく海側へ傾く
        sea = max(0.0, 1.0 - (cy - sea_edge(k, cx)) / 0.9)
        tilt_x = math.radians(rnd.uniform(-1.5, 1.5) - 6.0 * sea * rnd.random())
        tilt_y = math.radians(rnd.uniform(-2.0, 2.0))
        dz = rnd.uniform(-0.015, 0.015) - 0.03 * sea * rnd.random()
        piv = Vector((cx, cy, tdip * cy + (b0 + b1) / 2))
        xform = Matrix.Translation(piv + Vector((0, 0, dz))) @ Matrix.Rotation(tilt_x, 4, "X") @ \
            Matrix.Rotation(tilt_y, 4, "Y") @ Matrix.Translation(-piv)
        prisms = [(poly, lambda x, y, b1=b1: tdip * y + b1, lambda x, y, b0=b0: tdip * y + b0)]
        obj = _block(f"{name}_{n}", prisms, xform, rnd, max(80, int(tris * w / sum(weights))))
        parts.append(obj)
    return parts


def broken_blocks(name, seed, tris):
    """割れて転がったブロック（2 層のかけら）の山"""
    rnd = random.Random(seed)
    specs = [  # (中心, 大きさ(x, y), 層厚(下, 上), 回転(度: x, y, z))
        ((-0.45, 0.05), (0.95, 0.7), (0.16, 0.12), (4, -3, 12)),
        ((0.5, -0.1), (0.75, 0.6), (0.14, 0.11), (-6, 5, -20)),
        ((0.05, 0.45), (0.6, 0.45), (0.13, 0.1), (24, -8, 35)),
        ((-0.9, -0.45), (0.5, 0.42), (0.15, 0.0), (-14, 18, 70)),
        ((0.95, 0.45), (0.45, 0.35), (0.12, 0.1), (8, -30, -50)),
        ((0.2, -0.65), (0.35, 0.3), (0.12, 0.0), (35, 10, 15)),
    ]
    parts = []
    areas = [s[1][0] * s[1][1] for s in specs]
    for n, (c, (sx, sy), (t0, t1), rot) in enumerate(specs):
        # 不揃いな四角形（割れ口）。角を 1 つ落とすことも
        poly = [(-sx / 2 + rnd.uniform(0, 0.08), -sy / 2 + rnd.uniform(0, 0.06)),
                (sx / 2 - rnd.uniform(0, 0.08), -sy / 2 + rnd.uniform(0, 0.06)),
                (sx / 2 - rnd.uniform(0, 0.1), sy / 2 - rnd.uniform(0, 0.06)),
                (-sx / 2 + rnd.uniform(0, 0.1), sy / 2 - rnd.uniform(0, 0.08))]
        if rnd.random() < 0.6:
            ang = rnd.uniform(0, math.tau)
            u = Vector((math.cos(ang), math.sin(ang)))
            r = max(u.dot(Vector(p)) for p in poly)
            poly = _clip(poly, -u.x, -u.y, r - rnd.uniform(0.08, 0.16))
        prisms = [(poly, lambda x, y, t=t0: t, lambda x, y: 0.0)]
        if t1 > 0:
            # 上の層は少し小さく、ずれている（段）
            up = _inset(poly, rnd.uniform(0.05, 0.1))
            dx, dy = rnd.uniform(-0.05, 0.05), rnd.uniform(0.0, 0.08)
            up = [(x + dx, y + dy) for x, y in up]
            prisms.append((up, lambda x, y, t=t0 + t1: t, lambda x, y, t=t0 - 0.01: t))
        rx, ry, rz = (math.radians(a) for a in rot)
        rotm = Matrix.Rotation(rz, 4, "Z") @ Matrix.Rotation(ry, 4, "Y") @ Matrix.Rotation(rx, 4, "X")
        # 傾けたときの最下点が z=-0.12 になるよう下げる（砂に埋まる）
        zmin = min((rotm @ Vector((x, y, z))).z for poly_, zt, zb in prisms for x, y in poly_
                   for z in (zt(x, y), zb(x, y)))
        xform = Matrix.Translation((c[0], c[1], -zmin - 0.12 + rnd.uniform(0, 0.04))) @ rotm
        obj = _block(f"{name}_{n}", prisms, xform, rnd, int(tris * areas[n] / sum(areas)))
        parts.append(obj)
    return parts


# ---------------------------------------------------------------------------
# マテリアル
# ---------------------------------------------------------------------------
def beachrock_material(name, res, dip):
    """灰ベージュのざらついた砂岩。貝殻・サンゴ片が混じり、穴だらけ。海側・下部は濡れて暗く、緑や黒の藻の膜"""
    tdip = math.tan(math.radians(dip))

    def fn(nb):
        co = nb.coord("Object")
        s = nb.sep(co)
        y, z = s[1], s[2]
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        zb = nb.sub(z, nb.mul(y, tdip))                       # 層理面に沿った高さ
        big = nb.noise(co, 0.8, 4, 0.6)
        mid = nb.noise(nb.mapping(co, loc=(3.1, 1.7, 0.4)), 3.5, 5, 0.6)
        fine = nb.noise(co, 55.0, 3, 0.6)
        # 層ごとの色の違い（層理に沿った縞）
        band = nb.noise(nb.comb(nb.mul(s[0], 0.15), nb.mul(y, 0.15), nb.mul(zb, 9.0)), 1.0, 3, 0.5)
        col = nb.ramp(big, [(0.3, srgb("#8f897c")), (0.5, srgb("#a59e8e")), (0.72, srgb("#b8b09e"))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(band, 0.3, 0.7, 0.86, 1.1))
        col = nb.mix(col, srgb("#8b8579"), nb.mul(nb.smooth(mid, 0.5, 0.7), 0.5))
        # ざらざらの砂粒（明暗の粒）
        gv = nb.node("ShaderNodeTexVoronoi", feature="F1", distance="EUCLIDEAN", voronoi_dimensions="3D")
        nb.link(co, gv.inputs["Vector"])
        gv.inputs["Scale"].default_value = 160.0
        gc = nb.sep(gv.outputs["Color"])
        grain = nb.smooth(gv.outputs["Distance"], 0.35, 0.1)
        col = nb.mix(col, srgb("#d9d2c2"), nb.mul(grain, nb.math("GREATER_THAN", gc[0], 0.65)))
        col = nb.mix(col, srgb("#5d584e"), nb.mul(grain, nb.math("GREATER_THAN", gc[1], 0.85)))
        # 貝殻・サンゴ片（1〜4 cm）と小石
        fv = nb.node("ShaderNodeTexVoronoi", feature="F1", distance="EUCLIDEAN", voronoi_dimensions="3D")
        nb.link(nb.mapping(co, loc=(7.3, 2.1, 5.5)), fv.inputs["Vector"])
        fv.inputs["Scale"].default_value = 28.0
        fc = nb.sep(fv.outputs["Color"])
        frag = nb.mul(nb.smooth(fv.outputs["Distance"], 0.28, 0.16), nb.math("GREATER_THAN", fc[0], 0.82))
        fcol = nb.ramp(fc[1], [(0.0, srgb("#efe9dc")), (0.45, srgb("#ddd2bf")), (0.75, srgb("#cfa48d")),
                               (1.0, srgb("#7e796f"))], interp="CONSTANT")
        col = nb.mix(col, fcol, nb.mul(frag, 0.9))
        # 生物侵食の穴（奥は暗い）
        pv = nb.node("ShaderNodeTexVoronoi", feature="F1", distance="EUCLIDEAN", voronoi_dimensions="3D")
        nb.link(nb.mapping(co, loc=(1.3, 9.1, 3.7)), pv.inputs["Vector"])
        pv.inputs["Scale"].default_value = 11.0
        pit = nb.smooth(pv.outputs["Distance"], 0.2, 0.04)
        pv2 = nb.node("ShaderNodeTexVoronoi", feature="F1", distance="EUCLIDEAN", voronoi_dimensions="3D")
        nb.link(nb.mapping(co, loc=(4.2, 0.3, 8.8)), pv2.inputs["Vector"])
        pv2.inputs["Scale"].default_value = 30.0
        pit2 = nb.smooth(pv2.outputs["Distance"], 0.17, 0.03)
        pits = nb.math("MAXIMUM", pit, nb.mul(pit2, 0.7))
        col = nb.mix(col, srgb("#4e4940"), nb.mul(pits, 0.7))

        # 濡れ: 下部・海側ほど高くまで（上端はノイズで波打つ）
        wet_top = nb.add(0.2, nb.add(nb.mul(y, -0.1), nb.mul(nb.sub(big, 0.5), 0.2)))
        wet = nb.smooth(nb.sub(z, wet_top), 0.06, -0.12)
        # 黒い藻の帯（飛沫帯）と、濡れた所の緑の藻の膜
        black = nb.mul(nb.mul(nb.smooth(nb.sub(z, wet_top), 0.2, 0.05), nb.smooth(nb.sub(z, wet_top), -0.18, -0.02)),
                       nb.smooth(nb.noise(co, 2.2, 4, 0.6), 0.4, 0.55))
        green = nb.mul(nb.smooth(nb.sub(z, wet_top), -0.02, -0.2),
                       nb.smooth(nb.noise(nb.mapping(co, loc=(5.0, 5.0, 5.0)), 1.6, 4, 0.62), 0.42, 0.56))
        green = nb.mul(green, nb.maprange(nz, -0.2, 0.6, 0.5, 1.0))
        col = nb.mix(col, nb.hsv(col, 0.5, 0.85, 0.6), wet)
        col = nb.mix(col, srgb("#2b2a25"), nb.mul(black, 0.75))
        gcol = nb.mix(srgb("#3f5a2c"), srgb("#6f7d3b"), nb.noise(co, 9.0, 3, 0.6))
        col = nb.mix(col, gcol, nb.mul(green, 0.8))
        # 深く砂に埋まる所は砂色に染まる（前浜は傾いているので、海側の下の層はふつう露出する）
        sand = nb.smooth(nb.add(z, nb.mul(nb.sub(mid, 0.5), 0.1)), -0.26, -0.34)
        col = nb.mix(col, srgb("#cfc4a8"), nb.mul(sand, 0.7))
        # 陸側の上面は乾いて白っぽい
        dry_top = nb.mul(nb.smooth(nz, 0.6, 0.9), nb.sub(1.0, wet))
        col = nb.mix(col, nb.hsv(col, 0.5, 0.8, 1.12), nb.mul(dry_top, 0.6))

        # 側面に出る層理（数 cm の薄い層が段になる）
        side = nb.sub(1.0, nb.math("ABSOLUTE", nz))
        lam = nb.math("SINE", nb.add(nb.mul(zb, math.tau / 0.04), nb.mul(nb.noise(co, 1.5, 2, 0.5), 4.0)))
        lam = nb.mul(nb.smooth(lam, -0.2, 0.4), side)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(lam, 0.0, 1.0, 0.94, 1.04))
        height = nb.add(nb.mul(fine, 0.25), nb.mul(nb.sub(1.0, pits), 0.6))
        height = nb.add(height, nb.mul(lam, 0.35))
        height = nb.add(height, nb.mul(frag, 0.25))
        height = nb.add(height, nb.mul(grain, 0.12))
        height = nb.add(height, nb.mul(green, nb.mul(nb.noise(co, 30.0, 2, 0.6), 0.1)))
        rough = nb.maprange(wet, 0.0, 1.0, 0.9, 0.55)
        rough = nb.mixf(rough, 0.45, nb.mul(green, 0.6))
        rough = nb.mixf(rough, 0.95, nb.mul(sand, 0.8))
        cavity = nb.maprange(pits, 0.0, 1.0, 1.0, 0.45)
        return dict(color=col, rough=rough, height=height, height_scale=0.012, cavity=cavity)

    return pbr_material(name, fn, res=res, uv="smart", ao_distance=0.35, ao_samples=48)


def build():
    out = {}
    a_beds = [(-0.34, -0.12), (-0.12, 0.08), (0.08, 0.24), (0.24, 0.36)]
    a = slab_group("BeachRock_A", 6.0, 2.5, a_beds, 7.0, 11, 7000)
    mat_a = beachrock_material("BeachRockA", 2048, 7.0)
    for o in a:
        C.assign(o, mat_a)
    out["BeachRock_A"] = a
    b_beds = [(-0.3, -0.08), (-0.08, 0.12), (0.12, 0.3)]
    b = slab_group("BeachRock_B", 4.0, 2.0, b_beds, 8.0, 23, 5000, col_step=(1.1, 1.7), retreat=0.35)
    mat_b = beachrock_material("BeachRockB", 2048, 8.0)
    for o in b:
        C.assign(o, mat_b)
    out["BeachRock_B"] = b
    c = broken_blocks("BeachRock_C", 37, 3200)
    mat_c = beachrock_material("BeachRockC", 1024, 0.0)
    for o in c:
        C.assign(o, mat_c)
    out["BeachRock_C"] = c
    return out


# ---------------------------------------------------------------------------
# プレビュー（浜の汀線に置いて水を張る）
# ---------------------------------------------------------------------------
def _preview_extra(objs):
    from .reef_terrain import water_material
    scene = bpy.context.scene
    g = bpy.data.objects.get("PreviewGround")
    if g:
        bpy.data.objects.remove(g)
    path = os.path.join(C.MODELS_DIR, "BeachTerrain_Shore", "BeachTerrain_Shore.gltf")
    if not os.path.exists(path):
        return
    shore = [o for o in C.import_gltf(path) if o.type == "MESH"]
    for o in shore:
        o.parent = None
        o.location = (0, 0, 0)
        for dx in (-40, 40):
            d = bpy.data.objects.new(o.name + "_dup", o.data)
            C.link_object(d)
            d.location = (dx, 0, 0)
    # 岩はいったん遠くへ（地面の高さを調べるレイに当たらないように）
    roots = {o.name.split(".")[0]: o for o in objs if o.parent is None}
    for o in roots.values():
        o.location = (0, 0, -100)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()

    def ground(x, y):
        hit, loc, *_ = scene.ray_cast(dg, Vector((x, y, 50)), Vector((0, 0, -1)))
        return loc.z if hit else 0.0

    # 汀線（z ≈ 0）の少し下に置く: 陸側は砂に埋まり、海側の段が水に浸かる
    for name, (x, rot) in (("BeachRock_A", (-4.5, 4)), ("BeachRock_B", (3.5, -8)), ("BeachRock_C", (8.2, 20))):
        o = roots.get(name)
        if o is None:
            continue
        y = next((yy * 0.1 for yy in range(20, -80, -1) if ground(x, yy * 0.1) < -0.05), -3.0)
        o.location = (x, y, ground(x, y))
        o.rotation_euler = (0, 0, math.radians(rot))
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    sea = bpy.context.object
    sea.name = "PreviewSea"
    sea.scale = (400, 400, 20.0)
    sea.location = (0, 2.0 - 200, -10.0)
    C.assign(sea, water_material())
    scene.cycles.volume_bounces = 1
    scene.cycles.transmission_bounces = 8
    cam = scene.camera
    dbg = os.environ.get("OKI_DEBUG_DIR")
    if dbg:
        a = roots.get("BeachRock_A")
        if a is not None:
            cam.location = a.location + Vector((2.5, -5.0, 1.6))
            C.look_at(cam, a.location + Vector((0, 0.3, 0.1)))
            cam.data.lens = 35
            C.render(os.path.join(dbg, "beachrock_close.jpg"), samples=32)
    cam.location = (2.0, -17.0, 4.2)
    C.look_at(cam, (1.5, -2.0, 0.0))
    cam.data.lens = 32


PREVIEW = dict(cam_dir=(0.35, -1.0, 0.5), lens=40, spacing=1.2, extra=_preview_extra)
