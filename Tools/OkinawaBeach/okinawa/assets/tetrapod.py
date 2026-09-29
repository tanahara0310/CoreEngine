"""テトラポッド（消波ブロック）

中心から正四面体の頂点方向へ 4 本の脚（角を丸めた円錐台）。
形状は SDF（丸め円錐台の滑らかな和）で定義し、
ボクセルリメッシュ → SDF 面へ投影 → デシメートでローポリ化する。
3 本脚で接地し、z=0 = 接地面。

  Tetrapod_A: 比較的新しい灰色のコンクリート（型枠の継ぎ目・気泡穴・欠け）
  Tetrapod_B: 風化したもの（下半分の汚れ、藻・フジツボ、錆の垂れ）
"""
import math
import random

import bmesh
import bpy
import numpy as np
from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb

PREVIEW = dict(cam_dir=(0.25, -1.0, 0.35), lens=45)

LEG_L = 1.15    # 中心 → 脚の端面(m)
R_HUB = 0.40    # 中心での脚の半径
R_TIP = 0.23    # 端面の半径


def leg_dirs():
    s = math.sqrt(8.0 / 9.0)
    dirs = [np.array([0.0, 0.0, 1.0])]
    for k in range(3):
        a = math.radians(-90 + 120 * k)
        dirs.append(np.array([s * math.cos(a), s * math.sin(a), -1.0 / 3.0]))
    return dirs


# ---------------------------------------------------------------------------
# SDF（numpy, p: (N,3)）
# ---------------------------------------------------------------------------
def sd_capped_cone(p, a, b, ra, rb):
    """IQ の任意向き円錐台"""
    ba = b - a
    pa = p - a
    rba = rb - ra
    baba = ba @ ba
    papa = (pa * pa).sum(-1)
    paba = (pa @ ba) / baba
    x = np.sqrt(np.maximum(papa - paba * paba * baba, 0.0))
    cax = np.maximum(0.0, x - np.where(paba < 0.5, ra, rb))
    cay = np.abs(paba - 0.5) - 0.5
    k = rba * rba + baba
    f = np.clip((rba * (x - ra) + paba * baba) / k, 0.0, 1.0)
    cbx = x - ra - f * rba
    cby = paba - f
    s = np.where((cbx < 0.0) & (cay < 0.0), -1.0, 1.0)
    return s * np.sqrt(np.minimum(cax * cax + cay * cay * baba, cbx * cbx + cby * cby * baba))


def smin(a, b, k):
    h = np.maximum(k - np.abs(a - b), 0.0) / k
    return np.minimum(a, b) - h * h * k * 0.25


class TetrapodSDF:
    def __init__(self, round_r=0.045, blend=0.12, chips=(), wobble=0.004, seed=0):
        self.dirs = leg_dirs()
        self.rr = round_r
        self.k = blend
        self.chips = chips   # [(中心, 半径)]
        self.wobble = wobble
        self.off = Vector((seed * 7.3, seed * 1.9, seed * 4.1))

    def __call__(self, p):
        rr = self.rr
        d = None
        for di in self.dirs:
            b = di * (LEG_L - rr)
            # 付け根側は中心の少し奥から
            dl = sd_capped_cone(p, di * 0.0, b, R_HUB - rr, R_TIP - rr) - rr
            d = dl if d is None else smin(d, dl, self.k)
        for c, r in self.chips:
            d = np.maximum(d, -(np.linalg.norm(p - c, axis=-1) - r))
        return d

    def grad(self, p, e=1e-3):
        g = np.zeros_like(p)
        for i in range(3):
            o = np.zeros(3)
            o[i] = e
            g[:, i] = (self(p + o) - self(p - o)) / (2 * e)
        return g


def _verts_np(me):
    a = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)


def _project(obj, sdf, iters=6, disp=None):
    """頂点を SDF のゼロ面へニュートン法で寄せる（+ 任意の法線方向変位）"""
    me = obj.data
    p = _verts_np(me)
    for _ in range(iters):
        d = sdf(p)
        g = sdf.grad(p)
        gl2 = np.maximum((g * g).sum(-1), 1e-8)
        p = p - (d / gl2)[:, None] * g
    if disp is not None:
        g = sdf.grad(p)
        n = g / np.linalg.norm(g, axis=-1, keepdims=True)
        p = p + n * disp(p, n)[:, None]
    me.vertices.foreach_set("co", p.ravel())
    me.update()


def _remesh(obj, voxel):
    m = obj.modifiers.new("Remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = voxel
    m.adaptivity = 0.0
    C.apply_modifiers(obj)


def _rim_chips(rnd, n, rmin, rmax, legs=(0, 1, 2, 3)):
    """脚の端面の縁（角）に欠けを作る球"""
    dirs = leg_dirs()
    out = []
    for _ in range(n):
        i = rnd.choice(legs)
        d = Vector(dirs[i])
        t = d.orthogonal().normalized()
        u = d.cross(t)
        a = rnd.uniform(0, math.tau)
        rim = d * LEG_L + (t * math.cos(a) + u * math.sin(a)) * R_TIP
        # 縁から少し外側に中心を置く（角だけが削れる）
        r = rnd.uniform(rmin, rmax)
        c = rim + (t * math.cos(a) + u * math.sin(a)) * r * 0.45 + d * r * 0.45
        out.append((np.array(c), r))
    return out


def tetrapod_mesh(name, seed, round_r, chips, target_tris, bumpy=0.004):
    rnd = random.Random(seed)
    sdf = TetrapodSDF(round_r=round_r, chips=chips, seed=seed)
    # 下地: 脚ごとの円錐を結合 → ボクセルリメッシュで一体化
    parts = []
    for d in leg_dirs():
        dv = Vector(d)
        o = geo.cylinder("_leg", R_HUB, LEG_L, verts=32, radius2=R_TIP)
        o.location = dv * (LEG_L / 2)
        o.rotation_euler = dv.to_track_quat("Z", "Y").to_euler()
        parts.append(o)
    bpy.context.view_layer.update()  # matrix_world を確定させてから結合
    obj = C.join(parts, name)
    _remesh(obj, 0.03)
    _project(obj, sdf)
    _remesh(obj, 0.018)
    off = Vector((rnd.uniform(-50, 50), rnd.uniform(-50, 50), rnd.uniform(-50, 50)))

    def disp(p, n):
        # 型枠の歪み程度のわずかなうねり
        return np.array([geo.fbm(Vector(q) * 1.3 + off, 3) for q in p]) * bumpy

    _project(obj, sdf, disp=disp)
    geo.decimate(obj, target_tris=target_tris)
    C.set_smooth(obj, True, angle=42)
    # 接地面 z=0
    p = _verts_np(obj.data)
    zmin = p[:, 2].min()
    obj.data.vertices.foreach_set("co", (p - np.array([0, 0, zmin])).ravel())
    obj.data.update()
    return obj, -zmin


# ---------------------------------------------------------------------------
# コンクリート材質
# ---------------------------------------------------------------------------
def concrete_material(name, hub_z, weathered):
    dirs = leg_dirs()

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        p = nb.vmath("SUBTRACT", co, (0.0, 0.0, hub_z))  # 中心基準
        dots = [nb.vmath("DOT_PRODUCT", p, tuple(d), out=1) for d in dirs]

        big = nb.noise(co, 1.1, 4, 0.55)
        mid = nb.noise(co, 5.0, 5, 0.6)
        fine = nb.noise(co, 40.0, 3, 0.6)
        base = nb.ramp(big, [(0.3, srgb("#8f8d87")), (0.5, srgb("#9f9d96")), (0.72, srgb("#aeaba3"))])
        col = nb.hsv(base, 0.5, 1.0, nb.maprange(mid, 0.3, 0.7, 0.93, 1.05))
        # 打設のムラ（水平方向の色の層）
        layer = nb.noise(nb.mapping(co, scale=(0.4, 0.4, 7.0)), 1.0, 3, 0.5)
        col = nb.mix(col, srgb("#a19e95"), nb.mul(nb.smooth(layer, 0.5, 0.7), 0.4))

        # 型枠の継ぎ目: 脚 0-1 と 2-3 を含む鏡映面との交線 + 脚の周方向の継ぎ目
        seams = None
        for i, j in ((0, 1), (2, 3)):
            n = np.cross(dirs[i], dirs[j])
            n /= np.linalg.norm(n)
            dn = nb.math("ABSOLUTE", nb.vmath("DOT_PRODUCT", p, tuple(n), out=1))
            s = nb.smooth(dn, 0.006, 0.0015)
            seams = s if seams is None else nb.math("MAXIMUM", seams, s)
        for dt in dots:
            ring = nb.smooth(nb.math("ABSOLUTE", nb.sub(dt, LEG_L * 0.58)), 0.005, 0.0012)
            seams = nb.math("MAXIMUM", seams, ring)
        seams = nb.mul(seams, nb.maprange(nb.noise(co, 3.0, 2, 0.5), 0.3, 0.6, 0.5, 1.0))
        col = nb.mix(col, srgb("#85837d"), nb.mul(seams, 0.35))
        # 上面（打設面）は金ごて仕上げで少し明るく滑らか
        top = nb.smooth(dots[0], LEG_L - 0.015, LEG_L - 0.005)
        trowel = nb.noise(nb.mapping(co, scale=(9.0, 2.0, 1.0)), 3.0, 3, 0.5)
        col = nb.mix(col, nb.mix(srgb("#b4b1a9"), srgb("#9c9a93"), trowel), top)

        # 気泡穴（あばた）
        bh = nb.voronoi(co, 32.0)
        bhr = nb.voronoi(co, 32.0, out="Color")
        hole = nb.mul(nb.smooth(bh, 0.16, 0.06), nb.math("GREATER_THAN", nb.sep(bhr)[0], 0.8))
        bh2 = nb.voronoi(nb.mapping(co, loc=(3.3, 1.1, 7.7)), 90.0)
        hole2 = nb.mul(nb.smooth(bh2, 0.2, 0.08), nb.smooth(nb.noise(co, 2.0, 2, 0.5), 0.5, 0.65))
        holes = nb.math("MAXIMUM", hole, nb.mul(hole2, 0.7))
        col = nb.mix(col, srgb("#5c5b57"), nb.mul(holes, 0.8))
        # 小さな欠け（明るい骨材の露出）
        chip = nb.smooth(nb.noise(co, 7.0, 4, 0.6, distortion=0.3), 0.7, 0.74)
        col = nb.mix(col, srgb("#c4c1b8"), nb.mul(chip, 0.6))

        height = nb.add(nb.mul(holes, -1.0), nb.mul(fine, 0.25))
        height = nb.add(height, nb.mul(seams, 0.5))
        height = nb.add(height, nb.mul(chip, -0.6))
        height = nb.add(height, nb.mul(mid, 0.25))
        rough = nb.maprange(fine, 0.3, 0.7, 0.86, 0.94)
        rough = nb.mixf(rough, 0.8, top)
        cavity = nb.maprange(holes, 0, 1, 1.0, 0.45)

        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        if not weathered:
            # 接地付近に薄く砂ぼこり
            dust = nb.mul(nb.smooth(z, 0.35, 0.0), nb.smooth(nb.noise(co, 8.0, 3, 0.5), 0.35, 0.6))
            col = nb.mix(col, srgb("#b9b09a"), nb.mul(dust, 0.45))
        if weathered:
            # 全体に日焼け・塩で少しくすむ
            col = nb.hsv(col, 0.5, 0.85, 0.9)
            # 高さで決まる汚れの帯（上端をノイズで乱す）。下向きの面ほど濃い
            zz = nb.add(nb.add(z, nb.mul(nb.sub(big, 0.5), 0.4)), nb.mul(nb.sub(mid, 0.5), 0.12))
            zz = nb.add(zz, nb.mul(nb.math("MINIMUM", nz, 0.0), 0.25))
            stain = nb.smooth(zz, 1.45, 0.95)
            col = nb.mix(col, nb.mix(srgb("#74716a"), srgb("#6a6b62"), mid), nb.mul(stain, 0.6))
            # 潮間帯: 黒い付着膜（上端が波打つ帯）＋根元に緑の藻
            algae_n = nb.noise(co, 6.0, 6, 0.65)
            band = nb.smooth(zz, 0.95, 0.7)
            acol = nb.mix(srgb("#2f302b"), srgb("#45453c"), nb.smooth(nb.noise(co, 14.0, 3, 0.5), 0.35, 0.7))
            col = nb.mix(col, acol, nb.mul(band, nb.maprange(algae_n, 0.3, 0.7, 0.65, 0.92)))
            green = nb.mul(nb.smooth(zz, 0.5, 0.15), nb.smooth(algae_n, 0.4, 0.6))
            col = nb.mix(col, srgb("#3c4a2b"), nb.mul(green, 0.75))
            algae = nb.math("MAXIMUM", band, green)
            # フジツボ（白い点、群生）
            bv = nb.voronoi(co, 55.0)
            br = nb.sep(nb.voronoi(co, 55.0, out="Color"))
            colony = nb.smooth(nb.noise(co, 3.5, 3, 0.6), 0.48, 0.6)
            barn_z = nb.mul(nb.smooth(zz, 1.05, 0.8), nb.smooth(zz, 0.0, 0.15))
            barn = nb.mul(nb.mul(nb.smooth(bv, 0.3, 0.18), nb.math("GREATER_THAN", br[0], 0.35)),
                          nb.mul(colony, barn_z))
            ring = nb.mul(nb.smooth(bv, 0.3, 0.22), nb.smooth(bv, 0.1, 0.18))
            col = nb.mix(col, nb.mix(srgb("#e4dfd1"), srgb("#c9c2b0"), br[1]), barn)
            col = nb.mix(col, srgb("#7b776c"), nb.mul(nb.mul(barn, nb.sub(1.0, ring)), 0.5))
            # 錆の垂れ（縦に伸びた筋）
            streak = nb.noise(nb.mapping(co, scale=(26.0, 26.0, 1.0)), 1.3, 4, 0.55)
            src = nb.smooth(nb.noise(nb.mapping(co, scale=(3.0, 3.0, 0.15)), 1.0, 2, 0.5), 0.56, 0.66)
            rust = nb.mul(nb.mul(nb.smooth(streak, 0.56, 0.68), src), nb.smooth(z, 0.3, 0.9))
            rcol = nb.mix(srgb("#8a4a22"), srgb("#b8733a"), fine)
            col = nb.mix(col, rcol, nb.mul(rust, 0.65))
            # 白い塩の析出
            salt = nb.mul(nb.smooth(nb.noise(co, 9.0, 4, 0.6), 0.64, 0.72), nb.smooth(z, 1.1, 1.8))
            col = nb.mix(col, srgb("#cfccc3"), nb.mul(salt, 0.5))

            height = nb.add(height, nb.mul(barn, 1.4))
            height = nb.add(height, nb.mul(algae, nb.mul(algae_n, 0.4)))
            rough = nb.mixf(rough, 0.7, nb.mul(algae, 0.8))
            cavity = nb.math("MULTIPLY", cavity, nb.maprange(nb.mul(barn, nb.sub(1.0, ring)), 0, 1, 1.0, 0.7))

        return dict(color=col, rough=rough, height=height, height_scale=0.004, cavity=cavity)

    return pbr_material(name, fn, res=2048, ao_distance=0.8, ao_samples=48)


def build():
    rnd = random.Random(5)
    a, hub_a = tetrapod_mesh("Tetrapod_A", 1, 0.045, _rim_chips(rnd, 4, 0.03, 0.06), 5000, bumpy=0.003)
    C.assign(a, concrete_material("TetrapodConcreteA", hub_a, False))
    b, hub_b = tetrapod_mesh("Tetrapod_B", 2, 0.06, _rim_chips(rnd, 11, 0.04, 0.1), 5000, bumpy=0.008)
    C.assign(b, concrete_material("TetrapodConcreteB", hub_b, True))
    return {"Tetrapod_A": [a], "Tetrapod_B": [b]}
