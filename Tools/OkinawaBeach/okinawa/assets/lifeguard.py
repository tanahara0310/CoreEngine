"""アラハビーチの監視台

遊歩道から浜へ張り出したコンクリートの台に、水色の監視小屋・手すり・旗竿・浮き輪。

座標: z=0 が遊歩道の舗装面（護岸 seawall と同じ）、y=0 が護岸の前面の線。
台は -Y（海側）へ 3.2 m 張り出し、前面と側面の壁は浜の砂（z=-0.9）の下まで下がる。
原点は台の上面・護岸の線の中央。
"""
import math

from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import concrete, galvanized, painted_metal, paving
from . import _timber as T

SAND_Z = -0.9


def _preview_extra(objs):
    """浜の砂の高さを床に合わせて撮る"""
    for o in objs:
        if o.parent is None:
            o.location.z -= SAND_Z


PREVIEW = dict(cam_dir=(-0.75, -1.0, 0.45), lens=40, extra=_preview_extra)

DECK_W = 4.6           # X
DECK_D = 3.2           # 海側への張り出し
WALL_BOTTOM = -1.4
RAIL_H = 1.1
HUT_W, HUT_D, HUT_H = 2.1, 1.8, 2.35
HUT_X, HUT_Y = 0.7, -1.3


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def glass_material():
    """小屋の窓ガラス（内側の暗さと空の映り込み）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        grad = nb.smooth(Z, 0.9, 2.2)
        col = nb.mix(srgb("#1d2a33"), srgb("#4a6070"), grad)
        smudge = nb.smooth(nb.noise(co, 6.0, 4, 0.6), 0.55, 0.7)
        col = nb.mix(col, srgb("#7a8288"), nb.mul(smudge, 0.25))
        rough = nb.mixf(0.06, 0.3, smudge)
        return dict(color=col, rough=rough, metal=0.0)

    return pbr_material("LifeguardGlass", fn, res=256, ao_distance=0.3)


def hut_material():
    """水色に塗った鋼板の小屋。縦の折り目（リブ）と褪せ"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        nrm = nb.node("ShaderNodeNewGeometry").outputs["Normal"]
        nx, ny, nz = nb.sep(nrm)
        # 面の向きで水平の座標を選んでリブを作る
        hor = nb.mixf(X, Y, nb.math("GREATER_THAN", nb.math("ABSOLUTE", nx), 0.5))
        rib = nb.math("FRACT", nb.math("DIVIDE", hor, 0.15))
        ribh = nb.smooth(nb.math("ABSOLUTE", nb.sub(rib, 0.5)), 0.42, 0.36)
        fade = nb.noise(co, 1.2, 3, 0.5)
        paint = nb.mix(srgb("#7fc3dc"), srgb("#a4d6e6"), nb.smooth(fade, 0.35, 0.75))
        streak = nb.noise(nb.mapping(co, scale=(20.0, 20.0, 0.8)), 1.4, 4, 0.55)
        paint = nb.mix(paint, srgb("#5f8f9f"), nb.mul(nb.smooth(streak, 0.58, 0.74), 0.35))
        # 下端の砂ぼこりと錆の垂れ
        dirt = nb.smooth(Z, 0.5, 0.05)
        paint = nb.mix(paint, srgb("#a99f86"), nb.mul(dirt, 0.5))
        n = nb.noise(co, 9.0, 6, 0.65)
        chips = nb.mul(nb.smooth(n, 0.7, 0.72), nb.smooth(Z, 0.8, 0.1))
        rust = nb.mul(nb.smooth(streak, 0.6, 0.72), nb.smooth(nb.noise(nb.mapping(co, scale=(2, 2, 0.2)), 1.0, 2, 0.5), 0.6, 0.7))
        col = nb.mix(paint, srgb("#7a4020"), nb.math("MAXIMUM", chips, nb.mul(rust, 0.5)))
        rough = nb.mixf(0.45, 0.8, chips)
        height = nb.add(nb.mul(ribh, 1.0), nb.mul(nb.sub(1.0, chips), 0.1))
        return dict(color=col, rough=rough, height=height, height_scale=0.008)

    return pbr_material("LifeguardHut", fn, res=1024, ao_distance=0.6)


def flag_material():
    """青い遊泳旗（布）"""
    def fn(nb):
        co = nb.coord("Object")
        weave = nb.wave(nb.mapping(co, scale=(1, 1, 1)), 380.0, 0.0, 0.0, kind="BANDS", direction="Z")
        weave2 = nb.wave(co, 380.0, 0.0, 0.0, kind="BANDS", direction="X")
        fade = nb.noise(co, 2.0, 3, 0.5)
        col = nb.mix(srgb("#1f5fb8"), srgb("#4a83cc"), nb.smooth(fade, 0.4, 0.75))
        h = nb.add(nb.mul(weave, 0.5), nb.mul(weave2, 0.5))
        return dict(color=col, rough=0.85, height=h, height_scale=0.0008)

    return pbr_material("LifeguardFlag", fn, res=256, double_sided=True, ao_distance=0.2)


def buoy_material():
    """浮き輪（橙と白の帯）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        ang = nb.math("ARCTAN2", Z, X)
        band = nb.math("FRACT", nb.math("DIVIDE", nb.add(ang, math.pi / 8), math.pi / 2))
        white = nb.math("LESS_THAN", band, 0.25)
        dirt = nb.smooth(nb.noise(co, 8.0, 4, 0.6), 0.5, 0.7)
        col = nb.mix(srgb("#e8611f"), srgb("#eeeae0"), white)
        col = nb.mix(col, srgb("#8d7f68"), nb.mul(dirt, 0.3))
        return dict(color=col, rough=nb.mixf(0.45, 0.7, dirt), height=nb.noise(co, 60.0, 3, 0.5), height_scale=0.0008)

    return pbr_material("LifeguardBuoy", fn, res=256, ao_distance=0.2)


# ---------------------------------------------------------------------------
# 部品
# ---------------------------------------------------------------------------
def deck_parts():
    """台の上板（舗装）と、前面・側面の壁"""
    hx = DECK_W / 2
    top = C.mesh_object("deck_top", [(-hx, -DECK_D, 0), (hx, -DECK_D, 0), (hx, 0, 0), (-hx, 0, 0)], [(0, 1, 2, 3)])
    walls = []
    wt = 0.3
    h = -0.002 - WALL_BOTTOM
    walls.append(geo.box("wall_front", (DECK_W, wt, h), loc=(0, -DECK_D + wt / 2, WALL_BOTTOM + h / 2)))
    for sx in (-1, 1):
        walls.append(geo.box(f"wall_side{sx}", (wt, DECK_D - wt, h),
                             loc=(sx * (DECK_W / 2 - wt / 2), -(DECK_D - wt) / 2, WALL_BOTTOM + h / 2)))
    # 縁の笠木（角を丸めたコンクリート）
    coping = []
    for name, size, loc in (("cop_front", (DECK_W + 0.06, 0.32, 0.12), (0, -DECK_D + 0.13, 0.03)),
                            ("cop_l", (0.32, DECK_D - 0.2, 0.12), (-DECK_W / 2 + 0.13, -DECK_D / 2 + 0.1, 0.03)),
                            ("cop_r", (0.32, DECK_D - 0.2, 0.12), (DECK_W / 2 - 0.13, -DECK_D / 2 + 0.1, 0.03))):
        c = geo.box(name, size, loc=loc)
        geo.bevel(c, width=0.025, segments=2)
        coping.append(c)
    for w in walls:
        geo.bevel(w, width=0.01, segments=1)
    return [top], walls + coping


def railing():
    """笠木の上の手すり（亜鉛めっきのパイプ）"""
    parts = []
    z0 = 0.09
    pts = [(-DECK_W / 2 + 0.13, -0.05), (-DECK_W / 2 + 0.13, -DECK_D + 0.13),
           (DECK_W / 2 - 0.13, -DECK_D + 0.13), (DECK_W / 2 - 0.13, -0.05)]
    # 支柱
    posts = []
    for a, b in zip(pts[:-1], pts[1:]):
        a, b = Vector((*a, 0)), Vector((*b, 0))
        n = max(1, round((b - a).length / 1.1))
        for k in range(n + 1):
            p = a.lerp(b, k / n)
            if all((p - q).length > 0.05 for q in posts):
                posts.append(p)
    for i, p in enumerate(posts):
        parts.append(T.rod(f"post{i}", (p.x, p.y, z0), (p.x, p.y, RAIL_H), 0.03, sides=12))
        parts.append(T.lathe(f"base{i}", [(0.0, z0), (0.07, z0), (0.07, z0 + 0.01), (0.035, z0 + 0.03),
                                          (0.0, z0 + 0.03)], segs=12))
        parts[-1].location = (p.x, p.y, 0)
    # 横桟（上は太く角で曲げる）
    for z, r in ((RAIL_H, 0.026), (0.7, 0.018), (0.38, 0.018)):
        line = [Vector((x, y, z)) for x, y in pts]
        t = geo.tube_along(_round_corners(line, 0.12), [r] * len(_round_corners(line, 0.12)), sides=12,
                           name=f"rail{z}")
        parts.append(t)
    return parts


def _round_corners(pts, rad, segs=5):
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        a, b, c = pts[i - 1], pts[i], pts[i + 1]
        d0 = (a - b).normalized()
        d1 = (c - b).normalized()
        out += list(geo.bezier_points(b + d0 * rad, b + d0 * rad * 0.45, b + d1 * rad * 0.45, b + d1 * rad, segs))
    out.append(pts[-1])
    return out


def hut():
    """監視小屋。窓・扉・屋根・スピーカー・アンテナ"""
    body, white, glass, dark = [], [], [], []
    cx, cy = HUT_X, HUT_Y
    z0 = 0.12
    base = geo.box("hut_base", (HUT_W + 0.1, HUT_D + 0.1, z0), loc=(cx, cy, z0 / 2))
    geo.bevel(base, 0.01, 1)
    dark.append(base)
    b = geo.box("hut_body", (HUT_W, HUT_D, HUT_H), loc=(cx, cy, z0 + HUT_H / 2))
    geo.bevel(b, 0.015, 2)
    body.append(b)
    roof = geo.box("hut_roof", (HUT_W + 0.36, HUT_D + 0.36, 0.14), loc=(cx, cy, z0 + HUT_H + 0.07))
    geo.bevel(roof, 0.02, 2)
    white.append(roof)
    # 窓（海側は大きく、横は中くらい）
    zw0, zw1 = z0 + 1.0, z0 + 2.0

    def window(name, center, axis, width):
        """axis: 窓の面の外向き法線（±X か ±Y）"""
        ax = Vector(axis)
        side = Vector((-ax.y, ax.x, 0))
        c = Vector(center)
        h = zw1 - zw0
        g = geo.box(name + "_g", (width if ax.x == 0 else 0.02, width if ax.y == 0 else 0.02, h),
                    loc=c + ax * 0.005)
        glass.append(g)
        fw = 0.05
        for s in (-1, 1):
            p = c + side * s * (width / 2 + fw / 2) + ax * 0.02
            white.append(geo.box(name + f"_fv{s}", (fw if ax.x == 0 else 0.05, fw if ax.y == 0 else 0.05, h + fw * 2), loc=p))
        for s in (-1, 1):
            p = c + Vector((0, 0, s * (h / 2 + fw / 2))) + ax * 0.02
            white.append(geo.box(name + f"_fh{s}", (width + fw * 2 if ax.x == 0 else 0.05,
                                                    width + fw * 2 if ax.y == 0 else 0.05, fw), loc=p))
        # 中桟
        p = c + ax * 0.02
        white.append(geo.box(name + "_mid", (0.03 if ax.x == 0 else 0.04, 0.03 if ax.y == 0 else 0.04, h), loc=p))
        # 窓台
        p = c + Vector((0, 0, -h / 2 - 0.07)) + ax * 0.05
        white.append(geo.box(name + "_sill", (width + 0.16 if ax.x == 0 else 0.1, width + 0.16 if ax.y == 0 else 0.1, 0.04),
                             loc=p))

    zc = (zw0 + zw1) / 2
    window("wf", (cx, cy - HUT_D / 2, zc), (0, -1, 0), 1.6)
    window("wl", (cx - HUT_W / 2, cy, zc), (-1, 0, 0), 1.1)
    window("wr", (cx + HUT_W / 2, cy, zc), (1, 0, 0), 1.1)
    # 扉（陸側）
    dz = 1.95
    door = geo.box("door", (0.8, 0.03, dz), loc=(cx - 0.35, cy + HUT_D / 2 + 0.01, z0 + dz / 2 + 0.02))
    white.append(door)
    knob = T.rod("knob", (cx - 0.05, cy + HUT_D / 2 + 0.02, z0 + 1.0), (cx - 0.05, cy + HUT_D / 2 + 0.07, z0 + 1.0),
                 0.018, sides=8)
    dark.append(knob)
    dwin = geo.box("door_win", (0.5, 0.02, 0.5), loc=(cx - 0.35, cy + HUT_D / 2 + 0.03, z0 + 1.5))
    glass.append(dwin)
    # 屋根の上: スピーカー（ラッパ）2 個とアンテナ
    rz = z0 + HUT_H + 0.14
    mast = T.rod("spk_mast", (cx + 0.6, cy + 0.4, rz), (cx + 0.6, cy + 0.4, rz + 0.9), 0.025, sides=8)
    white.append(mast)
    for k, ang in enumerate((-0.6, 0.6)):
        d = Vector((math.sin(ang), -math.cos(ang), -0.15)).normalized()
        horn = T.lathe(f"horn{k}", [(0.0, 0.0), (0.05, 0.0), (0.06, 0.12), (0.17, 0.32), (0.17, 0.34), (0.0, 0.34)],
                       segs=14)
        T.orient(horn, (cx + 0.6, cy + 0.4, rz + 0.75), d)
        white.append(horn)
    ant = T.rod("antenna", (cx - 0.7, cy + 0.6, rz), (cx - 0.7, cy + 0.6, rz + 1.6), 0.008, sides=6)
    dark.append(ant)
    # エアコンの室外機（陸側の壁の下）
    ac = geo.box("ac", (0.7, 0.28, 0.55), loc=(cx + 0.55, cy + HUT_D / 2 + 0.2, z0 + 0.3))
    geo.bevel(ac, 0.01, 1)
    white.append(ac)
    fan = T.lathe("ac_fan", [(0.0, 0.0), (0.19, 0.0), (0.19, 0.01), (0.0, 0.01)], segs=16)
    T.orient(fan, (cx + 0.45, cy + HUT_D / 2 + 0.34, z0 + 0.3), (0, 1, 0))
    dark.append(fan)
    return body, white, glass, dark


def flagpole():
    """旗竿（白い鋼管）と青い旗"""
    x, y = -DECK_W / 2 + 0.45, -DECK_D + 0.5
    top = 6.0
    parts_white, parts_dark = [], []
    pole = T.lathe("pole", [(0.0, 0.0), (0.12, 0.0), (0.12, 0.05), (0.05, 0.08), (0.05, 0.5), (0.04, top - 0.3),
                            (0.03, top), (0.0, top)], segs=14)
    pole.location = (x, y, 0)
    parts_white.append(pole)
    ball = T.lathe("ball", [(0.0, -0.06)] + [(0.06 * math.sin(a), -0.06 * math.cos(a))
                                            for a in (k / 8 * math.pi for k in range(1, 8))] + [(0.0, 0.06)], segs=12)
    ball.location = (x, y, top + 0.05)
    parts_white.append(ball)
    # 旗（風下へなびく。上端と下端にロープの留め具）
    fw, fh = 1.3, 0.85
    nx, nz = 16, 8
    verts, faces, uvs = [], [], []
    for j in range(nz + 1):
        for i in range(nx + 1):
            u, v = i / nx, j / nz
            px = u * fw
            wave = 0.09 * u * math.sin(u * 7.0 + v * 0.8) + 0.03 * u * math.sin(u * 15.0 - v * 2.0)
            sag = -0.06 * u * u
            verts.append((x + 0.05 + px * 0.98, y + wave, top - 0.25 - (1 - v) * fh + sag))
    for j in range(nz):
        for i in range(nx):
            a = j * (nx + 1) + i
            faces.append((a, a + 1, a + nx + 2, a + nx + 1))
            uvs.append([(i / nx, j / nz), ((i + 1) / nx, j / nz), ((i + 1) / nx, (j + 1) / nz), (i / nx, (j + 1) / nz)])
    flag = C.mesh_object("flag", verts, faces, uvs)
    C.set_smooth(flag, True)
    rope = T.rod("rope", (x + 0.05, y, 0.9), (x + 0.05, y, top - 0.05), 0.005, sides=5)
    parts_dark.append(rope)
    cleat = geo.box("pole_cleat", (0.03, 0.03, 0.2), loc=(x + 0.06, y, 1.0))
    parts_dark.append(cleat)
    return parts_white, parts_dark, flag


def lifebuoy():
    """前の手すりに掛けた浮き輪"""
    R, r = 0.3, 0.06
    prof = [(R + r * math.cos(a), r * math.sin(a)) for a in (k / 12 * math.tau for k in range(12))]
    prof.append(prof[0])
    ring = T.lathe("buoy", prof, segs=24, fix_normals=False)
    T.recalc_normals(ring)
    ring.matrix_world = Matrix.Translation((-0.9, -DECK_D + 0.1, 0.78)) @ Matrix.Rotation(math.pi / 2, 4, "X")
    C.set_smooth(ring, True)
    return ring


def build():
    tops, walls = deck_parts()
    rails = railing()
    body, white, glass, dark = hut()
    pw, pd, flag = flagpole()
    buoy = lifebuoy()
    groups = [
        (tops, paving("LifeguardPaving", res=1024, x0=-DECK_W / 2)),
        (walls, concrete("LifeguardConcrete", res=1024, stain_top=-0.6)),
        (rails, galvanized("LifeguardRail", res=512)),
        (body, hut_material()),
        (white + pw, painted_metal("LifeguardWhite", "#e6e4dc", res=512, chip=0.15, gloss=0.4)),
        (glass, glass_material()),
        (dark + pd, painted_metal("LifeguardDark", "#33363a", res=256, chip=0.1, gloss=0.5)),
        ([flag], flag_material()),
        ([buoy], buoy_material()),
    ]
    out = []
    for objs, mat in groups:
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return {"LifeguardTower": out}
