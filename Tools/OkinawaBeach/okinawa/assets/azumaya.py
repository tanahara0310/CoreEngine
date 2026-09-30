"""5 赤瓦の東屋（あずまや）

寄棟の屋根を本瓦葺きの赤瓦で葺く。
  女瓦（平瓦）: 凹んだ溝の列          → 形状で波形を作る（軒の輪郭が波打つ）
  男瓦（丸瓦）: 溝の継ぎ目にかぶせる半円  → 同上
  漆喰        : 男瓦の両脇の目地・棟・隅棟・軒先の男瓦の小口を白く塗り固める
屋根の上の中央にシーサー、中にテーブルとベンチ。柱は木、礎石と床は琉球石灰岩。

寸法: 柱芯 3.5m 角、軒先 5m 角（軒高 ~2.3m）、棟の上端 ~3.8m。
原点: 床（基壇）の底面の中心。正面は -Y（シーサーが向く方向）。
"""
import math

import bmesh
import bpy
import numpy as np
from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import limestone
from .shisa import shisa_mesh, terracotta

PREVIEW = dict(cam_dir=(0.85, -1.25, 0.5), lens=40)

A = 2.5          # 軒先の半幅（5m 角）
R = 0.45         # 棟の半長
T_FB = A         # 前後の面の水平の奥行き（軒 → 棟）
T_LR = A - R     # 左右の面（軒 → 棟の端）
Z0 = 2.32        # 軒先での野地（屋根下地）の上面
RISE = 1.12      # 野地の立ち上がり
SLAB = 0.09      # 野地の厚さ（鉛直）
OVER = 0.05      # 瓦の軒先の出
POST = 1.75      # 柱芯の半間隔
FLOOR = 0.12     # 基壇の高さ

# 本瓦の断面（1 周期 = 男瓦中心 → 次の男瓦中心）
N_COL = 17
PERIOD = 2 * A / N_COL
RC = 0.058       # 男瓦の半径
FIL = 0.03       # 漆喰の幅
V_PAN = 0.02     # 女瓦の底
V_PAN_E = 0.036  # 女瓦の縁
V_CB = 0.048     # 男瓦の付け根
V_CT = 0.05      # 男瓦の盛り上がり

# 屋根面の定義: 原点 O、軒に沿う S、内向き D、自分の奥行き、隣の奥行き
FACES = [
    ((0, -A), (1, 0), (0, 1), T_FB, T_LR),
    ((0, A), (-1, 0), (0, -1), T_FB, T_LR),
    ((A, 0), (0, 1), (-1, 0), T_LR, T_FB),
    ((-A, 0), (0, -1), (1, 0), T_LR, T_FB),
]


def g_curve(u):
    """野地の断面（軒で緩く棟で急な、わずかな反り）"""
    return 0.72 * u + 0.28 * u * u


def g_slope(u):
    return 0.72 + 0.56 * u


def z_slab(t, T):
    return Z0 + RISE * g_curve(t / T)


def t_end(s, T, To):
    return min(T, T * (A - abs(s)) / To)


def _surface(face, s, t, v):
    """屋根面の点。v は面の法線方向のオフセット"""
    O, S, D, T, To = face
    k = RISE * g_slope(t / T) / T
    n = Vector((-D[0] * k, -D[1] * k, 1.0)).normalized()
    p = Vector((O[0] + S[0] * s + D[0] * t, O[1] + S[1] * s + D[1] * t, z_slab(t, T)))
    return p + n * v


# ---------------------------------------------------------------------------
# 本瓦の波板（1 面分）
# ---------------------------------------------------------------------------
def _profile():
    """1 周期分の断面点 (d, v, 区分)。区分: 0=男瓦, 1=漆喰, 2=女瓦（次の点までの区間の材質）"""
    pts = []
    for d in (0.0, 0.5 * RC, 0.85 * RC):
        pts.append((d, V_CB + V_CT * math.sqrt(max(0.0, 1 - (d / RC) ** 2)), 0))
    pts.append((RC, V_CB + 0.004, 1))
    pts.append((RC + FIL * 0.4, V_PAN_E + 0.008, 1))
    pan0 = RC + FIL
    pan1 = PERIOD - RC - FIL
    mid = (pan0 + pan1) / 2
    hw = (pan1 - pan0) / 2
    for i in range(4):
        d = pan0 + (pan1 - pan0) * i / 3
        pts.append((d, V_PAN + (V_PAN_E - V_PAN) * ((d - mid) / hw) ** 2, 2 if i < 3 else 1))
    pts.append((PERIOD - RC - FIL * 0.4, V_PAN_E + 0.008, 1))
    pts.append((PERIOD - RC, V_CB + 0.004, 0))
    for d in (0.85 * RC, 0.5 * RC):
        pts.append((PERIOD - d, V_CB + V_CT * math.sqrt(max(0.0, 1 - (d / RC) ** 2)), 0))
    return pts


def tile_face(face, fi, tile_mat, plaster_mat, rings=7):
    O, S, D, T, To = face
    prof = _profile()
    cols = []
    for k in range(N_COL):
        for d, v, z in prof:
            cols.append((-A + k * PERIOD + d, v, z))
    cols.append((A, prof[0][1], 0))
    n = len(cols)
    verts, faces, uvs, mats = [], [], [], []
    ovl = 0.05
    fr = [0.0, 0.1, 0.24, 0.4, 0.57, 0.74, 0.88, 1.0] if rings == 7 else list(np.linspace(0, 1, rings + 1))
    t0 = -OVER

    def tt(s, f):
        return t0 + (t_end(s, T, To) + ovl - t0) * f

    grid = []
    for f in fr:
        row = []
        for s, v, _ in cols:
            t = tt(s, f)
            row.append(len(verts))
            verts.append(_surface(face, s, t, v))
        grid.append(row)

    def uv_of(s, f):
        # u: 周期単位の位置（整数部 = 列番号、小数部 = 断面上の位置）+ 面ごとのずらし
        return ((s + A) / PERIOD + fi * 40.0, tt(s, f) + fi * 7.0)

    for j in range(len(fr) - 1):
        for i in range(n - 1):
            a, b = grid[j][i], grid[j][i + 1]
            c, d = grid[j + 1][i + 1], grid[j + 1][i]
            faces.append((a, b, c, d))
            s0, s1 = cols[i][0], cols[i + 1][0]
            uvs.append([uv_of(s0, fr[j]), uv_of(s1, fr[j]), uv_of(s1, fr[j + 1]), uv_of(s0, fr[j + 1])])
            mats.append(0 if cols[i][2] != 1 else 1)
    # 軒先の小口（瓦の厚み + 男瓦の端の漆喰）と、出の裏
    zb = z_slab(t0, T) - 0.012
    bot, inner = [], []
    for i, (s, v, z) in enumerate(cols):
        top = verts[grid[0][i]]
        bot.append(len(verts))
        verts.append(Vector((top.x, top.y, zb)))
    for i, (s, v, z) in enumerate(cols):
        p = _surface(face, s, 0.02, 0.0)
        inner.append(len(verts))
        verts.append(Vector((p.x, p.y, p.z - 0.004)))
    for i in range(n - 1):
        s0, s1 = cols[i][0], cols[i + 1][0]
        u0, u1 = (s0 + A) / PERIOD + fi * 40.0, (s1 + A) / PERIOD + fi * 40.0
        faces.append((grid[0][i], bot[i], bot[i + 1], grid[0][i + 1]))
        uvs.append([(u0, t0), (u0, t0 - 0.05), (u1, t0 - 0.05), (u1, t0)])
        mats.append(0 if cols[i][2] == 2 else 1)
        faces.append((bot[i], inner[i], inner[i + 1], bot[i + 1]))
        uvs.append([(u0, t0 - 0.05), (u0, t0 - 0.1), (u1, t0 - 0.1), (u1, t0 - 0.05)])
        mats.append(1)
    me = bpy.data.meshes.new(f"Tiles{fi}")
    me.from_pydata([tuple(v) for v in verts], [], faces)
    layer = me.uv_layers.new(name="Proc")
    li = 0
    for poly_uvs in uvs:
        for uv in poly_uvs:
            layer.data[li].uv = uv
            li += 1
    me.materials.append(tile_mat)
    me.materials.append(plaster_mat)
    for p, m in zip(me.polygons, mats):
        p.material_index = m
    me.update()
    obj = bpy.data.objects.new(f"Tiles{fi}", me)
    C.link_object(obj)
    return obj


# ---------------------------------------------------------------------------
# 掃引（断面を点列に沿わせる）
# ---------------------------------------------------------------------------
def sweep(name, path, profile, side_fn=None, uv_scale=1.0, cap=True):
    """閉じた断面 profile [(a,b)] を path に沿わせた立体。side_fn(i, tangent) -> side ベクトル
    Proc UV: u = 経路に沿った距離(m)（木目の方向）, v = 断面の周長(m)"""
    path = [Vector(p) for p in path]
    n = len(path)
    m = len(profile)
    verts, faces, uvs = [], [], []
    lengths = [0.0]
    for i in range(1, n):
        lengths.append(lengths[-1] + (path[i] - path[i - 1]).length)
    per = [0.0]
    for j in range(1, m + 1):
        a, b = profile[j - 1], profile[j % m]
        per.append(per[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    for i in range(n):
        t = (path[min(i + 1, n - 1)] - path[max(i - 1, 0)]).normalized()
        side = side_fn(i, t) if side_fn else t.cross(Vector((0, 0, 1)))
        side = (side - t * side.dot(t)).normalized()
        up = side.cross(t)
        if up.z < 0:
            up = -up
        for a, b in profile:
            verts.append(path[i] + side * a + up * b)
    for i in range(n - 1):
        for j in range(m):
            a = i * m + j
            b = i * m + (j + 1) % m
            faces.append((a, a + m, b + m, b))
            uvs.append([(lengths[i] * uv_scale, per[j]), (lengths[i + 1] * uv_scale, per[j]),
                        (lengths[i + 1] * uv_scale, per[j + 1]), (lengths[i] * uv_scale, per[j + 1])])
    if cap:
        faces.append(tuple(range(m)))
        uvs.append([(a * 0.5, b * 0.5 + 5.0) for a, b in profile])
        faces.append(tuple((n - 1) * m + j for j in range(m)))
        uvs.append([(a * 0.5 + 1.0, b * 0.5 + 5.0) for a, b in profile])
    obj = C.mesh_object(name, verts, faces, uvs)
    _fix_normals(obj)
    return obj


def _fix_normals(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data)
    bm.free()


def _rect(w, h, bevel=0.0):
    """中心基準の矩形断面（面取り付き）"""
    x, y = w / 2, h / 2
    if bevel <= 0:
        return [(-x, -y), (x, -y), (x, y), (-x, y)]
    b = bevel
    return [(-x + b, -y), (x - b, -y), (x, -y + b), (x, y - b), (x - b, y), (-x + b, y), (-x, y - b), (-x, -y + b)]


def timber(name, p0, p1, w, h, side=None, bevel=0.008):
    """角材。p0→p1 が木目の方向"""
    p0, p1 = Vector(p0), Vector(p1)
    sd = Vector(side) if side else None
    return sweep(name, [p0, p1], _rect(w, h, bevel), side_fn=(lambda i, t: sd) if sd else None)


def plaster_ridge(name, path, half_w, height, depth=0.2):
    """漆喰で塗り固めた棟（かまぼこ形）"""
    W = half_w
    prof = [(-W, -depth), (W, -depth), (W, 0.02), (W * 0.93, height * 0.45), (W * 0.75, height * 0.75),
            (W * 0.42, height * 0.95), (0.0, height), (-W * 0.42, height * 0.95), (-W * 0.75, height * 0.75),
            (-W * 0.93, height * 0.45), (-W, 0.02)]
    prof = prof[::-1]
    return sweep(name, path, prof)


# ---------------------------------------------------------------------------
# 野地（屋根の下地の板）と垂木
# ---------------------------------------------------------------------------
def roof_slab():
    bm = bmesh.new()
    nl = 8
    for fi, face in enumerate(FACES):
        O, S, D, T, To = face
        lam = [k / nl for k in range(nl + 1)]
        ss = [-(A - l * To) for l in lam] + [-R + 2 * R * k / 3 for k in range(1, 3)] + [A - l * To for l in lam[::-1]]
        ss = sorted(set(round(x, 6) for x in ss))
        rows = 6
        grid = []
        for j in range(rows + 1):
            row = []
            for s in ss:
                p = _surface(face, s, t_end(s, T, To) * j / rows, 0.0)
                row.append(bm.verts.new(p))
            grid.append(row)
        for j in range(rows):
            for i in range(len(ss) - 1):
                q = [grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]]
                try:
                    bm.faces.new(q)
                except ValueError:
                    pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges)
    ret = bmesh.ops.extrude_face_region(bm, geom=bm.faces[:], use_keep_orig=True)
    nv = [g for g in ret["geom"] if isinstance(g, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, vec=(0, 0, -SLAB), verts=nv)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("Slab")
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("Slab", me)
    C.link_object(obj)
    # 板目: 軒と平行（面ごとの S 方向）
    layer = me.uv_layers.new(name="Proc")
    for p in me.polygons:
        n = p.normal
        best = max(FACES, key=lambda f: abs(n.x * f[2][0] + n.y * f[2][1]))
        S, D = best[1], best[2]
        for li in p.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            layer.data[li].uv = (co.x * S[0] + co.y * S[1], co.x * D[0] + co.y * D[1] + co.z)
    return obj


def rafters():
    parts = []
    for fi, face in enumerate(FACES):
        O, S, D, T, To = face
        s = -A + 0.22
        k = 0
        while s < A - 0.2:
            te = t_end(s, T, To) - 0.04
            if te > 0.3:
                pts = []
                for i in range(6):
                    t = 0.02 + (te - 0.02) * i / 5
                    p = _surface(face, s, t, 0.0)
                    pts.append(p - Vector((0, 0, SLAB + 0.036)))
                parts.append(sweep(f"Rafter{fi}_{k}", pts, _rect(0.055, 0.07, 0.006),
                                   side_fn=lambda i, t, S=S: Vector((S[0], S[1], 0))))
            s += 0.42
            k += 1
    # 隅木と棟木
    for sx in (-1, 1):
        for sy in (-1, 1):
            pts = []
            for i in range(9):
                lam = 0.004 + 0.996 * i / 8
                x = sx * (A - lam * T_LR)
                y = sy * (A - lam * T_FB)
                z = z_slab(lam * T_FB, T_FB) - SLAB - 0.055
                pts.append(Vector((x, y, z)))
            parts.append(sweep(f"Hip{sx}{sy}", pts, _rect(0.09, 0.11, 0.008)))
    zr = z_slab(T_FB, T_FB) - SLAB - 0.06
    parts.append(timber("RidgeBeam", (-R - 0.08, 0, zr), (R + 0.08, 0, zr), 0.1, 0.12))
    return parts


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def _white(nb, w):
    n = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="2D")
    nb.link(w, n.inputs["Vector"])
    return n.outputs["Value"]


def tile_material():
    """赤瓦。Proc UV: u = 周期単位（整数部 = 列）, v = 軒からの距離(m)"""
    course = 0.27  # 瓦 1 枚の働き長さ

    def fn(nb):
        co = nb.coord("Object")
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        x = nb.math("FRACT", U)
        col_id = nb.math("FLOOR", U)
        dc = nb.math("MINIMUM", x, nb.sub(1.0, x))        # 男瓦中心からの距離（周期単位）
        cover = nb.smooth(dc, RC / PERIOD + 0.01, RC / PERIOD - 0.01)
        # 男瓦は女瓦と半周期ずれて並ぶ
        vv = nb.add(nb.math("DIVIDE", V, course), nb.mul(cover, 0.5))
        cf = nb.math("FRACT", vv)
        row = nb.math("FLOOR", vv)
        tid = _white(nb, nb.comb(nb.add(col_id, nb.mul(cover, 0.5)), row, 0.0))
        tid2 = _white(nb, nb.comb(row, nb.add(col_id, 13.7), 0.0))
        # 瓦ごとの焼き色
        base = nb.ramp(tid, [(0.0, srgb("#9c4a2f")), (0.3, srgb("#b35a37")), (0.6, srgb("#c26a42")),
                             (0.85, srgb("#a9553a")), (1.0, srgb("#8a3f2a"))])
        mid = nb.noise(co, 6.0, 5, 0.6)
        fine = nb.noise(co, 60.0, 4, 0.6)
        col = nb.hsv(base, 0.5, 0.9, nb.maprange(mid, 0.3, 0.7, 0.88, 1.08))
        # 褪せて白っぽい瓦・まだら
        faded = nb.math("MAXIMUM", nb.mul(nb.math("GREATER_THAN", tid2, 0.82), 0.7),
                        nb.smooth(nb.noise(co, 1.6, 4, 0.6, distortion=0.5), 0.55, 0.72))
        col = nb.mix(col, srgb("#d49a7a"), nb.mul(faded, 0.6))
        # 黒カビ: 流れに沿った縦筋（溝に多い）、軒先と下の方ほど濃い
        streak_co = nb.comb(nb.mul(U, 3.0), nb.mul(V, 0.35), 0.0)
        streak = nb.noise(streak_co, 2.5, 5, 0.6, distortion=0.3)
        pan = nb.sub(1.0, cover)
        lowv = nb.smooth(V, 1.6, 0.0)
        mould = nb.mul(nb.smooth(streak, 0.36, 0.66), nb.add(0.45, nb.mul(lowv, 0.55)))
        mould = nb.mul(mould, nb.add(0.5, nb.mul(pan, 0.5)))
        blot = nb.smooth(nb.noise(co, 1.8, 5, 0.65, distortion=0.3), 0.42, 0.6)
        mould = nb.math("MAXIMUM", mould, nb.mul(blot, 0.8))
        # 瓦の下端（重なり際）に溜まる汚れ
        edge_dirt = nb.mul(nb.smooth(cf, 0.35, 0.0), 0.45)
        mould = nb.math("MAXIMUM", mould, nb.mul(edge_dirt, nb.add(0.4, nb.mul(pan, 0.6))))
        mcol = nb.mix(srgb("#22201c"), srgb("#3d3a30"), fine)
        col = nb.mix(col, mcol, nb.math("MINIMUM", nb.mul(mould, 0.95), 0.92))
        # 灰白色の地衣類の斑点
        lich = nb.smooth(nb.noise(co, 14.0, 4, 0.6), 0.66, 0.72)
        col = nb.mix(col, srgb("#b9b3a5"), nb.mul(lich, 0.5))
        # 重なりの影（上の瓦の下端）と男瓦の継ぎ目の漆喰
        lap = nb.smooth(cf, 0.06, 0.0)
        col = nb.mix(col, srgb("#3a2419"), nb.mul(nb.mul(lap, pan), 0.6))
        collar = nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(cf, 0.03)), 0.05, 0.03), cover)
        col = nb.mix(col, nb.mix(srgb("#e3ded3"), srgb("#8f8a80"), nb.mul(mould, 0.8)), collar)
        rough = nb.maprange(nb.add(nb.mul(mould, 0.1), nb.mul(fine, 0.1)), 0.0, 0.2, 0.72, 0.92)
        # 高さ: 瓦の段差（下端が一段高い）+ 細かい肌
        step = nb.mul(nb.sub(1.0, cf), 1.0)
        height = nb.add(nb.add(nb.mul(step, 0.8), nb.mul(fine, 0.25)), nb.mul(collar, 0.6))
        cavity = nb.mul(nb.maprange(lap, 0, 1, 1.0, 0.55), nb.maprange(nb.mul(mould, 1.0), 0, 1, 1.0, 0.85))
        return dict(color=col, rough=rough, height=height, height_scale=0.012, cavity=cavity)

    return pbr_material("AzumayaTile", fn, res=2048, ao_distance=0.5)


def plaster_material():
    """漆喰。白地に雨だれの黒カビ、細かなひび"""

    def fn(nb):
        co = nb.coord("Object")
        geom = nb.node("ShaderNodeNewGeometry")
        up = nb.sep(geom.outputs["Normal"])[2]
        mid = nb.noise(co, 5.0, 5, 0.6)
        fine = nb.noise(co, 70.0, 4, 0.6)
        col = nb.mix(srgb("#e9e5dc"), srgb("#d3cdbf"), nb.smooth(mid, 0.4, 0.7))
        streak = nb.noise(nb.mapping(co, scale=(9, 9, 0.8)), 3.0, 5, 0.6)
        big = nb.noise(co, 1.3, 4, 0.6, distortion=0.4)
        mould = nb.math("MAXIMUM", nb.mul(nb.smooth(streak, 0.42, 0.7), 0.8),
                        nb.mul(nb.smooth(big, 0.48, 0.66), 0.9))
        mould = nb.mul(mould, nb.maprange(up, -0.2, 1.0, 0.7, 1.0))
        spots = nb.smooth(nb.noise(co, 18.0, 4, 0.6), 0.62, 0.7)
        mould = nb.math("MAXIMUM", mould, nb.mul(spots, 0.6))
        mcol = nb.mix(srgb("#34332e"), srgb("#6e6b62"), nb.smooth(fine, 0.35, 0.7))
        col = nb.mix(col, mcol, nb.mul(mould, 0.8))
        # 赤瓦の粉が付いた赤み
        col = nb.mix(col, srgb("#b88a72"), nb.mul(nb.smooth(nb.noise(co, 3.0, 4, 0.6), 0.62, 0.75), 0.35))
        cw = nb.vmath("ADD", co, nb.vmath("MULTIPLY", nb.noise(co, 12.0, 3, 0.5, out="Color"), (0.03, 0.03, 0.03)))
        crack = nb.smooth(nb.voronoi(cw, 14.0, feature="DISTANCE_TO_EDGE"), 0.008, 0.0)
        crack = nb.mul(crack, nb.smooth(nb.noise(co, 3.0, 3, 0.5), 0.55, 0.68))
        col = nb.mix(col, srgb("#5a554b"), nb.mul(crack, 0.5))
        rough = nb.maprange(fine, 0.3, 0.7, 0.78, 0.95)
        height = nb.add(nb.add(nb.mul(mid, 0.5), nb.mul(fine, 0.4)), nb.mul(crack, -0.8))
        return dict(color=col, rough=rough, height=height, height_scale=0.004,
                    cavity=nb.maprange(crack, 0, 1, 1.0, 0.6))

    return pbr_material("AzumayaPlaster", fn, res=2048, ao_distance=0.4)


def wood_material():
    """風雨にさらされた木（灰色がかった茶）。Proc UV: u = 木目の方向(m)"""

    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        co = nb.coord("Object")
        gco = nb.comb(nb.mul(U, 1.5), nb.mul(V, 30.0), 0.0)
        grain = nb.noise(gco, 1.0, 6, 0.6, distortion=0.8)
        rings = nb.wave(nb.comb(nb.mul(U, 0.2), nb.mul(V, 6.0), 0.0), scale=3.0, distortion=4.0, detail=3.0,
                        direction="Y")
        big = nb.noise(co, 1.2, 4, 0.6)
        base = nb.ramp(big, [(0.3, srgb("#5a4633")), (0.6, srgb("#6f5943")), (0.8, srgb("#7d6a55"))])
        col = nb.mix(base, srgb("#3b2c20"), nb.mul(nb.smooth(rings, 0.55, 0.85), 0.45))
        col = nb.mix(col, srgb("#34281e"), nb.mul(nb.smooth(grain, 0.55, 0.7), 0.5))
        # 日に焼けた銀灰色
        silver = nb.smooth(nb.noise(co, 2.0, 4, 0.6), 0.45, 0.7)
        col = nb.mix(col, srgb("#8d867b"), nb.mul(silver, 0.45))
        # 板の継ぎ目（野地板など）
        seam = nb.smooth(nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.mul(V, 1 / 0.18)), 0.5)), 0.49, 0.5)
        col = nb.mix(col, srgb("#1f1812"), nb.mul(seam, 0.6))
        dirt = nb.smooth(nb.noise(co, 5.0, 5, 0.6), 0.6, 0.72)
        col = nb.mix(col, srgb("#2a2621"), nb.mul(dirt, 0.4))
        fine = nb.noise(gco, 12.0, 4, 0.6)
        height = nb.add(nb.add(nb.mul(grain, 0.6), nb.mul(fine, 0.3)), nb.mul(seam, -1.0))
        rough = nb.maprange(grain, 0.3, 0.7, 0.72, 0.9)
        return dict(color=col, rough=rough, height=height, height_scale=0.003,
                    cavity=nb.maprange(seam, 0, 1, 1.0, 0.6))

    return pbr_material("AzumayaWood", fn, res=1024, ao_distance=0.6)


def floor_material():
    """基壇: 琉球石灰岩の敷石"""

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        vo = nb.node("ShaderNodeTexVoronoi", feature="DISTANCE_TO_EDGE", voronoi_dimensions="2D")
        warp = nb.vmath("ADD", nb.mapping(co, scale=(1, 1, 0)),
                        nb.vmath("MULTIPLY", nb.noise(co, 2.0, 3, 0.5, out="Color"), (0.06, 0.06, 0.0)))
        nb.link(warp, vo.inputs["Vector"])
        vo.inputs["Scale"].default_value = 2.3
        vo.inputs["Randomness"].default_value = 0.8
        edge = vo.outputs["Distance"]
        vc = nb.node("ShaderNodeTexVoronoi", feature="F1", voronoi_dimensions="2D")
        nb.link(warp, vc.inputs["Vector"])
        vc.inputs["Scale"].default_value = 2.3
        vc.inputs["Randomness"].default_value = 0.8
        rnd = nb.bw(vc.outputs["Color"])
        joint = nb.smooth(edge, 0.03, 0.012)
        # 側面（z が低い所）は目地なし
        top = nb.smooth(z, FLOOR - 0.02, FLOOR - 0.005)
        joint = nb.mul(joint, top)
        base = nb.ramp(rnd, [(0.0, srgb("#a09884")), (0.5, srgb("#bdb49e")), (1.0, srgb("#cfc6b0"))])
        mid = nb.noise(co, 4.0, 6, 0.6)
        fine = nb.noise(co, 50.0, 4, 0.6)
        pits = nb.smooth(nb.voronoi(co, 25.0), 0.15, 0.0)
        col = nb.mix(base, srgb("#8a8475"), nb.mul(nb.smooth(mid, 0.45, 0.7), 0.5))
        col = nb.mix(col, srgb("#4a453b"), nb.mul(pits, 0.6))
        lich = nb.smooth(nb.noise(co, 3.0, 5, 0.6), 0.6, 0.7)
        col = nb.mix(col, srgb("#3c3a34"), nb.mul(lich, 0.5))
        col = nb.mix(col, srgb("#6f6656"), joint)
        rough = nb.maprange(fine, 0.3, 0.7, 0.75, 0.95)
        height = nb.add(nb.add(nb.mul(joint, -1.0), nb.mul(pits, -0.5)), nb.mul(mid, 0.3))
        return dict(color=col, rough=rough, height=height, height_scale=0.012,
                    cavity=nb.maprange(joint, 0, 1, 1.0, 0.6))

    return pbr_material("AzumayaFloor", fn, res=1024, ao_distance=0.8)


# ---------------------------------------------------------------------------
def _box(name, size, loc, bevel=0.0, drop_bottom=False):
    o = geo.box(name, size, loc=loc)
    C.apply_transform(o)
    if bevel > 0:
        geo.bevel(o, width=bevel, segments=1)
    if drop_bottom:
        # 接地して見えない底面はテクスチャを食うので消す
        bm = bmesh.new()
        bm.from_mesh(o.data)
        zmin = min(v.co.z for v in bm.verts)
        bottom = [f for f in bm.faces if f.normal.z < -0.99 and f.calc_center_median().z < zmin + 1e-4]
        bmesh.ops.delete(bm, geom=bottom, context="FACES_ONLY")
        bm.to_mesh(o.data)
        bm.free()
    return o


def build():
    tile = tile_material()
    plaster = plaster_material()
    wood = wood_material()
    floor = floor_material()
    stone = limestone("AzumayaStone", res=1024, tide_top=-10, dark_top=0.5)
    parts = []
    # 屋根
    for fi, face in enumerate(FACES):
        parts.append(tile_face(face, fi, tile, plaster))
    slab = roof_slab()
    C.assign(slab, wood)
    parts.append(slab)
    for o in rafters():
        C.assign(o, wood)
        parts.append(o)
    # 棟と隅棟（漆喰）
    zr = z_slab(T_FB, T_FB)
    ridge_pts = [Vector((x, 0, zr + 0.13 + 0.05 * max(0.0, (abs(x) - R + 0.1) / 0.35) ** 2))
                 for x in np.linspace(-R - 0.12, R + 0.12, 9)]
    parts.append(plaster_ridge("Ridge", ridge_pts, 0.16, 0.19, depth=0.2))
    for sx in (-1, 1):
        for sy in (-1, 1):
            pts = []
            for i in range(12):
                lam = -0.035 + (1.0 + 0.035) * i / 11
                x = sx * (A - lam * T_LR)
                y = sy * (A - lam * T_FB)
                z = z_slab(lam * T_FB, T_FB) + 0.1
                # 先端をわずかに反り上げる
                z += 0.07 * max(0.0, (0.18 - lam) / 0.215) ** 1.5
                pts.append(Vector((x, y, z)))
            parts.append(plaster_ridge(f"HipCap{sx}{sy}", pts, 0.125, 0.15, depth=0.18))
    for o in parts[-5:]:
        C.assign(o, plaster)
    # 屋根のシーサー（棟の中央、正面向き）
    sh = shisa_mesh("RoofShisa", True, 0.0, height=0.45, target_tris=4000, h=0.006, seed=11, uv_angle=62)
    sh.location = (0, 0.02, zr + 0.13 + 0.17)
    C.assign(sh, terracotta("AzumayaShisa", res=1024, mould=0.6, plaster=0.3))
    parts.append(sh)

    # 柱・桁・梁
    zf = z_slab(A - POST, T_FB) - SLAB - 0.07     # 前後の桁の天端 = 垂木の下端
    zs = z_slab(A - POST, T_LR) - SLAB - 0.07     # 左右の梁の天端
    for sy in (-1, 1):
        parts.append(timber(f"Keta{sy}", (-POST - 0.22, sy * POST, zf - 0.09), (POST + 0.22, sy * POST, zf - 0.09),
                            0.13, 0.18, side=(0, 1, 0)))
    for sx in (-1, 1):
        parts.append(timber(f"Hari{sx}", (sx * POST, -POST - 0.22, zs - 0.1), (sx * POST, POST + 0.22, zs - 0.1),
                            0.13, 0.2, side=(1, 0, 0)))
    for sx in (-1, 1):
        for sy in (-1, 1):
            parts.append(timber(f"Post{sx}{sy}", (sx * POST, sy * POST, FLOOR + 0.13), (sx * POST, sy * POST, zf - 0.05),
                                0.18, 0.18, side=(1, 0, 0), bevel=0.014))
    for o in parts[-8:]:
        C.assign(o, wood)
    # テーブルとベンチ
    furn = [
        timber("TableTop", (-0.6, 0, FLOOR + 0.70), (0.6, 0, FLOOR + 0.70), 0.78, 0.05, side=(0, 1, 0)),
        timber("BenchA", (-0.6, -0.72, FLOOR + 0.42), (0.6, -0.72, FLOOR + 0.42), 0.32, 0.045, side=(0, 1, 0)),
        timber("BenchB", (-0.6, 0.72, FLOOR + 0.42), (0.6, 0.72, FLOOR + 0.42), 0.32, 0.045, side=(0, 1, 0)),
    ]
    for x in (-0.42, 0.42):
        furn.append(timber(f"TableLeg{x}", (x, 0, FLOOR), (x, 0, FLOOR + 0.68), 0.56, 0.07, side=(0, 1, 0)))
        furn.append(timber(f"TableFoot{x}", (x, -0.3, FLOOR + 0.03), (x, 0.3, FLOOR + 0.03), 0.09, 0.06,
                           side=(1, 0, 0)))
        for sy in (-1, 1):
            furn.append(timber(f"BenchLeg{x}{sy}", (x, sy * 0.72, FLOOR), (x, sy * 0.72, FLOOR + 0.4), 0.26, 0.06,
                               side=(0, 1, 0)))
    for o in furn:
        C.assign(o, wood)
    parts += furn
    # 基壇と礎石
    base = _box("Floor", (4.3, 4.3, FLOOR), (0, 0, FLOOR / 2), bevel=0.02, drop_bottom=True)
    C.assign(base, floor)
    parts.append(base)
    for sx in (-1, 1):
        for sy in (-1, 1):
            st = _box(f"Stone{sx}{sy}", (0.3, 0.3, 0.14), (sx * POST, sy * POST, FLOOR + 0.07 - 0.01), bevel=0.025)
            C.assign(st, stone)
            parts.append(st)
    return {"Azumaya_A": parts}
