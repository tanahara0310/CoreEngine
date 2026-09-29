"""浜辺の小物

  Driftwood_A / B     : 白く晒された流木（B は根張り付き）
  Shell_Cowrie        : タカラガイ（光沢のある斑点模様, 約 6 cm）
  Shell_SpiderConch   : スイジガイ（長い突起が「水」の字に見える, 約 25 cm）
  CoralPiece_A / B    : 白化して打ち上げられた枝サンゴ（A: ミドリイシの枝, B: 指状サンゴの塊）
  Coconut_Husk        : 落ちたココヤシの実（丸ごと + 割れた殻）
  GlassFloat          : ガラスの浮き玉（網ロープ付き, 約 30 cm）

原点は接地点（最下点が z=0）。
"""
import math
import random

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb

PREVIEW = dict(cam_dir=(0.2, -1.0, 0.7), lens=45)


# ---------------------------------------------------------------------------
# 形状の補助
# ---------------------------------------------------------------------------
def _verts_np(me):
    a = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)


def _ground(objs):
    """変形を適用し、全体の最下点を z=0 に揃える"""
    bpy.context.view_layer.update()  # matrix_world を確定させる
    for o in objs:
        C.apply_transform(o)
    zmin = min(_verts_np(o.data)[:, 2].min() for o in objs)
    for o in objs:
        o.data.transform(Matrix.Translation((0, 0, -zmin)))
        o.data.update()
    return objs


def _remesh(obj, voxel):
    m = obj.modifiers.new("Remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = voxel
    m.adaptivity = 0.0
    C.apply_modifiers(obj)


def _lsmooth(obj, factor=0.5, iterations=3):
    m = obj.modifiers.new("LSmooth", "LAPLACIANSMOOTH")
    m.lambda_factor = factor
    m.iterations = iterations
    m.use_volume_preserve = True
    m.use_normalized = True
    C.apply_modifiers(obj)


def _union(objs, name, voxel, smooth=(0.4, 2), target_tris=4000, smooth_angle=None):
    """閉じたパーツを結合 → ボクセルリメッシュで一体化 → 平滑化 → デシメート"""
    bpy.context.view_layer.update()
    obj = C.join(objs, name)
    _remesh(obj, voxel)
    if smooth:
        _lsmooth(obj, *smooth)
    geo.decimate(obj, target_tris=target_tris)
    C.set_smooth(obj, True, angle=smooth_angle)
    return obj


def _sphere(name, radius, loc, scale=(1, 1, 1), subdiv=3):
    bm = geo.icosphere_bm(subdiv, radius)
    bmesh.ops.scale(bm, vec=Vector(scale), verts=bm.verts)
    o = C.from_bmesh(name, bm)
    o.location = loc
    return o


def _wander(rnd, start, direction, length, n, bend=0.3, zdamp=1.0, zbias=0.0):
    """ゆるく曲がる点列"""
    p = Vector(start)
    d = Vector(direction).normalized()
    pts = [p.copy()]
    step = length / (n - 1)
    for _ in range(n - 1):
        d = (d + Vector((rnd.gauss(0, bend), rnd.gauss(0, bend), rnd.gauss(0, bend) * zdamp + zbias)) * 0.3)
        d.normalize()
        p = p + d * step
        pts.append(p.copy())
    return pts


def _cyl_coords(nb, twist=0.0, radius=0.3):
    """tube_along の Proc UV (u=周, v=長さ m) → 継ぎ目の無い円筒座標"""
    uv = nb.sep(nb.uv("Proc"))
    a = nb.add(nb.mul(uv[0], math.tau), nb.mul(uv[1], twist))
    return nb.comb(nb.mul(nb.math("COSINE", a), radius), nb.mul(nb.math("SINE", a), radius), uv[1]), uv


# ---------------------------------------------------------------------------
# 流木
# ---------------------------------------------------------------------------
DW_TWIST = 1.6  # 木目のねじれ (rad/m)


def driftwood_material(name):
    def fn(nb):
        cyl, uv = _cyl_coords(nb, DW_TWIST, 0.3)
        co = nb.coord("Object")
        # 木目: 周方向に細かく長さ方向に長い筋
        grain = nb.noise(nb.mapping(cyl, scale=(9.0, 9.0, 0.25)), 3.0, 6, 0.6, distortion=0.25)
        grain2 = nb.noise(nb.mapping(cyl, scale=(22.0, 22.0, 0.5)), 3.0, 4, 0.55)
        crack = nb.smooth(nb.math("ABSOLUTE", nb.sub(grain, 0.5)), 0.03, 0.0)
        crack = nb.mul(crack, nb.smooth(nb.noise(cyl, 2.0, 2, 0.5), 0.4, 0.55))
        big = nb.noise(co, 1.5, 4, 0.55)
        col = nb.ramp(big, [(0.3, srgb("#8f897e")), (0.5, srgb("#a8a397")), (0.72, srgb("#bfbaae"))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(grain2, 0.25, 0.75, 0.82, 1.12))
        col = nb.mix(col, srgb("#a19282"), nb.mul(nb.smooth(big, 0.35, 0.2), 0.5))
        # 日陰側・割れ目の奥に残る茶色
        col = nb.mix(col, srgb("#7d6a55"), nb.mul(crack, 0.85))
        knot = nb.smooth(nb.voronoi(nb.mapping(cyl, scale=(1, 1, 0.35)), 4.0), 0.12, 0.03)
        col = nb.mix(col, srgb("#6f6556"), nb.mul(knot, 0.6))
        # 砂の付着（下側）
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        sand = nb.mul(nb.smooth(nz, -0.2, -0.7), nb.smooth(nb.noise(co, 12.0, 3, 0.6), 0.45, 0.6))
        col = nb.mix(col, srgb("#d8ceb6"), nb.mul(sand, 0.7))
        height = nb.add(nb.mul(grain, 0.6), nb.mul(crack, -1.5))
        height = nb.add(height, nb.mul(grain2, 0.6))
        rough = nb.maprange(grain2, 0.3, 0.7, 0.8, 0.92)
        return dict(color=col, rough=rough, height=height, height_scale=0.004,
                    cavity=nb.maprange(crack, 0, 1, 1.0, 0.5))

    return pbr_material(name, fn, res=1024, ao_distance=0.3)


def _dw_tube(name, pts, r0, r1, rnd, sides=14, round_end=True, flare=None):
    n = len(pts)
    L = sum((pts[i] - pts[i - 1]).length for i in range(1, n))
    off = Vector((rnd.uniform(-50, 50), rnd.uniform(-50, 50), 0))
    radii = []
    acc = 0.0
    for i in range(n):
        if i:
            acc += (pts[i] - pts[i - 1]).length
        t = acc / L
        r = r0 + (r1 - r0) * t
        r *= 1.0 + 0.12 * geo.fbm(Vector((acc * 1.5, 0, 0)) + off, 2)
        if flare:
            r *= 1.0 + flare[0] * math.exp(-acc / flare[1])
        # 端は丸く摩耗
        e = min(r1 * 1.3, L * 0.1)
        if round_end and acc > L - e:
            r *= math.sqrt(max(0.05, 1.0 - ((acc - (L - e)) / e) ** 2))
        e0 = min(r0 * (1 + flare[0]) if flare else r0, L * 0.1)
        if round_end and flare and acc < e0:
            r *= math.sqrt(max(0.15, 1.0 - ((e0 - acc) / e0) ** 2))
        radii.append(r)
    seglen = [0.0]
    for i in range(1, n):
        seglen.append(seglen[-1] + (pts[i] - pts[i - 1]).length)

    def ring_fn(i, j, a):
        s = seglen[i]
        aa = a + DW_TWIST * s
        p = Vector((math.cos(aa) * 1.2, math.sin(aa) * 1.2, s * 0.8)) + off
        groove = max(0.0, math.sin(5 * aa + geo.fbm(p, 2) * 2.5)) ** 10
        return 1.0 + 0.07 * geo.fbm(p, 3) - 0.09 * groove

    return geo.tube_along(pts, radii, sides=sides, name=name, ring_fn=ring_fn)


def driftwood(prefix, length, seed, roots=False):
    rnd = random.Random(seed)
    mat = driftwood_material(prefix + "Wood")
    r0 = 0.085 if not roots else 0.1
    main = _wander(rnd, (-length / 2, 0, r0), (1, 0, 0), length, 40, bend=0.27, zdamp=0.2)
    parts = [_dw_tube(prefix + "_main", main, r0, 0.035, rnd, sides=16,
                      flare=(0.45, 0.35) if roots else None)]
    # 折れた枝
    for k in range(2 if not roots else 1):
        i = int(len(main) * rnd.uniform(0.35, 0.75))
        base = main[i]
        tang = (main[i + 1] - main[i - 1]).normalized()
        side = tang.cross(Vector((0, 0, 1))).normalized() * rnd.choice((-1, 1))
        d = (tang * 0.8 + side + Vector((0, 0, rnd.uniform(0.1, 0.35)))).normalized()
        pts = _wander(rnd, base - d * 0.03, d, rnd.uniform(0.3, 0.6), 12, bend=0.3, zdamp=0.3)
        parts.append(_dw_tube(f"{prefix}_br{k}", pts, 0.04, 0.018, rnd, sides=10))
    if roots:
        # 根張り: 根元から放射状に伸びて折れた根
        tip = main[0]
        axis = (main[0] - main[3]).normalized()
        u = axis.cross(Vector((0, 0, 1))).normalized()
        w = axis.cross(u)
        for k in range(6):
            a = k / 6 * math.tau + rnd.uniform(-0.3, 0.3)
            radial = u * math.cos(a) + w * math.sin(a)
            if radial.z < -0.4:
                radial.z *= 0.2
            d = (axis * 0.5 + radial).normalized()
            start = tip - axis * 0.12
            pts = _wander(rnd, start, d, rnd.uniform(0.25, 0.5), 12, bend=0.6, zdamp=0.4)
            # 根は途中で後ろ（幹の方向）へ曲がる。先は折れて丸く摩耗
            pts = [p - axis * 0.2 * (i / 11) ** 2 for i, p in enumerate(pts)]
            parts.append(_dw_tube(f"{prefix}_root{k}", pts, 0.058, 0.03, rnd, sides=10))
    for o in parts:
        C.set_smooth(o, True)
        C.assign(o, mat)
    # 少し回転させて置く
    for o in parts:
        o.rotation_euler = (0, 0, rnd.uniform(-0.2, 0.2))
    return _ground(parts)


# ---------------------------------------------------------------------------
# タカラガイ
# ---------------------------------------------------------------------------
def cowrie_material():
    def fn(nb):
        co = nb.coord("Object")
        s = nb.sep(co)
        x, y, z = s[0], s[1], s[2]
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        # 斑点（大小 2 層、セルごとに半径が違う）
        spots = None
        for sc, off in ((120.0, 0.0), (260.0, 3.7)):
            v = nb.mapping(co, loc=(off, off * 0.3, 0))
            d = nb.voronoi(v, sc)
            r = nb.sep(nb.voronoi(v, sc, out="Color"))[0]
            rad = nb.maprange(r, 0, 1, 0.18, 0.45)
            sp = nb.smooth(nb.sub(d, rad), 0.06, -0.02)
            spots = sp if spots is None else nb.math("MAXIMUM", spots, sp)
        base = nb.ramp(nb.noise(co, 60.0, 3, 0.5), [(0.3, srgb("#e6dccb")), (0.55, srgb("#cfc8bd")),
                                                     (0.75, srgb("#d9b99a"))])
        # 背の外套線（斑点が途切れる明るい線）
        mantle = nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.add(y, nb.mul(nb.sub(nb.noise(co, 40, 2, 0.5), 0.5), 0.004))),
                                  0.0022, 0.0008), nb.smooth(nz, 0.6, 0.9))
        dorsal = nb.smooth(nz, -0.3, 0.2)
        spot_col = nb.mix(srgb("#2e1d12"), srgb("#5a3a22"), nb.noise(co, 90.0, 2, 0.5))
        col = nb.mix(base, spot_col, nb.mul(nb.mul(spots, dorsal), nb.sub(1.0, mantle)))
        col = nb.mix(col, srgb("#d9c8a6"), nb.mul(mantle, 0.8))
        # 腹面は白く、殻口の歯は橙褐色
        belly = nb.smooth(nz, -0.2, -0.6)
        col = nb.mix(col, srgb("#f3efe6"), belly)
        teeth = nb.math("GREATER_THAN", nb.math("SINE", nb.mul(x, math.tau / 0.0024)), 0.2)
        ay = nb.math("ABSOLUTE", y)
        tz = nb.mul(nb.mul(nb.smooth(ay, 0.0075, 0.004), nb.smooth(ay, 0.0006, 0.0014)), belly)
        tz = nb.mul(tz, nb.smooth(nb.math("ABSOLUTE", x), 0.027, 0.02))
        col = nb.mix(col, srgb("#9c5a33"), nb.mul(teeth, tz))
        slit = nb.mul(nb.smooth(ay, 0.0012, 0.0004), belly)
        col = nb.mix(col, srgb("#3a2618"), slit)
        height = nb.add(nb.mul(nb.mul(teeth, tz), 1.0), nb.mul(slit, -1.0))
        return dict(color=col, rough=nb.mixf(0.07, 0.25, nb.mul(slit, 1.0)), height=height, height_scale=0.0004)

    return pbr_material("Cowrie", fn, res=512, ao_distance=0.02)


def cowrie():
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=40, v_segments=24, radius=1.0)
    L, W, H = 0.03, 0.021, 0.02
    for v in bm.verts:
        x, y, z = v.co
        # 卵形（後ろ側が少し太い）、背は平たいドームで裾（縁）が張り出す
        wy = W * (1.0 + 0.12 * x)
        if z >= 0:
            zz = H * (z ** 0.85) * (1.0 - 0.08 * x)
            spread = 1.0 + 0.1 * math.exp(-(z / 0.25) ** 2)
        else:
            zz = 0.005 * z
            spread = 1.1
        v.co = Vector((L * x, wy * y * spread, zz))
    for v in bm.verts:
        x, y, z = v.co
        if z < 0.002:
            # 腹面の殻口（細い溝）
            v.co.z += 0.004 * math.exp(-(y / 0.0022) ** 2) * min(1.0, (L - abs(x)) / 0.004)
        # 両端の水管溝の切れ込み
        endw = math.exp(-(y / 0.004) ** 2) * max(0.0, (abs(x) - L * 0.85) / (L * 0.15))
        v.co.x -= math.copysign(0.0025 * endw, x)
    o = C.from_bmesh("Cowrie", bm)
    C.set_smooth(o, True)
    C.assign(o, cowrie_material())
    return _ground([o])


# ---------------------------------------------------------------------------
# スイジガイ
# ---------------------------------------------------------------------------
def spider_conch_material():
    def fn(nb):
        co = nb.coord("Object")
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        big = nb.noise(co, 25.0, 4, 0.55)
        mottle = nb.noise(nb.mapping(co, scale=(1.0, 2.5, 1.0)), 45.0, 5, 0.6, distortion=0.6)
        col = nb.ramp(mottle, [(0.35, srgb("#efe6d2")), (0.5, srgb("#d9c3a0")), (0.62, srgb("#9a6a42")),
                               (0.75, srgb("#6b4428"))])
        col = nb.mix(col, srgb("#f1e9d8"), nb.mul(nb.smooth(big, 0.55, 0.7), 0.5))
        # 殻口側（下面）は光沢のある橙〜桃色、奥は紫褐色
        under = nb.smooth(nz, -0.1, -0.55)
        ap = nb.ramp(nb.noise(co, 18.0, 3, 0.5), [(0.3, srgb("#e8a27a")), (0.55, srgb("#f0bf96")),
                                                  (0.75, srgb("#c9785d"))])
        ap = nb.mix(ap, srgb("#6d3c3c"), nb.mul(nb.smooth(nb.noise(co, 60.0, 3, 0.5), 0.58, 0.7), 0.6))
        col = nb.mix(col, ap, under)
        growth = nb.noise(nb.mapping(co, scale=(3.0, 0.4, 0.4)), 120.0, 2, 0.5)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(growth, 0.3, 0.7, 0.92, 1.05))
        # 摩耗して白くなった突起・こぶ
        worn = nb.smooth(nb.node("ShaderNodeNewGeometry").outputs["Pointiness"], 0.52, 0.6)
        col = nb.mix(col, srgb("#f3eee4"), nb.mul(worn, 0.5))
        height = nb.add(nb.mul(growth, 0.6), nb.mul(mottle, 0.2))
        rough = nb.mixf(0.45, 0.14, under)
        return dict(color=col, rough=rough, height=height, height_scale=0.0006)

    return pbr_material("SpiderConch", fn, res=1024, ao_distance=0.06)


def _spine(name, pts, r0, r1):
    n = len(pts)
    radii = [r0 + (r1 - r0) * (i / (n - 1)) ** 0.8 for i in range(n)]
    t = geo.tube_along(pts, radii, sides=10, name=name)
    tip = _sphere(name + "_tip", r1, pts[-1], subdiv=2)
    return [t, tip]


def spider_conch():
    rnd = random.Random(7)
    parts = []
    # 胴（体層）: x+ が前（水管溝）, 外唇は y+ 側に張り出す
    body = _sphere("sc_body", 1.0, (0.0, 0.0, 0.034), scale=(0.075, 0.048, 0.036), subdiv=4)
    parts.append(body)
    # 肩のこぶ（中央の 2 つが大きい）
    for k, (x, r) in enumerate(((-0.04, 0.009), (-0.018, 0.014), (0.006, 0.016), (0.03, 0.011), (0.05, 0.008))):
        parts.append(_sphere(f"sc_knob{k}", r, (x, 0.014, 0.062 - abs(x) * 0.3), scale=(1.0, 0.8, 1.25),
                             subdiv=2))
    # 螺塔（後方）: 段のある円錐
    sp = geo.bezier_points((-0.045, -0.006, 0.045), (-0.07, -0.01, 0.05), (-0.095, -0.014, 0.056),
                           (-0.12, -0.018, 0.06), 24)
    sp_r = [0.03 * (1.0 - i / 23) ** 1.2 + 0.002 for i in range(24)]
    parts.append(geo.tube_along(sp, sp_r, sides=16, name="sc_spire",
                                ring_fn=lambda i, j, a: 1.0 + 0.16 * max(0.0, math.sin(i * 1.3)) ** 2))
    # 外唇（平たい翼）
    parts.append(_sphere("sc_lip", 1.0, (0.005, 0.05, 0.02), scale=(0.072, 0.045, 0.013), subdiv=3))
    # 突起: 前方の水管溝 / 後方の溝（鉤状）/ 外唇から扇状に 4 本
    # (起点, 向き, 長さ, (横への曲がり, 持ち上がり), 根元半径)
    spines = [
        ((0.065, 0.0, 0.025), (1.0, -0.25), 0.12, (-0.6, 0.35), 0.011),
        ((-0.055, 0.03, 0.03), (-0.75, 0.7), 0.11, (0.9, 0.3), 0.011),
    ]
    for ang, ln in ((28, 0.1), (62, 0.095), (100, 0.1), (138, 0.09)):
        a = math.radians(ang)
        p0 = (0.005 + 0.06 * math.cos(a), 0.05 + 0.038 * math.sin(a), 0.018)
        spines.append((p0, (math.cos(a), math.sin(a)), ln * 1.1, (0.8 if ang < 90 else -0.8, 0.0), 0.009))
    for k, (p0, d, ln, curl, r0) in enumerate(spines):
        p0 = Vector(p0)
        d = Vector((d[0], d[1], 0)).normalized()
        side = Vector((-d.y, d.x, 0))
        # 先端に向かって横へ曲がり、少し持ち上がる
        p1 = p0 + d * ln * 0.4 + Vector((0, 0, 0.004))
        p2 = p0 + d * ln * 0.75 + side * curl[0] * ln * 0.15 + Vector((0, 0, 0.012 + curl[1] * 0.01))
        p3 = p0 + d * ln + side * curl[0] * ln * 0.4 + Vector((0, 0, 0.026 + curl[1] * 0.015))
        pts = geo.bezier_points(p0, p1, p2, p3, 18)
        parts += _spine(f"sc_spine{k}", pts, r0, 0.003)
    obj = _union(parts, "SpiderConch", 0.0016, smooth=(0.5, 3), target_tris=5000, smooth_angle=None)
    # こぶ・螺肋の凹凸
    off = Vector((3.1, 7.7, 1.3))
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    geo.displace_along_normals(bm, lambda co, n: 0.0012 * geo.fbm(co * 60.0 + off, 3) * max(0.0, n.z))
    bm.to_mesh(obj.data)
    bm.free()
    C.set_smooth(obj, True)
    C.assign(obj, spider_conch_material())
    return _ground([obj])


# ---------------------------------------------------------------------------
# サンゴ片
# ---------------------------------------------------------------------------
def coral_material(name, pore_scale, res=1024):
    def fn(nb):
        co = nb.coord("Object")
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        # 莢（ポリプの入っていた小孔）: 縁が盛り上がり、中が暗い
        d = nb.voronoi(co, pore_scale)
        d2 = nb.voronoi(nb.mapping(co, loc=(1.3, 2.1, 0.7)), pore_scale * 2.3)
        rim = nb.mul(nb.smooth(d, 0.12, 0.26), nb.smooth(d, 0.42, 0.3))
        pit = nb.smooth(d, 0.16, 0.06)
        fine_pit = nb.smooth(d2, 0.14, 0.05)
        big = nb.noise(co, 12.0, 4, 0.55)
        col = nb.ramp(big, [(0.3, srgb("#e2dac6")), (0.5, srgb("#ede7d8")), (0.7, srgb("#f6f2e8"))])
        col = nb.mix(col, srgb("#b9ad92"), nb.mul(pit, 0.75))
        col = nb.mix(col, srgb("#cfc5ae"), nb.mul(fine_pit, 0.5))
        # 下面・根元に薄い藻や黄土色の汚れ
        stain = nb.mul(nb.smooth(nz, 0.0, -0.6), nb.smooth(nb.noise(co, 6.0, 3, 0.5), 0.4, 0.6))
        col = nb.mix(col, srgb("#c2b58f"), nb.mul(stain, 0.6))
        ochre = nb.smooth(nb.noise(co, 3.0, 3, 0.5, distortion=0.5), 0.6, 0.72)
        col = nb.mix(col, srgb("#dcc8a0"), nb.mul(ochre, 0.4))
        height = nb.add(nb.sub(nb.mul(rim, 0.8), nb.mul(pit, 1.0)), nb.mul(fine_pit, -0.4))
        height = nb.add(height, nb.mul(big, 0.3))
        rough = nb.maprange(pit, 0, 1, 0.82, 0.95)
        cavity = nb.maprange(nb.math("MAXIMUM", pit, nb.mul(fine_pit, 0.6)), 0, 1, 1.0, 0.55)
        return dict(color=col, rough=rough, height=height, height_scale=0.0012, cavity=cavity)

    return pbr_material(name, fn, res=res, ao_distance=0.08)


def _branch_tree(rnd, start, d, length, radius, depth, out):
    n = max(4, int(length / 0.02))
    pts = _wander(rnd, start, d, length, n, bend=0.18, zdamp=0.4)
    r_tip = max(0.0075, radius * 0.55)
    radii = [radius + (r_tip - radius) * i / (n - 1) for i in range(n)]
    out.append((pts, radii, depth))
    if depth <= 0:
        return
    for _ in range(rnd.randint(1, 3)):
        i = rnd.randint(int(n * 0.25), int(n * 0.8))
        tang = (pts[min(i + 1, n - 1)] - pts[i - 1]).normalized()
        side = tang.cross(Vector((0, 0, 1))).normalized() * rnd.choice((-1, 1))
        a = math.radians(rnd.uniform(30, 55))
        nd = (tang * math.cos(a) + side * math.sin(a) + Vector((0, 0, rnd.uniform(-0.05, 0.25)))).normalized()
        _branch_tree(rnd, pts[i], nd, length * rnd.uniform(0.35, 0.6), radii[i] * 0.8, depth - 1, out)


def coral_staghorn():
    rnd = random.Random(31)
    tree = []
    _branch_tree(rnd, (-0.15, 0.0, 0.018), (1, 0.1, 0.08), 0.3, 0.018, 2, tree)
    parts = []
    for k, (pts, radii, depth) in enumerate(tree):
        parts.append(geo.tube_along(pts, radii, sides=10, name=f"cA_{k}", cap_start=True, cap_end=True))
        parts.append(_sphere(f"cA_tip{k}", radii[-1], pts[-1], subdiv=2))
    obj = _union(parts, "CoralPiece_A", 0.0022, smooth=(0.4, 2), target_tris=4000)
    # 莢の突起（ミドリイシの枝の表面のざらつき）
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    geo.displace_along_normals(bm, lambda co, n: 0.0012 * geo.fbm(co * 90.0, 2))
    bm.to_mesh(obj.data)
    bm.free()
    C.set_smooth(obj, True)
    C.assign(obj, coral_material("CoralA", 260.0))
    return _ground([obj])


def coral_finger():
    """ハマサンゴ類の指状群体のかけら: 低い土台から多数の太い指が立つ"""
    rnd = random.Random(47)
    parts = [_sphere("cB_base", 1.0, (0, 0, 0.022), scale=(0.1, 0.075, 0.03), subdiv=3),
             _sphere("cB_base2", 1.0, (0.04, -0.02, 0.02), scale=(0.06, 0.05, 0.025), subdiv=3)]
    k = 0
    tries = 0
    placed = []
    while k < 15 and tries < 400:
        tries += 1
        x, y = rnd.uniform(-0.085, 0.09), rnd.uniform(-0.065, 0.06)
        if (x / 0.095) ** 2 + (y / 0.07) ** 2 > 1.0:
            continue
        if any((x - px) ** 2 + (y - py) ** 2 < 0.032 ** 2 for px, py in placed):
            continue
        placed.append((x, y))
        start = Vector((x, y, 0.02))
        # 外側の指ほど外へ傾く
        out = Vector((x, y, 0))
        tilt = 0.15 + 3.0 * out.length
        d = Vector((0, 0, 1)) + out.normalized() * tilt + Vector((rnd.gauss(0, 0.15), rnd.gauss(0, 0.15), 0))
        L = rnd.uniform(0.045, 0.1) * (1.1 - out.length * 3)
        pts = _wander(rnd, start, d, max(L, 0.03), 6, bend=0.15)
        r = rnd.uniform(0.012, 0.017)
        radii = [r * (1.05 - 0.1 * i / 5) for i in range(6)]
        parts.append(geo.tube_along(pts, radii, sides=12, name=f"cB_f{k}"))
        if rnd.random() < 0.8:
            parts.append(_sphere(f"cB_tip{k}", radii[-1] * 1.02, pts[-1], subdiv=2))
        # 二股に分かれる指
        if rnd.random() < 0.3:
            d2 = (pts[-1] - pts[-2]).normalized() + Vector((rnd.gauss(0, 0.6), rnd.gauss(0, 0.6), 0.2))
            p2 = _wander(rnd, pts[-2], d2, 0.035, 4, bend=0.1)
            parts.append(geo.tube_along(p2, [radii[-1] * 0.85] * 4, sides=10, name=f"cB_g{k}"))
            parts.append(_sphere(f"cB_gt{k}", radii[-1] * 0.85, p2[-1], subdiv=2))
        k += 1
    obj = _union(parts, "CoralPiece_B", 0.002, smooth=(0.5, 3), target_tris=4500)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    geo.displace_along_normals(bm, lambda co, n: 0.0015 * geo.fbm(co * 60.0 + Vector((5, 5, 5)), 3))
    bm.to_mesh(obj.data)
    bm.free()
    obj.rotation_euler = (math.radians(6), math.radians(-4), 0)
    C.set_smooth(obj, True)
    C.assign(obj, coral_material("CoralB", 420.0))
    return _ground([obj])


# ---------------------------------------------------------------------------
# ココヤシの実（殻）
# ---------------------------------------------------------------------------
def husk_material():
    def fn(nb):
        co = nb.coord("Object")
        fib = nb.noise(nb.mapping(co, scale=(6.0, 60.0, 60.0)), 3.0, 5, 0.6)
        fib2 = nb.noise(nb.mapping(co, scale=(3.0, 150.0, 150.0)), 2.0, 3, 0.5)
        big = nb.noise(co, 8.0, 4, 0.55)
        # 外皮（灰褐色で少し光沢）が剥げて繊維が見える
        skin_n = nb.noise(co, 14.0, 5, 0.6, distortion=0.3)
        skin = nb.smooth(skin_n, 0.47, 0.55)
        skin_col = nb.ramp(big, [(0.3, srgb("#5e4a36")), (0.55, srgb("#7a6249")), (0.75, srgb("#8f7e6c"))])
        fib_col = nb.ramp(fib, [(0.3, srgb("#5a3d22")), (0.55, srgb("#8c6440")), (0.75, srgb("#a88257"))])
        fib_col = nb.hsv(fib_col, 0.5, 1.0, nb.maprange(fib2, 0.3, 0.7, 0.75, 1.15))
        col = nb.mix(fib_col, skin_col, skin)
        # 日焼けで白っぽく
        col = nb.mix(col, srgb("#9d9384"), nb.mul(nb.smooth(big, 0.55, 0.75), 0.35))
        height = nb.add(nb.mul(skin, 0.8), nb.mul(nb.mul(fib2, nb.sub(1.0, skin)), 0.8))
        rough = nb.mixf(0.95, 0.7, skin)
        return dict(color=col, rough=rough, height=height, height_scale=0.002,
                    cavity=nb.maprange(nb.mul(fib2, nb.sub(1.0, skin)), 0, 1, 1.0, 0.75))

    return pbr_material("CoconutHusk", fn, res=1024, ao_distance=0.1)


def fibre_material():
    def fn(nb):
        co = nb.coord("Object")
        fib = nb.noise(nb.mapping(co, scale=(5.0, 90.0, 90.0)), 3.0, 5, 0.6)
        fib2 = nb.noise(nb.mapping(co, scale=(2.0, 220.0, 220.0)), 2.0, 3, 0.5)
        col = nb.ramp(fib, [(0.3, srgb("#8a6a45")), (0.55, srgb("#b89468")), (0.75, srgb("#cdb088"))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(fib2, 0.3, 0.7, 0.7, 1.15))
        return dict(color=col, rough=0.95, height=nb.add(fib, nb.mul(fib2, 0.8)), height_scale=0.0025,
                    cavity=nb.maprange(fib2, 0.2, 0.8, 0.7, 1.0))

    return pbr_material("CoconutFibre", fn, res=512, ao_distance=0.1)


def _husk_bm(L=0.14, R=0.105):
    """三稜のある卵形（x = 長軸, +x が先端）"""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=36, v_segments=24, radius=1.0)
    bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "Y"))
    for v in bm.verts:
        x, y, z = v.co
        ang = math.atan2(z, y)
        r = R * (1.0 + 0.1 * math.cos(3 * ang))
        # 先端側を細く尖らせる
        taper = 1.0 - 0.28 * max(0.0, x) ** 1.5
        xx = L * x * (1.0 + 0.12 * max(0.0, x))
        v.co = Vector((xx, y * r * taper, z * r * taper))
    return bm


def coconut_husk():
    hm = husk_material()
    fm = fibre_material()
    # 丸ごと（横倒し）
    bm = _husk_bm()
    whole = C.from_bmesh("husk_whole", bm)
    # へたの部分（がく）
    cap = _sphere("husk_cap", 0.025, (-0.142, 0, 0), scale=(0.5, 1, 1), subdiv=2)
    bpy.context.view_layer.update()
    whole = C.join([whole, cap], "husk_whole")
    whole.rotation_euler = (math.radians(12), 0, 0)  # 繊維は x 方向なので z 回転しない
    C.set_smooth(whole, True)
    C.assign(whole, hm)
    # 縦に割れた殻（内側を上に）
    bm = _husk_bm(0.13, 0.1)
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, 0, 0.012),
                           plane_no=(0, 0, 1), clear_outer=True)
    half = C.from_bmesh("husk_half", bm)
    half.data.materials.append(hm)
    half.data.materials.append(fm)
    m = half.modifiers.new("Solid", "SOLIDIFY")
    m.thickness = 0.03
    m.offset = -1.0
    m.material_offset = 1
    m.material_offset_rim = 1
    m.use_even_offset = True
    C.apply_modifiers(half)
    # 縁をぎざぎざに（繊維がほつれた割れ口）
    me = half.data
    for v in me.vertices:
        if v.co.z > 0.005:
            v.co.z += 0.006 * geo.fbm(Vector((v.co.x * 40, v.co.y * 40, 0)), 2)
    half.location = (0.05, 0.27, 0.0)
    half.rotation_euler = (math.radians(-6), math.radians(4), 0)
    C.set_smooth(half, True, angle=50)
    return _ground([whole, half])


# ---------------------------------------------------------------------------
# ガラスの浮き玉
# ---------------------------------------------------------------------------
def glass_material():
    def fn(nb):
        co = nb.coord("Object")
        big = nb.noise(co, 6.0, 3, 0.5)
        col = nb.ramp(big, [(0.3, srgb("#1f6b5c")), (0.5, srgb("#2e8a74")), (0.7, srgb("#3b8f9a"))])
        # 泡（小さな明るい点）
        bub = nb.smooth(nb.voronoi(co, 90.0), 0.08, 0.03)
        bub = nb.mul(bub, nb.math("GREATER_THAN", nb.sep(nb.voronoi(co, 90.0, out="Color"))[0], 0.7))
        col = nb.mix(col, srgb("#8fd0bf"), nb.mul(bub, 0.8))
        # 吹きガラスのゆがみ（筋）
        swirl = nb.noise(nb.mapping(co, scale=(1.0, 1.0, 4.0)), 12.0, 3, 0.5, distortion=1.5)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(swirl, 0.3, 0.7, 0.9, 1.1))
        # 波に擦られた曇り
        frost = nb.smooth(nb.noise(co, 9.0, 4, 0.6), 0.6, 0.72)
        rough = nb.mixf(0.05, 0.3, frost)
        col = nb.mix(col, srgb("#6aa597"), nb.mul(frost, 0.3))
        return dict(color=col, rough=rough, height=nb.add(swirl, nb.mul(bub, 0.5)), height_scale=0.0004)

    return pbr_material("FloatGlass", fn, res=512, ao_distance=0.1, ao_strength=0.6)


def rope_material():
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        # 3 本撚り（ピッチ 1.6 cm）
        t = nb.math("FRACT", nb.add(nb.mul(U, 3.0), nb.mul(V, 1.0 / 0.016)))
        strand = nb.math("SINE", nb.mul(t, math.pi))
        cyl = nb.comb(nb.mul(nb.math("COSINE", nb.mul(U, math.tau)), 0.1),
                      nb.mul(nb.math("SINE", nb.mul(U, math.tau)), 0.1), V)
        fib = nb.noise(nb.mapping(cyl, scale=(40, 40, 3)), 20.0, 3, 0.6)
        big = nb.noise(nb.coord("Object"), 10.0, 3, 0.5)
        col = nb.ramp(big, [(0.3, srgb("#7b6a55")), (0.55, srgb("#9a8a70")), (0.75, srgb("#b1a58f"))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(nb.add(nb.mul(strand, 0.6), nb.mul(fib, 0.4)), 0.2, 1.0, 0.6, 1.1))
        height = nb.add(strand, nb.mul(fib, 0.3))
        return dict(color=col, rough=0.9, height=height, height_scale=0.0015,
                    cavity=nb.maprange(strand, 0.0, 0.6, 0.55, 1.0))

    return pbr_material("FloatRope", fn, res=1024, ao_distance=0.05)


def glass_float():
    R = 0.15
    rr = 0.0045   # ロープ半径
    gm = glass_material()
    rm = rope_material()
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=40, v_segments=24, radius=R)
    ball = C.from_bmesh("float_ball", bm)
    # 吹き口の封じ（ボタン）
    btn = _sphere("float_btn", 0.022, (R * 0.96, 0, 0), scale=(0.35, 1, 1), subdiv=2)
    bpy.context.view_layer.update()
    ball = C.join([ball, btn], "float_ball")
    C.set_smooth(ball, True)
    C.assign(ball, gm)
    parts = [ball]
    Rr = R + rr * 0.8

    def sph(lat, lon, r=Rr):
        return Vector((r * math.cos(lat) * math.cos(lon), r * math.cos(lat) * math.sin(lon), r * math.sin(lat)))

    # 菱目の網: 左右にねじれる 2 系統の線
    n = 6
    lat0, lat1 = math.radians(-68), math.radians(68)
    for fam in (1, -1):
        for k in range(n):
            pts = []
            for i in range(29):
                t = i / 28
                lat = lat0 + (lat1 - lat0) * t
                lon = k / n * math.tau + fam * t * (math.tau / n) * 1.5
                pts.append(sph(lat, lon))
            parts.append(geo.tube_along(pts, [rr] * len(pts), sides=5, name=f"rope{fam}_{k}",
                                        cap_start=False, cap_end=False))
    # 上下の輪
    for lat in (lat0, lat1):
        pts = [sph(lat, a / 36 * math.tau) for a in range(37)]
        parts.append(geo.tube_along(pts, [rr * 1.3] * len(pts), sides=5, name="ring", cap_start=False,
                                    cap_end=False))
    # 吊り下げ用の輪
    top = Vector((0, 0, R * math.sin(lat1) + rr))
    pts = geo.bezier_points(top + Vector((-0.045, 0, -0.005)), top + Vector((-0.06, 0, 0.09)),
                            top + Vector((0.06, 0, 0.09)), top + Vector((0.045, 0, -0.005)), 20)
    parts.append(geo.tube_along(pts, [rr * 1.4] * len(pts), sides=6, name="loop"))
    for p in parts[1:]:
        C.set_smooth(p, True)
        C.assign(p, rm)
    for p in parts:
        p.rotation_euler = (math.radians(25), math.radians(-10), math.radians(15))
    return _ground(parts)


# ---------------------------------------------------------------------------
# プレビュー: 小物を浜に並べる
# ---------------------------------------------------------------------------
LAYOUT = {
    "Driftwood_A": ((-0.5, 1.55), 10), "Driftwood_B": ((0.9, 2.45), -25),
    "Coconut_Husk": ((-1.1, 0.35), 0), "GlassFloat": ((-0.35, 0.3), 0),
    "CoralPiece_A": ((0.35, 0.45), 30), "CoralPiece_B": ((0.95, 0.5), 0),
    "Shell_SpiderConch": ((0.2, -0.1), -10), "Shell_Cowrie": ((-0.12, -0.2), 0),
}


def _preview_extra(objs):
    import os
    scene = bpy.context.scene
    for o in objs:
        if o.type != "MESH":
            continue
        key = next((k for k in LAYOUT if o.name.startswith(k)), None)
        if key is None:
            continue
        (x, y), rz = LAYOUT[key]
        o.parent = None
        # 取り込み時の中心を原点へ戻して配置
        mn, mx = C.bounds([o])
        o.location.x += x - (mn.x + mx.x) / 2
        o.location.y += y - (mn.y + mx.y) / 2
        o.rotation_euler.z += math.radians(rz)
    cam = scene.camera
    cam.location = (0.35, -2.6, 1.9)
    C.look_at(cam, (0.0, 0.55, 0.0))
    cam.data.lens = 38
    scene.view_settings.exposure = -0.4
    dbg = os.environ.get("OKI_DEBUG_DIR")
    if dbg:
        for key, ((x, y), _) in LAYOUT.items():
            if key.startswith("Drift"):
                continue
            o = next(o for o in objs if o.name.startswith(key))
            mn, mx = C.bounds([o])
            c = (mn + mx) / 2
            s = (mx - mn).length
            cam.location = c + Vector((0.3, -1.0, 0.75)).normalized() * s * 1.6
            C.look_at(cam, c)
            C.render(os.path.join(dbg, f"props_{key}.jpg"), res=(640, 400), samples=24)
        cam.location = (0.35, -2.6, 1.9)
        C.look_at(cam, (0.0, 0.55, 0.0))


PREVIEW = dict(cam_dir=(0.2, -1.0, 0.7), lens=45, extra=_preview_extra)


def build():
    return {
        "Driftwood_A": driftwood("DriftA", 2.2, seed=3),
        "Driftwood_B": driftwood("DriftB", 1.7, seed=9, roots=True),
        "Shell_Cowrie": cowrie(),
        "Shell_SpiderConch": spider_conch(),
        "CoralPiece_A": coral_staghorn(),
        "CoralPiece_B": coral_finger(),
        "Coconut_Husk": coconut_husk(),
        "GlassFloat": glass_float(),
    }
