"""安良波公園の東屋・バーベキューの屋根・街灯・ベンチ

  Pergola        : 太い角柱から Y 字の方杖が 4 方へ伸び、格子の平らな屋根（半透明の板）を支える東屋。
                   柱を囲む木の腰掛けと、角の小さな腰掛け 2 つ、正方形の平板の床
  UmbrellaGazebo : 1 本柱に茶色の方形（四角錐）屋根を載せた傘形の東屋。柱のまわりのテーブルと 4 方のベンチ
  BBQShelter     : 4 本の鉄の柱に白い幕の切妻屋根。木のテーブルとベンチ、炭火のコンロ
  ParkLamp       : 曲がった腕の先に灯具の付く公園の街灯
  ParkBench      : コンクリートの脚に木の座板と背板のベンチ

座標: 原点 = 床の中心の地面。ベンチは座る向きが -Y
"""
import math
import random

from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import concrete, galvanized, painted_metal
from . import _timber as T

PREVIEW = dict(cam_dir=(-0.8, -1.0, 0.5), lens=50, spacing=1.25)


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def wood_material(name, light="#9a7a58", dark="#6a4e36", silver=0.35):
    """塗装の褪せた木（WoodCo の木目）"""
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        g = T.wood_grain(nb, co, rings=16.0, knots=0.25, crack_amt=0.8)
        col = nb.ramp(g["var"], [(0.2, srgb(dark)), (0.8, srgb(light))])
        col = nb.mix(col, nb.hsv(srgb(dark), 0.5, 1.0, 0.7), nb.mul(g["ring"], 0.45))
        col = nb.mix(col, srgb("#2e2218"), nb.mul(g["crack"], 0.8))
        col = nb.mix(col, srgb("#3a2a1e"), nb.mul(g["knot"], 0.6))
        oc = nb.coord("Object")
        sv = nb.smooth(nb.noise(oc, 1.5, 4, 0.6), 0.4, 0.7)
        col = nb.mix(col, srgb("#9a948a"), nb.mul(sv, silver))
        h = nb.add(nb.mul(g["fib"], 0.4), nb.mul(g["ring"], 0.5))
        h = nb.sub(h, nb.mul(g["crack"], 1.5))
        return dict(color=col, rough=nb.maprange(g["fib"], 0.3, 0.7, 0.7, 0.88), height=h, height_scale=0.003,
                    cavity=nb.maprange(g["crack"], 0, 1, 1.0, 0.5))

    return pbr_material(name, fn, res=1024, ao_distance=0.5)


def panel_material():
    """格子屋根の半透明の板（白く曇ったポリカーボネートに砂ぼこり）"""
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 1.0, 4, 0.55)
        col = nb.mix(srgb("#c8cbbf"), srgb("#e2e4da"), nb.smooth(n, 0.35, 0.7))
        dirt = nb.smooth(nb.noise(co, 3.0, 4, 0.6), 0.55, 0.72)
        col = nb.mix(col, srgb("#8f8a78"), nb.mul(dirt, 0.35))
        return dict(color=col, rough=0.35, height=nb.noise(co, 20.0, 3, 0.6), height_scale=0.0005)

    return pbr_material("ShelterPanel", fn, res=512, ao_distance=0.6)


def shingle_material():
    """方形屋根の茶色の板葺き（横の段と縦の目地）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        nrm = nb.node("ShaderNodeNewGeometry").outputs["Normal"]
        nx, ny, nz = nb.sep(nrm)
        across = nb.mixf(X, Y, nb.math("GREATER_THAN", nb.math("ABSOLUTE", nx), nb.math("ABSOLUTE", ny)))
        row = nb.math("FLOOR", nb.math("DIVIDE", Z, 0.12))
        fr = nb.math("FRACT", nb.math("DIVIDE", Z, 0.12))
        u = nb.add(nb.math("DIVIDE", across, 0.22), nb.mul(nb.math("FLOORED_MODULO", row, 2.0), 0.5))
        fu = nb.math("FRACT", u)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(nb.comb(nb.math("FLOOR", u), row, 0.0), wn.inputs["Vector"])
        h1 = wn.outputs["Value"]
        col = nb.ramp(h1, [(0.0, srgb("#4a3424")), (0.5, srgb("#5e4430")), (1.0, srgb("#70543c"))])
        fade = nb.smooth(nb.noise(co, 1.0, 3, 0.5), 0.45, 0.75)
        col = nb.mix(col, srgb("#7c6a58"), nb.mul(fade, 0.4))
        gap = nb.math("MAXIMUM", nb.smooth(nb.math("ABSOLUTE", nb.sub(fu, 0.5)), 0.47, 0.5), nb.smooth(fr, 0.9, 1.0))
        col = nb.mix(col, srgb("#1e140e"), nb.mul(gap, 0.7))
        height = nb.sub(fr, nb.mul(gap, 0.5))
        return dict(color=col, rough=0.85, height=height, height_scale=0.01,
                    cavity=nb.maprange(gap, 0, 1, 1.0, 0.6))

    return pbr_material("ShelterShingle", fn, res=1024, ao_distance=0.5)


def canvas_material():
    """白い幕（張った布の皺と雨じみ）"""
    def fn(nb):
        co = nb.coord("Object")
        wv = nb.wave(co, 260.0, 0.0, 0.0, kind="BANDS", direction="X")
        n = nb.noise(co, 1.2, 4, 0.55)
        col = nb.mix(srgb("#dcdcd4"), srgb("#f2f2ee"), nb.smooth(n, 0.35, 0.7))
        stain = nb.smooth(nb.noise(nb.mapping(co, scale=(1, 1, 2)), 2.5, 4, 0.6), 0.55, 0.72)
        col = nb.mix(col, srgb("#9c9886"), nb.mul(stain, 0.5))
        # 雨水の流れた跡と、縁の黒カビ・砂ぼこり
        run = nb.smooth(nb.noise(nb.mapping(co, scale=(9, 9, 0.7)), 1.5, 4, 0.6), 0.55, 0.7)
        col = nb.mix(col, srgb("#7e7b6f"), nb.mul(run, 0.3))
        mold = nb.smooth(nb.noise(co, 6.0, 5, 0.6), 0.62, 0.74)
        col = nb.mix(col, srgb("#55574e"), nb.mul(mold, 0.35))
        height = nb.add(nb.add(nb.mul(wv, 0.3), nb.mul(n, 0.4)), nb.mul(nb.noise(co, 40.0, 3, 0.6), 0.2))
        return dict(color=col, rough=0.85, height=height, height_scale=0.003)

    return pbr_material("ShelterCanvas", fn, res=1024, double_sided=True, ao_distance=0.5)


def floor_material():
    """東屋の床の正方形の平板（明るい灰色）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        t = 0.45
        fu, fv = nb.math("FRACT", nb.math("DIVIDE", X, t)), nb.math("FRACT", nb.math("DIVIDE", Y, t))
        edge = nb.mul(nb.math("MINIMUM", nb.math("MINIMUM", fu, nb.sub(1.0, fu)), nb.math("MINIMUM", fv, nb.sub(1.0, fv))), t)
        joint = nb.smooth(edge, 0.006, 0.002)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(nb.comb(nb.math("FLOOR", nb.math("DIVIDE", X, t)), nb.math("FLOOR", nb.math("DIVIDE", Y, t)), 0.0),
                wn.inputs["Vector"])
        col = nb.ramp(wn.outputs["Value"], [(0.0, srgb("#b2aea4")), (1.0, srgb("#cdc9be"))])
        dirt = nb.smooth(nb.noise(co, 1.0, 4, 0.6), 0.5, 0.75)
        col = nb.mix(col, srgb("#8a8476"), nb.mul(dirt, 0.3))
        col = nb.mix(col, srgb("#6a665e"), joint)
        return dict(color=col, rough=nb.mixf(0.8, 0.92, joint), height=nb.smooth(edge, 0.0, 0.01), height_scale=0.003,
                    cavity=nb.maprange(joint, 0, 1, 1.0, 0.7))

    return pbr_material("ShelterFloor", fn, res=1024, ao_distance=0.6)


def lamp_glass_material():
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 6.0, 3, 0.5)
        col = nb.mix(srgb("#d8d8d0"), srgb("#f0eee6"), n)
        return dict(color=col, rough=0.3)

    return pbr_material("LampGlass", fn, res=256, ao_distance=0.2)


# ---------------------------------------------------------------------------
# 形
# ---------------------------------------------------------------------------
def _assign(groups):
    out = []
    for objs, mat in groups:
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return out


def pergola(rnd):
    wood, frame, panel, fl = [], [], [], []
    H = 3.0
    S = 4.6
    post = T.board("post", (0.26, 0.26, H + 0.1), loc=(0, 0, (H + 0.1) / 2 - 0.05), rnd=rnd, bevel=0.01, axis="Z")
    wood.append(post)
    # Y 字の方杖（柱の上部から 4 方の対角へ）
    for k in range(4):
        a = math.radians(45 + 90 * k)
        d = Vector((math.cos(a), math.sin(a), 0))
        z0 = H - 1.3
        p0 = Vector((0, 0, z0)) + d * 0.12
        p1 = d * 1.1 + Vector((0, 0, H - 0.18))
        mid = p0.lerp(p1, 0.45)
        for name, a0, a1, w in (("arm", p0, p1, 0.16), ("fork", mid, d * 1.75 + Vector((0, 0, H - 0.18)), 0.12)):
            L = (a1 - a0).length
            b = T.board(f"{name}{k}", (0.14, w, L), rnd=rnd, bevel=0.008, axis="Z")
            T.orient(b, (a0 + a1) / 2, a1 - a0)
            C.apply_transform(b)
            wood.append(b)
        side = Vector((-d.y, d.x, 0))
        for s in (-1, 1):
            a1 = d * 1.2 + side * s * 0.9 + Vector((0, 0, H - 0.18))
            L = (a1 - mid).length
            b = T.board(f"fork{k}{s}", (0.1, 0.1, L), rnd=rnd, bevel=0.006, axis="Z")
            T.orient(b, (mid + a1) / 2, a1 - mid)
            C.apply_transform(b)
            wood.append(b)
    # 格子の屋根（外枠・桟・半透明の板）
    z = H
    for s in (-1, 1):
        frame.append(geo.box(f"rimx{s}", (S, 0.12, 0.2), loc=(0, s * (S / 2 - 0.06), z)))
        frame.append(geo.box(f"rimy{s}", (0.12, S, 0.2), loc=(s * (S / 2 - 0.06), 0, z)))
    for k in range(1, 6):
        t = -S / 2 + S * k / 6
        frame.append(geo.box(f"barx{k}", (S, 0.07, 0.14), loc=(0, t, z)))
        frame.append(geo.box(f"bary{k}", (0.07, S, 0.14), loc=(t, 0, z)))
    for b in frame:
        geo.bevel(b, 0.008, 1)
    panel.append(geo.box("roofpanel", (S - 0.1, S - 0.1, 0.012), loc=(0, 0, z + 0.075)))
    # 柱を囲む腰掛け、角の腰掛け
    for k in range(4):
        a = math.radians(90 * k)
        d = Vector((math.cos(a), math.sin(a), 0))
        side = Vector((-d.y, d.x, 0))
        top = T.board(f"seat{k}", (abs(d.x) * 0.5 + abs(d.y) * 1.3, abs(d.y) * 0.5 + abs(d.x) * 1.3, 0.06),
                      loc=d * 0.4 + side * 0.4 + Vector((0, 0, 0.44)), rnd=rnd)
        wood.append(top)
        wood.append(T.board(f"seatleg{k}", (0.1, 0.1, 0.42), loc=d * 0.6 + side * 0.85 + Vector((0, 0, 0.21)), rnd=rnd,
                            axis="Z"))
    for k, (x, y) in enumerate(((1.6, -1.3), (-1.4, 1.6))):
        wood.append(T.board(f"stool{k}", (0.42, 0.42, 0.06), loc=(x, y, 0.43), rnd=rnd))
        wood.append(T.board(f"stoolleg{k}", (0.12, 0.12, 0.4), loc=(x, y, 0.2), rnd=rnd, axis="Z"))
    slab = geo.box("floor", (5.4, 5.4, 0.1), loc=(0, 0, 0.05))
    fl.append(slab)
    return _assign([
        (wood, wood_material("PergolaWood", "#8a6a4a", "#5e432e", 0.3)),
        (frame, painted_metal("PergolaFrame", "#dcdcd2", res=512, chip=0.12, gloss=0.4)),
        (panel, panel_material()),
        (fl, floor_material()),
    ])


def umbrella(rnd):
    wood, roof, fl = [], [], []
    S = 4.2
    ze, zt = 2.35, 3.55
    wood.append(T.board("post", (0.22, 0.22, zt + 0.05), loc=(0, 0, (zt + 0.05) / 2 - 0.05), rnd=rnd, bevel=0.01,
                        axis="Z"))
    corners = [Vector((S / 2 * sx, S / 2 * sy, ze)) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    apex = Vector((0, 0, zt))
    # 方杖と隅木
    for k, c in enumerate(corners):
        p0 = Vector((0, 0, ze - 0.9))
        p1 = Vector((c.x * 0.5, c.y * 0.5, (ze + zt) / 2 - 0.12))
        L = (p1 - p0).length
        b = T.board(f"brace{k}", (0.1, 0.1, L), rnd=rnd, bevel=0.006, axis="Z")
        T.orient(b, (p0 + p1) / 2, p1 - p0)
        C.apply_transform(b)
        wood.append(b)
        L = (c - apex).length
        b = T.board(f"hip{k}", (0.12, 0.14, L), rnd=rnd, bevel=0.006, axis="Z")
        T.orient(b, (c + apex) / 2 - Vector((0, 0, 0.1)), c - apex)
        C.apply_transform(b)
        wood.append(b)
    for k in range(4):
        a, b = corners[k], corners[(k + 1) % 4]
        roof.append(C.mesh_object(f"roof{k}", [tuple(a), tuple(b), tuple(apex)], [(0, 1, 2)]))
        L = (b - a).length
        e = T.board(f"eave{k}", (L, 0.05, 0.16), rnd=rnd, axis="X")
        e.rotation_euler = (0, 0, math.atan2((b - a).y, (b - a).x))
        e.location = (a + b) / 2 + Vector((0, 0, -0.06))
        C.apply_transform(e)
        wood.append(e)
    # 軒裏（下から見える板）
    for k in range(4):
        a, b = corners[k], corners[(k + 1) % 4]
        o = Vector((0, 0, -0.04))
        roof.append(C.mesh_object(f"under{k}", [tuple(a + o), tuple(apex + o), tuple(b + o)], [(0, 1, 2)]))
    cap = T.lathe("cap", [(0.0, 0.0), (0.12, 0.0), (0.1, 0.12), (0.03, 0.2), (0.0, 0.22)], segs=8)
    cap.location = apex
    wood.append(cap)
    # テーブルとベンチ
    wood.append(T.board("table", (1.2, 1.2, 0.06), loc=(0, 0, 0.72), rnd=rnd))
    for s in (-1, 1):
        wood.append(T.board(f"tleg{s}", (0.08, 1.0, 0.68), loc=(s * 0.45, 0, 0.36), rnd=rnd, axis="Z"))
    for k in range(4):
        a = math.radians(90 * k)
        d = Vector((math.cos(a), math.sin(a), 0))
        side = Vector((-d.y, d.x, 0))
        seat = T.board(f"bench{k}", (abs(d.y) * 1.4 + abs(d.x) * 0.35, abs(d.x) * 1.4 + abs(d.y) * 0.35, 0.05),
                       loc=d * 1.15 + Vector((0, 0, 0.43)), rnd=rnd)
        wood.append(seat)
        for s in (-1, 1):
            wood.append(T.board(f"bleg{k}{s}", (0.07, 0.07, 0.41), loc=d * 1.15 + side * s * 0.55 + Vector((0, 0, 0.205)),
                                rnd=rnd, axis="Z"))
    fl.append(geo.box("floor", (4.6, 4.6, 0.1), loc=(0, 0, 0.05)))
    return _assign([
        (wood, wood_material("UmbrellaWood", "#7a5a3e", "#4e3624", 0.35)),
        (roof, shingle_material()),
        (fl, concrete("UmbrellaFloor", res=1024, tint="#b4b0a6", salt=0.05)),
    ])


def bbq(rnd):
    metal, canvas, wood, grill = [], [], [], []
    W, L, H = 3.2, 3.8, 2.3
    for sx in (-1, 1):
        for sy in (-1, 1):
            metal.append(T.rod(f"post{sx}{sy}", (sx * W / 2, sy * L / 2, 0.0), (sx * W / 2, sy * L / 2, H), 0.03, sides=10))
    for sy in (-1, 1):
        metal.append(geo.tube_along([Vector((-W / 2, sy * L / 2, H)), Vector((0, sy * L / 2, H + 0.55)),
                                     Vector((W / 2, sy * L / 2, H))], [0.025] * 3, sides=8, name=f"rafter{sy}"))
    metal.append(T.rod("ridge", (0, -L / 2, H + 0.55), (0, L / 2, H + 0.55), 0.025, sides=8))
    for sx in (-1, 1):
        metal.append(T.rod(f"eave{sx}", (sx * W / 2, -L / 2, H), (sx * W / 2, L / 2, H), 0.022, sides=8))
    # 幕（少したるんだ切妻）
    nx, ny = 6, 8
    for sx in (-1, 1):
        verts, faces = [], []
        for j in range(ny + 1):
            y = -L / 2 - 0.1 + (L + 0.2) * j / ny
            for i in range(nx + 1):
                t = i / nx
                x = sx * (W / 2 + 0.12) * t
                sag = 0.05 * math.sin(math.pi * t) * (1 - abs(2 * j / ny - 1) * 0.5)
                z = H + 0.58 - 0.58 * t - 0.06 * t - sag
                verts.append((x, y, z))
        for j in range(ny):
            for i in range(nx):
                a = j * (nx + 1) + i
                faces.append((a, a + 1, a + nx + 2, a + nx + 1) if sx > 0 else (a, a + nx + 1, a + nx + 2, a + 1))
        o = C.mesh_object(f"canvas{sx}", verts, faces)
        C.set_smooth(o, True)
        canvas.append(o)
    # 垂れ幕のふち
    for sx in (-1, 1):
        canvas.append(C.mesh_object(f"valance{sx}", [(sx * (W / 2 + 0.12), -L / 2 - 0.1, H - 0.06),
                                                     (sx * (W / 2 + 0.12), L / 2 + 0.1, H - 0.06),
                                                     (sx * (W / 2 + 0.12), L / 2 + 0.1, H - 0.28),
                                                     (sx * (W / 2 + 0.12), -L / 2 - 0.1, H - 0.28)],
                                    [(0, 3, 2, 1) if sx > 0 else (0, 1, 2, 3)]))
    # テーブルとベンチ（板を並べる）
    for k in range(5):
        wood.append(T.board(f"top{k}", (0.15, 1.8, 0.045), loc=(-0.32 + k * 0.16, 0.0, 0.74), rnd=rnd, axis="Y"))
    for s in (-1, 1):
        wood.append(T.board(f"tleg{s}", (0.8, 0.07, 0.72), loc=(0, s * 0.7, 0.36), rnd=rnd, axis="Z"))
        for k in range(2):
            wood.append(T.board(f"seat{s}{k}", (0.14, 1.8, 0.045), loc=(s * (0.72 + k * 0.15), 0.0, 0.44), rnd=rnd,
                                axis="Y"))
        for y in (-0.7, 0.7):
            wood.append(T.board(f"sleg{s}{y}", (0.3, 0.06, 0.42), loc=(s * 0.8, y, 0.21), rnd=rnd, axis="Z"))
    # 炭火のコンロ
    gx, gy = 0.0, L / 2 - 0.55
    box = geo.box("grill_box", (0.9, 0.42, 0.22), loc=(gx, gy, 0.78))
    geo.bevel(box, 0.01, 1)
    grill.append(box)
    for sx in (-1, 1):
        for sy in (-1, 1):
            grill.append(T.rod(f"gleg{sx}{sy}", (gx + sx * 0.4, gy + sy * 0.17, 0.0), (gx + sx * 0.4, gy + sy * 0.17, 0.7),
                               0.012, sides=6))
    for k in range(9):
        metal.append(T.rod(f"net{k}", (gx - 0.42, gy - 0.18 + k * 0.045, 0.9), (gx + 0.42, gy - 0.18 + k * 0.045, 0.9),
                           0.004, sides=4))
    return _assign([
        (metal, galvanized("BBQMetal", res=512)),
        (canvas, canvas_material()),
        (wood, wood_material("BBQWood", "#a07c58", "#6e5038", 0.45)),
        (grill, painted_metal("BBQGrill", "#2a2a2a", res=256, chip=0.3, gloss=0.6)),
    ])


def lamp(rnd):
    metal, glass = [], []
    H = 4.6
    pole = T.lathe("pole", [(0.0, 0.0), (0.11, 0.0), (0.11, 0.35), (0.07, 0.42), (0.065, H - 0.4), (0.05, H),
                            (0.0, H)], segs=12)
    metal.append(pole)
    pts = geo.bezier_points(Vector((0, 0, H - 0.3)), Vector((0, 0, H + 0.3)), Vector((0.3, 0, H + 0.55)),
                            Vector((0.85, 0, H + 0.45)), 10)
    metal.append(geo.tube_along(pts, [0.045 - 0.015 * i / 9 for i in range(10)], sides=10, name="arm"))
    head = T.lathe("head", [(0.0, 0.18), (0.2, 0.12), (0.24, 0.02), (0.22, 0.0), (0.0, 0.0)], segs=16)
    head.location = (0.95, 0, H + 0.33)
    metal.append(head)
    g = T.lathe("glass", [(0.0, 0.0), (0.2, 0.0), (0.16, -0.08), (0.0, -0.1)], segs=16)
    g.location = (0.95, 0, H + 0.33)
    glass.append(g)
    base = geo.cylinder("base", 0.22, 0.12, loc=(0, 0, 0.06), verts=12)
    return _assign([
        (metal, painted_metal("LampPaint", "#3c4044", res=512, chip=0.1, gloss=0.45)),
        (glass, lamp_glass_material()),
        ([base], concrete("LampBase", res=256, tint="#a8a49a")),
    ])


def bench(rnd):
    wood, conc = [], []
    L = 1.8
    for k in range(3):
        wood.append(T.board(f"seat{k}", (L, 0.12, 0.045), loc=(0, -0.12 + k * 0.14, 0.44), rnd=rnd))
    for k in range(2):
        b = T.board(f"back{k}", (L, 0.11, 0.04), rnd=rnd)
        b.rotation_euler = (math.radians(-75), 0, 0)
        b.location = (0, 0.24 + k * 0.03, 0.62 + k * 0.15)
        C.apply_transform(b)
        wood.append(b)
    for s in (-1, 1):
        leg = geo.box(f"leg{s}", (0.12, 0.5, 0.42), loc=(s * (L / 2 - 0.25), 0.0, 0.21))
        geo.bevel(leg, 0.02, 2)
        conc.append(leg)
        bk = geo.box(f"backleg{s}", (0.1, 0.08, 0.5), loc=(s * (L / 2 - 0.25), 0.25, 0.65))
        bk.rotation_euler = (math.radians(-12), 0, 0)
        C.apply_transform(bk)
        conc.append(bk)
    return _assign([
        (wood, wood_material("BenchWood", "#a48058", "#6e5034", 0.4)),
        (conc, concrete("BenchConcrete", res=512, tint="#b0aca2", salt=0.05)),
    ])


def build():
    rnd = random.Random(5)
    return {
        "Pergola": pergola(rnd),
        "UmbrellaGazebo": umbrella(rnd),
        "BBQShelter": bbq(rnd),
        "ParkLamp": lamp(rnd),
        "ParkBench": bench(rnd),
    }
