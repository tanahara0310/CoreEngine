"""6 シーサー（阿形・吽形の一対、台座付き）

胴・頭・脚・たてがみの渦・尾を符号付き距離関数 (SDF) のプリミティブで組み、
smooth union / subtraction で粘土をこねたように繋げる。
SDF をボクセル格子で評価し Surface Nets でメッシュ化 → スムーズ → 細部の凹凸 → デシメート。

胸を張って頭を高く上げた座り姿（高さ ~0.85m、頭は全高の約 4 割）。たてがみは巻貝状の渦を
顔の周りから胸の前掛けへ段々に流し、尾は渦を積んだ炎形。渦は表面を探って（SDF 上のレイマーチ）置く。
材質: 素焼きの赤土色。出っ張りは明るく窪みは暗い赤茶、黒カビと漆喰は控えめ。
台座: 琉球石灰岩の二段ブロック。

原点: 台座底面の中心（接地点）。正面は -Y。
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
from ..materials import limestone

PREVIEW = dict(cam_dir=(0.35, -1.0, 0.22), lens=55)

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


    def sample(self, P):
        from scipy.ndimage import map_coordinates
        idx = ((np.atleast_2d(P) - self.o) / self.h).T
        return map_coordinates(self.F, idx, order=1, mode="nearest")

    def surf(self, origin, direction, max_d=0.5):
        """origin（内側）から direction へ進んで表面に当たる点と外向き法線"""
        o = np.asarray(origin, dtype=np.float64)
        d = np.asarray(direction, dtype=np.float64)
        d = d / np.linalg.norm(d)
        ts = np.arange(0.0, max_d, self.h * 0.5)
        v = self.sample(o[None] + ts[:, None] * d[None])
        inside = np.nonzero(v < 0)[0]
        if not len(inside):
            return None
        out = np.nonzero(v[inside[0]:] > 0)[0]
        if not len(out):
            return None
        i = inside[0] + out[0]
        t = ts[i - 1] + (ts[i] - ts[i - 1]) * (-v[i - 1]) / (v[i] - v[i - 1])
        p = o + d * t
        e = self.h
        gr = np.array([self.sample(p + e * ax)[0] - self.sample(p - e * ax)[0] for ax in np.eye(3)])
        return p, gr / max(np.linalg.norm(gr), 1e-9)

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


def _axis_frame(zdir):
    """ローカル z 軸が zdir を向く回転（列 = ローカル軸）"""
    z = Vector(zdir).normalized()
    x = Vector((0, 0, 1)).cross(z)
    if x.length < 1e-4:
        x = Vector((1, 0, 0))
    x.normalize()
    y = z.cross(x)
    return np.array([[x[i], y[i], z[i]] for i in range(3)])


def _nrm(v):
    v = np.asarray(v, dtype=np.float64)
    return v / max(np.linalg.norm(v), 1e-9)


def whorl(g, c, n, r, up=(0, 0, 1), hand=1, turns=1.6, dome=0.35, k=0.008, **_):
    """巻貝のような渦巻きの房: 低い土台の上に、中心へ向かって高くなる太い渦の帯を巻く（溝に深さが出る）"""
    nrm, t1, t2 = _frame(n, up)
    c = np.asarray(c, dtype=np.float64)
    nrm, t1, t2 = np.asarray(nrm), np.asarray(t1), np.asarray(t2)
    R = np.column_stack([t1, t2, nrm])
    g.ellipsoid(c, (r * 0.95, r * 0.95, r * dome), k=k, R=R)
    pts, radii = [], []
    m = 34
    for i in range(m):
        s = i / (m - 1)
        rho = r * (0.8 - 0.72 * s)
        a = hand * s * turns * math.tau
        hh = r * (dome * 0.6 + 0.35 * s)
        pts.append(c + (t1 * math.cos(a) + t2 * math.sin(a)) * rho + nrm * hh)
        radii.append(r * (0.26 - 0.1 * s))
    g.tube(pts, radii, k=g.h * 0.5)


def shisa_sdf(open_mouth, turn_deg=0.0, h=0.0035, seed=0):
    """シーサーの SDF 格子。高さ ~0.85m、正面 -Y、足元 z=0。
    胸を張って頭を高く上げた座り姿。たてがみは巻貝状の渦を顔の周りから胸へ流す"""
    rnd = random.Random(seed)
    g = SDFGrid((-0.3, -0.42, -0.01), (0.3, 0.42, 0.92), h)
    hand = lambda x: 1 if x >= 0 else -1  # noqa: E731

    # --- 胴: 背筋を斜めに伸ばし、胸を前へ張る -------------------------------------
    g.round_box((0, -0.03, 0.025), (0.2, 0.27, 0.025), 0.015)                        # 一体の台
    for sx in (-1, 1):
        g.ellipsoid((sx * 0.115, 0.1, 0.15), (0.1, 0.16, 0.14), k=0.04)                # 腿
        g.ellipsoid((sx * 0.16, -0.05, 0.04), (0.055, 0.1, 0.04), k=0.02)              # 後ろ足
        for t in (-1, 0, 1):
            g.sphere((sx * 0.16 + t * 0.028, -0.14, 0.03), 0.018, k=0.008)
    g.cone((0, 0.11, 0.17), (0, -0.03, 0.47), 0.14, 0.115, k=0.05)                    # 胴
    g.ellipsoid((0, -0.09, 0.41), (0.13, 0.115, 0.16), k=0.05)                        # 胸
    for sx in (-1, 1):
        g.ellipsoid((sx * 0.092, -0.11, 0.34), (0.064, 0.078, 0.125), k=0.03)          # 上腕の筋肉
        g.cone((sx * 0.09, -0.13, 0.3), (sx * 0.094, -0.172, 0.07), 0.052, 0.042, k=0.025)  # 前腕
        g.ellipsoid((sx * 0.094, -0.2, 0.045), (0.062, 0.08, 0.045), k=0.02)           # 前足
        for t in (-1.5, -0.5, 0.5, 1.5):
            g.sphere((sx * 0.094 + t * 0.025, -0.265, 0.03), 0.019, k=0.006)          # 指
    g.cone((0, -0.04, 0.44), (0, -0.09, 0.58), 0.12, 0.105, k=0.05)                   # 首

    # --- 頭（首の付け根を軸に少し振り向く）---------------------------------------
    pivot = np.array([0.0, -0.08, 0.56])
    Rh = _rot_z(turn_deg)
    g.tf = (pivot, Rh, 1.0, pivot)

    def T(p):
        return Rh @ (np.asarray(p, dtype=np.float64) - pivot) + pivot

    def TD(d):
        return Rh @ np.asarray(d, dtype=np.float64)

    g.ellipsoid((0, -0.08, 0.68), (0.125, 0.13, 0.11), k=0.04)                        # 頭蓋
    for sx in (-1, 1):
        g.sphere((sx * 0.1, -0.17, 0.635), 0.055, k=0.03)                              # 頬
    g.ellipsoid((0, -0.19, 0.636), (0.106, 0.088, 0.044), k=0.03)                    # 上あご（鼻面）
    g.ellipsoid((0, -0.278, 0.664), (0.068, 0.033, 0.03), k=0.012)                   # 鼻（平たく広い）
    g.cone((0, -0.262, 0.688), (0, -0.228, 0.742), 0.022, 0.016, k=0.018)            # 鼻筋
    for sx in (-1, 1):
        g.sphere((sx * 0.062, -0.272, 0.656), 0.02, k=0.008)                           # 小鼻の張り
        g.sphere((sx * 0.034, -0.31, 0.653), 0.015, k=0.004, op="sub")                # 鼻の穴
    # 眉の土台（その上に渦を並べる）
    g.ellipsoid((0, -0.2, 0.745), (0.135, 0.055, 0.026), k=0.02)
    # 上唇（めくれ上がり、口角で巻く）
    lip = []
    for i in range(13):
        a = (i - 6) / 6.0
        x = a * 0.115
        lip.append((x, -0.19 - 0.09 * math.sqrt(max(0.0, 1 - (x / 0.13) ** 2)), 0.614 + 0.035 * a * a))
    g.tube(lip, [0.015] * len(lip), k=0.008)
    for sx in (-1, 1):
        g.tube([(sx * 0.115, -0.215, 0.649), (sx * 0.13, -0.205, 0.672), (sx * 0.12, -0.215, 0.688),
                (sx * 0.106, -0.225, 0.675)], [0.014, 0.012, 0.009, 0.006], k=0.006)
    if open_mouth:
        # 阿形: 大きく開いた口、上下の歯列と牙、舌
        g.ellipsoid((0, -0.245, 0.572), (0.106, 0.1, 0.042), k=0.012, op="sub")
        g.ellipsoid((0, -0.185, 0.58), (0.085, 0.07, 0.034), k=0.01, op="sub")
        g.ellipsoid((0, -0.19, 0.52), (0.1, 0.085, 0.035), k=0.02)                   # 下あご
        low = []
        for i in range(11):
            a = (i - 5) / 5.0
            x = a * 0.1
            low.append((x, -0.195 - 0.08 * math.sqrt(max(0.0, 1 - (x / 0.115) ** 2)), 0.54 + 0.012 * a * a))
        g.tube(low, [0.013] * len(low), k=0.008)                                      # 下唇
        g.ellipsoid((0, -0.21, 0.543), (0.06, 0.062, 0.013), k=0.008)               # 舌
        g.sphere((0, -0.262, 0.548), 0.016, k=0.01)                                  # 舌先
        for row_z, dz in ((0.598, -1), (0.552, 1)):
            for i in range(8):
                a = (i - 3.5) / 3.5
                x = a * 0.055
                y = -0.195 - 0.07 * math.sqrt(max(0.0, 1 - (x / 0.085) ** 2))
                g.cone((x, y, row_z), (x, y - 0.002, row_z + dz * 0.016), 0.008, 0.003, k=0.003)
        for sx in (-1, 1):
            g.cone((sx * 0.074, -0.255, 0.612), (sx * 0.076, -0.268, 0.556), 0.013, 0.003, k=0.004)  # 上の牙
            g.cone((sx * 0.066, -0.25, 0.534), (sx * 0.068, -0.264, 0.582), 0.012, 0.003, k=0.004)   # 下の牙
    else:
        # 吽形: 口を結び、上の牙が下唇にかかる
        g.ellipsoid((0, -0.195, 0.568), (0.1, 0.085, 0.036), k=0.02)
        g.tube([(x, -0.19 - 0.087 * math.sqrt(max(0.0, 1 - (x / 0.12) ** 2)), 0.592 + 0.02 * (x / 0.1) ** 2)
                for x in np.linspace(-0.1, 0.1, 11)], [0.006] * 11, k=0.004, op="sub")
        low = [(x, -0.19 - 0.083 * math.sqrt(max(0.0, 1 - (x / 0.12) ** 2)), 0.573) for x in np.linspace(-0.09, 0.09, 9)]
        g.tube(low, [0.013] * len(low), k=0.008)
        for sx in (-1, 1):
            g.cone((sx * 0.066, -0.27, 0.606), (sx * 0.067, -0.284, 0.56), 0.013, 0.003, k=0.004)
    # 目: 眉の下の深い眼窩に、にらみつける目
    for sx in (-1, 1):
        g.ellipsoid((sx * 0.058, -0.235, 0.705), (0.036, 0.03, 0.026), k=0.008, op="sub")
        g.ellipsoid((sx * 0.058, -0.228, 0.703), (0.029, 0.026, 0.021), k=0.004)
        g.sphere((sx * 0.061, -0.256, 0.702), 0.009, k=0.003, op="sub")
        # 上まぶたの厚いひさし（目尻が吊り上がる）
        g.tube([(sx * 0.028, -0.248, 0.716), (sx * 0.06, -0.252, 0.724), (sx * 0.092, -0.232, 0.73)],
               [0.008, 0.01, 0.007], k=0.005)
    g.tf = None

    # --- たてがみ: 表面に沿って渦を並べる（現在の形から表面位置と法線を求める）--------
    def put(origin, d, r, up=(0, 0, 1), head=False, **kw):
        if head:
            origin, d, up = T(origin), TD(d), TD(up)
        hit = g.surf(origin, d)
        if hit is None:
            return
        p, n = hit
        whorl(g, p - n * r * 0.2, n, r * rnd.uniform(0.92, 1.08), up=up, **kw)

    hc = np.array([0.0, -0.08, 0.68])
    # 眉の渦
    for sx in (-1, 1):
        for x in (0.032, 0.085, 0.13):
            put((sx * x, -0.16, 0.745), (sx * x * 2, -1.0, 0.15), 0.03 - x * 0.06, up=(0, 0, 1), head=True,
                hand=hand(sx))
    # 頭頂と耳（小さな炎）
    for i, a in enumerate((-45, -15, 15, 45)):
        ar = math.radians(a)
        put(hc, (math.sin(ar), -0.3, math.cos(ar)), 0.038, up=(0, -1, 0), head=True, hand=1 if i % 2 else -1)
    for a in (-60, -30, 0, 30, 60):
        ar = math.radians(a)
        put(hc, (math.sin(ar), 0.35, math.cos(ar)), 0.04, up=(0, -1, 0), head=True, hand=hand(a))
    for sx in (-1, 1):
        put(hc + np.array([sx * 0.02, 0, 0]), (sx * 0.8, -0.25, 0.75), 0.03, up=(0, 0, 1), head=True, hand=sx)
        g.ellipsoid(T((sx * 0.135, -0.07, 0.755)), (0.016, 0.042, 0.034), k=0.01,
                    R=Rh @ _euler(x=-30, y=sx * 35))                                   # 小さな炎形の耳
    # 顔を縁取る頬のたてがみ
    for sx in (-1, 1):
        for th in (50, 72, 94, 116, 138):
            tr = math.radians(th)
            put(hc + np.array([0, -0.07, 0]), (sx * math.sin(tr), -0.45, math.cos(tr)), 0.045, up=(0, -1, 0),
                head=True, hand=hand(sx))
        for th in (60, 95, 130):
            tr = math.radians(th)
            put(hc + np.array([0, 0.06, 0]), (sx * math.sin(tr), 0.45, math.cos(tr)), 0.048, up=(0, -1, 0),
                head=True, hand=-hand(sx))
        for th in (40, 75, 110, 145):
            tr = math.radians(th)
            put(hc + np.array([0, 0.0, 0]), (sx * math.sin(tr), 0.05, math.cos(tr)), 0.044, up=(0, -1, 0),
                head=True, hand=hand(sx) * (1 if th % 2 else -1))
    put(hc, (0, 1, 0.3), 0.05, up=(0, 0, 1), head=True)
    # あごひげ
    jaw_z = 0.52 if open_mouth else 0.568
    for x in (-0.05, 0.0, 0.05):
        put((x, -0.16, jaw_z - 0.02), (x * 5, -0.3, -1), 0.028, up=(0, -1, 0), head=True, hand=hand(x))
    # 胸へ流れ落ちる前掛け（V 字に段々）
    rows = [(0.515, (-0.11, -0.037, 0.037, 0.11)), (0.465, (-0.14, -0.07, 0.0, 0.07, 0.14)),
            (0.41, (-0.105, -0.035, 0.035, 0.105)), (0.35, (-0.07, 0.0, 0.07)), (0.295, (-0.035, 0.035)),
            (0.245, (0.0,))]
    for ri, (z, xs) in enumerate(rows):
        for x in xs:
            put((x * 0.5, -0.02, z), (x * 4.0, -1, -0.12), 0.048 - ri * 0.003, up=(0, 0, 1),
                hand=1 if (ri % 2) else -1)
    for sx in (-1, 1):
        for z in (0.53, 0.46, 0.39):
            put((0, -0.03, z), (sx, -0.25, 0.1), 0.044, up=(0, 0, 1), hand=sx)
        for z in (0.55, 0.48):
            put((0, 0.0, z), (sx * 0.6, 0.8, 0.2), 0.045, up=(0, 0, 1), hand=-sx)
    # 渦の間を流れる長い毛筋（首の脇を胸へ）
    for sx in (-1, 1):
        for x0 in (0.1, 0.145):
            pts = []
            for i, z in enumerate(np.linspace(0.56, 0.36, 6)):
                o = np.array([sx * x0 * 0.4, -0.03, z])
                hit = g.surf(o, np.array([sx * (x0 + 0.02 * i) * 6, -0.6, 0.0]))
                if hit is not None:
                    pts.append(hit[0] - hit[1] * 0.001)
            if len(pts) >= 3:
                g.tube(pts, list(np.linspace(0.009, 0.004, len(pts))), k=0.005)
    # 肘・足首・腿の房
    for sx in (-1, 1):
        put((sx * 0.09, -0.11, 0.3), (sx, 0.35, 0.0), 0.03, up=(0, 0, 1), hand=sx)
        put((sx * 0.09, -0.11, 0.24), (sx, 0.6, -0.1), 0.026, up=(0, 0, 1), hand=-sx)
        put((sx * 0.094, -0.17, 0.1), (sx, 0.3, 0.0), 0.022, up=(0, 0, 1), hand=sx)
        put((sx * 0.1, 0.1, 0.16), (sx, 0.1, 0.3), 0.045, up=(0, 1, 0), hand=sx)
        put((sx * 0.1, 0.12, 0.1), (sx, 0.5, -0.2), 0.034, up=(0, 1, 0), hand=-sx)

    # --- 尾: 背後に高く立ち上がる炎。平たい炎の芯に渦を積み重ねる -------------------
    spine = geo.bezier_points((0, 0.2, 0.1), (0, 0.34, 0.2), (0, 0.34, 0.46), (0, 0.27, 0.64), 12)
    g.tube([tuple(p) for p in spine], [0.06 - 0.03 * i / 11 for i in range(12)], k=0.035)
    for i in range(2, 12, 2):
        p = np.asarray(spine[i])
        w = 0.075 - 0.004 * i
        g.ellipsoid(p + np.array([0, 0.025, 0]), (0.04, w, w * 1.1), k=0.03, R=_euler(x=-20 + 4 * i))
    g.tube([tuple(spine[-1]), (0, 0.24, 0.7), (0, 0.19, 0.72), (0, 0.16, 0.7)], [0.028, 0.02, 0.012, 0.005],
           k=0.01)                                                                     # 炎の先（前へ巻く）
    for i, t in enumerate((0.2, 0.36, 0.52, 0.68, 0.84)):
        p = np.asarray(spine[int(round(t * 11))])
        r = 0.05 - 0.004 * i
        for sx in (-1, 1):
            put(p, (sx, 0.35, 0.1), r, up=(0, 1, 0.6), hand=sx)
        put(p + np.array([0, 0, 0.03]), (0, 1, 0.25), r * 0.9, up=(0, 0, 1), hand=1 if i % 2 else -1)
    put(np.asarray(spine[-1]), (0, 0.6, 1), 0.036, up=(0, 1, 0))
    return g


def unwrap_smooth_proxy(obj, angle, margin=0.003):
    """渦の多い形は Smart UV が 1 面ずつの島に砕けるので、
    強くならした複製で島を決め（渦が消えて大きな島になる）、その島の中で実形状に沿って等角展開し直す"""
    proxy = obj.copy()
    proxy.data = obj.data.copy()
    C.link_object(proxy)
    m = proxy.modifiers.new("Smooth", "SMOOTH")
    m.factor = 1.0
    m.iterations = 40
    C.apply_modifiers(proxy)
    C.smart_uv(proxy, angle=angle, margin=margin, uv_name="Bake")
    src = proxy.data.uv_layers["Bake"].data
    arr = np.empty(len(src) * 2, np.float32)
    src.foreach_get("uv", arr)
    layer = obj.data.uv_layers.get("Bake") or obj.data.uv_layers.new(name="Bake")
    layer.data.foreach_set("uv", arr)
    obj.data.uv_layers.active = layer
    C._select_only([obj], obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.seams_from_islands()
    bpy.ops.uv.unwrap(method="CONFORMAL", margin=margin)
    try:
        bpy.ops.uv.pack_islands(udim_source="CLOSEST_UDIM", rotate=True, margin=margin, shape_method="CONCAVE")
    except TypeError:
        bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")
    me = proxy.data
    bpy.data.objects.remove(proxy)
    bpy.data.meshes.remove(me)


def shisa_mesh(name, open_mouth, turn_deg=0.0, height=0.85, target_tris=20000, h=0.0035, seed=0, uv_angle=62):
    """height: 仕上がりの高さ(m)。uv_angle: ベイク用 Smart UV の角度（有機形状は大きめで島を減らす）"""
    g = shisa_sdf(open_mouth, turn_deg, h=h, seed=seed)
    obj = mesh_from_sdf(name, g)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=h * 0.05)
    # 面の向きは Surface Nets の符号から正しく決まっている（recalc は非多様体の所で逆に裏返すので使わない）
    bm.to_mesh(obj.data)
    bm.free()
    geo.smooth_mod(obj, factor=0.4, iterations=2)
    # 高密度のうちに UV 展開してからデシメート（UV の境界は保つ）
    unwrap_smooth_proxy(obj, uv_angle)
    # 細部: 手びねりのムラと素焼きの肌
    off = Vector((seed * 3.1, seed * 1.7, 0.3))
    bm = bmesh.new()
    bm.from_mesh(obj.data)

    def disp(co, n):
        p = co * 22.0 + off
        return geo.fbm(p, 3) * 0.0012 + geo.fbm(co * 90.0 + off, 2) * 0.0004

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
def terracotta(name="ShisaClay", res=2048, mould=0.35, plaster=0.3, scale=1.0):
    """素焼きの赤土色。出っ張りは明るく、窪み（局所 AO）は暗い赤茶。mould/plaster で黒カビ・漆喰の量"""

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
        base = nb.ramp(big, [(0.25, srgb("#a2482a")), (0.5, srgb("#bb5b34")), (0.75, srgb("#c96c42"))])
        base = nb.hsv(base, 0.5, 0.95, nb.maprange(mid, 0.3, 0.7, 0.9, 1.07))
        # 出っ張り（渦の稜線など）は擦れて明るく、窪みは焼きむらで暗い赤茶
        pt = nb.node("ShaderNodeNewGeometry").outputs["Pointiness"]
        col = nb.mix(base, srgb("#dd9166"), nb.mul(nb.smooth(pt, 0.52, 0.62), 0.55))
        col = nb.mix(col, srgb("#5e2716"), nb.mul(crev, 0.65))
        # 雨風でくすんだ大きなムラ（控えめ）
        grime = nb.smooth(nb.noise(co, 2.2, 4, 0.6, distortion=0.4), 0.5, 0.8)
        col = nb.mix(col, srgb("#7a5a4a"), nb.mul(grime, 0.3))
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
        rough = nb.maprange(nb.add(nb.mul(pl, 0.1), nb.mul(fine, 0.1)), 0.0, 0.2, 0.76, 0.88)
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
    lo = geo.box(name + "_lo", (0.6, 0.72, 0.11), loc=(0, -0.03, 0.055 - 0.02))
    hi = geo.box(name + "_hi", (0.5, 0.62, 0.13), loc=(0, -0.03, 0.09 + 0.065))
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
    for vname, open_mouth, turn, seed in (("Shisa_Agyo", True, -10.0, 3), ("Shisa_Ungyo", False, 10.0, 7)):
        s = shisa_mesh(vname + "_body", open_mouth, turn, seed=seed, target_tris=24000)
        s.location.z = PEDESTAL_H - 0.004
        C.assign(s, clay)
        p = pedestal(vname + "_ped", seed)
        C.assign(p, stone)
        out[vname] = [s, p]
    return out
