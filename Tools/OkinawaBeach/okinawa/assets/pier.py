"""木の桟橋（モジュール式、2 バリエーション）

座標: z=0 が水面（平均潮位）。床板の上面 z=+1.0、杭の下端 z=-2.5（海底に刺さる想定）。
原点はモジュールの始端・幅の中央。モジュールは +Y 方向に長さ 4m、幅 2m（x=-1..+1）。
  Pier_Straight: 4m 直線。y=0..4 なので、次のモジュールを y+4 に置けば継ぎ目なく並ぶ
                 （床板の隙間・杭の間隔 2m も 4m 周期で揃う）。
  Pier_End     : 4m の先端。y=4 側に鼻隠し板・水中へ降りるはしご・係船柱（ビット）・クリート。
                 Straight の後ろ（y+4）につなぐ。

構造: 杭（丸太）→ 2 枚挟みの桁受け（ボルト貫通）→ 縦桁 4 本 → 床板（隙間あり）。
杭の間は X 筋交い、長さ方向にも斜めの筋交い。金物は溶融亜鉛めっき。
杭と水中の筋交いには潮間帯（+0.3m 以下）に藻とフジツボ。
"""
import math
import random

from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from . import _timber as T

DECK_TOP = 1.0
PLANK_T = 0.05
N_PLANK = 26
PITCH = 4.0 / N_PLANK        # 床板の間隔（幅 0.14 + 隙間）
PLANK_W = 0.14
STRINGER_X = (-0.9, -0.3, 0.3, 0.9)
STR_W, STR_H = 0.075, 0.2
PILE_R = 0.14
PILE_X = 0.9
CAP_T, CAP_H = 0.075, 0.22
CAP_TOP = DECK_TOP - PLANK_T - STR_H      # 0.75
PILE_BOTTOM = -2.5


def _preview_extra(objs):
    """直線 2 本 + 先端をつないで並べ、杭が見えるよう持ち上げて撮る"""
    import bpy
    roots = [o for o in objs if o.parent is None]
    lift = 2.55
    straight = [o for o in roots if o.name.startswith("Pier_Straight")]
    end = [o for o in roots if o.name.startswith("Pier_End")]
    for o in straight:
        o.location = (0, -8.0, lift)
        c = o.copy()
        bpy.context.scene.collection.objects.link(c)
        c.location = (0, -4.0, lift)
    for o in end:
        o.location = (0, 0.0, lift)
    cam = bpy.context.scene.camera
    tgt = Vector((0.3, -2.2, lift - 1.0))
    cam.location = tgt + Vector((8.6, 9.0, 3.2))
    cam.data.lens = 30
    C.look_at(cam, tgt)


PREVIEW = dict(extra=_preview_extra)


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def _weathered(nb, g, r1, r2, base_dark=0.0):
    """日焼けして銀灰色になった木。g = wood_grain の戻り値"""
    tone = nb.add(nb.mul(g["var"], 0.6), nb.mul(r1, 0.4))
    col = nb.ramp(tone, [(0.15, srgb("#7d766b")), (0.5, srgb("#a19a8d")), (0.85, srgb("#bdb6a8"))])
    # 板によっては茶色が残る
    brown = nb.ramp(g["var"], [(0.2, srgb("#7a6650")), (0.8, srgb("#9a8468"))])
    col = nb.mix(col, brown, nb.mul(nb.smooth(r2, 0.6, 0.95), 0.55))
    col = nb.mix(col, srgb("#4f4a43"), nb.mul(g["ring"], 0.45))
    col = nb.hsv(col, 0.5, 1.0, nb.maprange(g["fib"], 0.25, 0.75, 0.85, 1.1))
    col = nb.mix(col, srgb("#3b352e"), nb.mul(g["knot"], 0.75))
    col = nb.mix(col, srgb("#1d1a16"), nb.mul(g["crack"], 0.9))
    if base_dark:
        col = nb.hsv(col, 0.5, 1.0, 1.0 - base_dark)
    # 風化で柔らかい早材がやせ、晩材と繊維が浮く
    h = nb.add(nb.add(nb.mul(g["fib"], 0.45), nb.mul(g["ring"], 0.6)), nb.mul(g["crack"], -1.5))
    return col, h


def deck_material():
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        r1, r2, _ = nb.sep(nb.attr("WoodId", "Vector"))
        oc = nb.coord("Object")
        X, Y, Z = nb.sep(oc)
        g = T.wood_grain(nb, co, rings=24.0, knots=0.3, crack_amt=1.3)
        col, h = _weathered(nb, g, r1, r2)
        # 釘（縦桁の上、1 枚につき 2 本）と錆の滲み
        dx = nb.mul(nb.sub(nb.math("FRACT", nb.add(nb.math("DIVIDE", nb.add(X, 0.9), 0.6), 0.5)), 0.5), 0.6)
        dy = nb.mul(nb.sub(nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.math("DIVIDE", Y, PITCH)), 0.5)),
                           0.04 / PITCH), PITCH)
        d = nb.vmath("LENGTH", nb.comb(dx, dy, 0.0), out=1)
        top = nb.smooth(Z, DECK_TOP - 0.004, DECK_TOP - 0.001)
        onx = nb.math("LESS_THAN", nb.math("ABSOLUTE", X), 0.95)
        nail = nb.mul(nb.mul(nb.smooth(d, 0.0055, 0.004), top), onx)
        halo = nb.mul(nb.mul(nb.smooth(d, 0.02, 0.004), top), onx)
        col = nb.mix(col, srgb("#6e4a2e"), nb.mul(halo, 0.55))
        col = nb.mix(col, srgb("#3a3632"), nail)
        # 人が歩く中央は明るく滑らか、端は砂と塩
        walk = nb.mul(nb.smooth(nb.math("ABSOLUTE", X), 0.7, 0.2), top)
        col = nb.hsv(col, 0.5, 1.0, nb.add(1.0, nb.mul(walk, 0.08)))
        sand = nb.mul(nb.smooth(nb.noise(oc, 6.0, 5, 0.6), 0.6, 0.72), nb.smooth(nb.math("ABSOLUTE", X), 0.6, 0.95))
        col = nb.mix(col, srgb("#d9ceb3"), nb.mul(sand, nb.mul(top, 0.5)))
        # 床板の裏・側面は湿って暗い
        col = nb.mix(col, srgb("#57524a"), nb.mul(nb.sub(1.0, top), 0.3))
        rough = nb.sub(0.86, nb.mul(walk, 0.12))
        h = nb.add(nb.mul(h, nb.sub(1.0, nb.mul(walk, 0.5))), nb.mul(nail, 0.6))
        return dict(color=col, rough=rough, height=h, height_scale=0.004,
                    cavity=nb.maprange(g["crack"], 0, 1, 1.0, 0.5))

    return pbr_material("PierDeck", fn, res=2048, ao_distance=0.5)


def frame_material():
    """縦桁・桁受け・筋交い。水中部分は付着物"""
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        r1, r2, _ = nb.sep(nb.attr("WoodId", "Vector"))
        oc = nb.coord("Object")
        X, Y, Z = nb.sep(oc)
        g = T.wood_grain(nb, co, rings=22.0, knots=0.3)
        col, h = _weathered(nb, g, r1, r2, base_dark=0.12)
        # 雨だれの縦筋
        streak = nb.noise(nb.mapping(oc, scale=(14, 14, 0.7)), 1.0, 4, 0.6)
        col = nb.mix(col, srgb("#3e3a33"), nb.mul(nb.smooth(streak, 0.55, 0.8), 0.35))
        m = T.marine(nb, oc, top=0.3)
        col = nb.hsv(col, 0.5, 1.0, nb.mixf(1.0, 0.55, m["wet"]))
        col = nb.mix(col, m["col"], m["cover"])
        rough = nb.mixf(0.85, 0.4, m["wet"])
        rough = nb.mixf(rough, m["rough"], m["cover"])
        h = nb.add(nb.mul(h, nb.sub(1.0, m["cover"])), nb.mul(m["height"], 1.5))
        return dict(color=col, rough=rough, height=h, height_scale=0.006,
                    cavity=nb.maprange(nb.math("MAXIMUM", g["crack"], nb.mul(m["barn"], 0.3)), 0, 1, 1.0, 0.5))

    return pbr_material("PierFrame", fn, res=1024, ao_distance=0.6)


def piling_material():
    """杭。濃い色の丸太、潮間帯から下は藻・フジツボ"""
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        r1, r2, _ = nb.sep(nb.attr("WoodId", "Vector"))
        oc = nb.coord("Object")
        X, Y, Z = nb.sep(oc)
        g = T.wood_grain(nb, co, rings=18.0, knots=0.35, crack_amt=1.5)
        tone = nb.add(nb.mul(g["var"], 0.6), nb.mul(r1, 0.4))
        col = nb.ramp(tone, [(0.2, srgb("#5a5147")), (0.55, srgb("#756b5e")), (0.85, srgb("#8d8475"))])
        col = nb.mix(col, srgb("#3a342d"), nb.mul(g["ring"], 0.5))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(g["fib"], 0.25, 0.75, 0.85, 1.1))
        col = nb.mix(col, srgb("#1a1714"), nb.mul(g["crack"], 0.95))
        # 防腐剤（クレオソート）の黒い染み
        creo = nb.smooth(nb.noise(nb.mapping(oc, scale=(3, 3, 0.8)), 2.0, 4, 0.6), 0.5, 0.75)
        col = nb.mix(col, srgb("#2c2621"), nb.mul(creo, 0.45))
        h = nb.add(nb.add(nb.mul(g["fib"], 0.5), nb.mul(g["ring"], 0.5)), nb.mul(g["crack"], -1.8))
        m = T.marine(nb, oc, top=0.3)
        col = nb.hsv(col, 0.5, 1.0, nb.mixf(1.0, 0.5, m["wet"]))
        col = nb.mix(col, m["col"], m["cover"])
        # 水面付近の白い塩の筋
        salt = nb.mul(nb.smooth(Z, 0.25, 0.4), nb.smooth(Z, 0.75, 0.45))
        salt = nb.mul(salt, nb.smooth(nb.noise(nb.mapping(oc, scale=(8, 8, 2)), 1.0, 4, 0.6), 0.55, 0.7))
        col = nb.mix(col, srgb("#b8b3a6"), nb.mul(salt, 0.5))
        rough = nb.mixf(0.85, 0.35, m["wet"])
        rough = nb.mixf(rough, m["rough"], m["cover"])
        h = nb.add(nb.mul(h, nb.sub(1.0, m["cover"])), nb.mul(m["height"], 1.6))
        return dict(color=col, rough=rough, height=h, height_scale=0.008,
                    cavity=nb.maprange(nb.math("MAXIMUM", g["crack"], nb.mul(m["barn"], 0.3)), 0, 1, 1.0, 0.45))

    return pbr_material("PierPiling", fn, res=2048, ao_distance=0.8, uv="keep")


def metal_material():
    """溶融亜鉛めっき鋼（スパングル模様・白錆・赤錆）"""
    def fn(nb):
        oc = nb.coord("Object")
        X, Y, Z = nb.sep(oc)
        sp = nb.bw(nb.voronoi(oc, 45.0, out="Color"))
        col = nb.mix(srgb("#8e9291"), srgb("#b4b8b6"), sp)
        n = nb.noise(oc, 9.0, 6, 0.65)
        white_r = nb.smooth(n, 0.55, 0.7)
        red_r = nb.smooth(nb.add(n, nb.mul(nb.smooth(Z, 0.8, -0.3), 0.25)), 0.68, 0.78)
        col = nb.mix(col, srgb("#c9cbc4"), nb.mul(white_r, 0.7))
        rust = nb.mix(srgb("#5e2f17"), srgb("#8a4a22"), nb.noise(oc, 40.0, 3, 0.6))
        col = nb.mix(col, rust, red_r)
        m = T.marine(nb, oc, top=0.25)
        col = nb.mix(col, m["col"], m["cover"])
        metal = nb.mul(nb.sub(1.0, nb.math("MAXIMUM", red_r, nb.mul(white_r, 0.6))), nb.sub(1.0, m["cover"]))
        rough = nb.mixf(nb.mixf(0.38, 0.75, white_r), 0.85, red_r)
        rough = nb.mixf(rough, m["rough"], m["cover"])
        h = nb.add(nb.mul(red_r, nb.noise(oc, 80.0, 3, 0.6)), nb.mul(m["height"], 1.2))
        return dict(color=col, rough=rough, metal=metal, height=h, height_scale=0.002)

    return pbr_material("PierMetal", fn, res=512, ao_distance=0.2)


def iron_material():
    """係船柱・クリート（黒塗りの鋳鉄、ロープで擦れて地金が光る）"""
    def fn(nb):
        oc = nb.coord("Object")
        nz = nb.sep(nb.coord("Normal"))[2]
        n = nb.noise(oc, 12.0, 8, 0.65)
        chip = nb.smooth(n, 0.64, 0.66)
        paint = nb.mix(srgb("#1b1d1f"), srgb("#2a2c2d"), nb.noise(oc, 5.0, 3, 0.5))
        rust = nb.mix(srgb("#4d2a18"), srgb("#7a4020"), nb.noise(oc, 50.0, 3, 0.6))
        col = nb.mix(paint, rust, chip)
        # ロープ擦れ: 胴のくびれと頭の上面
        worn = nb.mul(nb.smooth(nb.math("ABSOLUTE", nz), 0.5, 0.1), nb.smooth(n, 0.4, 0.55))
        col = nb.mix(col, srgb("#5c5d5c"), nb.mul(worn, 0.8))
        metal = nb.mul(worn, 0.9)
        rough = nb.mixf(nb.mixf(0.45, 0.9, chip), 0.35, worn)
        h = nb.add(nb.mul(nb.sub(1.0, chip), 0.5), nb.mul(nb.noise(oc, 60.0, 3, 0.6), 0.3))
        return dict(color=col, rough=rough, metal=metal, height=h, height_scale=0.0015)

    return pbr_material("PierIron", fn, res=512, ao_distance=0.2)


# ---------------------------------------------------------------------------
# 部品
# ---------------------------------------------------------------------------
def pile(name, x, y, top, rnd):
    """丸太の杭。わずかな傾きと太さのムラ、頭は面取り"""
    prof = [(0.0, PILE_BOTTOM), (PILE_R * 0.97, PILE_BOTTOM)]
    z = PILE_BOTTOM
    zs = [-2.2, -1.7, -1.2, -0.8, -0.45, -0.15, 0.1, 0.35, 0.6]
    for zz in zs:
        if zz < top - 0.1:
            prof.append((PILE_R * rnd.uniform(0.97, 1.02), zz))
    prof += [(PILE_R, top - 0.03), (PILE_R * 0.86, top), (0.0, top)]
    obj = T.lathe(name, prof, segs=14)
    for v in obj.data.vertices:
        a = math.atan2(v.co.y, v.co.x)
        k = 1.0 + 0.025 * math.sin(3 * a + rnd.uniform(0, 0.3) + v.co.z) + 0.012 * math.sin(7 * a)
        v.co.x *= k
        v.co.y *= k
    T.mark_seams(obj, lambda e: all(abs(v.co.y) < 1e-6 and v.co.x > 1e-4 for v in e.verts))
    T.wood_attrs(obj, rnd, axis="Z", pith=(0.0, 0.02))
    C.set_smooth(obj, True, angle=50)
    lean = (rnd.uniform(-0.012, 0.012), rnd.uniform(-0.012, 0.012))
    obj.matrix_world = Matrix.Translation((x, y, 0)) @ Matrix.Rotation(lean[0], 4, "X") @ Matrix.Rotation(lean[1], 4, "Y")
    return obj


def bolt(name, pos, direction, head=0.022, washer=0.034, nut=False, stub=0.0):
    """六角ボルト頭（またはナット＋ねじ先）と座金。direction = 頭が向く向き"""
    parts = []
    d = Vector(direction).normalized()
    w = T.lathe(name + "_w", [(0.0, 0.0), (washer, 0.0), (washer, 0.004), (0.0, 0.004)], segs=10)
    T.orient(w, pos, d)
    parts.append(w)
    h = T.lathe(name + "_h", [(0.0, 0.004), (head, 0.004), (head, 0.018), (head * 0.8, 0.021), (0.0, 0.021)], segs=6)
    T.orient(h, pos, d, roll=0.3)
    parts.append(h)
    if stub:
        s = T.lathe(name + "_s", [(0.0, 0.02), (0.011, 0.02), (0.011, 0.02 + stub), (0.008, 0.023 + stub),
                                  (0.0, 0.023 + stub)], segs=6)
        T.orient(s, pos, d)
        parts.append(s)
    return parts


def through_bolt(name, center, axis, half_len):
    """貫通ボルト: 片側に頭、反対側にナットとねじ先"""
    a = Vector(axis).normalized()
    c = Vector(center)
    return (bolt(name + "a", c - a * half_len, -a) + bolt(name + "b", c + a * half_len, a, stub=0.025))


def bracket(name, x, y, side):
    """縦桁と桁受けをつなぐ L 型金物（側面の板 + 上面の板）"""
    z0 = CAP_TOP
    xs = x + side * (STR_W / 2 + 0.002)
    a = geo.box(name + "a", (0.004, 0.09, 0.12), loc=(xs, y, z0 + 0.06))
    b = geo.box(name + "b", (0.07, 0.09, 0.004), loc=(xs + side * 0.035, y, z0 + 0.002))
    return [a, b]


def plank(name, y, rnd, length=2.0):
    L = length + rnd.uniform(-0.015, 0.02)
    o = T.board(name, (L, PLANK_W - rnd.uniform(0, 0.006), PLANK_T),
                loc=(rnd.uniform(-0.01, 0.01), y + rnd.uniform(-0.003, 0.003),
                     DECK_TOP - PLANK_T / 2 + rnd.uniform(-0.002, 0.0)),
                rot=(rnd.uniform(-0.006, 0.006), 0, rnd.uniform(-0.004, 0.004)), rnd=rnd, bevel=0.006, axis="X")
    return o


def module(prefix, end=False, seed=0):
    rnd = random.Random(seed)
    deck, frame, piles, metal, iron = [], [], [], [], []
    pile_y = (1.0, 3.86) if end else (1.0, 3.0)
    str_len = 3.96 if end else 3.996
    # 床板
    for k in range(N_PLANK):
        deck.append(plank(f"{prefix}_plank{k}", (k + 0.5) * PITCH, rnd))
    # 縦桁
    for i, x in enumerate(STRINGER_X):
        frame.append(T.board(f"{prefix}_str{i}", (STR_W, str_len, STR_H),
                             loc=(x, 0.002 + str_len / 2, CAP_TOP + STR_H / 2), rnd=rnd, bevel=0.006, axis="Y"))
    for pi, py in enumerate(pile_y):
        # 杭
        for sx in (-1, 1):
            piles.append(pile(f"{prefix}_pile{pi}{sx}", sx * PILE_X, py, CAP_TOP, rnd))
        # 2 枚挟みの桁受けと貫通ボルト
        for sy in (-1, 1):
            cy = py + sy * (PILE_R + CAP_T / 2)
            frame.append(T.board(f"{prefix}_cap{pi}{sy}", (2.3, CAP_T, CAP_H),
                                 loc=(rnd.uniform(-0.01, 0.01), cy, CAP_TOP - CAP_H / 2), rnd=rnd, bevel=0.008,
                                 axis="X"))
        for sx in (-1, 1):
            for dz in (-0.05, 0.06):
                metal += through_bolt(f"{prefix}_cb{pi}{sx}{dz}", (sx * PILE_X + dz * 0.6, py, CAP_TOP - CAP_H / 2 + dz),
                                      (0, 1, 0), PILE_R + CAP_T)
        # 縦桁と桁受けの金物
        for x in STRINGER_X:
            for sy in (-1, 1):
                if end and pi == 1 and sy > 0:
                    continue  # 先端側は縦桁が鼻隠しの手前で終わるので金物なし
                side = 1 if x < 0 else -1
                metal += bracket(f"{prefix}_br{pi}{x}{sy}", x, py + sy * (PILE_R + CAP_T / 2), side)
        # X 筋交い（杭の両面に 1 枚ずつ）
        for sy, (za, zb) in ((-1, (0.42, -1.55)), (1, (-1.55, 0.42))):
            by = py + sy * (PILE_R + 0.025)
            pa = Vector((-PILE_X - 0.1, by, za))
            pb = Vector((PILE_X + 0.1, by, zb))
            dvec = pb - pa
            ang = math.atan2(dvec.z, dvec.x)
            br = T.board(f"{prefix}_xb{pi}{sy}", (dvec.length, 0.05, 0.15), loc=(pa + pb) / 2,
                         rot=(0, -ang, 0), rnd=rnd, bevel=0.006, axis="X")
            frame.append(br)
            for p in (pa + dvec.normalized() * 0.1, pb - dvec.normalized() * 0.1):
                metal += bolt(f"{prefix}_xbb{pi}{sy}{p.z:.1f}", (p.x, by + sy * 0.025, p.z), (0, sy, 0))
            mid = (pa + pb) / 2
            metal += bolt(f"{prefix}_xbm{pi}{sy}", (mid.x, by + sy * 0.025, mid.z), (0, sy, 0))
    # 長さ方向の筋交い（両側面）
    for sx in (-1, 1):
        bx = sx * (PILE_X + PILE_R + 0.025)
        pa = Vector((bx, pile_y[0] - 0.1, 0.4))
        pb = Vector((bx, pile_y[1] + 0.1, -1.3))
        dvec = pb - pa
        ang = math.atan2(dvec.z, dvec.y)
        frame.append(T.board(f"{prefix}_lb{sx}", (0.05, dvec.length, 0.15), loc=(pa + pb) / 2,
                             rot=(ang, 0, 0), rnd=rnd, bevel=0.006, axis="Y"))
        for p in (pa + dvec.normalized() * 0.1, pb - dvec.normalized() * 0.1):
            metal += bolt(f"{prefix}_lbb{sx}{p.z:.1f}", (bx + sx * 0.025, p.y, p.z), (sx, 0, 0))
    if end:
        e = end_parts(prefix, pile_y[1], rnd)
        frame += e["frame"]
        metal += e["metal"]
        iron += e["iron"]

    # 杭 4 本をまとめて展開（縦のシーム + 頭と底）
    pj = C.join(piles, f"{prefix}_piles")
    T.unwrap(pj, seam_angle=40.0)
    mats = [(deck, deck_material()), (frame, frame_material()), ([pj], piling_material()),
            (metal, metal_material()), (iron, iron_material())]
    out = []
    for objs, mat in mats:
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return out


def ladder(prefix, x0, y_face, z_bottom):
    """亜鉛めっきパイプのはしご。上は床の上へ鳥居型に曲がって床に留まる"""
    parts = []
    w = 0.44
    yr = y_face + 0.16
    for k, sx in enumerate((-1, 1)):
        x = x0 + sx * w / 2
        pts = [Vector((x, yr, z_bottom))]
        pts += [Vector((x, yr, z)) for z in (0.0, 1.0, 1.55)]
        pts += list(geo.bezier_points((x, yr, 1.55), (x, yr, 1.95), (x, y_face - 0.2, 1.95), (x, y_face - 0.2, 1.55), 10))[1:]
        pts += [Vector((x, y_face - 0.2, DECK_TOP + 0.01))]
        t = geo.tube_along(pts, [0.024] * len(pts), sides=10, name=f"{prefix}_rail{k}")
        C.set_smooth(t, True, angle=50)
        parts.append(t)
        # 床への座金プレート
        parts.append(T.lathe(f"{prefix}_foot{k}", [(0.0, 0.0), (0.05, 0.0), (0.05, 0.008), (0.03, 0.012), (0.0, 0.012)], 10))
        parts[-1].location = (x, y_face - 0.2, DECK_TOP)
    # 踏み桟
    z = z_bottom + 0.2
    while z < DECK_TOP - 0.1:
        r = T.rod(f"{prefix}_rung{z:.2f}", (x0 - w / 2, yr, z), (x0 + w / 2, yr, z), 0.017, sides=8)
        parts.append(r)
        z += 0.3
    # 控え金物（上: 鼻隠し板、下: 腹起こし）
    for zb, yb in ((0.85, y_face + 0.04), (-0.35, y_face + 0.075)):
        for sx in (-1, 1):
            x = x0 + sx * w / 2
            parts.append(T.rod(f"{prefix}_st{zb}{sx}", (x, yb, zb), (x, yr, zb), 0.014, sides=8))
            p = geo.box(f"{prefix}_stp{zb}{sx}", (0.08, 0.008, 0.1), loc=(x, yb + 0.004, zb))
            parts.append(p)
            parts += bolt(f"{prefix}_stb{zb}{sx}", (x, yb + 0.008, zb + 0.03), (0, 1, 0), head=0.012, washer=0.018)
    return parts


def bollard(name):
    """係船柱（鋳鉄のビット）"""
    prof = [(0.0, 0.0), (0.13, 0.0), (0.13, 0.02), (0.1, 0.035), (0.085, 0.05), (0.078, 0.2), (0.08, 0.24),
            (0.11, 0.27), (0.115, 0.29), (0.1, 0.31), (0.05, 0.325), (0.0, 0.33)]
    return T.lathe(name, prof, segs=18)


def cleat(name):
    """ホーンクリート（長さ 0.32m、X 方向）"""
    spec = [(-0.16, 0.008, 0.056, 0.066), (-0.14, 0.013, 0.05, 0.074), (-0.1, 0.018, 0.046, 0.078),
            (-0.07, 0.024, 0.0, 0.08), (-0.035, 0.026, 0.0, 0.08), (-0.015, 0.021, 0.035, 0.076)]
    spec = spec + [(-x, w, z0, z1) for x, w, z0, z1 in reversed(spec)]
    rings = []
    for x, w, z0, z1 in spec:
        zc, hz = (z0 + z1) / 2, (z1 - z0) / 2
        ring = []
        for k in range(10):
            a = k / 10 * math.tau
            c, s = math.cos(a), math.sin(a)
            # 角の丸い長方形
            ring.append((x, w * math.copysign(abs(c) ** 0.6, c), zc + hz * math.copysign(abs(s) ** 0.6, s)))
        rings.append(ring)
    return T.loft(name, rings)


def end_parts(prefix, py, rnd):
    frame, metal, iron = [], [], []
    # 鼻隠し板（縦桁の端を隠す）と腹起こし（杭の外面、はしごの下の控え）
    frame.append(T.board(f"{prefix}_fascia", (2.06, 0.04, 0.28), loc=(0, 4.015, DECK_TOP - 0.14), rnd=rnd,
                         bevel=0.006, axis="X"))
    wale_y = py + PILE_R + 0.05 + CAP_T / 2
    frame.append(T.board(f"{prefix}_wale", (2.3, CAP_T, 0.2), loc=(0, wale_y, -0.35), rnd=rnd, bevel=0.008, axis="X"))
    for sx in (-1, 1):
        metal += through_bolt(f"{prefix}_wb{sx}", (sx * PILE_X, py + 0.05, -0.35), (0, 1, 0), PILE_R + 0.05 + CAP_T)
        metal += bolt(f"{prefix}_fb{sx}", (sx * 0.6, 4.035, DECK_TOP - 0.12), (0, 1, 0))
    metal += ladder(prefix, 0.3, 4.035, -1.6)
    # 係船柱 2 基（先端の角）とクリート 2 個（両舷）
    for sx in (-1, 1):
        b = bollard(f"{prefix}_bitt{sx}")
        # 右側ははしごの上端を避けて少し手前に置く
        b.location = (-0.62, 3.5, DECK_TOP) if sx < 0 else (0.72, 3.25, DECK_TOP)
        iron.append(b)
        for k in range(4):
            a = k / 4 * math.tau + math.pi / 4
            metal += bolt(f"{prefix}_bb{sx}{k}", (b.location.x + 0.105 * math.cos(a), b.location.y + 0.105 * math.sin(a),
                                                  DECK_TOP + 0.02), (0, 0, 1), head=0.012, washer=0.016)
        c = cleat(f"{prefix}_cleat{sx}")
        c.matrix_world = Matrix.Translation((sx * 0.9, 1.9, DECK_TOP)) @ Matrix.Rotation(math.pi / 2, 4, "Z")
        iron.append(c)
        for dy in (-0.05, 0.05):
            metal += bolt(f"{prefix}_clb{sx}{dy}", (sx * 0.9, 1.9 + dy * 1.0, DECK_TOP + 0.0), (0, 0, 1),
                          head=0.009, washer=0.012)
    return dict(frame=frame, metal=metal, iron=iron)


def build():
    return {
        "Pier_Straight": module("PierS", end=False, seed=3),
        "Pier_End": module("PierE", end=True, seed=4),
    }
