"""ハイビスカス（赤 / 橙黄）とブーゲンビリア（赤紫の苞）の低木 3 バリエーション

幹・枝: 細いチューブ（BarkPacker で 1 枚に詰めた樹皮テクスチャ）
葉: 小枝ごと描いた「葉の房」アトラス（2x2）を、V 字に少し折ったカードとして枝先・枝沿いに散らす
    アトラスは numpy で 2D 描画（鋸歯のある卵形の葉・葉脈・重なりの影）して画像ノードから焼く
花: ハイビスカスは 5 枚の花弁ジオメトリ + 突き出た雄しべ筒、ブーゲンビリアは苞の房をカードで表現
揺れ: 株全体は高さの 2 乗で曲がり、葉カードは付け根から先へしなる。花は 1 輪ごとに一体で揺れ、
      花弁の先が震える（okinawa/anim.py）
"""
import math
import os
import random

import bpy
import numpy as np
from mathutils import Vector
from scipy import ndimage

from .. import anim
from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from .adan import BarkPacker, MeshAcc, perp, register_cutout_aliases, rotate_toward

PREVIEW = dict(cam_dir=(0.35, -1.0, 0.28), lens=70, spacing=1.1)

UP = Vector((0, 0, 1))
CARD = 0.3          # 葉の房カードの一辺(m)
RES_SCALE = float(os.environ.get("OKI_RES_SCALE", "1"))
_MATS = {}


def _mat(key, fn):
    """同じマテリアル（アトラス描画）をバリエーション間で使い回す"""
    if key not in _MATS:
        _MATS[key] = fn()
    return _MATS[key]


# ---------------------------------------------------------------------------
# numpy による 2D 描画（アトラス用）
# ---------------------------------------------------------------------------
def _lin(hexstr):
    return np.array(srgb(hexstr), np.float32)


def _sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


class Canvas:
    """リニア色・アルファ・高さ・ラフネス・キャビティを重ね描きするキャンバス（行 0 = 下端 = V 0）"""

    def __init__(self, res):
        n = self.n = res
        self.col = np.zeros((n, n, 3), np.float32)
        self.a = np.zeros((n, n), np.float32)
        self.h = np.zeros((n, n), np.float32)
        self.r = np.full((n, n), 0.6, np.float32)
        self.cav = np.ones((n, n), np.float32)

    def region(self, pts, pad):
        pts = np.asarray(pts, np.float64)
        x0, y0 = pts.min(0) - pad
        x1, y1 = pts.max(0) + pad
        n = self.n
        j0, j1 = max(0, int(x0 * n)), min(n, int(math.ceil(x1 * n)))
        i0, i1 = max(0, int(y0 * n)), min(n, int(math.ceil(y1 * n)))
        if j1 <= j0 or i1 <= i0:
            return None
        X, Y = np.meshgrid((np.arange(j0, j1) + 0.5) / n, (np.arange(i0, i1) + 0.5) / n)
        return (slice(i0, i1), slice(j0, j1)), X, Y

    def put(self, sl, m, col, h, r, cav=1.0, shadow=0.45, sigma=0.004):
        m = m.astype(np.float32)
        if shadow > 0:
            sh = ndimage.gaussian_filter(m, sigma * self.n)
            k = shadow * np.clip(sh * 1.6, 0, 1) * (1 - m)
            self.cav[sl] *= 1 - k
            self.col[sl] *= (1 - 0.45 * k)[..., None]
        mm = m[..., None]
        self.col[sl] = self.col[sl] * (1 - mm) + col * mm
        self.a[sl] = np.maximum(self.a[sl], m)
        self.h[sl] = self.h[sl] * (1 - m) + h * m
        self.r[sl] = self.r[sl] * (1 - m) + r * m
        self.cav[sl] = self.cav[sl] * (1 - m) + cav * m

    def blade(self, o, A, B, hw_fn, shade, shadow=0.45, vmax=0.6):
        """o + u*A + v*B (u:0..1 付け根→先端, v: 横) の葉形。hw_fn(u) = 半幅（v 単位）"""
        o, A, B = (np.asarray(x, np.float64) for x in (o, A, B))
        corners = [o, o + A] + [o + s * B * vmax + t * A for s in (-1, 1) for t in (0, 1)]
        reg = self.region(corners, 0.02)
        if reg is None:
            return
        sl, X, Y = reg
        Mi = np.linalg.inv(np.array([[A[0], B[0]], [A[1], B[1]]]))
        dx, dy = X - o[0], Y - o[1]
        u = Mi[0, 0] * dx + Mi[0, 1] * dy
        v = Mi[1, 0] * dx + Mi[1, 1] * dy
        uc = np.clip(u, 0.0, 1.0)
        hw = hw_fn(uc)
        bl = np.linalg.norm(B) * self.n
        al = np.linalg.norm(A) * self.n
        m = np.clip((hw - np.abs(v)) * bl + 0.5, 0, 1)
        m *= np.clip(u * al + 0.5, 0, 1) * np.clip((1 - u) * al + 0.5, 0, 1)
        if not m.any():
            return
        col, h, r, cav = shade(uc, v, np.maximum(hw, 1e-4))
        self.put(sl, m, col, h, r, cav, shadow)

    def stroke(self, pts, r0, r1, c0, c1, rough=0.6, shadow=0.35):
        """太さが r0→r1 に変わる折れ線（小枝・葉柄）"""
        pts = np.asarray(pts, np.float64)
        reg = self.region(pts, max(r0, r1) + 0.02)
        if reg is None:
            return
        sl, X, Y = reg
        best = np.full(X.shape, 1e9)
        tpar = np.zeros(X.shape)
        seg_len = np.linalg.norm(np.diff(pts, axis=0), axis=1)
        acc = np.concatenate([[0], np.cumsum(seg_len)])
        total = max(acc[-1], 1e-6)
        for k in range(len(pts) - 1):
            p, q = pts[k], pts[k + 1]
            d = q - p
            ll = max(d @ d, 1e-12)
            t = np.clip(((X - p[0]) * d[0] + (Y - p[1]) * d[1]) / ll, 0, 1)
            dist = np.hypot(X - (p[0] + t * d[0]), Y - (p[1] + t * d[1]))
            better = dist < best
            best = np.where(better, dist, best)
            tpar = np.where(better, (acc[k] + t * seg_len[k]) / total, tpar)
        rad = r0 + (r1 - r0) * tpar
        m = np.clip((rad - best) * self.n + 0.5, 0, 1)
        if not m.any():
            return
        q = np.clip(best / np.maximum(rad, 1e-6), 0, 1)
        shade = (1.05 - 0.35 * q ** 2)[..., None]
        col = (np.asarray(c0) * (1 - tpar[..., None]) + np.asarray(c1) * tpar[..., None]) * shade
        h = np.sqrt(np.clip(1 - q ** 2, 0, 1)) * 0.8
        self.put(sl, m, col, h, rough, 1.0 - 0.2 * q, shadow)

    def disc(self, c, rad, col, h=0.5, rough=0.5, lobes=0, lobe_amt=0.0, shadow=0.25, rot=0.0):
        c = np.asarray(c, np.float64)
        reg = self.region([c], rad * (1 + lobe_amt) + 0.01)
        if reg is None:
            return
        sl, X, Y = reg
        dx, dy = X - c[0], Y - c[1]
        d = np.hypot(dx, dy)
        rr = rad * (1 + lobe_amt * np.cos(lobes * np.arctan2(dy, dx) + rot)) if lobes else rad
        m = np.clip((rr - d) * self.n + 0.5, 0, 1)
        q = np.clip(d / np.maximum(rr, 1e-6), 0, 1)
        self.put(sl, m, np.asarray(col) * (1.05 - 0.3 * q)[..., None], h * (1 - q), rough, 1.0, shadow)

    def finish(self, cells=2, border_px=3):
        """セル境界のアルファを落とし、透明部の色を近くの不透明色で埋める（ミップのにじみ対策）"""
        n = self.n
        step = n // cells
        for k in range(cells + 1):
            a0, a1 = max(0, k * step - border_px), min(n, k * step + border_px)
            self.a[a0:a1, :] = 0
            self.a[:, a0:a1] = 0
        mask = self.a > 0.5
        if mask.any():
            _, (iy, ix) = ndimage.distance_transform_edt(~mask, return_indices=True)
            self.col = self.col[iy, ix]
            self.h = np.where(mask, self.h, 0.0)
            self.r = self.r[iy, ix]
            self.cav = self.cav[iy, ix]

    def images(self, name):
        """3 枚の Non-Color float 画像にする: 色 / (alpha, height, cavity) / roughness"""
        def mk(tag, rgb):
            n = self.n
            img = bpy.data.images.new(f"{name}_{tag}", n, n, alpha=True, float_buffer=True)
            img.colorspace_settings.name = "Non-Color"
            arr = np.ones((n, n, 4), np.float32)
            arr[..., :3] = rgb
            img.pixels.foreach_set(arr.ravel())
            img.update()
            return img
        one = np.ones_like(self.a)
        return (mk("col", self.col), mk("ahc", np.stack([self.a, self.h, self.cav], -1)),
                mk("rough", np.stack([self.r, one, one], -1)))


def image_material(name, canvas, res, atlas_size, height_scale=0.003):
    """Canvas の画像を Proc UV で読んでベイクする atlas マテリアル"""
    ic, iahc, ir = canvas.images(name)

    def fn(nb):
        uv = nb.uv("Proc")
        col = nb.image(ic, uv, interp="Closest", ext="CLIP")["Color"]
        # 高さは Linear でないとバンプの微分が 0 になる
        ahc = nb.sep(nb.image(iahc, uv, interp="Linear", ext="CLIP")["Color"])
        rough = nb.sep(nb.image(ir, uv, interp="Closest", ext="CLIP")["Color"])[0]
        return dict(color=col, alpha=ahc[0], height=ahc[1], height_scale=height_scale, rough=rough, cavity=ahc[2])

    return pbr_material(name, fn, res=res, uv="atlas", double_sided=True, atlas_size=atlas_size)


# --- 葉の形 ---------------------------------------------------------------
def hib_hw(W=0.33, teeth=12, amt=0.09):
    """ハイビスカス: 卵形で先が尖り、上半分に粗い鋸歯"""
    def f(u):
        base = np.sin(np.pi * np.clip(u, 0, 1) ** 0.72) ** 0.9
        saw = 1 - amt * (1 - np.mod(u * teeth, 1.0)) * _sstep(0.22, 0.4, u)
        return W * base * saw
    return f


def boug_hw(W=0.3):
    """ブーゲンビリア: 全縁の卵形、先は細く伸びる"""
    def f(u):
        return W * np.sin(np.pi * np.clip(u, 0, 1) ** 0.62) ** 1.25
    return f


def bract_hw(W=0.4):
    """苞: 幅広い卵形〜心形"""
    def f(u):
        return W * np.sin(np.pi * np.clip(u, 0, 1) ** 0.55) ** 0.75 * (1 - 0.15 * _sstep(0.0, 0.12, 0.12 - u))
    return f


def leaf_shade(c_dark, c_light, vein_col, W, rough=0.35, vein_amt=0.4, rnd=None):
    rnd = rnd or random.Random(0)
    lit = rnd.uniform(0.05, 0.1) * rnd.choice([-1, 1])
    k = rnd.uniform(0, 1)
    base = np.asarray(c_dark) * (1 - k) + np.asarray(c_light) * k
    ph0 = rnd.random()

    def f(u, v, hw):
        q = np.clip(np.abs(v) / hw, 0, 1)
        # 側脈（先端側へ傾いた羽状脈）と中肋
        phase = u * 8.0 - np.abs(v) / W * 1.4 + ph0
        vein = np.exp(-((np.mod(phase, 1.0) - 0.5) / 0.045) ** 2) * (1 - q) ** 0.6 * _sstep(0.05, 0.15, u)
        mid = np.exp(-(np.abs(v) / (0.012 * (1 - u) + 0.004)) ** 2)
        shade = 1 + lit * np.sign(v) - 0.12 * q ** 3 + 0.06 * (1 - u)
        col = base * shade[..., None]
        col = col * (1 - (vein * vein_amt)[..., None]) + np.asarray(vein_col) * (vein * vein_amt)[..., None]
        col = col * (1 - (mid * 0.6)[..., None]) + np.asarray(vein_col) * (mid * 0.6)[..., None]
        h = np.sqrt(np.clip(1 - q ** 2, 0, 1)) * 0.7 - vein * 0.35 - mid * 0.3
        r = rough + vein * 0.12
        cav = 1 - 0.12 * vein - 0.15 * mid
        return col, h, r, cav
    return f


def bract_shade(rnd):
    c_body = _lin(rnd.choice(["#c8287e", "#d0348a", "#bd2277"]))
    c_edge = _lin("#d8479a")
    c_vein = _lin("#e27ab4")
    c_base = _lin("#9a3a6a")

    def f(u, v, hw):
        q = np.clip(np.abs(v) / hw, 0, 1)
        phase = u * 5.0 - np.abs(v) * 6.0
        vein = np.exp(-((np.mod(phase, 1.0) - 0.5) / 0.06) ** 2) * (1 - q) * _sstep(0.05, 0.2, u)
        mid = np.exp(-(np.abs(v) / 0.008) ** 2) * (1 - u)
        col = c_body * (1 - q[..., None] * 0.3) + c_edge * (q[..., None] * 0.3)
        col = col * (1 - _sstep(0.25, 0.0, u)[..., None] * 0.5) + c_base * (_sstep(0.25, 0.0, u)[..., None] * 0.5)
        col = col * (1 - (vein * 0.35)[..., None]) + c_vein * (vein * 0.35)[..., None]
        col = col * (1 - (mid * 0.5)[..., None]) + _lin("#9aa060") * (mid * 0.5)[..., None]
        col = col * (1 + 0.08 * np.sign(v))[..., None]
        h = np.sqrt(np.clip(1 - q ** 2, 0, 1)) * 0.5 + vein * 0.3
        return col, h, 0.55, 1 - 0.1 * vein
    return f


def _fit(o, A, B, W, cell, margin=0.03):
    """葉がセルからはみ出すなら縮める"""
    o = np.asarray(o)
    for _ in range(20):
        pts = [o + A * t + B * W * s for t in (0.35, 0.6, 1.0) for s in (-1, 0, 1)]
        pts = np.array(pts)
        lo = np.array(cell[:2]) + margin
        hi = np.array(cell[:2]) + cell[2] - margin
        if (pts >= lo).all() and (pts <= hi).all():
            return A, B
        A, B = A * 0.9, B * 0.9
    return A, B


def _bez2(p0, p1, p2, n):
    t = np.linspace(0, 1, n)[:, None]
    return (1 - t) ** 2 * np.asarray(p0) + 2 * (1 - t) * t * np.asarray(p1) + t ** 2 * np.asarray(p2)


def draw_spray(cv, rnd, cell, kind, n_leaves=7, bud=False, bracts=0):
    """セル (x0, y0, size) に小枝の房を描く。付け根はセル下端中央"""
    x0, y0, cs = cell

    def P(x, y):
        return np.array([x0 + x * cs, y0 + y * cs])

    bend = rnd.uniform(-0.12, 0.12)
    top_y = 0.78 if not bracts else 0.7
    twig = _bez2(P(0.5, 0.0), P(0.5 + bend, top_y * 0.5), P(0.5 - bend * 0.3, top_y), 24)
    if kind == "hib":
        tc0, tc1 = _lin("#5a4a36"), _lin("#5f7a36")
        dark, light, vein = _lin("#2d6024"), _lin("#4c8430"), _lin("#86ad58")
        hw, W, rough = hib_hw(W=0.36), 0.36, 0.3
    else:
        tc0, tc1 = _lin("#6a5540"), _lin("#6d8040")
        dark, light, vein = _lin("#35652a"), _lin("#5a8c38"), _lin("#86a85a")
        hw, W, rough = boug_hw(), 0.3, 0.5
    cv.stroke(twig, 0.012 * cs, 0.005 * cs, tc0, tc1)

    def at(t):
        i = min(len(twig) - 2, int(t * (len(twig) - 1)))
        d = twig[i + 1] - twig[i]
        return twig[i], d / np.linalg.norm(d)

    side = rnd.choice([-1, 1])
    leaves = []
    for k in range(n_leaves):
        t = 0.1 + 0.85 * k / max(1, n_leaves - 1) + rnd.uniform(-0.03, 0.03)
        t = min(t, 0.97)
        p, d = at(t)
        nrm = np.array([-d[1], d[0]])
        side = -side
        ang = math.radians(rnd.uniform(35, 65) * (1 - 0.4 * t))
        ld = d * math.cos(ang) + nrm * side * math.sin(ang)
        pet = (0.07 if kind == "hib" else 0.03) * (1 - 0.5 * t) * cs
        base = p + ld * pet
        young = t > 0.8
        L = cs * rnd.uniform(0.4, 0.46) * (1 - 0.5 * t ** 1.8)
        sq = rnd.uniform(0.65, 1.0)
        A = ld * L
        B = np.array([-ld[1], ld[0]]) * L * sq
        A, B = _fit(base, A, B, W, cell)
        leaves.append((p, base, A, B, young, t))
        if kind == "boug" and not bracts:
            # 葉腋の棘
            th = p + (-nrm * side) * 0.035 * cs + d * 0.02 * cs
            cv.stroke([p, th], 0.004 * cs, 0.0008 * cs, _lin("#6d6a40"), _lin("#4a3a28"), shadow=0.2)
    for (p, base, A, B, young, t) in leaves:
        cv.stroke([p, base], 0.006 * cs, 0.004 * cs, tc1, tc1 * 1.1)
        lc = light * 1.25 if young else light
        dc = light if young else dark
        cv.blade(base, A, B, hw, leaf_shade(dc, lc, vein, W, rough, rnd=rnd))
    if bud:
        p, d = at(0.98)
        bl = 0.1 * cs
        A = d * bl
        B = np.array([-d[1], d[0]]) * bl
        cv.blade(p, A, B, lambda u: 0.28 * np.sin(np.pi * np.clip(u, 0, 1) ** 0.8),
                 lambda u, v, h: (np.where((u > 0.55)[..., None], _lin("#b8182a"), _lin("#4d7a2a"))
                                  * (1 + 0.1 * np.sign(v))[..., None], 0.6, 0.45, 1.0))
    for b in range(bracts):
        # 苞 3 枚の房（先端側ほど密）
        t = 0.3 + 0.7 * (b / max(1, bracts - 1)) ** 0.8
        p, d = at(min(t, 0.99))
        nrm = np.array([-d[1], d[0]])
        c = p + nrm * rnd.uniform(-0.2, 0.2) * cs + d * rnd.uniform(0.0, 0.1) * cs
        c = np.clip(c, P(0.18, 0.18), P(0.82, 0.82))
        cv.stroke([p, c], 0.004 * cs, 0.003 * cs, tc1, _lin("#8a5a60"), shadow=0.2)
        a0 = rnd.uniform(0, math.tau)
        tilt = rnd.uniform(0.45, 1.0)
        tilt_dir = rnd.uniform(0, math.tau)
        Lb = cs * rnd.uniform(0.13, 0.16)
        sh = bract_shade(rnd)
        for k in range(3):
            a = a0 + k * math.tau / 3 + rnd.uniform(-0.2, 0.2)
            dd = np.array([math.cos(a), math.sin(a)])
            # 傾き（遠近の縮み）を 1 方向にかける
            td = np.array([math.cos(tilt_dir), math.sin(tilt_dir)])
            sq = lambda v: v - td * (v @ td) * (1 - tilt)  # noqa: E731
            A = sq(dd * Lb)
            B = sq(np.array([-dd[1], dd[0]]) * Lb)
            cv.blade(c, A, B, bract_hw(), sh, shadow=0.35)
        for k in range(rnd.choice([1, 1, 2, 3])):
            a = a0 + k * math.tau / 3 + 0.3
            dd = np.array([math.cos(a), math.sin(a)])
            fc = c + dd * Lb * 0.35
            cv.stroke([c, fc], 0.003 * cs, 0.004 * cs, _lin("#c07a98"), _lin("#e0c8c0"), shadow=0.15)
            cv.disc(fc, 0.011 * cs, _lin("#f3efdc"), lobes=5, lobe_amt=0.3, rot=a, shadow=0.2)
            cv.disc(fc, 0.004 * cs, _lin("#e6c84a"), shadow=0.0)


def hibiscus_leaf_material():
    rnd = random.Random(21)
    cv = Canvas(max(64, int(2048 * RES_SCALE)))
    specs = [(0, 0, 7, False), (0.5, 0, 8, False), (0, 0.5, 6, True), (0.5, 0.5, 7, False)]
    for x, y, n, bud in specs:
        draw_spray(cv, rnd, (x, y, 0.5), "hib", n_leaves=n, bud=bud)
    cv.finish()
    return image_material("HibiscusLeaf", cv, 2048, (CARD * 2, CARD * 2))


def boug_leaf_material():
    rnd = random.Random(33)
    cv = Canvas(max(64, int(2048 * RES_SCALE)))
    draw_spray(cv, rnd, (0, 0, 0.5), "boug", n_leaves=8)
    draw_spray(cv, rnd, (0.5, 0, 0.5), "boug", n_leaves=9)
    draw_spray(cv, rnd, (0, 0.5, 0.5), "boug", n_leaves=3, bracts=10)
    draw_spray(cv, rnd, (0.5, 0.5, 0.5), "boug", n_leaves=2, bracts=12)
    cv.finish()
    return image_material("BougainLeaf", cv, 2048, (CARD * 2, CARD * 2))


# --- 花のアトラス ------------------------------------------------------------
PETAL_ROWS = [(0.0, 0.06), (0.33, 0.38), (0.66, 0.5), (1.0, 0.5)]  # (u, 半幅) セル単位


def petal_hw(u):
    """倒卵形: 付け根から緩やかに広がり、先は丸い"""
    low = 0.49 * np.sin(np.pi / 2 * np.clip(u / 0.62, 0, 1)) ** 1.1
    top = 0.49 * np.sqrt(np.clip(1 - ((u - 0.62) / 0.38) ** 2, 0, 1))
    w = np.where(u < 0.62, low, top)
    return np.maximum(w, np.where(u < 0.3, 0.045, 0)) * (1 + 0.025 * np.sin(u * 50.0) * _sstep(0.5, 0.8, u))


def petal_shade(c_main, c_edge, c_eye, c_vein):
    def f(u, v, hw):
        q = np.clip(np.abs(v) / hw, 0, 1)
        ang = np.arctan2(v, u + 0.05)
        vein = (0.5 + 0.5 * np.cos(ang * 46.0)) ** 6 * _sstep(0.05, 0.3, u)
        eye = _sstep(0.32, 0.1, u)
        col = c_main * (1 - _sstep(0.5, 1.0, u)[..., None] * 0.5) + c_edge * (_sstep(0.5, 1.0, u)[..., None] * 0.5)
        col = col * (1 - (vein * 0.25)[..., None]) + c_vein * (vein * 0.25)[..., None]
        col = col * (1 - eye[..., None]) + c_eye * eye[..., None]
        col = col * (1 - 0.1 * q ** 2)[..., None]
        h = vein * 0.4 + 0.3 * np.sin(u * 14 + v * 6) * _sstep(0.5, 1.0, u)
        return col, h, 0.42, 1 - 0.1 * eye
    return f


def flower_material():
    """セル: (0,0)=赤い花弁 (1,0)=橙黄の花弁 (0,1)=左:雄しべ筒 右:赤いつぼみ (1,1)=左:萼 右:黄のつぼみ"""
    n = max(64, int(1024 * RES_SCALE))
    cv = Canvas(n)
    cs = 0.5
    specs = [(0.0, 0.0, "#d0142a", "#e0303e", "#5a0714", "#a00c20"),
             (0.5, 0.0, "#f2a51e", "#f7c640", "#b31c2a", "#e08a20")]
    for x0, y0, cm, ce, eye, vein in specs:
        cv.blade((x0 + 0.5 * cs, y0 + 0.01), (0, 0.97 * cs), (cs, 0), petal_hw,
                 petal_shade(_lin(cm), _lin(ce), _lin(eye), _lin(vein)), shadow=0.0)
    # 雄しべ筒: 下=赤い筒、中=黄色い葯、先=赤い柱頭
    sl, X, Y = cv.region([(0.02, 0.51), (0.23, 0.99)], 0.0)
    v = (Y - 0.5) / 0.5
    rnd = np.random.RandomState(4)
    col = np.where((v < 0.55)[..., None], _lin("#c8182c"), _lin("#b0202a"))
    dots = np.zeros_like(v)
    for _ in range(90):
        cx, cy = rnd.uniform(0.02, 0.23), rnd.uniform(0.5 + 0.5 * 0.56, 0.5 + 0.5 * 0.9)
        dots = np.maximum(dots, np.clip(1 - np.hypot(X - cx, Y - cy) / 0.012, 0, 1))
    dots = _sstep(0.1, 0.4, dots)
    col = col * (1 - dots[..., None]) + _lin("#f3c534") * dots[..., None]
    col = np.where((v > 0.92)[..., None], _lin("#7a0a18"), col)
    cv.put(sl, np.ones_like(v), col, dots * 0.8, 0.5, 1.0, shadow=0)
    # つぼみ（赤 / 黄）と萼
    for (x0, x1, top) in [(0.27, 0.48, "#c8182a"), (0.77, 0.98, "#f0a830")]:
        sl, X, Y = cv.region([(x0, 0.51), (x1, 0.99)], 0.0)
        v = (Y - 0.5) / 0.5
        k = _sstep(0.45, 0.6, v)[..., None]
        stripes = (0.5 + 0.5 * np.cos((X - x0) / (x1 - x0) * math.tau * 5))[..., None]
        col = _lin("#4a7a2a") * (0.9 + 0.15 * stripes) * (1 - k) + _lin(top) * (0.85 + 0.2 * stripes) * k
        cv.put(sl, np.ones_like(v), col, stripes[..., 0] * 0.3, 0.5, 1.0, shadow=0)
    sl, X, Y = cv.region([(0.52, 0.51), (0.73, 0.99)], 0.0)
    stripes = 0.5 + 0.5 * np.cos((X - 0.52) / 0.21 * math.tau * 5)
    col = _lin("#3f6e26")[None, None] * (0.85 + 0.2 * stripes)[..., None]
    cv.put(sl, np.ones_like(X), col, stripes * 0.4, 0.55, 1.0, shadow=0)
    # 小さい隙間を残してセル境界を落とす（筒の短冊は横に接するので finish は使わない）
    mask = cv.a > 0.5
    _, (iy, ix) = ndimage.distance_transform_edt(~mask, return_indices=True)
    cv.col = cv.col[iy, ix]
    return image_material("HibiscusFlower", cv, 1024, (0.14, 0.14), height_scale=0.0015)


# 花アトラスの UV 範囲 (u0, v0, u1, v1)
UV_PETAL = {"red": (0.0, 0.0, 0.5, 0.5), "yellow": (0.5, 0.0, 1.0, 0.5)}
UV_COLUMN = (0.03, 0.51, 0.22, 0.99)
UV_BUD = {"red": (0.28, 0.51, 0.47, 0.99), "yellow": (0.78, 0.51, 0.97, 0.99)}
UV_CALYX = (0.53, 0.51, 0.72, 0.99)


def bark_material():
    """細い幹。Proc U の整数部 0=ハイビスカス（灰褐色で皮目）, 2=ブーゲンビリア（褐色で縦筋）"""

    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        boug = nb.math("GREATER_THAN", U, 1.5)
        a = nb.mul(nb.math("FRACT", U), math.tau)
        R = 0.02
        cyl = nb.comb(nb.mul(nb.math("COSINE", a), R), nb.mul(nb.math("SINE", a), R), V)
        big = nb.noise(cyl, 6.0, 4, 0.6)
        fine = nb.noise(nb.mapping(cyl, scale=(6, 6, 0.6)), 20.0, 4, 0.6)
        lent = nb.smooth(nb.voronoi(nb.mapping(cyl, scale=(1, 1, 0.5)), 90.0), 0.12, 0.0)
        hib = nb.ramp(big, [(0.3, srgb("#5e5446")), (0.6, srgb("#7a6d5a")), (0.8, srgb("#8d826c"))])
        hib = nb.mix(hib, srgb("#b3a88e"), nb.mul(lent, 0.7))
        bg = nb.ramp(big, [(0.3, srgb("#5a4634")), (0.6, srgb("#735b42")), (0.8, srgb("#86704f"))])
        bg = nb.mix(bg, srgb("#3e3024"), nb.mul(nb.smooth(fine, 0.55, 0.7), 0.6))
        col = nb.mix(hib, bg, boug)
        z = nb.sep(nb.coord("Object"))[2]
        col = nb.mix(col, srgb("#bcae90"), nb.mul(nb.smooth(z, 0.12, -0.02), 0.6))
        height = nb.add(nb.mul(lent, nb.sub(1.0, boug)), nb.mul(fine, 0.6))
        return dict(color=col, rough=nb.maprange(big, 0.3, 0.7, 0.7, 0.9), height=height, height_scale=0.002)

    return pbr_material("ShrubBark", fn, res=512, ao_distance=0.3, uv="keep")


# ---------------------------------------------------------------------------
# 形状
# ---------------------------------------------------------------------------
def _uvmap(rect, x, y):
    u0, v0, u1, v1 = rect
    return (u0 + (u1 - u0) * x, v0 + (v1 - v0) * y)


def _card(acc, base, d, normal_hint, cell, rnd, size=CARD, fold=18.0, droop=0.15, rows=4, wind=None):
    """葉の房カード。セル下端中央 = base、d 方向へ伸びる。V 字に少し折り、先を垂らす。
    wind = dict(G, A, b, r)"""
    d = d.normalized()
    side = normal_hint.cross(d)
    if side.length < 1e-3:
        side = perp(d)
    side.normalize()
    nrm = d.cross(side).normalized()
    if nrm.dot(normal_hint) < 0:
        side, nrm = -side, -nrm
    fa = math.radians(fold)
    x0, y0 = cell
    rows_v, rows_uv = [], []
    for i in range(rows):
        y = i / (rows - 1)
        c = base + d * size * y - UP * droop * size * y * y
        row, ruv = [], []
        for x in (0.0, 0.5, 1.0):
            o = (x - 0.5) * size
            q = c + side * o * math.cos(fa) + nrm * abs(o) * math.sin(fa)
            q.z = max(q.z, 0.02)  # 砂に潜らせない
            row.append(q)
            ruv.append((x0 + (0.004 + x * 0.492), y0 + (0.004 + y * 0.492)))
        rows_v.append(row)
        rows_uv.append(ruv)
    if wind:
        acc.set_anim(len(acc.verts), [(wind["r"] * (i / (rows - 1)) * (0.5 + 0.5 * abs(x - 0.5) * 2), wind["G"],
                                       wind["b"] * (i / (rows - 1)) ** 1.5, wind["A"])
                                      for i in range(rows) for x in (0.0, 0.5, 1.0)])
    acc.grid(rows_v, rows_uv)


def _tube_acc(acc, pts, radii, sides, rect, cap=True, v_range=(0.0, 1.0)):
    """atlas UV 付きの細いチューブ（花の雄しべ筒・萼・つぼみ）"""
    pts = [Vector(p) for p in pts]
    n = len(pts)
    t0 = (pts[1] - pts[0]).normalized()
    nrm = perp(t0)
    rows, uvs = [], []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
        nrm = (nrm - t * nrm.dot(t)).normalized()
        b = t.cross(nrm)
        row, ruv = [], []
        for j in range(sides + 1):
            a = j / sides * math.tau
            row.append(p + (nrm * math.cos(a) + b * math.sin(a)) * radii[i])
            ruv.append(_uvmap(rect, j / sides, v_range[0] + (v_range[1] - v_range[0]) * i / (n - 1)))
        rows.append(row)
        uvs.append(ruv)
    acc.grid(rows, uvs)
    if cap:
        o = len(acc.verts)
        acc.verts.append(tuple(pts[-1] + (pts[-1] - pts[-2]).normalized() * radii[-1] * 0.5))
        base = o - (sides + 1)
        tip_uv = _uvmap(rect, 0.5, v_range[1] - 0.01)
        for j in range(sides):
            acc.faces.append((base + j, base + j + 1, o))
            acc.uvs.append([_uvmap(rect, j / sides, v_range[1]), _uvmap(rect, (j + 1) / sides, v_range[1]), tip_uv])


def _flower(acc, c, n, rnd, color="red", size=0.065):
    """5 枚の花弁 + 雄しべ筒 + 萼。c = 花の中心, n = 花の向き"""
    n = n.normalized()
    t1 = perp(n)
    t2 = n.cross(t1)
    rect = UV_PETAL[color]
    a0 = rnd.uniform(0, math.tau)
    L = size * rnd.uniform(0.9, 1.1)
    for k in range(5):
        a = a0 + k * math.tau / 5 + rnd.uniform(-0.08, 0.08)
        r = t1 * math.cos(a) + t2 * math.sin(a)
        sd = n.cross(r)
        rows, uvs = [], []
        for (u, hw) in PETAL_ROWS:
            # 付け根は立ち上がり、外側は平らに開いて縁がやや反る
            lift = L * (0.55 * u - 0.45 * u * u + 0.12 * max(0.0, u - 0.7) ** 2 * 10)
            row, ruv = [], []
            for x in (-1.0, 0.0, 1.0):
                w = x * hw * L
                # 風車状の重なり: 片側の縁を上げる
                p = c + r * (u * L * 0.97 + 0.004) + sd * w + n * (lift + abs(x) * hw * L * 0.18
                                                                   + x * 0.12 * L * u)
                row.append(p)
                ruv.append(_uvmap(rect, 0.5 + x * hw, 0.02 + u * 0.97))
            rows.append(row)
            uvs.append(ruv)
        acc.grid(rows, uvs)
    # 雄しべ筒: 花の中心から前へ突き出し、先が少し上へ反る
    bend = t1 * rnd.uniform(-1, 1) * 0.2 + UP * 0.25
    L2 = size * rnd.uniform(1.1, 1.35)
    pts = [c + n * (L2 * s) + bend * (L2 * s * s * 0.5) for s in (0.0, 0.35, 0.6, 0.85, 1.0)]
    radii = [0.0025, 0.0025, 0.004, 0.0055, 0.0035]
    _tube_acc(acc, pts, radii, 4, UV_COLUMN)
    # 萼
    pts = [c - n * 0.028, c - n * 0.012, c + n * 0.004]
    _tube_acc(acc, pts, [0.004, 0.011, 0.013], 5, UV_CALYX, cap=False)


def _bud(acc, base, d, rnd, color="red", length=0.05):
    d = d.normalized()
    pts = [base + d * length * s for s in (0.0, 0.25, 0.55, 0.8, 1.0)]
    radii = [0.004, 0.009, 0.011, 0.008, 0.002]
    _tube_acc(acc, pts, radii, 5, UV_BUD[color], cap=False)


def _skeleton(bp, rnd, H, n_stems, depth, kind, lean=(10, 35), r0=0.02, fork=(20, 45), arch=0.0,
              children=(2, 3), ratio=(0.55, 0.75), len0=0.5, reach=None):
    """株立ちの枝。戻り値 segs=[(pts, depth)], tips=[(pos, dir)]"""
    segs, tips = [], []

    def grow(start, d, L, ra, dep, name):
        end_dir = (d + UP * 0.25 - UP * arch * (1.0 if dep < depth else 0.0)).normalized()
        wob = Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-0.3, 0.3))) * 0.08 * L
        p0 = Vector(start)
        p3 = p0 + (d * 0.5 + end_dir * 0.5).normalized() * L + wob
        if reach and Vector((p3.x, p3.y)).length > reach:
            # 株から遠くへ伸びすぎる枝は短くする
            k = max(0.3, (reach - Vector((p0.x, p0.y)).length) / max(1e-3, (p3 - p0).length))
            L *= min(1.0, k)
            p3 = p0 + (p3 - p0) * min(1.0, k)
        p1 = p0 + d * L * 0.4
        p2 = p3 - end_dir * L * 0.35
        m = max(3, int(L / 0.12) + 1)
        pts = geo.bezier_points(p0, p1, p2, p3, m)
        rb = ra * 0.65
        radii = [ra + (rb - ra) * i / (m - 1) for i in range(m)]
        sides = 6 if ra > 0.012 else (5 if ra > 0.007 else 4)
        bp.tube(pts, radii, sides, name, kind=kind, cap_end=True)
        segs.append((pts, dep))
        tdir = (pts[-1] - pts[-2]).normalized()
        if dep == 0:
            tips.append((pts[-1], tdir))
            return
        nc = rnd.randint(*children)
        az = rnd.uniform(0, math.tau)
        pa = perp(tdir)
        pb = tdir.cross(pa)
        for c in range(nc):
            a = az + c * math.tau / nc + rnd.uniform(-0.4, 0.4)
            cd = rotate_toward(tdir, pa * math.cos(a) + pb * math.sin(a), math.radians(rnd.uniform(*fork)))
            # 横枝は途中から出す
            k = len(pts) - 1 if c == 0 else rnd.randint(len(pts) // 2, len(pts) - 1)
            grow(pts[k] - tdir * 0.01, cd, L * rnd.uniform(*ratio), max(0.004, radii[k] * 0.75), dep - 1,
                 f"{name}_{c}")

    for s in range(n_stems):
        az = s * math.tau / n_stems + rnd.uniform(-0.3, 0.3)
        ln = math.radians(rnd.uniform(*lean))
        d = Vector((math.cos(az) * math.sin(ln), math.sin(az) * math.sin(ln), math.cos(ln)))
        start = Vector((math.cos(az) * 0.04, math.sin(az) * 0.04, -0.12))
        grow(start, d, H * len0 * rnd.uniform(0.85, 1.1), r0 * rnd.uniform(0.8, 1.1), depth, f"stem{s}")
    return segs, tips


def _anchors(rnd, segs, tips, n_extra, center, min_depth_frac=0.35, tip_cards=2):
    """葉カードを付ける位置と向き"""
    out = []
    for (p, d) in tips:
        for _ in range(tip_cards):
            out.append((p, d))
    pool = []
    for pts, dep in segs:
        for i in range(1, len(pts)):
            pool.append((pts[i - 1], pts[i], dep))
    w = np.array([(b - a).length * (1.0 if dep < 2 else 0.4) for a, b, dep in pool])
    w /= w.sum()
    idx = np.random.RandomState(rnd.randrange(1 << 30)).choice(len(pool), n_extra, p=w)
    for k in idx:
        a, b, dep = pool[k]
        t = rnd.random()
        p = a.lerp(b, t)
        if ((p - center).length < 0.2 and p.z < center.z) or p.z < 0.15:
            continue
        out.append((p, (b - a).normalized()))
    return out


def _dress(acc, rnd, anchors, center, cells, up_bias=0.35, out_bias=0.7, size=CARD, droop=0.15, wind=None):
    """アンカーごとに外向き・上向きの葉カードを付ける。wind = dict(key, bend_at, b, r)"""
    for k, (p, d) in enumerate(anchors):
        out = p - center
        out.z *= 0.5
        out = out.normalized() if out.length > 1e-3 else Vector((1, 0, 0))
        jit = Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1))) * 0.45
        dd = (d * 0.6 + out * out_bias + UP * up_bias + jit).normalized()
        hint = (UP * 0.8 + out * 0.6 + jit * 0.6).normalized()
        base = p - dd * 0.03
        s = size * rnd.uniform(0.8, 1.1)
        w = None
        if wind:
            w = dict(G=anim.hash01(wind["key"], k), A=wind["bend_at"](p.z), b=wind["b"] * s / size, r=wind["r"])
        _card(acc, base, dd, hint, rnd.choice(cells), rnd, size=s, droop=droop * rnd.uniform(0.5, 1.5), wind=w)


def _shell_anchors(rnd, n, center, radii, zmin, rmin=0.55, rmax=0.9):
    """楕円体の外殻付近に葉カードの付け根を散らす（外向き）"""
    out = []
    rx, ry, rz = radii
    while len(out) < n:
        v = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1)))
        if v.length < 1e-3:
            continue
        v.normalize()
        k = rnd.uniform(rmin, rmax)
        p = center + Vector((v.x * rx, v.y * ry, v.z * rz)) * k
        if p.z < zmin:
            continue
        nrm = Vector((v.x / rx, v.y / ry, v.z / rz)).normalized()
        out.append((p, nrm))
    return out


def _flowers_on_shell(fl, rnd, n, center, radii, color, size, zmin, wind=None):
    """wind = dict(key, bend_at, b, r) — 花は 1 輪（花弁・雄しべ筒・萼・花柄）ごとに一体で揺れる"""
    for k, (p, nrm) in enumerate(_shell_anchors(rnd, n, center, radii, zmin, 0.92, 1.0)):
        nrm = (nrm + UP * 0.35 + Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), 0)) * 0.25).normalized()
        c = p + nrm * 0.02
        start = len(fl.verts)
        _flower(fl, c, nrm, rnd, color=color, size=size)
        # 花柄（葉の中へ）
        q = c - nrm * 0.026
        _tube_acc(fl, [q - nrm * 0.12 - UP * 0.03, q - nrm * 0.05, q], [0.0035, 0.003, 0.0035], 4, UV_CALYX,
                  cap=False)
        if wind:
            G, A = anim.hash01(wind["key"], k), wind["bend_at"](c.z)
            # 先頭の 5 枚の花弁（各 PETAL_ROWS x 3 頂点）は先ほど震える。筒・萼・花柄は震えない
            vals = [(wind["r"] * u, G, wind["b"], A) for _ in range(5) for u, _hw in PETAL_ROWS for _x in range(3)]
            vals += [(0.0, G, wind["b"], A)] * (len(fl.verts) - start - len(vals))
            fl.set_anim(start, vals)


def _wood_wind(parts, H, main):
    """幹・枝（BarkPacker のチューブ）: 高さの 2 乗で曲がる"""
    for o in parts:
        anim.write(o, 0.0, anim.hash01(o.name), 0.0, anim.main_bend(anim.world_co(o)[:, 2], H, main))


def hibiscus(prefix, seed, H, n_stems, n_cards, n_flowers, color, depth=2):
    rnd = random.Random(seed)
    bp = BarkPacker(chunk_len=0.35)
    segs, tips = _skeleton(bp, rnd, H * 0.85, n_stems, depth, kind=0, lean=(8, 28), r0=0.02, len0=0.5)
    center = Vector((0, 0, H * 0.58))
    radii = (H * 0.44, H * 0.42, H * 0.4)
    leaves = MeshAcc()
    cells = [(0.0, 0.0), (0.5, 0.0), (0.0, 0.5), (0.5, 0.5)]
    anchors = [(p, d) for p, d in tips if p.z > 0.25]
    anchors += _anchors(rnd, segs, [], n_cards // 4, center)
    anchors += _shell_anchors(rnd, n_cards - len(anchors), center, radii, H * 0.2)
    # 揺れ: 株の高さ H で main（小さな低木なので数 cm）
    main = 0.03 * H

    def bend_at(z):
        return float(anim.main_bend(z, H, main))

    _dress(leaves, rnd, anchors, center, cells, size=CARD * (1.0 if H > 1.2 else 0.85),
           wind=dict(key=(prefix, "leaf"), bend_at=bend_at, b=0.035, r=0.012))
    # 花: 外殻に外向きに咲く。つぼみは枝先に
    fl = MeshAcc()
    _flowers_on_shell(fl, rnd, n_flowers, center, radii, color, 0.068 if H > 1.2 else 0.06, H * 0.4,
                      wind=dict(key=(prefix, "flower"), bend_at=bend_at, b=0.02, r=0.006))
    for k, (p, d) in enumerate(rnd.sample(tips, min(len(tips), max(3, n_flowers // 3)))):
        start = len(fl.verts)
        _bud(fl, p + d * 0.04, (d + UP * 0.5).normalized(), rnd, color=color)
        # つぼみは枝先に付いたまま（しならない）
        fl.set_anim(start, [(0.0, anim.hash01(prefix, "bud", k), 0.0, bend_at(p.z))] * (len(fl.verts) - start))
    parts = []
    bmat = _mat("bark", bark_material)
    for o in bp.pack(margin=0.008):
        C.set_smooth(o, True)
        C.assign(o, bmat)
        parts.append(o)
    _wood_wind(parts, H, main)
    lo = leaves.build(f"{prefix}_leaves")
    C.set_smooth(lo, True)
    C.assign(lo, _mat("hleaf", hibiscus_leaf_material))
    fo = fl.build(f"{prefix}_flowers")
    C.set_smooth(fo, True)
    C.assign(fo, _mat("flower", flower_material))
    parts += [lo, fo]
    print(f"  {prefix}: {len(tips)} tips, {len(anchors)} cards")
    return parts


def bougainvillea(prefix, seed, H, n_leaf, n_bract):
    rnd = random.Random(seed)
    bp = BarkPacker(chunk_len=0.35)
    # 弓なりに伸びて先が垂れる枝
    segs, tips = _skeleton(bp, rnd, H * 0.85, 7, 2, kind=1, lean=(12, 38), r0=0.018, fork=(25, 45), arch=0.3,
                           children=(2, 3), ratio=(0.55, 0.7), len0=0.6, reach=H * 0.5)
    # 太い枝の棘
    for i, (pts, dep) in enumerate(segs):
        if dep < 1:
            continue
        for k in range(1, len(pts) - 1, 2):
            t = (pts[k + 1] - pts[k - 1]).normalized()
            o = perp(t)
            a = rnd.uniform(0, math.tau)
            o = rotate_toward(o, t.cross(o), a)
            p = pts[k] + o * 0.01
            bp.tube([p, p + o * 0.012 + t * 0.006], [0.0028, 0.0004], 3, f"thorn{i}_{k}", kind=1,
                    cap_end=False)
    center = Vector((0, 0, H * 0.55))
    radii = (H * 0.48, H * 0.46, H * 0.42)
    leaves = MeshAcc()
    anchors = [(p, d) for p, d in tips if p.z > 0.25]
    anchors += _anchors(rnd, segs, [], n_leaf // 3, center)
    anchors += _shell_anchors(rnd, n_leaf - len(anchors), center, radii, H * 0.15)
    # 揺れ: 弓なりの枝なので低木よりやや大きく
    main = 0.035 * H

    def bend_at(z):
        return float(anim.main_bend(z, H, main))

    _dress(leaves, rnd, anchors, center, [(0.0, 0.0), (0.5, 0.0)], droop=0.2, size=CARD * 1.1,
           wind=dict(key=(prefix, "leaf"), bend_at=bend_at, b=0.045, r=0.012))
    # 苞の房は上側・外側に多く（外殻の外寄り）
    bracts = [(p, d) for p, d in _shell_anchors(rnd, n_bract * 2, center, radii, H * 0.25, 0.75, 1.0)]
    bracts.sort(key=lambda a: -(a[0].z - center.z + rnd.uniform(-0.5, 0.5)))
    bracts = bracts[:n_bract]
    _dress(leaves, rnd, bracts, center, [(0.0, 0.5), (0.5, 0.5)], up_bias=0.5, droop=0.25, size=CARD * 1.1,
           wind=dict(key=(prefix, "bract"), bend_at=bend_at, b=0.045, r=0.015))
    parts = []
    bmat = _mat("bark", bark_material)
    for o in bp.pack(margin=0.008):
        C.set_smooth(o, True)
        C.assign(o, bmat)
        parts.append(o)
    _wood_wind(parts, H, main)
    lo = leaves.build(f"{prefix}_leaves")
    C.set_smooth(lo, True)
    C.assign(lo, _mat("bleaf", boug_leaf_material))
    parts.append(lo)
    print(f"  {prefix}: {len(tips)} tips, {len(anchors)} leaf cards, {len(bracts)} bract cards")
    return parts


def build():
    _MATS.clear()
    return register_cutout_aliases({
        "Hibiscus_A": hibiscus("HibA", 3, 1.6, 6, 340, 22, "red"),
        "Hibiscus_B": hibiscus("HibB", 8, 1.0, 5, 170, 10, "yellow"),
        "Bougainvillea_A": bougainvillea("BougA", 5, 1.8, 240, 240),
    })
