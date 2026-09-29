"""6 シーサー（阿形・吽形の一対、台座付き）

胴・頭・脚・たてがみの渦・尾を符号付き距離関数 (SDF) のプリミティブで組み、
smooth union / subtraction で粘土をこねたように繋げる。
SDF をボクセル格子で評価し Surface Nets でメッシュ化 → スムーズ → 細部の凹凸 → デシメート。

材質: 風化した素焼きの赤土色。窪みに黒カビ、ところどころ白い漆喰の残り。
台座: 琉球石灰岩の二段ブロック。

原点: 台座底面の中心（接地点）。正面は -Y。
"""
import math
import random

import bmesh
import numpy as np
from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import limestone

PREVIEW = dict(cam_dir=(0.25, -1.0, 0.32), lens=55)

PEDESTAL_H = 0.22


# ---------------------------------------------------------------------------
# SDF プリミティブ（numpy, P は (..., 3)）
# ---------------------------------------------------------------------------
def _len(v):
    return np.sqrt(np.sum(v * v, axis=-1))


def sd_sphere(P, c, r):
    return _len(P - np.asarray(c)) - r


def sd_ellipsoid(P, c, radii, R=None):
    """R: ローカル軸を列に持つ 3x3 回転（None で軸平行）"""
    q = P - np.asarray(c)
    if R is not None:
        q = q @ np.asarray(R)
    r = np.asarray(radii, dtype=np.float64)
    k0 = _len(q / r)
    k1 = _len(q / (r * r))
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


def sd_round_box(P, c, half, r):
    q = np.abs(P - np.asarray(c)) - (np.asarray(half) - r)
    out = _len(np.maximum(q, 0.0))
    inside = np.minimum(np.max(q, axis=-1), 0.0)
    return out + inside - r


def sd_round_cone(P, a, b, r1, r2):
    """半径が線形に変わるカプセル（iq の sdRoundCone）"""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    ba = b - a
    l2 = float(ba @ ba)
    rr = r1 - r2
    a2 = l2 - rr * rr
    il2 = 1.0 / l2
    pa = P - a
    y = pa @ ba
    z = y - l2
    xv = pa * l2 - y[..., None] * ba
    x2 = np.sum(xv * xv, axis=-1)
    y2 = y * y * l2
    z2 = z * z * l2
    k = math.copysign(1.0, rr) * rr * rr * x2
    d_top = np.sqrt(x2 + z2) * il2 - r2
    d_bot = np.sqrt(x2 + y2) * il2 - r1
    d_mid = (np.sqrt(np.maximum(x2 * a2 * il2, 0.0)) + y * rr) * il2 - r1
    return np.where(np.sign(z) * a2 * z2 > k, d_top, np.where(np.sign(y) * a2 * y2 < k, d_bot, d_mid))


def sd_torus(P, c, R, axis_R, big, small):
    q = (P - np.asarray(c)) @ np.asarray(axis_R)   # ローカル z 軸がトーラスの軸
    qx = np.sqrt(q[..., 0] ** 2 + q[..., 1] ** 2) - big
    return np.sqrt(qx * qx + q[..., 2] ** 2) - small


def smin(a, b, k):
    if k <= 0:
        return np.minimum(a, b)
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1 - h) + a * h - k * h * (1 - h)


def smax(a, b, k):
    return -smin(-a, -b, k)


class SDFGrid:
    """格子上の SDF。プリミティブは外接箱の範囲だけ評価して合成する"""

    def __init__(self, bmin, bmax, h):
        self.h = h
        self.o = np.asarray(bmin, dtype=np.float64) - 2 * h
        n = np.ceil((np.asarray(bmax) - np.asarray(bmin)) / h).astype(int) + 5
        self.n = n
        self.F = np.full(tuple(n), 1.0, dtype=np.float64)
        self.tf = None   # (pin, R, s, pout): ローカル形状を pin 中心に s 倍・R 回転して pout へ置く


    def _slices(self, lo, hi):
        i0 = np.clip(np.floor((np.asarray(lo) - self.o) / self.h).astype(int), 0, self.n - 1)
        i1 = np.clip(np.ceil((np.asarray(hi) - self.o) / self.h).astype(int) + 1, 0, self.n)
        return tuple(slice(a, b) for a, b in zip(i0, i1)), i0, i1

    def apply(self, fn, lo, hi, op="union", k=0.0):
        lo, hi = np.asarray(lo, dtype=np.float64), np.asarray(hi, dtype=np.float64)
        sc = 1.0
        if self.tf is not None:
            pin, R, sc, pout = self.tf
            corners = np.array([[(lo, hi)[i >> j & 1][j] for j in range(3)] for i in range(8)])
            w = ((corners - pin) * sc) @ R.T + pout
            lo, hi = w.min(0), w.max(0)
        pad = k * sc + 3 * self.h
        sl, i0, i1 = self._slices(lo - pad, hi + pad)
        if any(b <= a for a, b in zip(i0, i1)):
            return
        axes = [self.o[d] + np.arange(i0[d], i1[d]) * self.h for d in range(3)]
        X, Y, Z = np.meshgrid(*axes, indexing="ij")
        P = np.stack([X, Y, Z], -1)
        if self.tf is not None:
            P = ((P - pout) @ R) / sc + pin
            d = fn(P) * sc
            k = k * sc
        else:
            d = fn(P)
        F = self.F[sl]
        if op == "union":
            self.F[sl] = smin(F, d, k)
        else:
            self.F[sl] = smax(F, -d, k)

    # 形ごとの便利関数（外接箱を自動計算）
    def sphere(self, c, r, k=0.0, op="union"):
        c = np.asarray(c)
        self.apply(lambda P: sd_sphere(P, c, r), c - r, c + r, op, k)

    def ellipsoid(self, c, radii, k=0.0, op="union", R=None):
        c = np.asarray(c)
        m = max(radii)
        self.apply(lambda P: sd_ellipsoid(P, c, radii, R), c - m, c + m, op, k)

    def round_box(self, c, half, r, k=0.0, op="union"):
        c = np.asarray(c)
        self.apply(lambda P: sd_round_box(P, c, half, r), c - np.asarray(half), c + np.asarray(half), op, k)

    def cone(self, a, b, r1, r2, k=0.0, op="union"):
        a, b = np.asarray(a), np.asarray(b)
        m = max(r1, r2)
        self.apply(lambda P: sd_round_cone(P, a, b, r1, r2), np.minimum(a, b) - m, np.maximum(a, b) + m, op, k)

    def tube(self, pts, radii, k=0.0, op="union"):
        """点列に沿って太さの変わるチューブ"""
        pts = [np.asarray(p, dtype=np.float64) for p in pts]
        m = max(radii)
        lo = np.min(pts, axis=0) - m
        hi = np.max(pts, axis=0) + m

        def fn(P):
            d = None
            for i in range(len(pts) - 1):
                if np.linalg.norm(pts[i + 1] - pts[i]) < 1e-6:
                    continue
                di = sd_round_cone(P, pts[i], pts[i + 1], radii[i], radii[i + 1])
                d = di if d is None else np.minimum(d, di)
            return d

        self.apply(fn, lo, hi, op, k)

    def torus(self, c, axis_R, big, small, k=0.0, op="union"):
        c = np.asarray(c)
        m = big + small
        self.apply(lambda P: sd_torus(P, c, None, axis_R, big, small), c - m, c + m, op, k)


def surface_nets(F, origin, h):
    """Naive Surface Nets。F<0 が内側。戻り値 (verts (N,3), quads (M,4))"""
    nx, ny, nz = F.shape
    offs = [(dx, dy, dz) for dz in (0, 1) for dy in (0, 1) for dx in (0, 1)]
    cs = [F[dx:nx - 1 + dx, dy:ny - 1 + dy, dz:nz - 1 + dz] for dx, dy, dz in offs]
    cmin = np.minimum.reduce(cs)
    cmax = np.maximum.reduce(cs)
    active = (cmin < 0) & (cmax >= 0)
    acc = np.zeros(active.shape + (3,))
    cnt = np.zeros(active.shape)
    for i in range(8):
        for j in range(i + 1, 8):
            if bin(i ^ j).count("1") != 1:
                continue
            a, b = cs[i], cs[j]
            m = (a < 0) != (b < 0)
            t = np.where(m, a / np.where(m, a - b, 1.0), 0.0)
            oi = np.asarray(offs[i], dtype=np.float64)
            oj = np.asarray(offs[j], dtype=np.float64)
            pos = oi + t[..., None] * (oj - oi)
            acc[m] += pos[m]
            cnt[m] += 1
    ids = np.full(active.shape, -1, dtype=np.int64)
    idx = np.nonzero(active)
    ids[idx] = np.arange(len(idx[0]))
    local = acc[idx] / cnt[idx][:, None]
    verts = np.asarray(origin) + (np.stack(idx, -1) + local) * h
    inside = F < 0
    quads = []
    for a in range(3):
        b, c = (a + 1) % 3, (a + 2) % 3
        sl0 = [slice(None)] * 3
        sl1 = [slice(None)] * 3
        sl0[a] = slice(0, F.shape[a] - 1)
        sl1[a] = slice(1, F.shape[a])
        s0 = inside[tuple(sl0)]
        s1 = inside[tuple(sl1)]
        edge = s0 != s1
        # 周囲 4 セルが存在する辺だけ
        e = np.zeros_like(edge)
        sub = [slice(None)] * 3
        sub[b] = slice(1, F.shape[b] - 1)
        sub[c] = slice(1, F.shape[c] - 1)
        e[tuple(sub)] = edge[tuple(sub)]
        P = np.stack(np.nonzero(e), -1)
        flip = ~s0[tuple(P.T)]
        cells = []
        for db, dc in ((-1, -1), (0, -1), (0, 0), (-1, 0)):
            q = P.copy()
            q[:, b] += db
            q[:, c] += dc
            cells.append(ids[tuple(q.T)])
        qd = np.stack(cells, -1)
        qd[flip] = qd[flip][:, ::-1]
        quads.append(qd)
    quads = np.concatenate(quads)
    quads = quads[(quads >= 0).all(1)]
    return verts, quads


def mesh_from_sdf(name, grid):
    verts, quads = surface_nets(grid.F, grid.o, grid.h)
    me = C.bpy.data.meshes.new(name)
    me.from_pydata(verts.tolist(), [], quads.tolist())
    me.update()
    obj = C.bpy.data.objects.new(name, me)
    return C.link_object(obj)


# ---------------------------------------------------------------------------
# 形
# ---------------------------------------------------------------------------
def _rot_z(deg):
    return np.asarray(Matrix.Rotation(math.radians(deg), 3, "Z"))


def _euler(x=0, y=0, z=0):
    from mathutils import Euler
    return np.asarray(Euler((math.radians(x), math.radians(y), math.radians(z))).to_matrix())


def _frame(normal, up_hint):
    n = Vector(normal).normalized()
    t1 = Vector(up_hint) - n * n.dot(Vector(up_hint))
    t1.normalize()
    t2 = n.cross(t1)
    return n, t1, t2


def _curl_pts(center, normal, up_hint, r_out, turns=1.35, tail=1.6, hand=1, n=26, lift=0.35):
    """巴形の渦巻き（外へ伸びる尾 → 内側へ巻く）。center は渦の中心"""
    nrm, t1, t2 = _frame(normal, up_hint)
    c = Vector(center)
    pts = []
    # 尾（t1 方向の外側から渦の外周へ流れ込む）
    for i in range(4):
        s = i / 4
        a = -0.9 + 0.9 * s
        r = r_out * (tail - (tail - 1.0) * s)
        pts.append(c + (t1 * math.cos(a * hand) * 1.0 + t2 * math.sin(a * hand) * 0.6) * r
                   + nrm * r_out * lift * 0.2 * (1 - s))
    for i in range(n):
        s = i / (n - 1)
        a = hand * s * turns * math.tau
        r = r_out * (1.0 - 0.82 * s)
        pts.append(c + (t1 * math.cos(a) + t2 * math.sin(a)) * r + nrm * r_out * lift * s)
    return [tuple(p) for p in pts]


def _curl(g, center, normal, up_hint, r_out, tube0, tube1, hand=1, k=0.006, turns=1.35, tail=1.6):
    pts = _curl_pts(center, normal, up_hint, r_out, turns=turns, tail=tail, hand=hand)
    m = len(pts)
    radii = [tube0 * (1 - i / (m - 1)) + tube1 * i / (m - 1) for i in range(m)]
    radii[0] *= 0.6
    g.tube(pts, radii, k=k)


def _axis_frame(zdir):
    """ローカル z 軸が zdir を向く回転（列 = ローカル軸）"""
    z = Vector(zdir).normalized()
    x = Vector((0, 0, 1)).cross(z)
    if x.length < 1e-4:
        x = Vector((1, 0, 0))
    x.normalize()
    y = z.cross(x)
    return np.array([[x[i], y[i], z[i]] for i in range(3)])


def shisa_sdf(open_mouth, turn_deg=0.0, h=0.004, seed=0):
    """シーサーの SDF 格子。高さ ~0.68m、正面 -Y、足元 z=0"""
    rnd = random.Random(seed)
    g = SDFGrid((-0.3, -0.42, -0.01), (0.3, 0.32, 0.84), h)

    # --- 胴（ずんぐり）----------------------------------------------------------
    g.round_box((0, -0.02, 0.022), (0.18, 0.22, 0.022), 0.012)                       # 一体の台
    for sx in (-1, 1):
        g.ellipsoid((sx * 0.1, 0.05, 0.12), (0.092, 0.13, 0.1), k=0.03)               # 後ろ脚の腿
        g.ellipsoid((sx * 0.132, -0.06, 0.035), (0.052, 0.08, 0.034), k=0.02)         # 後ろ足
    g.cone((0, 0.055, 0.13), (0, -0.03, 0.3), 0.13, 0.11, k=0.04)                    # 胴
    g.ellipsoid((0, -0.08, 0.28), (0.12, 0.09, 0.1), k=0.04)                         # 胸
    for sx in (-1, 1):
        g.cone((sx * 0.072, -0.09, 0.28), (sx * 0.08, -0.13, 0.06), 0.058, 0.048, k=0.025)  # 前脚
        g.ellipsoid((sx * 0.08, -0.165, 0.035), (0.056, 0.07, 0.035), k=0.02)         # 前足
        for t in (-1, 0, 1):
            g.sphere((sx * 0.08 + t * 0.032, -0.222, 0.029), 0.019, k=0.01)            # 指
        # 腿・肘の巻き毛
        _curl(g, (sx * 0.185, 0.06, 0.15), (sx, 0.1, 0.1), (0, -1, 0.6), 0.055, 0.019, 0.007,
              hand=sx, k=0.008)
        _curl(g, (sx * 0.122, -0.095, 0.215), (sx, -0.45, 0.0), (0, 0.2, 1), 0.036, 0.014, 0.005,
              hand=-sx, k=0.006)
    g.cone((0, -0.03, 0.29), (0, -0.07, 0.40), 0.11, 0.11, k=0.04)                   # 首

    # --- 尾（背中を立ち上がり、渦を巻いて炎のように広がる）---------------------
    tail = geo.bezier_points((0, 0.16, 0.10), (0, 0.25, 0.16), (0, 0.24, 0.29), (0, 0.21, 0.37), 8)
    g.tube([tuple(p) for p in tail], [0.038 - 0.012 * i / 7 for i in range(8)], k=0.02)
    _curl(g, (0, 0.225, 0.405), (0, 1, 0.15), (0, 0, 1), 0.064, 0.024, 0.008, hand=1, k=0.01, turns=1.5)
    for sx in (-1, 1):
        _curl(g, (sx * 0.08, 0.21, 0.32), (sx * 0.4, 1, 0.1), (sx, 0, 0.8), 0.046, 0.018, 0.006,
              hand=sx, k=0.008)
        _curl(g, (sx * 0.065, 0.22, 0.225), (sx * 0.3, 1, -0.1), (sx, 0, 0.3), 0.036, 0.015, 0.005,
              hand=-sx, k=0.007)

    # --- 頭（大きめに拡大し、首の付け根を軸に少し振り向く）-------------------------
    g.tf = (np.array([0.0, -0.05, 0.36]), _rot_z(turn_deg), 1.2, np.array([0.0, -0.06, 0.345]))
    g.ellipsoid((0, -0.08, 0.47), (0.152, 0.13, 0.13), k=0.04)                      # 頭
    g.ellipsoid((0, -0.11, 0.535), (0.13, 0.10, 0.075), k=0.03)                     # 額
    g.ellipsoid((0, -0.03, 0.585), (0.15, 0.13, 0.115), k=0.05)                     # 頭頂（たてがみの内側を埋める）
    for sx in (-1, 1):
        g.sphere((sx * 0.105, -0.175, 0.445), 0.066, k=0.03)                         # 頬
    g.ellipsoid((0, -0.195, 0.445), (0.12, 0.08, 0.075), k=0.03)                    # 鼻面
    # 鼻（幅広の団子鼻）と鼻筋
    g.ellipsoid((0, -0.272, 0.484), (0.072, 0.044, 0.036), k=0.018)
    g.cone((0, -0.255, 0.5), (0, -0.235, 0.575), 0.028, 0.02, k=0.02)
    # 眉（太く、外側で巻き上がる）
    for sx in (-1, 1):
        brow = [(sx * 0.016, -0.238, 0.598), (sx * 0.06, -0.248, 0.616), (sx * 0.102, -0.232, 0.618),
                (sx * 0.132, -0.205, 0.608), (sx * 0.142, -0.185, 0.588), (sx * 0.128, -0.185, 0.572)]
        g.tube(brow, [0.022, 0.029, 0.028, 0.023, 0.017, 0.01], k=0.012)
    # 耳（垂れ耳）
    for sx in (-1, 1):
        g.ellipsoid((sx * 0.158, -0.05, 0.55), (0.03, 0.058, 0.04), k=0.012, R=_euler(y=sx * 35, z=-sx * 20))
    # 目（丸く飛び出した目 + まぶたの縁）
    for sx in (-1, 1):
        ec = np.array([sx * 0.074, -0.236, 0.55])
        gaze = np.array([sx * 0.25, -1.0, 0.05])
        g.sphere(ec, 0.048, k=0.01)
        g.torus(ec + gaze / np.linalg.norm(gaze) * 0.014, _axis_frame(gaze), 0.043, 0.009, k=0.006)
    # 口
    if open_mouth:
        # 阿形: 大きく開いた口、上下の歯と牙、舌
        g.ellipsoid((0, -0.2, 0.378), (0.105, 0.075, 0.042), k=0.02)                # 下あご
        g.ellipsoid((0, -0.258, 0.425), (0.092, 0.075, 0.03), k=0.012, op="sub")
        g.ellipsoid((0, -0.215, 0.425), (0.075, 0.05, 0.024), k=0.01, op="sub")
        g.ellipsoid((0, -0.215, 0.404), (0.055, 0.05, 0.012), k=0.008)              # 舌
        for row_z, dz in ((0.449, -1), (0.401, 1)):
            for i in range(7):
                a = (i - 3) / 3.0
                x = a * 0.062
                y = -0.2 - 0.064 * math.sqrt(max(0.0, 1 - (x / 0.085) ** 2))
                g.ellipsoid((x, y, row_z + dz * 0.004), (0.0085, 0.008, 0.011), k=0.004)
        for sx in (-1, 1):
            g.cone((sx * 0.066, -0.245, 0.458), (sx * 0.07, -0.262, 0.418), 0.012, 0.004, k=0.005)
            g.cone((sx * 0.062, -0.24, 0.39), (sx * 0.064, -0.258, 0.43), 0.011, 0.004, k=0.005)
    else:
        # 吽形: 口を結び、上の牙が下唇にかかる
        g.ellipsoid((0, -0.195, 0.39), (0.1, 0.075, 0.04), k=0.02)
        lip = []
        for i in range(13):
            a = (i - 6) / 6.0
            x = a * 0.1
            y = -0.195 - 0.078 * math.sqrt(max(0.0, 1 - (x / 0.115) ** 2))
            lip.append((x, y, 0.425 + 0.018 * a * a))
        g.tube(lip, [0.0065] * len(lip), k=0.004, op="sub")
        for sx in (-1, 1):
            g.cone((sx * 0.058, -0.258, 0.432), (sx * 0.06, -0.272, 0.402), 0.012, 0.004, k=0.004)
    # 鼻の穴と瞳（彫り込み）
    for sx in (-1, 1):
        g.sphere((sx * 0.032, -0.312, 0.472), 0.016, k=0.006, op="sub")
        g.sphere((sx * 0.087, -0.283, 0.551), 0.02, k=0.004, op="sub")
    # あごひげの巻き毛
    for sx in (-1, 0, 1):
        _curl(g, (sx * 0.065, -0.185 + abs(sx) * 0.03, 0.332 - (0.012 if sx == 0 else 0)),
              (sx * 0.4, -1, -0.5), (0, 0, -1), 0.036, 0.015, 0.005, hand=1 if sx >= 0 else -1, k=0.006)

    # たてがみ: 頭の後ろの襟巻き + 渦巻きの房（3 重）
    g.torus((0, -0.01, 0.47), _axis_frame((0, 1, 0)), 0.13, 0.055, k=0.035)
    hc = np.array([0, -0.04, 0.47])
    for ring, (yb, rr, count, span, r0) in enumerate(((-0.01, 0.19, 11, 260, 0.052),
                                                     (0.06, 0.165, 9, 240, 0.05),
                                                     (0.12, 0.11, 6, 220, 0.045))):
        for i in range(count):
            phi = math.radians(90 - span / 2 + span * i / (count - 1) + (ring % 2) * 6)
            d = np.array([math.cos(phi), 0.0, math.sin(phi)])
            c = hc + np.array([0, yb, 0]) + d * rr
            nrm = d + np.array([0, 0.2 + ring * 0.7, 0])
            tang = np.array([-math.sin(phi), 0, math.cos(phi)])
            hand = 1 if math.cos(phi) >= 0 else -1
            _curl(g, c, nrm, np.array([0, 1, 0]) + tang * 0.3 * hand, r0 * rnd.uniform(0.9, 1.1),
                  0.021, 0.007, hand=hand, k=0.01)
    # 頭頂の巻き毛（前髪の列）
    for i in range(5):
        a = math.radians(-52 + 26 * i)
        for row, (yy, rz, r0) in enumerate(((-0.11, 0.1, 0.034), (-0.04, 0.112, 0.038))):
            if row == 1 and i in (0, 4):
                continue
            c = (0.135 * math.sin(a), yy, 0.585 + rz * math.cos(a))
            _curl(g, c, (math.sin(a), -0.35 + row * 0.2, math.cos(a)), (0, 1, 0), r0, 0.015, 0.005,
                  hand=1 if (i + row) % 2 else -1, k=0.008)
    g.tf = None
    return g


def shisa_mesh(name, open_mouth, turn_deg=0.0, height=0.68, target_tris=12000, h=0.004, seed=0, uv_angle=62):
    """height: 仕上がりの高さ(m)。uv_angle: ベイク用 Smart UV の角度（有機形状は大きめで島を減らす）"""
    g = shisa_sdf(open_mouth, turn_deg, h=h, seed=seed)
    obj = mesh_from_sdf(name, g)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=h * 0.05)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data)
    bm.free()
    geo.smooth_mod(obj, factor=0.5, iterations=3)
    # 高密度で滑らかなうちに UV 展開（島が大きくまとまる）。デシメートは UV の境界を保つ
    C.smart_uv(obj, angle=uv_angle, margin=0.004, uv_name="Bake")
    # 細部: 手びねりのムラと素焼きの肌
    off = Vector((seed * 3.1, seed * 1.7, 0.3))
    bm = bmesh.new()
    bm.from_mesh(obj.data)

    def disp(co, n):
        p = co * 22.0 + off
        return geo.fbm(p, 3) * 0.0022 + geo.fbm(co * 90.0 + off, 2) * 0.0006

    geo.displace_along_normals(bm, disp)
    bm.to_mesh(obj.data)
    bm.free()
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    m = obj.modifiers.new("Decimate", "DECIMATE")
    m.ratio = min(1.0, target_tris / tris)
    m.use_collapse_triangulate = True
    m.delimit = {"UV"}
    C.apply_modifiers(obj)
    zmax = max(v.co.z for v in obj.data.vertices)
    obj.data.transform(Matrix.Scale(height / zmax, 4))
    C.set_smooth(obj, True, angle=70)
    return obj


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def terracotta(name="ShisaClay", res=2048, mould=1.0, plaster=1.0, scale=1.0):
    """風化した素焼き。窪み（局所 AO）に黒カビ、ところどころ白い漆喰"""

    def fn(nb):
        co = nb.mapping(nb.coord("Object"), scale=(1 / scale,) * 3)
        geom = nb.node("ShaderNodeNewGeometry")
        up = nb.sep(geom.outputs["Normal"])[2]
        aon = nb.node("ShaderNodeAmbientOcclusion", samples=16, only_local=True)
        aon.inputs["Distance"].default_value = 0.045 * scale
        ao = aon.outputs["AO"]
        crev = nb.smooth(ao, 0.92, 0.45)        # 1 = 奥まった所
        big = nb.noise(co, 5.0, 5, 0.6)
        mid = nb.noise(co, 16.0, 5, 0.6)
        fine = nb.noise(co, 90.0, 4, 0.6)
        base = nb.ramp(big, [(0.25, srgb("#7c3421")), (0.5, srgb("#9b4529")), (0.75, srgb("#b35a34"))])
        base = nb.hsv(base, 0.5, 0.88, nb.maprange(mid, 0.3, 0.7, 0.85, 1.1))
        # 雨風で灰色がかった汚れ（大きなムラ）
        grime = nb.smooth(nb.noise(co, 2.2, 4, 0.6, distortion=0.4), 0.45, 0.75)
        col = nb.mix(base, srgb("#665046"), nb.mul(grime, 0.7))
        # 日焼けで白っぽく褪せたところ（上向き・出っ張り）
        faded = nb.mul(nb.smooth(nb.noise(co, 3.0, 4, 0.6, distortion=0.6), 0.4, 0.68),
                       nb.smooth(ao, 0.8, 1.0))
        col = nb.mix(col, srgb("#c99378"), nb.mul(faded, 0.6))
        # 白い漆喰の残り（窪みに溜まり、ところどころ斑に）
        pl_n = nb.noise(co, 7.0, 6, 0.7, distortion=0.3)
        pl = nb.math("MAXIMUM", nb.mul(nb.smooth(pl_n, 0.63, 0.68), 0.9),
                     nb.mul(nb.smooth(crev, 0.3, 0.55), nb.smooth(pl_n, 0.5, 0.58)))
        pl = nb.mul(pl, plaster)
        col = nb.mix(col, nb.mix(srgb("#e4dccd"), srgb("#b5ab9a"), nb.mul(fine, 0.7)), pl)
        # 黒カビ: 窪み + 上向き面から垂れる筋 + 斑点
        streak = nb.noise(nb.mapping(co, scale=(16, 16, 1.2)), 3.0, 4, 0.6)
        drip = nb.mul(nb.smooth(streak, 0.48, 0.7), nb.smooth(up, -0.3, 0.5))
        spots = nb.smooth(nb.noise(co, 7.0, 5, 0.65), 0.5, 0.66)
        top = nb.mul(nb.smooth(up, 0.2, 0.9), nb.smooth(nb.noise(co, 4.0, 4, 0.6), 0.35, 0.55))
        m = nb.math("MAXIMUM", nb.mul(crev, 1.0), nb.mul(drip, 0.6))
        m = nb.math("MAXIMUM", m, nb.mul(spots, 0.7))
        m = nb.math("MAXIMUM", m, nb.mul(top, 0.85))
        m = nb.mul(m, mould)
        mcol = nb.mix(srgb("#24221d"), srgb("#3b3d2b"), nb.smooth(mid, 0.4, 0.7))
        mcol = nb.mix(mcol, srgb("#56504a"), nb.mul(nb.smooth(fine, 0.5, 0.8), 0.5))
        col = nb.mix(col, mcol, nb.math("MINIMUM", nb.mul(m, nb.maprange(fine, 0.2, 0.8, 0.75, 1.0)), 0.95))
        rough = nb.maprange(nb.add(nb.mul(pl, 0.1), nb.mul(fine, 0.1)), 0.0, 0.2, 0.78, 0.95)
        pits = nb.smooth(nb.voronoi(co, 70.0), 0.12, 0.0)
        height = nb.add(nb.add(nb.mul(mid, 0.5), nb.mul(fine, 0.35)), nb.mul(pits, -0.5))
        height = nb.add(height, nb.mul(pl, 0.4))
        cavity = nb.maprange(ao, 0.3, 1.0, 0.55, 1.0)
        return dict(color=col, rough=rough, height=height, height_scale=0.0025 * scale, cavity=cavity)

    return pbr_material(name, fn, res=res, ao_distance=0.25 * scale, uv="keep")


# ---------------------------------------------------------------------------
# 台座
# ---------------------------------------------------------------------------
def pedestal(name, seed):
    rnd = random.Random(seed)
    lo = geo.box(name + "_lo", (0.52, 0.58, 0.11), loc=(0, -0.02, 0.055 - 0.02))
    hi = geo.box(name + "_hi", (0.44, 0.50, 0.13), loc=(0, -0.02, 0.09 + 0.065))
    parts = []
    for o in (lo, hi):
        C.apply_transform(o)
        geo.bevel(o, width=0.018, segments=2)
        bm = bmesh.new()
        bm.from_mesh(o.data)
        off = Vector((rnd.uniform(0, 50), rnd.uniform(0, 50), 0))
        for v in bm.verts:
            v.co += Vector((geo.fbm(v.co * 6 + off, 2), geo.fbm(v.co * 6 + off + Vector((5, 0, 0)), 2), 0)) * 0.006
        bm.to_mesh(o.data)
        bm.free()
        parts.append(o)
    ped = C.join(parts, name)
    return ped


def build():
    clay = terracotta("ShisaClay", res=2048)
    stone = limestone("ShisaPedestal", res=1024, tide_top=-10, dark_top=0.5)
    out = {}
    for vname, open_mouth, turn, seed in (("Shisa_Agyo", True, -12.0, 3), ("Shisa_Ungyo", False, 12.0, 7)):
        s = shisa_mesh(vname + "_body", open_mouth, turn, seed=seed, target_tris=12500)
        s.location.z = PEDESTAL_H - 0.004
        C.assign(s, clay)
        p = pedestal(vname + "_ped", seed)
        C.assign(p, stone)
        out[vname] = [s, p]
    return out
