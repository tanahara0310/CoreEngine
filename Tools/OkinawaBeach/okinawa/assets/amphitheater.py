"""安良波公園の野外ステージと噴水

  ArahaAmphitheater: 涙形の低いステージ（上面は水色の浅い噴水池、側面はモザイクタイル）を囲む半円の芝の段、
                     段の上の壁画の壁と手すり付きの通路、外側の植え込みの斜面。
                     背中側に、貝のモニュメントから水が段々に落ちる階段状の噴水と、その両脇の階段・段々の花壇、
                     下に受けの池と、灰色の石と赤いレンガの帯の広場。

座標: 原点 = ステージの中心の地面（z=0 が周りの芝生の高さ）。+X が段と噴水の向き（客席はステージ = -X を向く）。
ステージの尖った先は TAIL_DIR の向き。
"""
import math

from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import concrete, galvanized
from . import _timber as T

PREVIEW = dict(cam_dir=(-1.0, -0.55, 0.55), lens=45)

STAGE_R = 5.0
STAGE_H = 0.55
TAIL_DIR = Vector((-0.82, 0.57, 0.0)).normalized()
TAIL_LEN = 8.2
APRON_R = 6.8
PLAZA_W_R = 13.0
SEAT_R0 = 15.5
STEP_T = 1.55
STEP_H = 0.42
N_STEPS = 8
SEAT_R1 = SEAT_R0 + STEP_T * N_STEPS
TOP_Z = STEP_H * N_STEPS
WALK_Z = TOP_Z + 0.9
WALK_R1 = SEAT_R1 + 2.8
SPAN = math.radians(85)
END = math.radians(104)
COPE = 0.45
SIDE_Y = 10.5          # 噴水の区画の半幅
STAIR_Y = (1.75, 4.8)  # 階段の Y の範囲（両側）
CAS_Y = 1.5            # 噴水の流れの半幅
CAS_X = (WALK_R1 + 2.0, WALK_R1 + 10.0)
STAIR_X1 = WALK_R1 + 10.0
BASIN_X = (STAIR_X1, STAIR_X1 + 2.8)
BASIN_Y = 2.6
BASIN_WATER = 0.3
PLAZA_X = (STAIR_X1, STAIR_X1 + 18.0)
PLAZA_Y = 14.0
TIER_X = (WALK_R1 + 2.9, WALK_R1 + 6.4, STAIR_X1)
TIER_Z = (3.3, 2.15, 1.0)


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def grass_material():
    """段の芝生（セントオーガスチングラス）。踏まれて薄くなった所と黄ばみ"""
    def fn(nb):
        co = nb.coord("Object")
        big = nb.noise(co, 0.15, 3, 0.5)
        mid = nb.noise(co, 1.2, 4, 0.55)
        col = nb.ramp(big, [(0.3, srgb("#56672f")), (0.5, srgb("#687a3a")), (0.7, srgb("#7f8c47"))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(mid, 0.3, 0.7, 0.88, 1.08))
        dry = nb.smooth(nb.noise(co, 0.4, 3, 0.55), 0.58, 0.72)
        col = nb.mix(col, srgb("#a39c62"), nb.mul(dry, 0.6))
        bare = nb.smooth(nb.noise(co, 0.6, 4, 0.6), 0.7, 0.78)
        col = nb.mix(col, srgb("#b5a37e"), nb.mul(bare, 0.6))
        cd = nb.voronoi(co, 55.0)
        cr = nb.sep(nb.voronoi(co, 55.0, out="Color"))
        tuft = nb.smooth(cd, 0.35, 0.05)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(nb.mul(tuft, cr[0]), 0, 1, 0.85, 1.15))
        fine = nb.noise(co, 20.0, 3, 0.6)
        height = nb.add(nb.mul(tuft, 0.6), nb.mul(fine, 0.4))
        return dict(color=col, rough=0.88, height=height, height_scale=0.01)

    return pbr_material("AmphiGrass", fn, res=2048, ao_distance=0.8)


def soil_material():
    """花壇の土とマルチの木片"""
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 3.0, 4, 0.6)
        chips = nb.smooth(nb.voronoi(co, 30.0), 0.25, 0.0)
        col = nb.ramp(n, [(0.3, srgb("#3e3326")), (0.6, srgb("#56473a")), (0.8, srgb("#6a5a46"))])
        col = nb.mix(col, srgb("#8a6e4e"), nb.mul(chips, 0.5))
        return dict(color=col, rough=0.95, height=nb.add(chips, nb.mul(n, 0.5)), height_scale=0.01)

    return pbr_material("AmphiSoil", fn, res=1024, ao_distance=0.6)


def walk_material():
    """段の上の通路。円弧に沿って並ぶ灰色の小舗石"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        r = nb.vmath("LENGTH", nb.comb(X, Y, 0.0), out=1)
        ang = nb.math("ARCTAN2", Y, X)
        s = nb.mul(ang, SEAT_R1 + 1.4)
        bw, bh = 0.2, 0.2
        row = nb.math("FLOOR", nb.math("DIVIDE", r, bh))
        u = nb.add(nb.math("DIVIDE", s, bw), nb.mul(nb.math("FLOORED_MODULO", row, 2.0), 0.5))
        coln = nb.math("FLOOR", u)
        fx = nb.math("FRACT", u)
        fy = nb.math("FRACT", nb.math("DIVIDE", r, bh))
        edge = nb.math("MINIMUM", nb.mul(nb.math("MINIMUM", fx, nb.sub(1.0, fx)), bw),
                       nb.mul(nb.math("MINIMUM", fy, nb.sub(1.0, fy)), bh))
        joint = nb.smooth(edge, 0.012, 0.004)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(nb.comb(coln, row, 0.0), wn.inputs["Vector"])
        h1 = wn.outputs["Value"]
        col = nb.ramp(h1, [(0.0, srgb("#76736c")), (0.4, srgb("#8d8a82")), (0.75, srgb("#a29e95")),
                           (1.0, srgb("#b4afa3"))])
        fade = nb.noise(co, 0.8, 4, 0.55)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(fade, 0.3, 0.7, 0.92, 1.06))
        col = nb.mix(col, srgb("#5c5a52"), joint)
        grain = nb.noise(co, 70.0, 3, 0.6)
        height = nb.add(nb.smooth(edge, 0.0, 0.025), nb.mul(grain, 0.15))
        return dict(color=col, rough=nb.mixf(0.82, 0.95, joint), height=height, height_scale=0.006,
                    cavity=nb.maprange(joint, 0, 1, 1.0, 0.65))

    return pbr_material("AmphiWalk", fn, res=2048, ao_distance=0.3)


def plaza_material():
    """噴水の前の広場。灰色の石の平板に、赤いレンガの帯で大きな升目"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        tile = 0.6
        u = nb.math("DIVIDE", X, tile)
        v = nb.math("DIVIDE", Y, tile)
        fu, fv = nb.math("FRACT", u), nb.math("FRACT", v)
        edge = nb.mul(nb.math("MINIMUM", nb.math("MINIMUM", fu, nb.sub(1.0, fu)),
                               nb.math("MINIMUM", fv, nb.sub(1.0, fv))), tile)
        joint = nb.smooth(edge, 0.006, 0.002)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(nb.comb(nb.math("FLOOR", u), nb.math("FLOOR", v), 0.0), wn.inputs["Vector"])
        h1 = wn.outputs["Value"]
        speck = nb.noise(co, 60.0, 3, 0.6)
        stone = nb.ramp(h1, [(0.0, srgb("#8f8c86")), (0.5, srgb("#a8a59e")), (1.0, srgb("#bcb8ae"))])
        stone = nb.hsv(stone, 0.5, 1.0, nb.maprange(speck, 0.35, 0.65, 0.9, 1.08))
        # 赤い帯（4.8 m おき、幅 0.6 m）
        bu = nb.math("FLOORED_MODULO", nb.math("FLOOR", u), 8.0)
        bv = nb.math("FLOORED_MODULO", nb.math("FLOOR", v), 8.0)
        band = nb.math("MAXIMUM", nb.math("LESS_THAN", bu, 0.5), nb.math("LESS_THAN", bv, 0.5))
        brick = nb.ramp(h1, [(0.0, srgb("#7a3a2a")), (0.5, srgb("#94483a")), (1.0, srgb("#a85a44"))])
        col = nb.mix(stone, brick, band)
        dirt = nb.smooth(nb.noise(co, 0.5, 4, 0.6), 0.5, 0.75)
        col = nb.mix(col, srgb("#6e6a60"), nb.mul(dirt, 0.25))
        col = nb.mix(col, srgb("#55524c"), joint)
        return dict(color=col, rough=nb.mixf(0.75, 0.92, joint), height=nb.add(nb.smooth(edge, 0.0, 0.01),
                                                                               nb.mul(speck, 0.1)),
                    height_scale=0.004, cavity=nb.maprange(joint, 0, 1, 1.0, 0.7))

    return pbr_material("AmphiPlaza", fn, res=2048, ao_distance=0.4)


def mosaic_material():
    """ステージの側面のモザイクタイル（白地に青い波と黄色・赤の模様）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        s = nb.mul(nb.math("ARCTAN2", Y, X), STAGE_R)
        t = 0.05
        u = nb.math("DIVIDE", s, t)
        v = nb.math("DIVIDE", Z, t)
        fu, fv = nb.math("FRACT", u), nb.math("FRACT", v)
        edge = nb.mul(nb.math("MINIMUM", nb.math("MINIMUM", fu, nb.sub(1.0, fu)),
                               nb.math("MINIMUM", fv, nb.sub(1.0, fv))), t)
        joint = nb.smooth(edge, 0.004, 0.0015)
        cu, cv = nb.mul(nb.math("FLOOR", u), t), nb.mul(nb.math("FLOOR", v), t)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(nb.comb(cu, cv, 0.0), wn.inputs["Vector"])
        h1 = wn.outputs["Value"]
        wave = nb.math("SINE", nb.add(nb.mul(cu, 2.2), nb.mul(nb.math("SINE", nb.mul(cu, 0.7)), 1.5)))
        wl = nb.add(0.22, nb.mul(wave, 0.08))
        sea = nb.math("LESS_THAN", cv, wl)
        sun = nb.math("LESS_THAN", nb.vmath("DISTANCE", nb.comb(nb.math("FLOORED_MODULO", cu, 3.1), cv, 0.0),
                                            (1.5, 0.38, 0.0), out=1), 0.11)
        fish = nb.mul(nb.math("GREATER_THAN", nb.noise(nb.comb(cu, cv, 0.0), 1.4, 2, 0.5), 0.62), sea)
        col = nb.mix(srgb("#ece6d6"), srgb("#e2dccb"), h1)
        col = nb.mix(col, nb.mix(srgb("#2f78b4"), srgb("#4a96c8"), h1), sea)
        col = nb.mix(col, nb.mix(srgb("#e8b830"), srgb("#e07a24"), h1), sun)
        col = nb.mix(col, nb.mix(srgb("#d8442a"), srgb("#f0d050"), h1), fish)
        dirt = nb.smooth(Z, 0.15, 0.0)
        col = nb.mix(col, srgb("#7a7466"), nb.mul(dirt, 0.4))
        col = nb.mix(col, srgb("#9a968a"), joint)
        return dict(color=col, rough=nb.mixf(0.3, 0.8, joint), height=nb.sub(1.0, joint), height_scale=0.0015)

    return pbr_material("AmphiMosaic", fn, res=1024, ao_distance=0.3)


def mural_material():
    """段の上の壁の壁画（海と空、太陽、魚やサンゴの形）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        s = nb.mul(nb.math("ARCTAN2", Y, X), SEAT_R1)
        z = nb.sub(Z, TOP_Z)
        h = WALK_Z - TOP_Z
        f = nb.math("DIVIDE", z, h)
        sky = nb.mix(srgb("#7cc0e0"), srgb("#d8eef0"), nb.smooth(f, 0.5, 1.0))
        horizon = nb.add(0.5, nb.mul(nb.math("SINE", nb.mul(s, 0.9)), 0.06))
        sea = nb.mix(srgb("#1e5a9a"), srgb("#3a8ac0"), nb.smooth(f, 0.0, 0.5))
        col = nb.mix(sea, sky, nb.math("GREATER_THAN", f, horizon))
        cell = nb.math("FLOOR", nb.math("DIVIDE", s, 4.0))
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="1D")
        nb.link(cell, wn.inputs["W"])
        r = wn.outputs["Value"]
        cx = nb.mul(nb.add(cell, nb.maprange(r, 0, 1, 0.3, 0.7)), 4.0)
        d = nb.vmath("DISTANCE", nb.comb(s, z, 0.0), nb.comb(cx, nb.mul(h, 0.78), 0.0), out=1)
        sun = nb.mul(nb.math("LESS_THAN", d, 0.18), nb.math("GREATER_THAN", r, 0.55))
        col = nb.mix(col, srgb("#f0c030"), sun)
        ring = nb.mul(nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(d, 0.26)), 0.03, 0.0),
                             nb.math("GREATER_THAN", r, 0.55)), 1.0)
        col = nb.mix(col, srgb("#f09030"), ring)
        n = nb.noise(nb.comb(nb.mul(s, 0.8), nb.mul(z, 2.5), 0.0), 1.0, 2, 0.5)
        fish = nb.mul(nb.smooth(n, 0.64, 0.66), nb.math("LESS_THAN", f, nb.sub(horizon, 0.08)))
        fcol = nb.ramp(nb.noise(nb.comb(nb.mul(s, 0.3), 0.0, 0.0), 2.0, 1, 0.5),
                       [(0.3, srgb("#e8502a")), (0.5, srgb("#f0d040")), (0.7, srgb("#e870a0"))],
                       interp="CONSTANT")
        col = nb.mix(col, fcol, fish)
        coral = nb.mul(nb.smooth(nb.noise(nb.comb(nb.mul(s, 2.0), nb.mul(z, 3.0), 5.0), 1.0, 3, 0.6), 0.6, 0.62),
                       nb.math("LESS_THAN", f, 0.22))
        col = nb.mix(col, srgb("#d86a5a"), coral)
        # 日焼けで褪せ、下端は雨の跳ね返りで汚れる
        fade = nb.noise(co, 0.7, 3, 0.5)
        col = nb.mix(col, nb.hsv(col, 0.5, 0.65, 1.1), nb.mul(nb.smooth(fade, 0.4, 0.7), 0.6))
        col = nb.mix(col, srgb("#7a766c"), nb.mul(nb.smooth(f, 0.12, 0.0), 0.5))
        grain = nb.noise(co, 40.0, 3, 0.6)
        return dict(color=col, rough=0.8, height=grain, height_scale=0.002)

    return pbr_material("AmphiMural", fn, res=2048, ao_distance=0.4)


def pool_material():
    """噴水池の底（水色の塗装と水垢）"""
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 1.5, 4, 0.55)
        col = nb.mix(srgb("#8ec4d2"), srgb("#a8d4dc"), nb.smooth(n, 0.35, 0.7))
        stain = nb.smooth(nb.noise(co, 4.0, 4, 0.6), 0.55, 0.72)
        col = nb.mix(col, srgb("#6f8f86"), nb.mul(stain, 0.35))
        crack = nb.smooth(nb.math("ABSOLUTE", nb.sub(nb.noise(co, 2.0, 5, 0.6), 0.5)), 0.012, 0.0)
        col = nb.mix(col, srgb("#5a6c6c"), nb.mul(crack, 0.5))
        return dict(color=col, rough=0.55, height=nb.mul(crack, -1.0), height_scale=0.002)

    return pbr_material("AmphiPool", fn, res=1024, ao_distance=0.5)


def water_material():
    """噴水の水面（浅い水。底の水色が透けた明るい青緑）"""
    def fn(nb):
        co = nb.coord("Object")
        rip = nb.noise(nb.mapping(co, scale=(1.0, 3.0, 1.0)), 6.0, 3, 0.5)
        col = nb.mix(srgb("#4f9aa6"), srgb("#7cc0c8"), nb.smooth(rip, 0.35, 0.7))
        return dict(color=col, rough=0.05, height=rip, height_scale=0.004)

    return pbr_material("AmphiWater", fn, res=512, ao_strength=0.3, ao_distance=0.3)


def fall_material():
    """段から落ちる水の幕（白く泡立った縦の筋）"""
    def fn(nb):
        co = nb.coord("Object")
        st = nb.noise(nb.mapping(co, scale=(1.0, 14.0, 0.5)), 6.0, 4, 0.6)
        col = nb.mix(srgb("#8fc4cc"), srgb("#e8f2f2"), nb.smooth(st, 0.4, 0.75))
        return dict(color=col, rough=0.15, height=st, height_scale=0.003)

    return pbr_material("AmphiFall", fn, res=512, ao_strength=0.3, ao_distance=0.3)


def shell_material():
    """貝のモニュメント（クリーム色の擬石、内側はわずかに桃色）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        n = nb.noise(co, 2.0, 4, 0.55)
        col = nb.ramp(n, [(0.3, srgb("#d8cdb8")), (0.6, srgb("#e8e0cf")), (0.8, srgb("#f2ece0"))])
        inner = nb.smooth(X, 0.05, 0.25)
        col = nb.mix(col, srgb("#ecc8b8"), nb.mul(inner, 0.35))
        stain = nb.mul(nb.smooth(nb.noise(nb.mapping(co, scale=(1, 1, 0.3)), 3.0, 4, 0.6), 0.55, 0.7),
                       nb.smooth(Z, 1.2, 0.0))
        col = nb.mix(col, srgb("#8a8a7a"), nb.mul(stain, 0.4))
        fine = nb.noise(co, 50.0, 3, 0.6)
        return dict(color=col, rough=0.6, height=fine, height_scale=0.001)

    return pbr_material("AmphiShell", fn, res=1024, ao_distance=0.6)


def shrub_material():
    """植え込みの低木（刈り込んだ葉の塊）"""
    def fn(nb):
        co = nb.coord("Object")
        cd = nb.voronoi(co, 14.0)
        cr = nb.sep(nb.voronoi(co, 14.0, out="Color"))
        leaf = nb.smooth(cd, 0.45, 0.05)
        big = nb.noise(co, 0.8, 3, 0.5)
        col = nb.ramp(big, [(0.3, srgb("#2e4a1e")), (0.55, srgb("#466a2a")), (0.8, srgb("#6a8a34"))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(nb.mul(leaf, cr[0]), 0, 1, 0.65, 1.2))
        yel = nb.smooth(nb.noise(co, 0.5, 2, 0.5, distortion=0.5), 0.62, 0.72)
        col = nb.mix(col, srgb("#a8a838"), nb.mul(yel, 0.6))
        return dict(color=col, rough=0.7, height=leaf, height_scale=0.03,
                    cavity=nb.maprange(leaf, 0, 1, 0.55, 1.0))

    return pbr_material("AmphiShrub", fn, res=1024, ao_distance=0.8)


# ---------------------------------------------------------------------------
# 形の補助
# ---------------------------------------------------------------------------
def _angles(r, a0, a1, step=0.8):
    n = max(2, int(math.ceil(abs(a1 - a0) * r / step)))
    return [a0 + (a1 - a0) * i / n for i in range(n + 1)]


def _polar(r, a, z):
    return (r * math.cos(a), r * math.sin(a), z)


def ring_top(name, r0, r1, a0, a1, z_fn, rings=1, step=0.8):
    """r0..r1・a0..a1 の帯の上向きの面。z_fn(r, a)"""
    angs = _angles(max(r0, r1), a0, a1, step)
    rs = [r0 + (r1 - r0) * j / rings for j in range(rings + 1)]
    m = len(rs)
    verts = [_polar(r, a, z_fn(r, a)) for a in angs for r in rs]
    faces = [(i * m + j, i * m + j + 1, (i + 1) * m + j + 1, (i + 1) * m + j)
             for i in range(len(angs) - 1) for j in range(m - 1)]
    if a1 < a0:
        faces = [f[::-1] for f in faces]
    return C.mesh_object(name, verts, faces)


def ring_wall(name, r, a0, a1, z0_fn, z1_fn, outward=True, step=0.8):
    """半径 r の円筒の壁。outward=True なら中心と反対を向く"""
    angs = _angles(r, a0, a1, step)
    verts = []
    for a in angs:
        verts += [_polar(r, a, z0_fn(a)), _polar(r, a, z1_fn(a))]
    faces = [(2 * i, 2 * i + 2, 2 * i + 3, 2 * i + 1) for i in range(len(angs) - 1)]
    if (not outward) ^ (a1 < a0):
        faces = [f[::-1] for f in faces]
    return C.mesh_object(name, verts, faces)


def ngon(name, pts, normal_hint):
    """平面の多角形 1 枚（normal_hint の側を表にする）"""
    obj = C.mesh_object(name, [tuple(p) for p in pts], [tuple(range(len(pts)))])
    n = obj.data.polygons[0].normal
    if n.dot(Vector(normal_hint)) < 0:
        obj.data.polygons[0].flip()
    return obj


def prism(name, poly, z0, z1, top=True, bottom=False):
    """XY の多角形 poly（反時計回り）を z0..z1 に押し出した柱。戻り値 (上面, 側面)"""
    n = len(poly)
    verts = [(x, y, z0) for x, y in poly] + [(x, y, z1) for x, y in poly]
    sides = [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    side = C.mesh_object(name + "_side", verts, sides)
    t = C.mesh_object(name + "_top", [(x, y, z1) for x, y in poly], [tuple(range(n))]) if top else None
    if bottom:
        b = C.mesh_object(name + "_bot", [(x, y, z0) for x, y in poly], [tuple(reversed(range(n)))])
        side = C.join([side, b], name + "_side")
    return t, side


def sector_block(name, r0, r1, a0, a1, z0, z1):
    """扇形の台（上面と 4 つの側面）"""
    top = ring_top(name + "_t", r0, r1, a0, a1, lambda r, a: z1)
    w = [ring_wall(name + "_o", r1, a0, a1, lambda a: z0, lambda a: z1, outward=True),
         ring_wall(name + "_i", r0, a0, a1, lambda a: z0, lambda a: z1, outward=False)]
    for a, s in ((a0, -1), (a1, 1)):
        tang = Vector((-math.sin(a), math.cos(a), 0)) * s
        w.append(ngon(name + f"_e{s}", [_polar(r0, a, z0), _polar(r1, a, z0), _polar(r1, a, z1), _polar(r0, a, z1)],
                      tang))
    return top, C.join(w, name + "_w")


def railing(name, pts, height=1.0, post_every=1.8, mid=True):
    """点列（地面側）に沿った手すり。支柱と、上と中の横桟"""
    parts = []
    acc = 0.0
    last = None
    for i, p in enumerate(pts):
        if i:
            acc += (pts[i] - pts[i - 1]).length
        if last is None or acc - last >= post_every or i == len(pts) - 1:
            parts.append(T.rod(f"{name}_p{i}", p, p + Vector((0, 0, height)), 0.024, sides=8))
            last = acc
    top = [p + Vector((0, 0, height)) for p in pts]
    parts.append(geo.tube_along(top, [0.03] * len(top), sides=10, name=f"{name}_top"))
    if mid:
        m = [p + Vector((0, 0, height * 0.5)) for p in pts]
        parts.append(geo.tube_along(m, [0.016] * len(m), sides=8, name=f"{name}_mid"))
    return parts


def blob(name, center, radii, rnd_seed, detail=2):
    """低木の塊（でこぼこの楕円体）"""
    bm = geo.icosphere_bm(detail, 1.0)
    for v in bm.verts:
        d = v.co.normalized()
        k = 1.0 + 0.12 * math.sin(d.x * 5.1 + rnd_seed) * math.sin(d.y * 4.3 + rnd_seed * 1.7) \
            + 0.08 * math.sin(d.z * 7.0 + rnd_seed * 0.3)
        v.co = Vector((d.x * radii[0], d.y * radii[1], max(-0.2, d.z) * radii[2])) * k
    obj = C.from_bmesh(name, bm)
    obj.location = center
    C.apply_transform(obj)
    C.set_smooth(obj, True)
    return obj


# ---------------------------------------------------------------------------
# 部品
# ---------------------------------------------------------------------------
def stage_outline(inset=0.0, n_arc=56):
    """涙形（円と尖った先）の輪郭。inset だけ内側へずらす（点の数は inset によらず同じ）"""
    R = STAGE_R - inset
    d = TAIL_LEN - inset / math.sin(math.acos(STAGE_R / TAIL_LEN))
    phi = math.acos(R / d)
    th = math.atan2(TAIL_DIR.y, TAIL_DIR.x)
    a0, a1 = th + phi, th - phi + math.tau
    pts = [(R * math.cos(a), R * math.sin(a)) for a in (a0 + (a1 - a0) * i / (n_arc - 1) for i in range(n_arc))]
    pts.append((TAIL_DIR.x * d, TAIL_DIR.y * d))
    return pts


def stage():
    """ステージ: 側面のモザイク、上面の縁石、浅い池の底、噴き出し口"""
    outer = stage_outline()
    inner = stage_outline(0.4)
    n = len(outer)
    _, side = prism("stage", outer, -0.05, STAGE_H, top=False)
    # 縁石の上面（外周と内周の間）
    verts = [(x, y, STAGE_H) for x, y in outer] + [(x, y, STAGE_H) for x, y in inner]
    faces = [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    rim = C.mesh_object("stage_rim", verts, faces)
    # 池の内壁と底
    floor_z = STAGE_H - 0.22
    verts = [(x, y, floor_z) for x, y in inner] + [(x, y, STAGE_H) for x, y in inner]
    faces = [(n + i, n + (i + 1) % n, (i + 1) % n, i) for i in range(n)]
    inwall = C.mesh_object("stage_inwall", verts, faces)
    floor = C.mesh_object("stage_floor", [(x, y, floor_z) for x, y in inner], [tuple(range(n))])
    nozzles = []
    for k in range(7):
        a = k * math.tau / 7
        r = 2.6 if k else 0.0
        nozzles.append(geo.cylinder(f"nozzle{k}", 0.06, 0.06, loc=(r * math.cos(a), r * math.sin(a), floor_z + 0.03),
                                    verts=10))
    return side, rim, inwall, floor, nozzles


def seats():
    """半円の段。前縁のコンクリートの座面と蹴上げ、奥の芝生、両端の壁"""
    grass, conc = [], []
    for k in range(N_STEPS):
        r0 = SEAT_R0 + k * STEP_T
        z = (k + 1) * STEP_H
        conc.append(ring_wall(f"riser{k}", r0, -SPAN, SPAN, lambda a, k=k: k * STEP_H - (0.05 if k == 0 else 0.0),
                              lambda a, z=z: z + 0.04, outward=False))
        conc.append(ring_top(f"cope{k}", r0, r0 + COPE, -SPAN, SPAN, lambda r, a, z=z: z + 0.04))
        conc.append(ring_wall(f"copeback{k}", r0 + COPE, -SPAN, SPAN, lambda a, z=z: z, lambda a, z=z: z + 0.04,
                              outward=True))
        grass.append(ring_top(f"terrace{k}", r0 + COPE, r0 + STEP_T, -SPAN, SPAN, lambda r, a, z=z: z))
    # 段の上の壁画の壁（内向きの面）と笠木
    mural = ring_wall("mural", SEAT_R1, -SPAN, SPAN, lambda a: TOP_Z, lambda a: WALK_Z, outward=False, step=0.5)
    conc.append(ring_top("mural_cap", SEAT_R1 - 0.12, SEAT_R1 + 0.25, -SPAN, SPAN, lambda r, a: WALK_Z + 0.12))
    conc.append(ring_wall("mural_cap_in", SEAT_R1 - 0.12, -SPAN, SPAN, lambda a: WALK_Z - 0.05,
                          lambda a: WALK_Z + 0.12, outward=False))
    # 両端の壁（段の形）
    for s in (-1, 1):
        a = SPAN * s
        prof = [(SEAT_R0, -0.05)]
        for k in range(N_STEPS):
            r0 = SEAT_R0 + k * STEP_T
            prof += [(r0, (k + 1) * STEP_H + 0.04), (r0 + STEP_T, (k + 1) * STEP_H + 0.04)]
        prof += [(SEAT_R1, WALK_Z + 0.12), (SEAT_R1 + 0.25, WALK_Z + 0.12), (SEAT_R1 + 0.25, -0.05)]
        pts = [_polar(r, a, z) for r, z in prof]
        conc.append(ngon(f"endwall{s}", pts, (-math.sin(a) * s, math.cos(a) * s, 0)))
    return grass, conc, mural


def walk_height(a):
    """通路の高さ（両端は階段で地面まで下がる）"""
    t = (abs(a) - SPAN) / (END - SPAN)
    return WALK_Z if t <= 0 else max(0.0, WALK_Z * (1 - t))


def walkway_and_ends():
    """段の上の通路、両端の下り階段と内側の擁壁"""
    walk = [ring_top("walk", SEAT_R1, WALK_R1, -SPAN, SPAN, lambda r, a: WALK_Z, rings=2)]
    conc = []
    n = 25
    for s in (-1, 1):
        for k in range(n - 1):
            a0 = s * (SPAN + (END - SPAN) * k / n)
            a1 = s * (SPAN + (END - SPAN) * (k + 1) / n)
            z = WALK_Z * (1 - (k + 1) / n)
            t, w = sector_block(f"stair{s}_{k}", SEAT_R1, WALK_R1, min(a0, a1), max(a0, a1), -0.05, z)
            walk.append(t)
            conc.append(w)
        conc.append(ring_wall(f"endret{s}", SEAT_R1 + 0.25, s * SPAN, s * END, lambda a: -0.05,
                              lambda a: walk_height(a) + 0.12, outward=False, step=0.3))
    return walk, conc


def outer_slope():
    """通路の外側の斜面（噴水の区画を除く）と生け垣"""
    grass = []
    for s in (-1, 1):
        nu, ns = 70, 4
        verts = []
        for i in range(nu + 1):
            u = i / nu
            for j in range(ns + 1):
                sv = j / ns
                r_c = WALK_R1 + sv * WALK_Z
                a_s = math.asin(SIDE_Y / r_c)
                a = a_s + (END - a_s) * u
                hh = walk_height(a)
                r = WALK_R1 + sv * max(hh, 0.3)
                z = hh * (1 - sv) ** 1.15 - 0.05 * sv
                verts.append((r * math.cos(a), r * math.sin(a) * s, z))
        m = ns + 1
        faces = [(i * m + j, i * m + j + 1, (i + 1) * m + j + 1, (i + 1) * m + j) for i in range(nu) for j in range(ns)]
        if s < 0:
            faces = [f[::-1] for f in faces]
        grass.append(C.mesh_object(f"slope{s}", verts, faces))
    return grass


def cascade_complex(rnd):
    """背中側の噴水: 上の池と貝、段々に落ちる水、両脇の階段と花壇、受けの池"""
    conc, water, fall, soil, shrubs, pool = [], [], [], [], [], []
    x_top, x_bot = CAS_X
    # 上の池（通路の外側）
    hp_z = WALK_Z - 0.1
    water.append(C.mesh_object("headpool", [(WALK_R1 + 0.3, -CAS_Y, hp_z), (x_top, -CAS_Y, hp_z), (x_top, CAS_Y, hp_z),
                                            (WALK_R1 + 0.3, CAS_Y, hp_z)], [(0, 1, 2, 3)]))
    t, w = prism("hp_wall_back", [(WALK_R1, -CAS_Y - 0.25), (WALK_R1 + 0.3, -CAS_Y - 0.25),
                                  (WALK_R1 + 0.3, CAS_Y + 0.25), (WALK_R1, CAS_Y + 0.25)], 0.0, WALK_Z + 0.35)
    conc += [t, w]
    # 段々の流れ
    n = 12
    rise = (hp_z - BASIN_WATER) / n
    tread = (x_bot - x_top) / n
    for k in range(n):
        xa = x_top + k * tread
        za = hp_z - k * rise
        zb = za - rise
        fall.append(C.mesh_object(f"fall{k}", [(xa + 0.02, -CAS_Y, zb), (xa + 0.02, CAS_Y, zb), (xa + 0.02, CAS_Y, za),
                                               (xa + 0.02, -CAS_Y, za)], [(0, 1, 2, 3)]))
        if k < n - 1:
            water.append(C.mesh_object(f"cas{k}", [(xa, -CAS_Y, zb), (xa + tread, -CAS_Y, zb),
                                                   (xa + tread, CAS_Y, zb), (xa, CAS_Y, zb)], [(0, 1, 2, 3)]))
        # 流れの両側の縁石
        for s in (-1, 1):
            y0, y1 = sorted((s * CAS_Y, s * STAIR_Y[0]))
            t, w = prism(f"curb{k}_{s}", [(xa, y0), (xa + tread, y0), (xa + tread, y1), (xa, y1)], -0.05, za + 0.25)
            conc += [t, w]
    for s in (-1, 1):
        y0, y1 = sorted((s * CAS_Y, s * STAIR_Y[0]))
        t, w = prism(f"curb_top_{s}", [(WALK_R1 + 0.3, y0), (x_top, y0), (x_top, y1), (WALK_R1 + 0.3, y1)], -0.05,
                     hp_z + 0.35)
        conc += [t, w]
    # 両脇の階段
    ns = 25
    srise = WALK_Z / ns
    stread = (STAIR_X1 - WALK_R1) / ns
    steps = []
    for s in (-1, 1):
        y0, y1 = sorted((s * STAIR_Y[0], s * STAIR_Y[1]))
        for k in range(ns - 1):
            # 段 k は通路の縁から k+1 段目の先まで（重なった台の前面が段になる）
            x1 = WALK_R1 + (k + 1) * stread
            z = WALK_Z - (k + 1) * srise
            t, w = prism(f"step{s}_{k}", [(WALK_R1, y0), (x1, y0), (x1, y1), (WALK_R1, y1)], -0.05, z)
            steps.append(t)
            conc.append(w)
    # 段々の花壇（区画の外側）
    for s in (-1, 1):
        for k in range(3):
            xa = TIER_X[k - 1] if k else None
            xb = TIER_X[k]
            if k == 0:
                arc = []
                for y in [STAIR_Y[1] + (SIDE_Y - STAIR_Y[1]) * i / 8 for i in range(9)]:
                    arc.append((math.sqrt(WALK_R1 ** 2 - y * y), y))
                poly = [(xb, STAIR_Y[1])] + [(xb, SIDE_Y)] + arc[::-1]
            else:
                poly = [(xa, STAIR_Y[1]), (xb, STAIR_Y[1]), (xb, SIDE_Y), (xa, SIDE_Y)]
            if s < 0:
                poly = [(x, -y) for x, y in poly][::-1]
            if _signed_area(poly) < 0:
                poly = poly[::-1]
            z = TIER_Z[k]
            # 縁の笠木の内側を土にする
            t, w = prism(f"tier{s}_{k}", poly, -0.05, z)
            conc.append(w)
            t.location.z -= 0.08
            C.apply_transform(t)
            soil.append(t)
            cap_poly = _inset_ring(poly, 0.25)
            for ci, (a, b) in enumerate(cap_poly):
                ct, cw = prism(f"tiercap{s}_{k}_{ci}", [a[0], a[1], b[1], b[0]], z - 0.1, z + 0.06)
                conc += [ct, cw]
            cx = sum(p[0] for p in poly) / len(poly)
            cy = sum(p[1] for p in poly) / len(poly)
            for j in range(6 if k == 0 else 4):
                px = cx + rnd.uniform(-1.0, 1.0)
                py = cy + rnd.uniform(-1.6, 1.6)
                rr = rnd.uniform(0.45, 0.75)
                shrubs.append(blob(f"tshrub{s}_{k}_{j}", (px, py, z - 0.1), (rr, rr, rr * 0.75),
                                   rnd.uniform(0, 10)))
    # 受けの池
    bx0, bx1 = BASIN_X
    t, w = prism("basin_out", [(bx0, -BASIN_Y), (bx1, -BASIN_Y), (bx1, BASIN_Y), (bx0, BASIN_Y)], -0.05, 0.45,
                 top=False)
    conc.append(w)
    inner = [(bx0 + 0.25, -BASIN_Y + 0.25), (bx1 - 0.25, -BASIN_Y + 0.25), (bx1 - 0.25, BASIN_Y - 0.25),
             (bx0 + 0.25, BASIN_Y - 0.25)]
    outer = [(bx0, -BASIN_Y), (bx1, -BASIN_Y), (bx1, BASIN_Y), (bx0, BASIN_Y)]
    verts = [(x, y, 0.45) for x, y in outer] + [(x, y, 0.45) for x, y in inner]
    conc.append(C.mesh_object("basin_cap", verts, [(i, (i + 1) % 4, 4 + (i + 1) % 4, 4 + i) for i in range(4)]))
    verts = [(x, y, BASIN_WATER - 0.1) for x, y in inner] + [(x, y, 0.45) for x, y in inner]
    pool.append(C.mesh_object("basin_inwall", verts, [(4 + i, 4 + (i + 1) % 4, (i + 1) % 4, i) for i in range(4)]))
    water.append(C.mesh_object("basin_water", [(x, y, BASIN_WATER) for x, y in inner], [(0, 1, 2, 3)]))
    # 区画の両側の壁（外向き）
    for s in (-1, 1):
        prof = [(math.sqrt(WALK_R1 ** 2 - SIDE_Y ** 2), WALK_Z + 0.12), (TIER_X[0], WALK_Z + 0.12)]
        prof = [(math.sqrt(WALK_R1 ** 2 - SIDE_Y ** 2), -0.05)] + prof
        for k in range(3):
            xa = TIER_X[k - 1] if k else TIER_X[0]
            prof += [(xa, TIER_Z[k] + 0.06), (TIER_X[k], TIER_Z[k] + 0.06)]
        prof += [(STAIR_X1, -0.05)]
        pts = [(x, s * (SIDE_Y + 0.01), z) for x, z in prof]
        conc.append(ngon(f"cwall{s}", pts, (0, s, 0)))
    return conc, water, fall, soil, shrubs, pool, steps


def _signed_area(poly):
    return 0.5 * sum(poly[i][0] * poly[i - 1][1] - poly[i - 1][0] * poly[i][1] for i in range(len(poly))) * -1


def _inset_ring(poly, w):
    """多角形の各辺に沿った幅 w の帯の四隅 [((外 a, 外 b), (内 a, 内 b)), ...]（笠木用）"""
    n = len(poly)
    P = [Vector((x, y, 0)) for x, y in poly]
    out = []
    for i in range(n):
        a, b = P[i], P[(i + 1) % n]
        d = (b - a).normalized()
        nrm = Vector((-d.y, d.x, 0))
        out.append(((a.xy[:], b.xy[:]), ((a + nrm * w).xy[:], (b + nrm * w).xy[:])))
    return out


def shell():
    """噴水の上の貝（扇形の放射状の肋と、付け根の耳）。+X を向いて立つ"""
    nu, nv = 48, 16
    W, H = 2.6, 2.3
    span = math.radians(78)
    verts, faces = [], []
    for j in range(nv + 1):
        rho = 0.04 + 0.96 * j / nv
        for i in range(nu + 1):
            th = -span + 2 * span * i / nu
            rib = 0.06 * abs(math.cos(th * 14)) ** 1.5 * rho
            bulge = 0.42 * math.sin(math.pi * min(1.0, rho * 1.05)) * math.cos(th * 0.9) + rib
            verts.append((bulge, rho * math.sin(th) * W / 2, rho * math.cos(th) * H))
    m = nu + 1
    for j in range(nv):
        for i in range(nu):
            a = j * m + i
            faces.append((a, a + m, a + m + 1, a + 1))
    # 後ろの面は前の面を薄く押し戻したもの。外周の帯で前後をつないで閉じた形にする
    nf = len(verts)
    back_verts = [(-0.08 + v[0] * 0.35, v[1] * 0.98, v[2] * 0.98) for v in verts]
    loop = [(0, i) for i in range(m)] + [(j, nu) for j in range(1, nv + 1)] \
        + [(nv, i) for i in range(nu - 1, -1, -1)] + [(j, 0) for j in range(nv - 1, 0, -1)]
    idx = [j * m + i for j, i in loop]
    rim = [(idx[k], idx[(k + 1) % len(idx)], nf + idx[(k + 1) % len(idx)], nf + idx[k]) for k in range(len(idx))]
    body = C.mesh_object("shell_body", verts + back_verts, faces + [tuple(nf + i for i in f[::-1]) for f in faces] + rim)
    T.recalc_normals(body)
    parts = [body]
    for s in (-1, 1):
        ear = geo.box(f"shell_ear{s}", (0.35, 0.55, 0.35), loc=(0.05, s * 0.32, 0.12))
        geo.bevel(ear, 0.06, 2)
        parts.append(ear)
    sh = C.join(parts, "shell")
    C.set_smooth(sh, True)
    ped = geo.box("shell_ped", (1.0, 1.6, 0.7), loc=(0, 0, 0.35))
    geo.bevel(ped, 0.03, 2)
    sh.location = (WALK_R1 + 1.0, 0.0, WALK_Z + 0.55)
    ped.location = (WALK_R1 + 1.0, 0.0, WALK_Z - 0.15)
    C.apply_transform(sh)
    C.apply_transform(ped)
    return sh, ped


def floors():
    """ステージの周りのコンクリートの縁、西側の舗装の広場、噴水の前の石の広場"""
    apron = [ring_top("apron", STAGE_R - 0.1, APRON_R, -math.pi, math.pi, lambda r, a: 0.03, step=0.6)]
    apron.append(ring_top("plaza_w", APRON_R, PLAZA_W_R, math.radians(115), math.radians(245),
                          lambda r, a: 0.03, rings=3, step=0.8))
    x0, x1 = PLAZA_X
    plaza = C.mesh_object("plaza_e", [(x0, -PLAZA_Y, 0.03), (x1, -PLAZA_Y, 0.03), (x1, PLAZA_Y, 0.03),
                                      (x0, PLAZA_Y, 0.03)], [(0, 1, 2, 3)])
    return apron, [plaza]


def hedge_and_rails(rnd):
    """通路の外の生け垣・手すり、外の斜面の植え込み"""
    shrubs, rails = [], []
    a_open = math.asin(STAIR_Y[1] / WALK_R1)
    a_side = math.asin(SIDE_Y / WALK_R1)
    for s in (-1, 1):
        # 内側（壁画の壁の上）の手すり
        pts = [Vector(_polar(SEAT_R1 + 0.1, a * s, WALK_Z + 0.12)) for a in _angles(SEAT_R1, 0.0, SPAN, 1.0)]
        rails += railing(f"rail_in{s}", pts, 0.95)
        # 外側の手すり（噴水と階段の口を除く）と、階段に沿って下がる手すり
        angs = _angles(WALK_R1, a_open, END, 1.0)
        pts = [Vector(_polar(WALK_R1 - 0.1, a * s, walk_height(a))) for a in angs if walk_height(a) > 0.05]
        rails += railing(f"rail_out{s}", pts, 1.0)
        angs = _angles(SEAT_R1, SPAN, END, 0.6)
        pts = [Vector(_polar(SEAT_R1 + 0.15, a * s, walk_height(a) + 0.12)) for a in angs if walk_height(a) > 0.3]
        rails += railing(f"rail_stair{s}", pts, 0.95)
        # 生け垣（外の斜面の上端）
        for a in _angles(WALK_R1 + 0.8, a_side, SPAN, 1.3):
            r = WALK_R1 + 0.9 + rnd.uniform(-0.15, 0.15)
            z = walk_height(a) - 0.25
            rr = rnd.uniform(0.6, 0.85)
            shrubs.append(blob(f"hedge{s}_{a:.3f}", _polar(r, a * s, z), (rr, rr * 1.1, rr * 0.8), rnd.uniform(0, 9)))
        for k in range(26):
            a = rnd.uniform(a_side + 0.05, END - 0.05)
            sv = rnd.uniform(0.25, 0.9)
            hh = walk_height(a)
            r = WALK_R1 + sv * max(hh, 0.3)
            z = hh * (1 - sv) ** 1.15
            rr = rnd.uniform(0.45, 0.9)
            shrubs.append(blob(f"slshrub{s}_{k}", _polar(r, a * s, z - 0.2), (rr, rr, rr * 0.7), rnd.uniform(0, 9)))
    return shrubs, rails


def build():
    import random
    rnd = random.Random(7)
    st_side, st_rim, st_inwall, st_floor, nozzles = stage()
    grass, seat_conc, mural = seats()
    walk, walk_conc = walkway_and_ends()
    slope = outer_slope()
    conc, water, fall, soil, cshrubs, pool, steps = cascade_complex(rnd)
    sh, ped = shell()
    apron, plaza = floors()
    hshrubs, rails = hedge_and_rails(rnd)
    concrete_mat = concrete("AmphiConcrete", res=4096, tint="#b4b0a5", salt=0.15)
    groups = [
        ([st_side], mosaic_material()),
        ([st_rim, ped] + seat_conc + walk_conc + conc, concrete_mat),
        ([st_inwall, st_floor] + pool, pool_material()),
        (nozzles + rails, galvanized("AmphiMetal", res=512)),
        (grass + slope, grass_material()),
        ([mural], mural_material()),
        (walk + steps, walk_material()),
        (water, water_material()),
        (fall, fall_material()),
        (soil, soil_material()),
        (cshrubs + hshrubs, shrub_material()),
        ([sh], shell_material()),
        (apron, concrete("AmphiApron", res=2048, tint="#c6c2b7", salt=0.0)),
        (plaza, plaza_material()),
    ]
    out = []
    for objs, mat in groups:
        for o in objs:
            if o is None:
                continue
            C.assign(o, mat)
            out.append(o)
    return {"ArahaAmphitheater": out}
