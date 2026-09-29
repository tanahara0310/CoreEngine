"""3 ココヤシ（3 バリエーション）

幹: 曲がった円柱に葉痕のリング模様
葉: 羽状葉をアルファ付きアトラス（左半分=緑葉 / 右半分=枯れ葉）で表現し、V 字に折れたカードに貼る
実: 樹冠の下に房状
"""
import math
import random

from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb

PREVIEW = dict(cam_dir=(0.25, -1.0, 0.28), lens=40)

FROND_W = 1.9  # 羽片の左右幅(m)
FROND_L = 4.4  # 葉の長さ(m)


def _white(nb, w):
    n = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="1D")
    nb.link(w, n.inputs["W"])
    return n.outputs["Value"]


def frond_material():
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        dry = nb.math("GREATER_THAN", U, 0.5)
        x = nb.math("FRACT", nb.mul(U, 2.0))
        s = nb.mul(nb.math("ABSOLUTE", nb.sub(x, 0.5)), 2.0)  # 0=葉軸, 1=縁
        y = V
        # 羽片の長さの包絡線（付け根は葉柄、先端に向かって短く）
        env = nb.mul(nb.smooth(y, 0.12, 0.32), nb.maprange(y, 0.55, 1.0, 1.0, 0.18))
        # 斜めに並ぶ羽片
        n_leaflets = 52.0
        t = nb.sub(nb.mul(y, n_leaflets), nb.mul(s, 3.2))
        side = nb.math("GREATER_THAN", x, 0.5)
        lid = nb.add(nb.math("FLOOR", t), nb.mul(side, 1000.0))
        lid = nb.add(lid, nb.mul(dry, 5000.0))
        rnd = _white(nb, lid)
        rnd2 = _white(nb, nb.add(lid, 77.7))
        f = nb.math("FRACT", t)
        length = nb.mul(env, nb.maprange(rnd, 0, 1, 0.85, 1.05))
        q = nb.math("DIVIDE", s, nb.math("MAXIMUM", length, 0.001))
        # 羽片の幅（付け根から先端へ細くなる）
        width = nb.mul(0.62, nb.pow(nb.math("MAXIMUM", nb.sub(1.0, nb.pow(q, 2.2)), 0.0), 0.5))
        d = nb.math("ABSOLUTE", nb.sub(f, 0.5))
        leaf = nb.mul(nb.smooth(d, nb.mul(width, 0.5), nb.sub(nb.mul(width, 0.5), 0.03)),
                      nb.math("LESS_THAN", q, 1.0))
        # 枯れ葉はところどころ欠ける
        missing = nb.mul(dry, nb.math("GREATER_THAN", rnd2, 0.72))
        leaf = nb.mul(leaf, nb.sub(1.0, missing))
        # 葉軸
        rw = nb.maprange(y, 0.0, 1.0, 0.035, 0.008)
        rachis = nb.mul(nb.smooth(s, rw, nb.mul(rw, 0.6)), nb.math("LESS_THAN", y, 0.985))
        alpha = nb.math("MAXIMUM", leaf, rachis)

        # 色
        g1 = nb.mix(srgb("#35601a"), srgb("#5f8a26"), rnd)
        mid = nb.smooth(d, 0.06, 0.0)
        g = nb.mix(g1, srgb("#8fa03c"), nb.mul(mid, 0.55))
        g = nb.mix(g, srgb("#a29a45"), nb.mul(nb.smooth(q, 0.75, 1.0), 0.8))
        brown_tip = nb.mul(nb.smooth(q, 0.9, 1.0), nb.math("GREATER_THAN", rnd2, 0.55))
        g = nb.mix(g, srgb("#7a5a2f"), brown_tip)
        dcol = nb.mix(srgb("#7d6039"), srgb("#a88b5c"), rnd)
        dcol = nb.mix(dcol, srgb("#5e4428"), nb.mul(nb.smooth(q, 0.6, 1.0), 0.7))
        col = nb.mix(g, dcol, dry)
        rcol = nb.mix(srgb("#b3a452"), srgb("#8c7147"), dry)
        col = nb.mix(col, rcol, rachis)
        grain = nb.noise(nb.comb(nb.mul(U, 200.0), nb.mul(V, 20.0), 0.0), 1.0, 3, 0.5)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(grain, 0.3, 0.7, 0.88, 1.08))

        rough = nb.mixf(0.5, 0.8, dry)
        height = nb.add(nb.mul(nb.sub(1.0, nb.math("DIVIDE", d, nb.math("MAXIMUM", nb.mul(width, 0.5), 0.001))),
                               leaf), nb.mul(rachis, 1.5))
        return dict(color=col, rough=rough, alpha=alpha, height=height, height_scale=0.004,
                    cavity=nb.maprange(nb.smooth(d, 0.0, 0.2), 0, 1, 0.8, 1.0))

    return pbr_material("PalmFrond", fn, res=2048, uv="atlas", double_sided=True,
                        atlas_size=(FROND_W * 2, FROND_L))


def trunk_material(trunk_len):
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        a = nb.mul(U, math.tau)
        cyl = nb.comb(nb.mul(nb.math("COSINE", a), 0.35), nb.mul(nb.math("SINE", a), 0.35), V)
        warp = nb.noise(cyl, 1.5, 3, 0.5)
        # 葉痕のリング
        r = nb.math("FRACT", nb.add(nb.mul(V, 4.2), nb.mul(warp, 0.35)))
        ring = nb.smooth(nb.math("ABSOLUTE", nb.sub(r, 0.5)), 0.5, 0.36)
        ring_soft = nb.smooth(nb.math("ABSOLUTE", nb.sub(r, 0.5)), 0.5, 0.15)
        fiss = nb.noise(nb.mapping(cyl, scale=(9, 9, 1.2)), 3.0, 6, 0.6)
        fiss_line = nb.smooth(nb.math("ABSOLUTE", nb.sub(fiss, 0.5)), 0.03, 0.0)
        big = nb.noise(cyl, 0.8, 4, 0.6)
        col = nb.ramp(big, [(0.3, srgb("#6f675b")), (0.55, srgb("#8d8475")), (0.75, srgb("#a59d8c"))])
        col = nb.mix(col, srgb("#4d443a"), nb.mul(ring, 0.8))
        col = nb.mix(col, srgb("#3e362d"), nb.mul(fiss_line, 0.6))
        lichen = nb.smooth(nb.noise(cyl, 6.0, 4, 0.6), 0.6, 0.7)
        col = nb.mix(col, srgb("#b9b39d"), nb.mul(lichen, 0.5))
        # 根元
        base = nb.smooth(V, 0.9, 0.1)
        col = nb.mix(col, srgb("#4f4335"), nb.mul(base, 0.8))
        # 樹冠の繊維（葉鞘）
        boot = nb.smooth(V, trunk_len - 0.9, trunk_len - 0.5)
        fib = nb.noise(nb.mapping(cyl, scale=(25, 25, 0.8)), 2.0, 5, 0.7)
        bcol = nb.mix(srgb("#5a4127"), srgb("#8a6a40"), fib)
        col = nb.mix(col, bcol, boot)
        height = nb.add(nb.mul(ring_soft, -0.8), nb.mul(fiss_line, -0.5))
        height = nb.add(height, nb.mul(nb.mul(fib, boot), 0.8))
        rough = nb.mixf(0.85, 0.95, boot)
        return dict(color=col, rough=rough, height=height, height_scale=0.015,
                    cavity=nb.maprange(ring, 0, 1, 1.0, 0.8))

    return pbr_material("PalmTrunk", fn, res=2048, ao_distance=0.6, uv="keep")


def coconut_material():
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 1.2, 3, 0.5)
        fine = nb.noise(co, 40.0, 4, 0.6)
        col = nb.ramp(n, [(0.35, srgb("#5f6e22")), (0.55, srgb("#8d8a2c")), (0.7, srgb("#b07a2c"))])
        col = nb.mix(col, srgb("#4b3a22"), nb.mul(nb.smooth(fine, 0.62, 0.72), 0.5))
        return dict(color=col, rough=0.45, height=fine, height_scale=0.002)

    return pbr_material("Coconut", fn, res=512, ao_distance=0.3)


def _frond(name, top, azim, elev, length, droop, width, dry=False, twist=0.0, rnd=None):
    """V 字に折れた葉カード + 葉軸チューブ"""
    rnd = rnd or random.Random(0)
    d_h = Vector((math.cos(azim), math.sin(azim), 0.0))
    up = Vector((0, 0, 1))
    start_dir = (d_h * math.cos(elev) + up * math.sin(elev)).normalized()
    p0 = top
    p1 = top + start_dir * length * 0.4
    p2 = top + d_h * length * 0.75 + up * (math.sin(elev) * length * 0.35 - droop * 0.4)
    p3 = top + d_h * length * 0.95 + up * (math.sin(elev) * length * 0.2 - droop)
    pts = geo.bezier_points(p0, p1, p2, p3, 15)
    side = Vector((-math.sin(azim), math.cos(azim), 0.0))
    xs = [0.0, 0.2, 0.5, 0.8, 1.0]
    verts, faces, uvs = [], [], []
    u0 = 0.5 if dry else 0.0
    fold = math.radians(38 if not dry else 70)
    n = len(pts)
    for i, p in enumerate(pts):
        y = i / (n - 1)
        tng = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
        sd = (side - tng * side.dot(tng)).normalized()
        a = twist * y
        sd = (sd * math.cos(a) + tng.cross(sd) * math.sin(a))
        dn = tng.cross(sd).normalized()
        if dn.z > 0:
            dn = -dn
        # 付け根は羽片が無いので幅を絞る（エッジのはみ出し防止）
        wscale = min(1.0, 0.25 + y * 3.0) * (1.0 - 0.55 * max(0.0, y - 0.6) / 0.4)
        for x in xs:
            o = (x - 0.5) * width * wscale
            verts.append(p + sd * o * math.cos(fold) + dn * abs(o) * math.sin(fold)
                         + dn * (abs(o) ** 2) * 0.15)
    for i in range(n - 1):
        for j in range(len(xs) - 1):
            a = i * len(xs) + j
            b = a + len(xs)
            faces.append((a, a + 1, b + 1, b))
            y0, y1 = i / (n - 1), (i + 1) / (n - 1)
            uvs.append([(u0 + xs[j] * 0.5, y0), (u0 + xs[j + 1] * 0.5, y0),
                        (u0 + xs[j + 1] * 0.5, y1), (u0 + xs[j] * 0.5, y1)])
    card = C.mesh_object(name, verts, faces, uvs)
    # UV の幅方向は付け根で狭めたので、テクスチャの x もそれに合わせて縮む → 羽片の長さ包絡線と整合
    tube = geo.tube_along(pts, [0.045 * (1 - 0.8 * i / (n - 1)) for i in range(n)], sides=6,
                          name=name + "_rachis", cap_start=False, cap_end=True)
    layer = tube.data.uv_layers["Proc"]
    total = sum((pts[i] - pts[i - 1]).length for i in range(1, n))
    for d in layer.data:
        d.uv = (u0 + 0.25 + (d.uv[0] - 0.5) * 0.006, min(0.98, d.uv[1] / total))
    return [card, tube]


def palm(prefix, height, lean, n_fronds, n_dry, n_nuts, seed, bend=0.0):
    rnd = random.Random(seed)
    lx, ly = lean
    # 幹の中心線（根元は少し埋める）
    p0 = Vector((0, 0, -0.3))
    p1 = Vector((lx * 0.05 + bend * 0.3, ly * 0.05, height * 0.35))
    p2 = Vector((lx * 0.55 - bend * 0.3, ly * 0.55, height * 0.7))
    p3 = Vector((lx, ly, height))
    pts = geo.bezier_points(p0, p1, p2, p3, 40)
    total = sum((pts[i] - pts[i - 1]).length for i in range(1, len(pts)))
    radii = []
    acc = 0.0
    for i, p in enumerate(pts):
        if i:
            acc += (pts[i] - pts[i - 1]).length
        t = acc / total
        r = 0.16 + 0.2 * math.exp(-acc / 0.45) + 0.02 * math.sin(t * 5.0)
        if acc > total - 0.8:
            r += 0.05 * (acc - (total - 0.8)) / 0.8
        radii.append(r * (height / 8.0) ** 0.3)

    def ring_fn(i, j, a):
        return 1.0 + 0.025 * math.sin(i * 2.7) + 0.02 * math.sin(a * 3 + i)

    trunk = geo.tube_along(pts, radii, sides=16, name=prefix + "_trunk", cap_start=True, cap_end=True,
                           ring_fn=ring_fn)
    C.set_smooth(trunk, True)
    C.assign(trunk, trunk_material(total))

    top = pts[-1] + Vector((0, 0, 0.05))
    frond_mat = frond_material()
    parts = [trunk]
    golden = math.radians(137.5)
    for k in range(n_fronds):
        az = k * golden + rnd.uniform(-0.2, 0.2)
        age = k / max(1, n_fronds - 1)  # 0=若い（上向き）, 1=古い（垂れる）
        elev = math.radians(70 - 95 * age + rnd.uniform(-8, 8))
        L = FROND_L * rnd.uniform(0.85, 1.05) * (height / 8.0) ** 0.25
        droop = 0.6 + 2.2 * age
        for o in _frond(f"{prefix}_frond{k}", top, az, elev, L, droop, FROND_W * rnd.uniform(0.9, 1.05),
                        twist=rnd.uniform(-0.4, 0.4), rnd=rnd):
            C.assign(o, frond_mat)
            parts.append(o)
    for k in range(n_dry):
        az = rnd.uniform(0, math.tau)
        L = FROND_L * 0.8
        for o in _frond(f"{prefix}_dry{k}", top - Vector((0, 0, 0.3)), az, math.radians(-78), L, 0.2,
                        FROND_W * 0.6, dry=True, twist=rnd.uniform(-0.6, 0.6), rnd=rnd):
            C.assign(o, frond_mat)
            parts.append(o)
    nut_mat = coconut_material()
    for k in range(n_nuts):
        a = rnd.uniform(0, math.tau)
        r = radii[-1] + 0.2 + rnd.uniform(0.0, 0.08)
        c = top + Vector((math.cos(a) * r, math.sin(a) * r, rnd.uniform(-0.5, -0.25)))
        bm = geo.icosphere_bm(3, 0.14)
        for v in bm.verts:
            # 三稜のある卵形
            ang = math.atan2(v.co.y, v.co.x)
            v.co.x *= 1.0 + 0.06 * math.cos(3 * ang)
            v.co.y *= 1.0 + 0.06 * math.cos(3 * ang)
            v.co.z *= 1.22
        nut = C.from_bmesh(f"{prefix}_nut{k}", bm)
        nut.location = c
        nut.rotation_euler = (rnd.uniform(-0.5, 0.5), rnd.uniform(-0.5, 0.5), rnd.uniform(0, 6))
        C.set_smooth(nut, True)
        C.assign(nut, nut_mat)
        parts.append(nut)
    return parts


def build():
    return {
        "CoconutPalm_A": palm("PalmA", 8.5, (1.9, 0.5), 24, 3, 8, seed=1, bend=0.4),
        "CoconutPalm_B": palm("PalmB", 7.0, (0.7, -0.5), 22, 2, 6, seed=2, bend=-0.5),
        "CoconutPalm_C": palm("PalmC", 4.0, (0.4, 0.2), 14, 1, 0, seed=3),
    }
