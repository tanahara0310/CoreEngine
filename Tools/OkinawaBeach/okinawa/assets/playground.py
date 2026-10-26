"""安良波公園の幼児用の遊具

  PlayTower  : 黄色い柱のやぐらに赤い四角錐の屋根。黄色いすべり台、赤い枠の登り網、はしご、窓の付いた青い腰板。
               青いゴムチップ舗装の台（白い縁石）に載る
  SpringLion : 黄色いライオンのスプリング遊具（茶色のたてがみ）
  SpringHippo: 水色のカバのスプリング遊具

座標: 原点 = 舗装の中心の地面。PlayTower のすべり台は +X へ降りる。スプリング遊具は顔が -Y
"""
import math

from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import concrete, painted_metal
from . import _timber as T

PREVIEW = dict(cam_dir=(0.8, -1.0, 0.55), lens=50, spacing=1.3)

DECK_Z = 1.5
TOWER = 1.6
PAD = [(-5.5, -3.8), (5.8, -3.8), (5.8, 2.2), (3.6, 3.8), (-5.5, 3.8)]


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def rubber_material():
    """ゴムチップ舗装（青い粒に白と水色の粒が混じる）"""
    def fn(nb):
        co = nb.coord("Object")
        cd = nb.voronoi(co, 180.0)
        cr = nb.sep(nb.voronoi(co, 180.0, out="Color"))
        col = nb.mix(srgb("#2a6ab8"), srgb("#3a80c8"), cr[0])
        col = nb.mix(col, srgb("#d8e4ec"), nb.math("GREATER_THAN", cr[1], 0.92))
        col = nb.mix(col, srgb("#1e4a8a"), nb.math("LESS_THAN", cr[2], 0.12))
        wear = nb.smooth(nb.noise(co, 0.8, 4, 0.6), 0.55, 0.75)
        col = nb.mix(col, srgb("#7a8a9a"), nb.mul(wear, 0.35))
        sand = nb.smooth(nb.noise(co, 2.5, 4, 0.6), 0.62, 0.75)
        col = nb.mix(col, srgb("#c8bc9e"), nb.mul(sand, 0.4))
        return dict(color=col, rough=0.92, height=nb.smooth(cd, 0.4, 0.0), height_scale=0.003)

    return pbr_material("PlayRubber", fn, res=2048, ao_distance=0.6)


def plastic_material(name, color, res=512):
    """日焼けしたつやのある樹脂（すべり台・腰板）"""
    def fn(nb):
        co = nb.coord("Object")
        fade = nb.noise(co, 1.2, 3, 0.5)
        col = nb.mix(srgb(color), nb.hsv(srgb(color), 0.5, 0.8, 1.12), nb.smooth(fade, 0.4, 0.75))
        scuff = nb.smooth(nb.noise(nb.mapping(co, scale=(1, 6, 1)), 4.0, 4, 0.6), 0.6, 0.72)
        col = nb.mix(col, nb.hsv(col, 0.5, 0.6, 1.2), nb.mul(scuff, 0.4))
        dirt = nb.smooth(nb.noise(co, 3.0, 4, 0.6), 0.58, 0.72)
        col = nb.mix(col, srgb("#7a7060"), nb.mul(dirt, 0.2))
        return dict(color=col, rough=nb.mixf(0.25, 0.55, scuff), height=nb.noise(co, 30.0, 3, 0.6),
                    height_scale=0.0005)

    return pbr_material(name, fn, res=res, ao_distance=0.5)


def rope_material():
    def fn(nb):
        co = nb.coord("Object")
        tw = nb.wave(co, 120.0, 2.0, 1.0, kind="BANDS", direction="Z")
        col = nb.mix(srgb("#7a2a1e"), srgb("#a8402a"), tw)
        return dict(color=col, rough=0.85, height=tw, height_scale=0.002)

    return pbr_material("PlayRope", fn, res=256, ao_distance=0.2)


def eye_material():
    def fn(nb):
        co = nb.coord("Object")
        return dict(color=nb.mix(srgb("#101010"), srgb("#262626"), nb.noise(co, 20.0, 2, 0.5)), rough=0.2)

    return pbr_material("PlayEye", fn, res=128, ao_distance=0.1)


# ---------------------------------------------------------------------------
# やぐら
# ---------------------------------------------------------------------------
def _frames(t, up=Vector((0, 0, 1))):
    side = t.cross(up)
    if side.length < 1e-4:
        side = Vector((0, 1, 0))
    side.normalize()
    return side, side.cross(t).normalized()


def slide(name, pts, width=0.62, wall=0.28):
    """U 字断面のすべり台（厚み付き）"""
    hw = width / 2
    prof_out = [(-hw - 0.03, wall), (-hw - 0.03, -0.04), (-hw + 0.08, -0.09), (0.0, -0.1), (hw - 0.08, -0.09),
                (hw + 0.03, -0.04), (hw + 0.03, wall)]
    prof_in = [(hw, wall), (hw, 0.0), (hw - 0.08, -0.05), (0.0, -0.06), (-hw + 0.08, -0.05), (-hw, 0.0), (-hw, wall)]
    prof = prof_out + prof_in
    rings = []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        side, u = _frames(t)
        rings.append([p + side * a + u * b for a, b in prof])
    obj = T.loft(name, rings, cap_start=True, cap_end=True)
    C.set_smooth(obj, True, angle=40)
    return obj


def play_tower():
    yellow, red, blue, slide_p, rope, rubber, curb = [], [], [], [], [], [], []
    h = TOWER / 2
    top = 3.2
    for sx in (-1, 1):
        for sy in (-1, 1):
            yellow.append(T.rod(f"post{sx}{sy}", (sx * h, sy * h, -0.1), (sx * h, sy * h, top), 0.06, sides=12))
            cap = T.lathe(f"cap{sx}{sy}", [(0.0, 0.0), (0.07, 0.0), (0.07, 0.03), (0.0, 0.08)], segs=12)
            cap.location = (sx * h, sy * h, top)
            yellow.append(cap)
    deck = geo.box("deck", (TOWER + 0.1, TOWER + 0.1, 0.06), loc=(0, 0, DECK_Z))
    red.append(deck)
    # 手すり（すべり台・網・はしごの口を開ける）
    rz = DECK_Z + 0.75
    for a, b in (((-h, h), (h, h)),):
        red.append(T.rod("rail_n", (a[0], a[1], rz), (b[0], b[1], rz), 0.03, sides=8))
    # 腰板（北の面）に丸い窓
    pan = geo.box("panel_n", (TOWER - 0.1, 0.04, 0.7), loc=(0, h, DECK_Z + 0.42))
    blue.append(pan)
    win = geo.cylinder("window", 0.2, 0.06, loc=(0, h + 0.01, DECK_Z + 0.42), rot=(math.pi / 2, 0, 0), verts=16)
    yellow.append(win)
    under = geo.box("panel_under", (TOWER - 0.1, 0.04, 1.0), loc=(0, h, 0.7))
    blue.append(under)
    # 屋根
    apex = Vector((0, 0, top + 0.85))
    corners = [Vector((sx * (h + 0.25), sy * (h + 0.25), top)) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    for k in range(4):
        a, b = corners[k], corners[(k + 1) % 4]
        red.append(C.mesh_object(f"roof{k}", [tuple(a), tuple(b), tuple(apex)], [(0, 1, 2)]))
        red.append(C.mesh_object(f"roofu{k}", [tuple(a), tuple(apex), tuple(b)], [(0, 1, 2)]))
    ball = T.lathe("finial", [(0.0, -0.1)] + [(0.1 * math.sin(t), -0.1 * math.cos(t))
                                             for t in (k / 8 * math.pi for k in range(1, 8))] + [(0.0, 0.1)], segs=12)
    ball.location = apex + Vector((0, 0, 0.08))
    yellow.append(ball)
    # すべり台（+X）
    pts = geo.bezier_points(Vector((h, 0, DECK_Z + 0.05)), Vector((h + 1.2, 0, DECK_Z - 0.2)),
                            Vector((h + 2.4, 0, 0.55)), Vector((h + 3.6, 0, 0.32)), 16)
    pts.append(Vector((h + 4.2, 0, 0.3)))
    slide_p.append(slide("slide", pts))
    for x in (h + 2.2, h + 3.6):
        z = 0.32 if x > h + 3 else 0.62
        for s in (-1, 1):
            yellow.append(T.rod(f"sleg{x}{s}", (x, s * 0.28, -0.1), (x, s * 0.28, z - 0.08), 0.035, sides=8))
    for s in (-1, 1):
        yellow.append(T.rod(f"shand{s}", (h, s * 0.36, DECK_Z), (h, s * 0.36, DECK_Z + 0.8), 0.03, sides=8))
        yellow.append(geo.tube_along([Vector((h, s * 0.36, DECK_Z + 0.8)), Vector((h + 0.25, s * 0.36, DECK_Z + 0.85)),
                                      Vector((h + 0.45, s * 0.36, DECK_Z + 0.35))], [0.03] * 3, sides=8,
                                     name=f"sarc{s}"))
    # 登り網（-Y、斜めの赤い枠）
    y0, y1 = -h, -h - 1.6
    z0, z1 = DECK_Z, 0.0
    for s in (-1, 1):
        red.append(T.rod(f"nframe{s}", (s * h, y0, z0), (s * h, y1, z1), 0.04, sides=10))
    red.append(T.rod("nframe_b", (-h, y1, z1 + 0.05), (h, y1, z1 + 0.05), 0.04, sides=10))
    for k in range(1, 6):
        t = k / 6
        p = Vector((0, y0 + (y1 - y0) * t, z0 + (z1 - z0) * t))
        rope.append(T.rod(f"rh{k}", (-h, p.y, p.z), (h, p.y, p.z), 0.014, sides=6))
    for k in range(1, 5):
        x = -h + TOWER * k / 5
        rope.append(T.rod(f"rv{k}", (x, y0, z0), (x, y1, z1), 0.014, sides=6))
    # はしご（-X）
    for s in (-1, 1):
        yellow.append(T.rod(f"lad{s}", (-h - 0.6, s * 0.25, -0.1), (-h, s * 0.25, DECK_Z), 0.035, sides=8))
    for k in range(1, 5):
        t = k / 5
        x = -h - 0.6 + 0.6 * t
        z = DECK_Z * t
        red.append(T.rod(f"rung{k}", (x, -0.25, z), (x, 0.25, z), 0.022, sides=8))
    # ゴムチップの台と縁石
    t, side = _prism(PAD, 0.0, 0.07)
    rubber.append(t)
    rubber.append(side)
    for i in range(len(PAD)):
        a, b = Vector((*PAD[i], 0)), Vector((*PAD[(i + 1) % len(PAD)], 0))
        d = (b - a)
        c = geo.box(f"curb{i}", (d.length + 0.2, 0.2, 0.16), loc=((a + b) / 2) + Vector((0, 0, 0.04)))
        c.rotation_euler = (0, 0, math.atan2(d.y, d.x))
        C.apply_transform(c)
        geo.bevel(c, 0.02, 1)
        curb.append(c)
    groups = [
        (yellow, painted_metal("PlayYellow", "#f0c41e", res=512, chip=0.12, gloss=0.35)),
        (red, painted_metal("PlayRed", "#d0281e", res=512, chip=0.12, gloss=0.35)),
        (blue, plastic_material("PlayBlue", "#2a7ac8")),
        (slide_p, plastic_material("PlaySlide", "#f2a81e", res=1024)),
        (rope, rope_material()),
        (rubber, rubber_material()),
        (curb, concrete("PlayCurb", res=512, tint="#d0ccc0", salt=0.05)),
    ]
    return _assign(groups)


def _prism(poly, z0, z1):
    n = len(poly)
    verts = [(x, y, z0) for x, y in poly] + [(x, y, z1) for x, y in poly]
    side = C.mesh_object("pad_side", verts, [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)])
    top = C.mesh_object("pad_top", [(x, y, z1) for x, y in poly], [tuple(range(n))])
    return top, side


# ---------------------------------------------------------------------------
# スプリング遊具
# ---------------------------------------------------------------------------
def _ell(name, c, r, subdiv=3):
    bm = geo.icosphere_bm(subdiv)
    for v in bm.verts:
        v.co = Vector((v.co.x * r[0], v.co.y * r[1], v.co.z * r[2]))
    o = C.from_bmesh(name, bm)
    o.location = Vector(c)
    C.apply_transform(o)
    return o


def _remesh(obj, voxel):
    m = obj.modifiers.new("Remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = voxel
    m.adaptivity = 0.0
    C.apply_modifiers(obj)


def _body(name, parts, voxel=0.018, tris=9000):
    """重なった楕円体・棒を 1 つのなめらかな形にまとめる"""
    obj = C.join(parts, name)
    _remesh(obj, voxel)
    geo.smooth_mod(obj, 0.5, 4)
    geo.decimate(obj, target_tris=tris)
    C.set_smooth(obj, True)
    return obj


def spring_base(prefix):
    """ばね・台座・足かけ"""
    metal, conc = [], []
    pts = []
    turns, R, H = 6.0, 0.11, 0.32
    n = 96
    for i in range(n + 1):
        a = i / n * turns * math.tau
        pts.append(Vector((R * math.cos(a), R * math.sin(a), 0.06 + H * i / n)))
    metal.append(geo.tube_along(pts, [0.016] * len(pts), sides=8, name=prefix + "_spring"))
    plate = geo.cylinder(prefix + "_plate", 0.2, 0.04, loc=(0, 0, 0.04), verts=16)
    metal.append(plate)
    top_plate = geo.cylinder(prefix + "_tplate", 0.17, 0.03, loc=(0, 0, 0.39), verts=16)
    metal.append(top_plate)
    conc.append(geo.cylinder(prefix + "_found", 0.45, 0.06, loc=(0, 0, 0.0), verts=20))
    return metal, conc


def spring_lion():
    zb = 0.62
    body = [_ell("torso", (0, 0.05, zb), (0.2, 0.36, 0.2)),
            _ell("head", (0, -0.36, zb + 0.22), (0.2, 0.19, 0.19)),
            _ell("muzzle", (0, -0.52, zb + 0.15), (0.11, 0.08, 0.08))]
    for sx in (-1, 1):
        for sy, ly in ((-1, -0.2), (1, 0.28)):
            body.append(T.rod(f"leg{sx}{sy}", (sx * 0.11, ly, zb - 0.05), (sx * 0.12, ly - 0.02, zb - 0.22), 0.065,
                              sides=10))
            body.append(_ell(f"paw{sx}{sy}", (sx * 0.12, ly - 0.05, zb - 0.22), (0.07, 0.09, 0.05), 2))
        body.append(_ell(f"ear{sx}", (sx * 0.15, -0.3, zb + 0.4), (0.06, 0.04, 0.06), 2))
    tail = geo.tube_along([Vector((0, 0.4, zb + 0.05)), Vector((0, 0.52, zb + 0.15)), Vector((0, 0.56, zb + 0.32))],
                          [0.03, 0.025, 0.02], sides=8, name="tail")
    body.append(tail)
    yellow = _body("lion", body, tris=6000)
    mane = []
    for k in range(14):
        a = k / 14 * math.tau
        mane.append(_ell(f"mane{k}", (math.cos(a) * 0.2, -0.28, zb + 0.22 + math.sin(a) * 0.2), (0.09, 0.07, 0.09), 2))
    mane.append(_ell("tailtuft", (0, 0.56, zb + 0.36), (0.05, 0.05, 0.06), 2))
    brown = _body("mane", mane, voxel=0.015, tris=2500)
    eyes = [_ell(f"eye{sx}", (sx * 0.08, -0.53, zb + 0.29), (0.025, 0.015, 0.03), 2) for sx in (-1, 1)]
    eyes.append(_ell("nose", (0, -0.6, zb + 0.18), (0.035, 0.025, 0.025), 2))
    metal, conc = spring_base("lion")
    red = [T.rod("handle", (-0.17, -0.38, zb + 0.28), (0.17, -0.38, zb + 0.28), 0.018, sides=8)]
    red += [T.rod(f"peg{s}", (s * 0.12, -0.05, zb - 0.12), (s * 0.3, -0.05, zb - 0.12), 0.018, sides=8) for s in (-1, 1)]
    return _assign([
        ([yellow], plastic_material("LionBody", "#f2c42a")),
        ([brown], plastic_material("LionMane", "#b8641e")),
        (eyes, eye_material()),
        (metal + red, painted_metal("LionMetal", "#c82a20", res=256, chip=0.15, gloss=0.4)),
        (conc, concrete("LionBase", res=256, tint="#b8b4aa", salt=0.05)),
    ])


def spring_hippo():
    zb = 0.6
    body = [_ell("torso", (0, 0.05, zb), (0.25, 0.4, 0.22)),
            _ell("head", (0, -0.4, zb + 0.12), (0.2, 0.2, 0.17)),
            _ell("snout", (0, -0.58, zb + 0.06), (0.2, 0.14, 0.12))]
    for sx in (-1, 1):
        for sy, ly in ((-1, -0.2), (1, 0.3)):
            body.append(T.rod(f"leg{sx}{sy}", (sx * 0.14, ly, zb - 0.05), (sx * 0.15, ly, zb - 0.22), 0.08, sides=10))
        body.append(_ell(f"ear{sx}", (sx * 0.12, -0.38, zb + 0.3), (0.05, 0.035, 0.05), 2))
        body.append(_ell(f"nostril{sx}", (sx * 0.08, -0.66, zb + 0.15), (0.04, 0.035, 0.03), 2))
    blue = _body("hippo", body, tris=6000)
    eyes = [_ell(f"eye{sx}", (sx * 0.1, -0.5, zb + 0.24), (0.03, 0.02, 0.035), 2) for sx in (-1, 1)]
    metal, conc = spring_base("hippo")
    red = [T.rod("handle", (-0.2, -0.36, zb + 0.24), (0.2, -0.36, zb + 0.24), 0.018, sides=8)]
    red += [T.rod(f"peg{s}", (s * 0.15, -0.05, zb - 0.12), (s * 0.33, -0.05, zb - 0.12), 0.018, sides=8) for s in (-1, 1)]
    return _assign([
        ([blue], plastic_material("HippoBody", "#4aa8d8")),
        (eyes, eye_material()),
        (metal + red, painted_metal("HippoMetal", "#f0c41e", res=256, chip=0.15, gloss=0.4)),
        (conc, concrete("HippoBase", res=256, tint="#b8b4aa", salt=0.05)),
    ])


def _assign(groups):
    out = []
    for objs, mat in groups:
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return out


def build():
    return {"PlayTower": play_tower(), "SpringLion": spring_lion(), "SpringHippo": spring_hippo()}
