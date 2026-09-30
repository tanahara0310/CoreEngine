"""サバニ（沖縄の伝統的な漁船、2 バリエーション）

A: 杉板の素地に油を引いた船。喫水付近は黒いタール（チャン塗り）
B: 船底は赤、舷側は白、舷縁に青帯のペンキ塗り。剥げて木地が見える

船体は外板・内板・舷縁・トランサム（船尾板）を 1 枚の閉じたメッシュで作る（厚み 2.4cm）。
長さ方向 = +X（船首が +X）、竜骨の下端が z≈0（砂に 3cm 埋める）。原点は船体中央の接地点。
外板の継ぎ目（はぎ目）は断面の周長比 t の一定位置に置くので、船首・船尾で板が細くなる。
内側のはぎ目には蝶型の木栓（フンドゥ）を描く。エーク（櫂）を 1 本、座板の上に載せる。
"""
import math
import random

import numpy as np
from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from . import _timber as T



def _preview_extra(objs):
    """2 艘を横に並べ、斜め前から撮る"""
    import bpy
    for o in objs:
        if o.parent is None:
            o.location = (0.0, 0.85 if o.name.startswith("Sabani_A") else -0.85, 0.0)
    cam = bpy.context.scene.camera
    tgt = Vector((0.4, 0.0, 0.2))
    cam.location = tgt + Vector((4.6, -6.2, 3.4))
    cam.data.lens = 33
    C.look_at(cam, tgt)


PREVIEW = dict(extra=_preview_extra)

L0 = -3.3      # 竜骨線の船尾端 x
LEN = 6.6      # 竜骨線の長さ
THK = 0.024    # 外板の厚み
BURY = 0.03    # 砂への埋まり
M = 12         # 片舷の断面分割数
N = 64         # 長さ方向の断面数
SEAMS = (0.28, 0.52, 0.76)  # はぎ目の t（片舷の周長比）


def _ss(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------------------
# 船型
# ---------------------------------------------------------------------------
def half_beam(s):
    """舷縁での半幅"""
    if s <= 0.44:
        return 0.24 + 0.21 * (1 - ((0.44 - s) / 0.44) ** 2)
    u = (s - 0.44) / 0.56
    return max(0.004, 0.45 * (1 - u ** 2.0))


def keel_z(s):
    """竜骨の高さ（船首は反り上がる）"""
    z = 0.0
    if s < 0.25:
        z += 0.07 * ((0.25 - s) / 0.25) ** 2
    if s > 0.6:
        z += 0.34 * ((s - 0.6) / 0.4) ** 2.2
    return z


def sheer_z(s):
    """舷縁の高さ（船首が高く反る）"""
    z = 0.6
    if s > 0.45:
        z += 0.36 * ((s - 0.45) / 0.55) ** 2.3
    if s < 0.3:
        z += 0.08 * ((0.3 - s) / 0.3) ** 2
    return z


def x_at(s, zn):
    """船首材は上ほど前へ傾き（レーキ）、トランサムは上ほど後ろへ傾く"""
    x = L0 + LEN * s
    if s > 0.72:
        x += 0.5 * ((s - 0.72) / 0.28) ** 2 * zn ** 1.15
    if s < 0.1:
        x -= 0.07 * zn * (1 - s / 0.1)
    return x


# 正規化した断面（y/B, z/深さ）。船尾はふくらみ、船首は鋭い V
SEC_STERN = [(0, 0), (0.5, 0.09), (0.8, 0.28), (0.95, 0.6), (1.0, 1.0)]
SEC_MID = [(0, 0), (0.36, 0.16), (0.63, 0.37), (0.84, 0.63), (1.0, 1.0)]
SEC_BOW = [(0, 0), (0.17, 0.25), (0.4, 0.5), (0.7, 0.77), (1.0, 1.0)]


def _catmull(pts, per_seg=16):
    p = [pts[0]] + list(pts) + [pts[-1]]
    out = []
    for i in range(1, len(p) - 2):
        p0, p1, p2, p3 = (np.array(p[k], float) for k in (i - 1, i, i + 1, i + 2))
        for k in range(per_seg):
            t = k / per_seg
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                              + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3))
    out.append(np.array(p[-2], float))
    return np.array(out)


def section(s, B, depth, count=M + 1):
    """片舷の断面を周長で等分した点列 (y, z相対) と周長 G。t=0 が竜骨、t=1 が舷縁"""
    ws = 1 - _ss(0.0, 0.38, s)
    wb = _ss(0.55, 0.95, s)
    wm = max(0.0, 1 - ws - wb)
    ctrl = [(ws * a[0] + wm * b[0] + wb * c[0], ws * a[1] + wm * b[1] + wb * c[1])
            for a, b, c in zip(SEC_STERN, SEC_MID, SEC_BOW)]
    ctrl = [(y * B, z * depth) for y, z in ctrl]
    dense = _catmull(ctrl)
    seg = np.linalg.norm(np.diff(dense, axis=0), axis=1)
    acc = np.concatenate([[0], np.cumsum(seg)])
    G = acc[-1]
    ts = np.linspace(0, 1, count)
    ys = np.interp(ts * G, acc, dense[:, 0])
    zs = np.interp(ts * G, acc, dense[:, 1])
    return ys, zs, G, ts


def _row_s(i):
    u = i / (N - 1)
    return 0.45 * u + 0.55 * (1 - (1 - u) ** 1.7)


def _solve_s_b():
    """内板が閉じる位置（半幅が板厚ぶんになる所）"""
    lo, hi = 0.5, 1.0
    for _ in range(40):
        mid = (lo + hi) / 2
        if half_beam(mid) - THK * 1.15 > 0.006:
            lo = mid
        else:
            hi = mid
    return lo


def hull_mesh(name):
    """外板・内板・舷縁・トランサム・船首を閉じた 1 メッシュで作る"""
    mb = T.MeshBuilder()
    s_b = _solve_s_b()
    s_st = 0.03 / LEN
    info = {}  # vi -> (x, 符号付き周長位置 m, t, G, zn)

    def grid(inner):
        g = [[None] * (2 * M + 1) for _ in range(N)]
        for i in range(N):
            s0 = _row_s(i)
            s = s_st + (s_b - s_st) * s0 if inner else s0
            B = half_beam(s)
            K = keel_z(s)
            H = sheer_z(s)
            if inner:
                B = max(0.002, B - THK * 1.15)
                K = K + THK
            ys, zs, G, ts = section(s, B, H - K)
            for j in range(2 * M + 1):
                k = abs(j - M)
                side = -1 if j < M else 1
                zn = zs[k] / (H - K)
                co = (x_at(s, zn), side * ys[k], K + zs[k] - BURY)
                vi = mb.v(co)
                g[i][j] = vi
                info[vi] = (co[0], side * ts[k] * G, ts[k], G, zn)
        return g

    O = grid(False)
    I = grid(True)

    def skin_attr(inside):
        return dict(WoodCo=lambda vi: (info[vi][0], info[vi][1], 0.0),
                    HullMask=lambda vi: (inside, info[vi][2], info[vi][3]),
                    HullReg=(0.0, 0.0, 0.0))

    for g, inside in ((O, 0.0), (I, 1.0)):
        at = skin_attr(inside)
        for i in range(N - 1):
            for j in range(2 * M):
                mb.f((g[i][j], g[i][j + 1], g[i + 1][j + 1], g[i + 1][j]), **at)
    # 舷縁（上端の木口）
    rim = dict(WoodCo=lambda vi: (info[vi][0], info[vi][1], 0.0),
               HullMask=lambda vi: (0.5, 1.0, info[vi][3]), HullReg=(1.0, 0.0, 0.0))
    for i in range(N - 1):
        mb.f((O[i][0], O[i + 1][0], I[i + 1][0], I[i][0]), **rim)
        mb.f((O[i][2 * M], I[i][2 * M], I[i + 1][2 * M], O[i + 1][2 * M]), **rim)
    # トランサム（船尾板）: 外面・内面・上端。木目は横（y）方向
    for g, inside in ((O, 0.0), (I, 1.0)):
        row = g[0]
        mb.f(tuple(row), WoodCo=lambda vi: (mb.verts[vi][1] + 11.0, mb.verts[vi][2], 0.2),
             HullMask=lambda vi, ins=inside: (ins, info[vi][4], 1.0), HullReg=(0.0, 1.0, 0.0))
    mb.f((O[0][0], I[0][0], I[0][2 * M], O[0][2 * M]),
         WoodCo=lambda vi: (mb.verts[vi][1] + 11.0, mb.verts[vi][2], 0.2),
         HullMask=(0.5, 1.0, 1.0), HullReg=(1.0, 1.0, 0.0))
    # 船首（外板の先端の細い面・内板の終わり・上端）
    for g, inside in ((O, 0.0), (I, 1.0)):
        mb.f(tuple(g[N - 1]), WoodCo=lambda vi: (info[vi][0], info[vi][1], 0.0),
             HullMask=lambda vi, ins=inside: (ins, info[vi][2], info[vi][3]), HullReg=(1.0, 0.0, 0.0))
    mb.f((O[N - 1][0], O[N - 1][2 * M], I[N - 1][2 * M], I[N - 1][0]), **rim)
    obj = mb.build(name)
    C.set_smooth(obj, True, angle=38)
    return obj, O, I, mb.verts


def _set_idx_attr(obj, name, vals):
    """頂点 index 順の値リストを CORNER 属性にする"""
    me = obj.data
    arr = np.zeros((len(me.loops), 4), np.float32)
    arr[:, 3] = 1.0
    for li, lp in enumerate(me.loops):
        v = vals[lp.vertex_index]
        arr[li, :len(v)] = v
    a = me.color_attributes.new(name, "FLOAT_COLOR", "CORNER")
    a.data.foreach_set("color", arr.ravel())


def rails(prefix, O, verts):
    """舷縁の笠木（両舷）と竜骨・船首材"""
    out = []
    G_top = 1.0
    for side, col in ((-1, 0), (1, 2 * M)):
        pts = [Vector(verts[O[i][col]]) for i in range(N)]
        # 船首の先端は左右がぶつかるので少し手前で止める
        pts = pts[:-1]
        outw = 0.016
        inw = THK + 0.012
        if side < 0:
            prof = [(-inw, -0.036), (outw, -0.036), (outw, 0.016), (-inw, 0.016)]
        else:
            prof = [(-outw, -0.036), (inw, -0.036), (inw, 0.016), (-outw, 0.016)]
        obj, info = T.sweep(f"{prefix}_rail{col}", pts, prof)
        T.ring_cuts(obj, 4, (N // 3, 2 * N // 3))
        _set_idx_attr(obj, "WoodCo", [(a + 5.0 * (side + 2), b, c + 0.3) for a, b, c in info])
        _set_idx_attr(obj, "HullMask", [(0.5, 1.0, G_top)] * len(info))
        _set_idx_attr(obj, "HullReg", [(1.0, 0.0, 0.0)] * len(info))
        C.set_smooth(obj, True, angle=35)
        out.append(obj)
    # 竜骨（船底の当て木）→ 船首材（ステム）
    keel = [Vector(verts[O[i][M]]) for i in range(N)]
    stem = [Vector(verts[O[N - 1][j]]) for j in range(M - 1, -1, -1)]
    stem[-1] = stem[-1] + Vector((0.0, 0.0, 0.03))
    pts = keel[1:] + stem
    pts.insert(0, keel[0] + Vector((0.01, 0, 0)))
    prof = [(-0.026, -0.02), (0.012, -0.02), (0.012, 0.02), (-0.026, 0.02)]
    obj, info = T.sweep(f"{prefix}_keel", pts, prof, up=(0, 1, 0))
    T.ring_cuts(obj, 4, (N // 3, 2 * N // 3))
    _set_idx_attr(obj, "WoodCo", [(a + 40.0, b, c + 0.25) for a, b, c in info])
    _set_idx_attr(obj, "HullMask", [(0.0, 0.0, 1.0)] * len(info))
    _set_idx_attr(obj, "HullReg", [(0.0, 0.0, 1.0)] * len(info))
    C.set_smooth(obj, True, angle=35)
    out.append(obj)
    # トランサム上端の笠木
    H0 = sheer_z(0.0) - BURY
    yb = half_beam(0.0)
    cap = T.board(f"{prefix}_transomcap", (0.065, 2 * yb + 0.03, 0.045), loc=(L0 - 0.05, 0, H0 - 0.012),
                  rot=(0, math.radians(-4), 0), rnd=random.Random(5), axis="Y")
    T.set_attr(cap, "HullMask", lambda co, n: (0.5, 1.0, 1.0))
    T.set_attr(cap, "HullReg", lambda co, n: (1.0, 0.0, 0.0))
    out.append(cap)
    return out


def _inner_half_width(s, z_world):
    """内板の、高さ z での半幅"""
    B = max(0.002, half_beam(s) - THK * 1.15)
    K = keel_z(s) + THK
    H = sheer_z(s)
    ys, zs, G, ts = section(s, B, H - K, 64)
    return float(np.interp(z_world + BURY - K, zs, ys))


def thwarts(prefix, xs, rnd, paint_flag):
    """座板。両端を内板の傾き（フレア）に合わせて台形に切る"""
    out = []
    tops = []
    for k, xc in enumerate(xs):
        s = (xc - L0) / LEN
        top = sheer_z(s) - BURY - 0.085
        bot = top - 0.028
        yt = _inner_half_width(s, top) + 0.006
        yb = _inner_half_width(s, bot) + 0.006
        w = 0.19
        mb = T.MeshBuilder()
        c = []
        for (y, z) in ((-yb, bot), (yb, bot), (yt, top), (-yt, top)):
            c.append((mb.v((-w / 2, y, z)), mb.v((w / 2, y, z))))
        # 4 側面 + 上下
        mb.f((c[0][0], c[1][0], c[1][1], c[0][1]))
        mb.f((c[1][0], c[2][0], c[2][1], c[1][1]))
        mb.f((c[2][0], c[3][0], c[3][1], c[2][1]))
        mb.f((c[3][0], c[0][0], c[0][1], c[3][1]))
        mb.f((c[0][0], c[3][0], c[2][0], c[1][0]))
        mb.f((c[0][1], c[1][1], c[2][1], c[3][1]))
        obj = mb.build(f"{prefix}_thwart{k}")
        geo.bevel(obj, width=0.004, segments=1, limit_angle=30)
        T.wood_attrs(obj, rnd, axis="Y", flag=paint_flag)
        C.set_smooth(obj, True, angle=35)
        obj.location = (xc, 0, 0)
        out.append(obj)
        tops.append(top)
    return out, tops


def eku(name, rnd):
    """エーク（櫂）。細長い木の葉形のブレード。ローカル +X が柄→先端"""
    prof = [  # (l, 半幅, 半厚)
        (0.0, 0.012, 0.012), (0.012, 0.02, 0.02), (0.04, 0.021, 0.021), (0.1, 0.018, 0.018),
        (0.45, 0.019, 0.019), (0.55, 0.035, 0.017), (0.68, 0.062, 0.014), (0.85, 0.08, 0.012),
        (1.1, 0.086, 0.011), (1.3, 0.08, 0.010), (1.42, 0.064, 0.009), (1.5, 0.04, 0.008),
        (1.545, 0.014, 0.006),
    ]
    rings = []
    for l, w, th in prof:
        ring = []
        for k in range(16):
            a = k / 16 * math.tau
            ring.append((l, w * math.cos(a), th * math.sin(a) * (1.0 if w < 0.025 else 0.85 + 0.15 * abs(math.cos(a)))))
        rings.append(ring)
    obj = T.loft(name, rings)
    T.wood_attrs(obj, rnd, axis="X", flag=0.0)
    C.set_smooth(obj, True, angle=50)
    return obj


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def hull_material(name, painted):
    def fn(nb):
        wx, wy, _ = nb.sep(nb.attr("WoodCo", "Vector"))
        inside, t, G = nb.sep(nb.attr("HullMask", "Vector"))
        rim, transom, keel = nb.sep(nb.attr("HullReg", "Vector"))
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        ins = nb.smooth(inside, 0.6, 0.9)
        # 板番号（0 = 船底板は左右共通）と板ごとの乱数
        pid = nb.math("FLOOR", nb.math("MAXIMUM", nb.math("MINIMUM", nb.math("DIVIDE", nb.sub(t, 0.04), 0.24), 3.0), 0.0))
        side = nb.math("GREATER_THAN", wy, 0.0)
        key = nb.add(nb.add(pid, nb.mul(nb.mul(side, nb.math("GREATER_THAN", pid, 0.5)), 10.0)),
                     nb.add(nb.mul(transom, 20.0), nb.add(nb.mul(rim, 30.0), nb.mul(keel, 40.0))))
        r = T.white(nb, key)
        r2 = T.white(nb, nb.add(key, 17.3))
        gco = nb.comb(nb.add(wx, nb.mul(r, 37.0)),
                      nb.add(nb.math("ABSOLUTE", wy), nb.sub(nb.mul(r2, 0.5), 0.25)),
                      nb.add(nb.mul(r, 0.25), nb.add(0.18, nb.mul(ins, 0.024))))
        g = T.wood_grain(nb, gco, rings=26.0, knots=0.18)
        # はぎ目（周長比 t の一定位置、船首・船尾で細る）
        dt = nb.mul(nb.sub(nb.math("FRACT", nb.add(nb.math("DIVIDE", nb.sub(t, 0.28), 0.24), 0.5)), 0.5), 0.24)
        dm = nb.mul(dt, G)
        adm = nb.math("ABSOLUTE", dm)
        on = nb.mul(nb.mul(nb.math("GREATER_THAN", t, 0.16), nb.math("LESS_THAN", t, 0.88)),
                    nb.mul(nb.sub(1.0, rim), nb.mul(nb.sub(1.0, transom), nb.sub(1.0, keel))))
        seam = nb.mul(nb.smooth(adm, 0.009, 0.003), on)
        seam_near = nb.mul(nb.smooth(adm, 0.05, 0.0), on)
        # トランサムは横板 2 枚
        tseam = nb.mul(transom, nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.mul(Z, 3.4)), 0.5)),
                                                 0.49, 0.475), nb.sub(1.0, rim)))
        seam = nb.math("MAXIMUM", seam, tseam)
        # フンドゥ（蝶型の木栓、内側のはぎ目に 60cm おき）
        sk = nb.math("FLOOR", nb.add(nb.math("DIVIDE", nb.sub(t, 0.28), 0.24), 0.5))
        a = nb.mul(nb.sub(nb.math("FRACT", nb.math("DIVIDE", nb.add(X, nb.mul(sk, 0.21)), 0.6)), 0.5), 0.6)
        wlim = nb.add(0.011, nb.mul(adm, 0.36))
        fundo = nb.mul(nb.mul(nb.smooth(adm, 0.043, 0.039), nb.smooth(nb.sub(nb.math("ABSOLUTE", a), wlim), 0.002, -0.001)),
                       nb.mul(ins, on))
        fundo_edge = nb.mul(fundo, nb.sub(1.0, nb.mul(nb.smooth(adm, 0.037, 0.033),
                                                      nb.smooth(nb.sub(nb.math("ABSOLUTE", a), wlim), -0.002, -0.005))))
        # 木地の色（杉の赤身、油で濃い）
        wood = nb.ramp(nb.add(nb.mul(g["var"], 0.7), nb.mul(r, 0.3)),
                       [(0.2, srgb("#6b4128")), (0.5, srgb("#86573a")), (0.8, srgb("#a0714c"))])
        wood = nb.mix(wood, srgb("#4a2c1a"), nb.mul(g["ring"], 0.55))
        wood = nb.mix(wood, srgb("#3a2517"), nb.mul(g["knot"], 0.8))
        wood = nb.hsv(wood, 0.5, 1.0, nb.maprange(g["fib"], 0.3, 0.7, 0.9, 1.08))
        # 日焼けで灰色に（外側の上ほど、舷縁は強く）
        sun = nb.add(nb.mul(nb.mul(nb.sub(1.0, ins), nb.sub(1.0, rim)), nb.mul(nb.smooth(t, 0.35, 1.0), 0.45)),
                     nb.add(nb.mul(rim, 0.3), nb.mul(ins, 0.2)))
        sun = nb.mul(sun, nb.maprange(g["var"], 0.3, 0.7, 0.6, 1.2))
        grey = nb.mix(srgb("#8a8174"), srgb("#a39a8b"), g["fib"])
        grey = nb.mix(grey, srgb("#5f574c"), nb.mul(g["ring"], 0.5))
        wood = nb.mix(wood, grey, nb.math("MINIMUM", sun, 0.85))
        wood = nb.mix(wood, srgb("#1b140e"), nb.mul(g["crack"], 0.9))
        wood_h = nb.add(nb.add(nb.mul(g["fib"], 0.35), nb.mul(g["ring"], nb.mixf(0.15, 0.5, sun))),
                        nb.mul(g["crack"], -1.2))
        # 継ぎ目の汚れと木栓
        dirt = nb.mul(nb.smooth(adm, 0.03, 0.0), on)
        n_big = nb.noise(co, 1.3, 4, 0.6)
        n_fine = nb.noise(co, 14.0, 6, 0.65)
        # 船内の底にたまった砂
        sand_m = nb.mul(ins, nb.smooth(nb.add(Z, nb.mul(nb.sub(n_big, 0.5), 0.12)), 0.13, 0.03))
        sand_m = nb.mul(sand_m, nb.smooth(n_fine, 0.35, 0.55))
        sand = nb.mix(srgb("#bfae8a"), srgb("#e0d3b2"), nb.noise(co, 90.0, 2, 0.5))

        if not painted:
            col = wood
            col = nb.mix(col, srgb("#2b1b10"), nb.mul(dirt, 0.35))
            col = nb.mix(col, srgb("#6b4a33"), nb.mul(fundo, 0.9))
            col = nb.mix(col, srgb("#2a1a10"), nb.mul(fundo_edge, 0.8))
            # 喫水付近のタール（上端はノイズで乱れ、垂れがある）
            drip = nb.mul(nb.smooth(nb.noise(nb.mapping(co, scale=(22, 22, 1.2)), 1.0, 3, 0.5), 0.6, 0.75), 0.07)
            tar_edge = nb.add(Z, nb.mul(nb.sub(n_big, 0.5), 0.07))
            tar = nb.mul(nb.smooth(nb.sub(tar_edge, drip), 0.23, 0.2), nb.sub(1.0, ins))
            tar = nb.math("MAXIMUM", tar, keel)
            worn_tar = nb.mul(tar, nb.smooth(n_fine, 0.68, 0.75))
            tcol = nb.mix(srgb("#15100b"), srgb("#2c2016"), nb.noise(co, 30.0, 3, 0.6))
            col = nb.mix(col, tcol, nb.mul(tar, 0.96))
            col = nb.mix(col, srgb("#6d6255"), nb.mul(worn_tar, 0.5))
            col = nb.mix(col, sand, nb.mul(sand_m, 0.9))
            seam_col = nb.mix(srgb("#1a120c"), srgb("#3a2a1c"), ins)
            col = nb.mix(col, seam_col, nb.mul(seam, 0.9))
            rough = nb.maprange(sun, 0.0, 0.8, 0.55, 0.82)
            rough = nb.mixf(rough, 0.32, nb.mul(tar, nb.sub(1.0, worn_tar)))
            rough = nb.mixf(rough, 0.95, sand_m)
            height = nb.add(nb.mul(wood_h, nb.sub(1.0, nb.mul(tar, 0.7))), nb.mul(seam, -1.6))
            height = nb.add(height, nb.mul(fundo_edge, -0.8))
            height = nb.add(height, nb.mul(sand_m, nb.mul(nb.noise(co, 120.0, 2, 0.5), 0.6)))
        else:
            # 塗り分け: 船底（喫水下）赤 / 舷側 白 / 舷縁の帯 青 / 赤の細線 / 内側 灰青
            band = nb.math("MAXIMUM", nb.smooth(t, 0.855, 0.86), rim)
            pin = nb.mul(nb.smooth(t, 0.815, 0.82), nb.smooth(t, 0.84, 0.835))
            bottom = nb.smooth(nb.add(Z, nb.mul(nb.sub(n_big, 0.5), 0.01)), 0.185, 0.18)
            band = nb.math("MAXIMUM", band, keel)
            out_col = nb.mix(srgb("#e6e2d6"), srgb("#1c5892"), band)
            out_col = nb.mix(out_col, srgb("#b3261c"), nb.mul(pin, nb.sub(1.0, band)))
            out_col = nb.mix(out_col, srgb("#8a2a1e"), bottom)
            out_col = nb.mix(out_col, srgb("#1c5892"), nb.mul(transom, nb.sub(1.0, ins)))
            paint = nb.mix(out_col, srgb("#93aab2"), nb.mul(ins, nb.sub(1.0, rim)))
            paint = nb.hsv(paint, 0.5, 1.0, nb.maprange(nb.noise(co, 3.0, 4, 0.6), 0.3, 0.7, 0.9, 1.04))
            # 剥げ: 舷縁・はぎ目・竜骨・船内の床で多い
            wear = nb.add(nb.mul(rim, 0.3), nb.add(nb.mul(seam_near, 0.12), nb.mul(keel, 0.25)))
            wear = nb.add(wear, nb.mul(ins, nb.mul(nb.smooth(Z, 0.3, 0.08), 0.35)))
            wear = nb.add(wear, nb.mul(nb.smooth(t, 0.9, 1.0), nb.mul(nb.sub(1.0, ins), 0.25)))
            chipn = nb.noise(co, 7.0, 10, 0.72)
            thr = nb.sub(0.69, nb.mul(wear, 0.22))
            chip = nb.smooth(chipn, thr, nb.add(thr, 0.012))
            chip_rim = nb.mul(nb.smooth(chipn, nb.sub(thr, 0.025), thr), nb.sub(1.0, chip))
            col = nb.mix(paint, srgb("#d8d2c2"), nb.mul(chip_rim, 0.6))
            col = nb.mix(col, wood, chip)
            # 塗膜の汚れ・雨だれ
            streak = nb.noise(nb.mapping(co, scale=(9, 9, 0.8)), 1.0, 4, 0.6)
            col = nb.mix(col, srgb("#6b5d48"), nb.mul(nb.mul(nb.smooth(streak, 0.55, 0.8), nb.sub(1.0, chip)), 0.18))
            col = nb.mix(col, srgb("#3a3028"), nb.mul(dirt, 0.35))
            col = nb.mix(col, srgb("#80766a"), nb.mul(fundo_edge, 0.6))
            col = nb.mix(col, sand, nb.mul(sand_m, 0.9))
            col = nb.mix(col, srgb("#2a211a"), nb.mul(seam, 0.75))
            rough = nb.mixf(0.42, 0.8, chip)
            rough = nb.mixf(rough, 0.95, sand_m)
            height = nb.add(nb.mul(nb.sub(1.0, chip), 0.6), nb.mul(wood_h, nb.mixf(0.15, 1.0, chip)))
            height = nb.add(height, nb.mul(seam, -1.6))
            height = nb.add(height, nb.mul(fundo_edge, -0.6))
        cavity = nb.maprange(nb.math("MAXIMUM", seam, nb.mul(g["crack"], 0.6)), 0, 1, 1.0, 0.55)
        return dict(color=col, rough=rough, height=height, height_scale=0.0035, cavity=cavity)

    return pbr_material(name, fn, res=2048, ao_distance=0.6, ao_samples=64, uv="keep")


def fittings_material(name, painted):
    """座板・エーク（WoodId.z = 1 の部材は B では塗装）"""
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        r1, r2, flag = nb.sep(nb.attr("WoodId", "Vector"))
        g = T.wood_grain(nb, co, rings=34.0, knots=0.2)
        oc = nb.coord("Object")
        wood = nb.ramp(nb.add(nb.mul(g["var"], 0.7), nb.mul(r1, 0.3)),
                       [(0.2, srgb("#6b4631")), (0.55, srgb("#83593b")), (0.85, srgb("#9b7250"))])
        wood = nb.mix(wood, srgb("#553520"), nb.mul(g["ring"], 0.5))
        wood = nb.mix(wood, srgb("#3a2517"), nb.mul(g["knot"], 0.8))
        wood = nb.hsv(wood, 0.5, 1.0, nb.maprange(g["fib"], 0.3, 0.7, 0.9, 1.08))
        # 上面ほど日焼けして灰色
        nrm = nb.sep(nb.coord("Normal"))
        sun = nb.mul(nb.smooth(nrm[2], 0.2, 0.9), nb.maprange(r2, 0, 1, 0.45, 0.8))
        grey = nb.mix(srgb("#8d8579"), srgb("#a69d8e"), g["fib"])
        wood = nb.mix(wood, grey, sun)
        # 手擦れで艶のある所（エークの柄）
        wood = nb.mix(wood, srgb("#1c130c"), nb.mul(g["crack"], 0.9))
        h = nb.add(nb.add(nb.mul(g["fib"], 0.4), nb.mul(g["ring"], 0.3)), nb.mul(g["crack"], -1.2))
        rough = nb.maprange(sun, 0, 0.7, 0.55, 0.8)
        col = wood
        if painted:
            chipn = nb.noise(oc, 9.0, 10, 0.7)
            thr = nb.sub(0.66, nb.mul(nb.smooth(nrm[2], 0.3, 0.95), 0.1))
            chip = nb.smooth(chipn, thr, nb.add(thr, 0.012))
            paint = nb.hsv(srgb("#2a6aa3"), 0.5, 1.0, nb.maprange(nb.noise(oc, 4.0, 3, 0.6), 0.3, 0.7, 0.88, 1.05))
            pm = nb.mul(flag, nb.sub(1.0, chip))
            col = nb.mix(wood, paint, pm)
            rough = nb.mixf(rough, 0.45, pm)
            h = nb.add(nb.mul(h, nb.sub(1.0, nb.mul(pm, 0.8))), nb.mul(pm, 0.5))
        return dict(color=col, rough=rough, height=h, height_scale=0.002,
                    cavity=nb.maprange(g["crack"], 0, 1, 1.0, 0.6))

    return pbr_material(name, fn, res=1024, ao_distance=0.4)


# ---------------------------------------------------------------------------
def sabani(prefix, painted, seed):
    rnd = random.Random(seed)
    hull, O, I, verts = hull_mesh(f"{prefix}_hull")
    print(f"  [{prefix}] hull volume {T.signed_volume(hull):.4f} m3")
    hm = hull_material(f"SabaniHull{'B' if painted else 'A'}", painted)
    # 船体まわり（外板・笠木・竜骨）は 1 つにまとめ、シームを入れて展開する
    # （外板・内板は長さ方向に 3 分割、竜骨の V と舷縁は鋭い辺として自動で切れる）
    cut_rows = {N // 3, 2 * N // 3}
    cuts = set()
    for g in (O, I):
        for i in cut_rows:
            for j in range(2 * M):
                cuts.add(frozenset((g[i][j], g[i][j + 1])))
    for i in cut_rows:
        cuts.add(frozenset((O[i][0], I[i][0])))
        cuts.add(frozenset((O[i][2 * M], I[i][2 * M])))
    T.mark_seams(hull, lambda e: frozenset((e.verts[0].index, e.verts[1].index)) in cuts)
    rl = rails(prefix, O, verts)
    hull = C.join([hull] + rl, f"{prefix}_hull")
    T.unwrap(hull, seam_angle=40.0)
    C.assign(hull, hm)
    parts = [hull]
    fm = fittings_material(f"SabaniFittings{'B' if painted else 'A'}", painted)
    th, tops = thwarts(prefix, [-1.75, -0.35, 0.95], rnd, 1.0)
    for o in th:
        C.assign(o, fm)
    parts += th
    paddle = eku(f"{prefix}_eku", rnd)
    # 2 枚目と 3 枚目の座板に渡して置く
    z = tops[1] + 0.019
    yaw = math.radians(rnd.uniform(4, 9)) * (1 if painted else -1)
    paddle.matrix_world = (Matrix.Translation((-0.5, 0.06 if painted else -0.08, z))
                           @ Matrix.Rotation(yaw, 4, "Z"))
    C.assign(paddle, fm)
    parts.append(paddle)
    return parts


def build():
    return {
        "Sabani_A": sabani("SabaniA", False, 7),
        "Sabani_B": sabani("SabaniB", True, 8),
    }
