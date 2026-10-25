"""遊泳区域のハブクラゲ防止ネットと浮き桟橋（3 バリエーション）

座標: z=0 が平均水面。
  SwimNet     : 浮きの並んだロープと、海底まで垂れたネット。X 方向に 10 m（x=-5..+5）で、
                次のモジュールを x+10 に置けば浮きの間隔（1 m）とネットの目がつながる
  FloatingPier: 白い樹脂のキューブフロートを並べた浮き桟橋（12 m × 2.5 m）。原点は中心の水面。+X の端にはしご
  FloatingPier_Walk: はしごの無い同じ浮き桟橋（X 方向につないで通路にする）
"""
import math
import random

from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import galvanized
from . import _timber as T

HALF = 5.0
NET_BOTTOM = -2.6
MESH = 0.05              # ネットの目（菱形の対角線の間隔）
CUBE = 0.5
PIER_NX, PIER_NY = 24, 5
CUBE_TOP = 0.12


def _preview_extra(objs):
    import bpy
    cam = bpy.context.scene.camera
    mn, mx = C.bounds(objs)
    tgt = Vector(((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, -0.3))
    cam.data.lens = 35
    cam.location = tgt + Vector((-4.0, -17.0, 5.0))
    C.look_at(cam, tgt)
    # 水面の下が見えるように床を下げる
    g = bpy.data.objects.get("PreviewGround")
    if g:
        g.location.z = NET_BOTTOM - 0.05


PREVIEW = dict(extra=_preview_extra)


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def float_material():
    """黄色の発泡浮き（日焼けで褪せ、水際に藻）"""
    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        fade = nb.noise(co, 3.0, 3, 0.5)
        col = nb.mix(srgb("#efc21a"), srgb("#e8d48a"), nb.mul(nb.smooth(fade, 0.4, 0.75), 0.7))
        pores = nb.voronoi(co, 140.0)
        col = nb.mix(col, nb.hsv(col, 0.5, 1.0, 0.8), nb.mul(nb.smooth(pores, 0.12, 0.04), 0.5))
        algae = nb.smooth(nb.add(z, nb.mul(nb.sub(fade, 0.5), 0.05)), 0.0, -0.04)
        col = nb.mix(col, srgb("#4c5730"), nb.mul(algae, 0.75))
        return dict(color=col, rough=nb.mixf(0.7, 0.4, algae), height=nb.smooth(pores, 0.12, 0.04),
                    height_scale=0.001)

    return pbr_material("SwimNetFloat", fn, res=256, ao_distance=0.2)


def rope_material(name, color):
    """撚ったロープ（PP の 3 つ撚り）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        ang = nb.math("ARCTAN2", Z, Y)
        twist = nb.math("FRACT", nb.add(nb.math("DIVIDE", X, 0.03), nb.math("DIVIDE", ang, math.tau / 3)))
        strand = nb.smooth(nb.math("ABSOLUTE", nb.sub(twist, 0.5)), 0.5, 0.15)
        col = nb.hsv(srgb(color), 0.5, 1.0, nb.maprange(strand, 0, 1, 0.75, 1.05))
        return dict(color=col, rough=0.8, height=strand, height_scale=0.002)

    return pbr_material(name, fn, res=256, ao_distance=0.05, uv="keep")


def net_material():
    """菱目のネット（1 m 角の模様を並べる。糸の外は抜く）"""
    def fn(nb):
        uv = nb.uv("Proc")
        u, v, _ = nb.sep(uv)
        a = nb.math("DIVIDE", nb.add(u, v), MESH)
        b = nb.math("DIVIDE", nb.sub(u, v), MESH)
        da = nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", a), 0.5))
        db = nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", b), 0.5))
        # 糸からの距離（m）
        la = nb.mul(nb.sub(0.5, da), MESH * 0.7071)
        lb = nb.mul(nb.sub(0.5, db), MESH * 0.7071)
        dmin = nb.math("MINIMUM", la, lb)
        twine = nb.smooth(dmin, 0.0024, 0.0016)
        knot = nb.mul(nb.smooth(la, 0.004, 0.002), nb.smooth(lb, 0.004, 0.002))
        alpha = nb.math("MAXIMUM", twine, knot)
        n = nb.noise(nb.mapping(uv, scale=(1, 1, 1)), 4.0, 3, 0.5)
        col = nb.mix(srgb("#d8d6cb"), srgb("#b9b8a8"), n)
        return dict(color=col, rough=0.85, alpha=alpha, height=alpha, height_scale=0.001)

    return pbr_material("SwimNetMesh", fn, res=1024, uv="atlas", atlas_size=(1.0, 1.0), double_sided=True)


def pier_float_material(name="PierFloat"):
    """白い高密度ポリエチレンのキューブフロート。上面の滑り止めの粒、水際の藻"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        n = nb.noise(co, 2.0, 4, 0.55)
        col = nb.mix(srgb("#e9e7df"), srgb("#d3d0c3"), nb.smooth(n, 0.35, 0.7))
        # 上面の粒（6 cm 間隔）
        gx = nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.math("DIVIDE", X, 0.0625)), 0.5))
        gy = nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.math("DIVIDE", Y, 0.0625)), 0.5))
        dot = nb.smooth(nb.vmath("LENGTH", nb.comb(gx, gy, 0.0), out=1), 0.22, 0.14)
        top = nb.smooth(Z, CUBE_TOP - 0.02, CUBE_TOP - 0.005)
        dirt = nb.mul(nb.smooth(nb.noise(co, 5.0, 4, 0.6), 0.5, 0.7), top)
        col = nb.mix(col, srgb("#9e9682"), nb.mul(dirt, 0.4))
        # 足跡の砂、目地の汚れ
        fx = nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.math("DIVIDE", X, CUBE)), 0.5))
        fy = nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.math("DIVIDE", Y, CUBE)), 0.5))
        seam = nb.smooth(nb.math("MAXIMUM", fx, fy), 0.44, 0.5)
        col = nb.mix(col, srgb("#7d7565"), nb.mul(seam, 0.5))
        edge = nb.add(Z, nb.mul(nb.sub(n, 0.5), 0.06))
        algae = nb.smooth(edge, 0.02, -0.06)
        col = nb.mix(col, nb.mix(srgb("#3d4a26"), srgb("#5d6b34"), n), nb.mul(algae, 0.85))
        rough = nb.mixf(nb.mixf(0.5, 0.7, dot), 0.45, algae)
        height = nb.add(nb.mul(dot, nb.mul(top, 1.0)), nb.mul(n, 0.1))
        return dict(color=col, rough=rough, height=height, height_scale=0.003)

    return pbr_material(name, fn, res=2048, ao_distance=0.3)


# ---------------------------------------------------------------------------
# ネット
# ---------------------------------------------------------------------------
def swim_net():
    floats, ropes, leads, net = [], [], [], []
    # 浮き（1 m おき、ロープが中を通る円筒）
    for k in range(10):
        x = -HALF + 0.5 + k
        prof = [(0.0, -0.175), (0.09, -0.175), (0.115, -0.15), (0.12, -0.1), (0.12, 0.1), (0.115, 0.15),
                (0.09, 0.175), (0.0, 0.175)]
        f = T.lathe(f"float{k}", prof, segs=16)
        f.matrix_world = Matrix.Translation((x, 0, 0.03)) @ Matrix.Rotation(math.pi / 2, 4, "Y")
        C.set_smooth(f, True, angle=40)
        floats.append(f)
    # 浮きのロープ（浮きの間でわずかに沈む）
    pts = []
    for i in range(101):
        x = -HALF + i * 0.1
        sag = 0.035 * math.sin(math.pi * ((x + HALF) % 1.0)) ** 2
        pts.append(Vector((x, 0.0, 0.03 - sag)))
    ropes.append(geo.tube_along(pts, [0.009] * len(pts), sides=8, name="float_rope", cap_start=False, cap_end=False))
    # ネットの下端のおもりのロープ
    pts = [Vector((-HALF + i * 0.25, 0.0, NET_BOTTOM + 0.02 * math.sin(i))) for i in range(41)]
    leads.append(geo.tube_along(pts, [0.008] * len(pts), sides=6, name="lead_rope", cap_start=False, cap_end=False))
    # ネット（潮でわずかに沖へふくらむ）
    nx, nz = 40, 12
    verts, faces, uvs = [], [], []
    for j in range(nz + 1):
        for i in range(nx + 1):
            x = -HALF + 2 * HALF * i / nx
            z = 0.0 + (NET_BOTTOM - 0.0) * j / nz
            bulge = -0.25 * math.sin(math.pi * j / nz)
            verts.append((x, bulge, z))
    for j in range(nz):
        for i in range(nx):
            a = j * (nx + 1) + i
            faces.append((a, a + 1, a + nx + 2, a + nx + 1))
            uvs.append([(2 * HALF * i / nx, -NET_BOTTOM * (1 - j / nz)),
                        (2 * HALF * (i + 1) / nx, -NET_BOTTOM * (1 - j / nz)),
                        (2 * HALF * (i + 1) / nx, -NET_BOTTOM * (1 - (j + 1) / nz)),
                        (2 * HALF * i / nx, -NET_BOTTOM * (1 - (j + 1) / nz))])
    n = C.mesh_object("net", verts, faces, uvs)
    C.set_smooth(n, True)
    net.append(n)
    out = []
    for objs, mat in ((floats, float_material()), (ropes, rope_material("SwimNetRope", "#2f6fb0")),
                      (leads, rope_material("SwimNetLead", "#3b3a36")), (net, net_material())):
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return out


# ---------------------------------------------------------------------------
# 浮き桟橋
# ---------------------------------------------------------------------------
def floating_pier(ladder=True, seed=9):
    rnd = random.Random(seed)
    cubes, metal = [], []
    x0 = -PIER_NX * CUBE / 2
    y0 = -PIER_NY * CUBE / 2
    h = 0.4
    for i in range(PIER_NX):
        for j in range(PIER_NY):
            cx = x0 + (i + 0.5) * CUBE
            cy = y0 + (j + 0.5) * CUBE
            c = geo.box(f"cube{i}_{j}", (CUBE - 0.012, CUBE - 0.012, h),
                        loc=(cx, cy, CUBE_TOP - h / 2 + rnd.uniform(-0.006, 0.006)),
                        rot=(rnd.uniform(-0.006, 0.006), rnd.uniform(-0.006, 0.006), 0))
            geo.bevel(c, width=0.035, segments=2)
            cubes.append(c)
    # 角の連結の耳（4 個ずつの継ぎ目に丸い留め具）
    for i in range(PIER_NX + 1):
        for j in range(PIER_NY + 1):
            if (i + j) % 2:
                continue
            p = T.lathe(f"pin{i}_{j}", [(0.0, 0.0), (0.045, 0.0), (0.045, 0.012), (0.03, 0.02), (0.0, 0.02)], segs=10)
            p.location = (x0 + i * CUBE, y0 + j * CUBE, CUBE_TOP - 0.01)
            cubes.append(p)
    # 海側の端のはしご（ladder のとき）と、両脇のクリート
    xe = -x0
    for sx in ((-1, 1) if ladder else ()):
        y = sx * 0.25
        pts = list(geo.bezier_points((xe - 0.15, y, CUBE_TOP), (xe - 0.15, y, CUBE_TOP + 0.65),
                                     (xe + 0.25, y, CUBE_TOP + 0.65), (xe + 0.25, y, CUBE_TOP + 0.2), 10))
        pts.append(Vector((xe + 0.25, y, -1.2)))
        metal.append(geo.tube_along(pts, [0.021] * len(pts), sides=10, name=f"ladder{sx}"))
    for k, z in enumerate((-0.15, -0.45, -0.75, -1.05) if ladder else ()):
        metal.append(T.rod(f"rung{k}", (xe + 0.25, -0.25, z), (xe + 0.25, 0.25, z), 0.016, sides=8))
    for k, x in enumerate((-4.5, 0.0, 4.5)):
        for sy in (-1, 1):
            c = geo.box(f"cleat{k}{sy}", (0.25, 0.04, 0.05), loc=(x, sy * (-y0 - 0.08), CUBE_TOP + 0.06))
            geo.bevel(c, 0.012, 2)
            metal.append(c)
            for dx in (-0.08, 0.08):
                metal.append(T.rod(f"cleat_leg{k}{sy}{dx}", (x + dx, sy * (-y0 - 0.08), CUBE_TOP),
                                   (x + dx, sy * (-y0 - 0.08), CUBE_TOP + 0.05), 0.015, sides=8))
    out = []
    tag = "" if ladder else "Walk"
    for objs, mat in ((cubes, pier_float_material("PierFloat" + tag)),
                      (metal, galvanized("PierLadder" + tag, res=256, white_rust=0.2))):
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return out


def build():
    return {"SwimNet": swim_net(), "FloatingPier": floating_pier(), "FloatingPier_Walk": floating_pier(False, 10)}
