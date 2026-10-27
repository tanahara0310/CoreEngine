"""アラハビーチの両端の突堤（丸い大石を積んだ捨て石の堤、3 バリエーション）

断面は天端の幅 3.2 m（z=+2.0）、勾配 1:1.4 の斜面が海底（z=-2.2）まで下がる台形。
中の芯（暗い砕石の面）の上に、0.9〜1.6 m の丸みのある石を 1 個ずつ並べる。

座標: z=0 が平均水面。堤は陸側から海側（-Y）へ伸びる。原点はモジュールの陸側の端の天端の中心の真下。
  Jetty_Straight: 直線 10 m（y=0..-10）。次のモジュールを y-10 に置けば石の並びが続く
  Jetty_HeadR    : 先端。4 m まっすぐ進んでから +X へ 75° 曲がり、丸く終わる（鉤形）
  Jetty_HeadL    : 先端。-X へ曲がる（HeadR の左右反対）
"""
import math
import random

import bmesh
import numpy as np
from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from . import _timber as T

CREST = 2.0
CREST_HALF = 1.6
SLOPE = 1.4
SEABED = -2.2
STONE_MIN_Z = -1.7
STRAIGHT_LEN = 10.0


def _preview_extra(objs):
    import bpy
    cam = bpy.context.scene.camera
    cam.data.lens = 32
    mn, mx = C.bounds(objs)
    tgt = Vector(((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, 0.5))
    cam.location = tgt + Vector((-14.0, -26.0, 13.0))
    C.look_at(cam, tgt)


PREVIEW = dict(extra=_preview_extra, spacing=1.15)


# ---------------------------------------------------------------------------
# 中心線と高さ
# ---------------------------------------------------------------------------
def centerline_head(side):
    """先端の中心線（y=0 から -Y へ 4 m、その後 side 側へ 75° 曲がる）"""
    pts = [Vector((0.0, -t, 0.0)) for t in np.linspace(0.0, 4.0, 9)]
    R = 7.0
    for th in np.linspace(0.0, math.radians(75), 24)[1:]:
        pts.append(Vector((side * (R - R * math.cos(th)), -4.0 - R * math.sin(th), 0.0)))
    return pts


def _dist_polyline(px, py, pts):
    """平面上の点群 (px, py) から折れ線までの距離"""
    d = np.full(px.shape, 1e9)
    for a, b in zip(pts[:-1], pts[1:]):
        ax, ay, bx, by = a.x, a.y, b.x, b.y
        vx, vy = bx - ax, by - ay
        L2 = vx * vx + vy * vy
        t = np.clip(((px - ax) * vx + (py - ay) * vy) / L2, 0.0, 1.0)
        d = np.minimum(d, np.hypot(px - (ax + t * vx), py - (ay + t * vy)))
    return d


def height_from_dist(d):
    """中心線からの距離 → 芯の高さ（天端の肩は丸める）"""
    k = 0.6
    x = d - CREST_HALF
    soft = np.where(x < -k, 0.0, np.where(x > k, x, (x + k) ** 2 / (4 * k)))
    return np.maximum(CREST - soft / SLOPE, SEABED - 0.2)


class Field:
    """平面の (x, y) → 高さ・法線"""

    def __init__(self, dist_fn):
        self.dist = dist_fn

    def h(self, x, y):
        return height_from_dist(self.dist(np.asarray(x, float), np.asarray(y, float)))

    def normal(self, x, y, e=0.05):
        hx = (self.h(x + e, y) - self.h(x - e, y)) / (2 * e)
        hy = (self.h(x, y + e) - self.h(x, y - e)) / (2 * e)
        n = Vector((-float(hx), -float(hy), 1.0))
        return n.normalized()


# ---------------------------------------------------------------------------
# 芯（高さの格子）
# ---------------------------------------------------------------------------
def core_mesh(name, field, x0, x1, y0, y1, step=0.3, inset=0.35):
    nx = int(round((x1 - x0) / step)) + 1
    ny = int(round((y1 - y0) / step)) + 1
    xs = np.linspace(x0, x1, nx)
    ys = np.linspace(y0, y1, ny)
    X, Y = np.meshgrid(xs, ys)
    H = field.h(X, Y)
    floor = SEABED - 0.2
    Z = np.where(H > floor + 1e-3, H - inset, floor)
    verts = [(float(X[j, i]), float(Y[j, i]), float(Z[j, i])) for j in range(ny) for i in range(nx)]
    faces = []
    for j in range(ny - 1):
        for i in range(nx - 1):
            a = j * nx + i
            q = (a, a + 1, a + nx + 1, a + nx)
            if all(Z.flat[v] <= floor + 1e-3 for v in q):
                continue
            faces.append(q)
    obj = C.mesh_object(name, verts, faces)
    # 使われない頂点を消す
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.to_mesh(obj.data)
    bm.free()
    C.set_smooth(obj, True)
    T.set_attr(obj, "StoneId", lambda co, n: (0.5, 0.5, 1.0))
    return obj


# ---------------------------------------------------------------------------
# 石
# ---------------------------------------------------------------------------
def stone_library(rnd, count=10):
    """割れ面のある石の原形（長径 1 m）。平たい軸が Z"""
    lib = []
    for k in range(count):
        off = Vector((rnd.uniform(-50, 50), rnd.uniform(-50, 50), rnd.uniform(-50, 50)))
        sx, sy, sz = 0.5, rnd.uniform(0.36, 0.48), rnd.uniform(0.26, 0.36)
        # 割れ面（平らな面）をいくつか作る平面
        planes = []
        for _ in range(rnd.randint(6, 10)):
            n = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1))).normalized()
            planes.append((n, rnd.uniform(0.55, 0.8)))
        bm = geo.icosphere_bm(3)
        for v in bm.verts:
            d = v.co.normalized()
            # 角の取れた箱と楕円体の中間（丸みのある塊）
            p = 3.2
            s = (abs(d.x) ** p + abs(d.y) ** p + abs(d.z) ** p) ** (-1.0 / p)
            q = d * (0.45 + 0.55 * s)
            for n, dist in planes:
                h = q.dot(n)
                if h > dist:
                    q = q - n * (h - dist) * 0.97
            nz = geo.fbm(d * 1.6 + off, 3) * 0.1 + geo.fbm(d * 4.5 + off, 3) * 0.045 + geo.fbm(d * 11.0 + off, 2) * 0.014
            v.co = Vector((q.x * sx, q.y * sy, q.z * sz)) * (1.0 + nz)
        obj = C.from_bmesh(f"_stone{k}", bm)
        geo.smooth_mod(obj, 0.3, 1)
        geo.decimate(obj, target_tris=520)
        C.set_smooth(obj, True, angle=55)
        lib.append(obj)
    return lib


def sample_stones(rnd, field, x0, x1, y0, y1, period_y=None, r_min=0.92, tries=9000):
    """平面のダーツ投げで石の中心を決める。period_y を渡すと Y 方向に周期的な間隔で判定する"""
    pts = []
    for _ in range(tries):
        x = rnd.uniform(x0, x1)
        y = rnd.uniform(y0, y1)
        z = float(field.h(x, y))
        if z < STONE_MIN_Z:
            continue
        size = rnd.uniform(0.9, 1.6)
        ok = True
        for (px, py, ps) in pts:
            dy = y - py
            if period_y:
                dy -= period_y * round(dy / period_y)
            if (x - px) ** 2 + dy ** 2 < (r_min * (size + ps) / 2.5) ** 2:
                ok = False
                break
        if ok:
            pts.append((x, y, size))
    return pts


def place_stones(prefix, rnd, lib, field, pts):
    objs = []
    for i, (x, y, size) in enumerate(pts):
        z = float(field.h(x, y))
        n = field.normal(x, y)
        up = (n * 0.7 + Vector((0, 0, 0.3))).normalized()
        src = rnd.choice(lib)
        o = src.copy()
        o.data = src.data.copy()
        C.link_object(o)
        rot = up.to_track_quat("Z", "Y").to_matrix().to_4x4() @ Matrix.Rotation(rnd.uniform(0, math.tau), 4, "Z") \
            @ Matrix.Rotation(rnd.uniform(-0.25, 0.25), 4, "X")
        s = size * rnd.uniform(0.95, 1.05)
        # 芯にめり込ませ、上 6 割ほどが出る
        c = Vector((x, y, z)) - up * (0.06 * size)
        o.matrix_world = Matrix.Translation(c) @ rot @ Matrix.Diagonal((s, s, s, 1.0))
        r1, r2 = rnd.random(), rnd.random()
        T.set_attr(o, "StoneId", lambda co, nn, r1=r1, r2=r2: (r1, r2, 0.0))
        o.name = f"{prefix}_s{i}"
        objs.append(o)
    return objs


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def stone_material(name, res=2048):
    """灰色の大石。上は日に焼けて明るく、潮の当たる下は黒ずみ、水中は藻。石灰岩の小さな穴と潮間帯のフジツボ"""
    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        r1, r2, _ = nb.sep(nb.attr("StoneId", "Vector"))
        big = nb.noise(co, 0.9, 4, 0.55)
        mid = nb.noise(co, 5.0, 5, 0.6)
        fine = nb.noise(co, 30.0, 4, 0.6)
        speck = nb.voronoi(co, 70.0)
        base = nb.ramp(r1, [(0.0, srgb("#8a8b88")), (0.5, srgb("#969893")), (1.0, srgb("#a19f98"))])
        col = nb.mix(base, nb.hsv(base, 0.52, 1.05, 0.95), nb.smooth(r2, 0.6, 0.9))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(mid, 0.3, 0.7, 0.88, 1.08))
        # 石英の白い粒と黒い粒
        col = nb.mix(col, srgb("#d6d4cc"), nb.mul(nb.smooth(speck, 0.12, 0.05), 0.35))
        col = nb.mix(col, srgb("#3c3c3a"), nb.mul(nb.smooth(nb.voronoi(nb.mapping(co, loc=(3, 1, 7)), 55.0), 0.1, 0.04), 0.3))
        # 地衣類（上面）
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        lichen = nb.mul(nb.smooth(nb.noise(co, 6.0, 4, 0.6), 0.6, 0.7), nb.smooth(nz, 0.3, 0.8))
        col = nb.mix(col, srgb("#c9c3a6"), nb.mul(lichen, 0.4))
        # 潮間帯の黒い帯、水中の藻
        edge = nb.add(z, nb.mul(nb.sub(big, 0.5), 0.5))
        tide = nb.smooth(edge, 1.0, 0.4)
        col = nb.mix(col, nb.mix(srgb("#3a3a34"), srgb("#4d4b40"), fine), nb.mul(tide, 0.85))
        weed = nb.smooth(edge, -0.1, -0.6)
        col = nb.mix(col, nb.mix(srgb("#2e3622"), srgb("#4b5530"), nb.smooth(mid, 0.35, 0.7)), nb.mul(weed, 0.9))
        # 白い塩と鳥のふん
        salt = nb.mul(nb.smooth(nb.noise(co, 8.0, 4, 0.6), 0.66, 0.74), nb.smooth(z, 0.8, 1.4))
        col = nb.mix(col, srgb("#dedbd2"), nb.mul(salt, 0.45))
        # 石灰岩の溶けた小さな穴と、潮間帯のフジツボ
        pit_d = nb.voronoi(nb.mapping(co, loc=(5, 2, 9)), 18.0)
        pit_r = nb.sep(nb.voronoi(nb.mapping(co, loc=(5, 2, 9)), 18.0, out="Color"))[0]
        pits = nb.mul(nb.smooth(pit_d, 0.22, 0.08), nb.math("GREATER_THAN", pit_r, 0.55))
        col = nb.mix(col, srgb("#4a4945"), nb.mul(pits, 0.55))
        barn_d = nb.voronoi(nb.mapping(co, loc=(1, 7, 3)), 90.0)
        barn = nb.mul(nb.mul(nb.smooth(barn_d, 0.32, 0.18), nb.smooth(edge, 1.1, 0.7)), nb.smooth(edge, -0.2, 0.2))
        barn = nb.mul(barn, nb.smooth(nb.noise(co, 3.0, 3, 0.6), 0.4, 0.6))
        col = nb.mix(col, srgb("#cfc8b6"), nb.mul(barn, 0.8))
        wet = nb.smooth(edge, 0.9, 0.2)
        rough = nb.mixf(nb.maprange(fine, 0.3, 0.7, 0.66, 0.9), 0.25, wet)
        rough = nb.mixf(rough, 0.85, barn)
        height = nb.add(nb.mul(mid, 0.45), nb.add(nb.mul(fine, 0.3), nb.mul(nb.smooth(speck, 0.12, 0.05), 0.1)))
        height = nb.add(height, nb.add(nb.mul(pits, -0.5), nb.mul(barn, 0.35)))
        return dict(color=col, rough=rough, height=height, height_scale=0.025,
                    cavity=nb.maprange(pits, 0, 1, 1.0, 0.55))

    # UV の島を大きく分け、島どうしの余白を広げる
    return pbr_material(name, fn, res=res, ao_distance=1.2, ao_samples=48, uv_angle=70.0, uv_margin=0.012,
                        ao_full=True)


def core_material(name, res=512):
    """石の隙間の奥に見える暗い砕石"""
    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        g = nb.voronoi(co, 9.0)
        col = nb.mix(srgb("#2c2b28"), srgb("#55534d"), nb.smooth(g, 0.0, 0.4))
        col = nb.mix(col, srgb("#26301c"), nb.mul(nb.smooth(z, -0.1, -0.6), 0.8))
        return dict(color=col, rough=0.95, height=g, height_scale=0.02)

    return pbr_material(name, fn, res=res, ao_distance=1.0, ao_samples=32)


# ---------------------------------------------------------------------------
# バリエーション
# ---------------------------------------------------------------------------
def straight(name, seed):
    rnd = random.Random(seed)
    field = Field(lambda x, y: np.abs(x))
    w = CREST_HALF + (CREST - SEABED) * SLOPE + 0.5
    core = core_mesh(f"{name}_core", field, -w, w, -STRAIGHT_LEN, 0.0)
    lib = stone_library(rnd)
    pts = sample_stones(rnd, field, -w, w, -STRAIGHT_LEN, 0.0, period_y=STRAIGHT_LEN)
    stones = place_stones(name, rnd, lib, field, pts)
    import bpy
    for o in lib:
        bpy.data.objects.remove(o, do_unlink=True)
    return [core] + stones


def head(name, side, seed):
    rnd = random.Random(seed)
    pts_c = centerline_head(side)
    field = Field(lambda x, y: _dist_polyline(x, y, pts_c))
    w = CREST_HALF + (CREST - SEABED) * SLOPE + 0.5
    xs = [p.x for p in pts_c]
    ys = [p.y for p in pts_c]
    x0, x1 = min(xs) - w, max(xs) + w
    y0, y1 = min(ys) - w, 0.0
    core = core_mesh(f"{name}_core", field, x0, x1, y0, y1)
    lib = stone_library(rnd)
    pts = sample_stones(rnd, field, x0, x1, y0, y1, tries=16000)
    stones = place_stones(name, rnd, lib, field, pts)
    import bpy
    for o in lib:
        bpy.data.objects.remove(o, do_unlink=True)
    return [core] + stones


def build():
    out = {}
    for vname, fn, mname in (("Jetty_Straight", lambda: straight("JtS", 31), "JettyStoneS"),
                             ("Jetty_HeadR", lambda: head("JtR", 1, 47), "JettyStoneR"),
                             ("Jetty_HeadL", lambda: head("JtL", -1, 53), "JettyStoneL")):
        objs = fn()
        mat = stone_material(mname)
        cmat = core_material(mname + "Core")
        for o in objs:
            C.assign(o, cmat if o.name.endswith("_core") else mat)
        out[vname] = objs
    return out
