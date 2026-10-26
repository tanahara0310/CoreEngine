"""インディアン・オーク号の遊具と由来の石碑（2 バリエーション）

1840 年に北谷沖で座礁した英国船を模した、砂に半分埋まった帆船の遊具。
船体は風化した灰茶の板張り、3 本のマストに帆の無い帆桁と見張り台、縄はしごの網。
右舷側に赤い鉄骨の登り台・斜路・すべり台、船尾に筒のすべり台、まわりに青いゴムの床。

座標: z=0 が地面。原点は船体の中心の真下。船首が +X、正面（遊具の付く側）が -Y。
  IndianOak_Ship    : 船の遊具一式（長さ 20 m・幅 5.6 m、船首の帆柱を入れて約 26 m）
  IndianOak_Monument: 由来を書いた黒い銘板をはめた自然石の碑（高さ 1.4 m）
"""
import math
import os
import random
import tempfile

import bpy
import numpy as np
from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from . import _timber as T

PREVIEW = dict(cam_dir=(0.55, -1.0, 0.42), lens=38, spacing=1.08)

L_HALF = 10.0
KEEL = -1.0
DECK_INSET = 0.94        # 甲板の縁 = 半幅 × これ
STERN_X = -5.2           # 船尾楼の前端
FORE_X = 6.4             # 船首楼の後端
STERN_RISE = 1.3
FORE_RISE = 0.8
MASTS = [  # (x, 地面からの高さ, 見張り台の高さ, [(帆桁の高さ, 長さ)...])
    (4.6, 11.0, 7.0, [(5.6, 5.2), (8.4, 4.4), (10.2, 3.2)]),
    (-0.4, 12.6, 7.6, [(5.8, 6.0), (9.0, 5.0), (11.6, 3.6)]),
    (-6.8, 10.0, None, [(6.6, 4.0), (8.8, 3.0)]),
]
FONT_JA = ["C:/Windows/Fonts/YuGothB.ttc", "C:/Windows/Fonts/meiryob.ttc",
           "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf"]


# ---------------------------------------------------------------------------
# 船体の形
# ---------------------------------------------------------------------------
def half_beam(x):
    if x > 2.0:
        return max(0.06, 2.8 * math.sqrt(max(0.0, 1.0 - ((x - 2.0) / 8.05) ** 2)))
    if x < -3.0:
        return 2.8 - 0.7 * ((-x - 3.0) / 7.0) ** 2
    return 2.8


def sheer(x):
    if x < -2.0:
        return 2.6 + 0.3 * ((-x - 2.0) / 8.0) ** 2
    if x > 2.0:
        return 2.6 + 0.6 * ((x - 2.0) / 8.0) ** 2
    return 2.6


def keel(x):
    if x > 5.5:
        return KEEL + 2.4 * ((x - 5.5) / 4.5) ** 2
    return KEEL


HALF_SECTION = [(0.0, 0.0), (0.45, 0.06), (0.75, 0.2), (0.93, 0.4), (1.0, 0.62), (0.985, 0.82), (DECK_INSET, 1.0)]


def deck_y(x):
    return half_beam(x) * DECK_INSET


def hull_mesh():
    xs = list(np.linspace(-L_HALF, 6.0, 33)) + list(np.linspace(6.0, L_HALF, 17))[1:]
    rings = []
    for x in xs:
        b, s, zk = half_beam(x), sheer(x), keel(x)
        right = [Vector((x, b * u, zk + v * (s - zk))) for u, v in HALF_SECTION]
        deck = [Vector((x, deck_y(x) * t, s + 0.06 * (1 - t * t))) for t in (0.5, 0.0, -0.5)]
        left = [Vector((p.x, -p.y, p.z)) for p in reversed(right[1:])]
        rings.append(right + deck + left)
    obj = T.loft("hull", rings, cap_start=True, cap_end=True, closed=True)
    C.set_smooth(obj, True, angle=40)
    return obj


def castle(name, x0, x1, rise, n=10):
    """船尾楼・船首楼（船体の側面をそのまま上へ伸ばした箱）"""
    rings = []
    for x in np.linspace(x0, x1, n):
        y = deck_y(x)
        s = sheer(x)
        rings.append([Vector((x, y, s - 0.1)), Vector((x, y * 1.0, s + rise)), Vector((x, -y, s + rise)),
                      Vector((x, -y, s - 0.1))])
    obj = T.loft(name, rings, cap_start=True, cap_end=True, closed=True)
    return obj


def split_deck_faces(obj, deck_slot=1):
    """上向きの面を甲板の材質スロットへ"""
    for p in obj.data.polygons:
        if p.normal.z > 0.7 and p.center.z > 1.5:
            p.material_index = deck_slot


# ---------------------------------------------------------------------------
# 部品
# ---------------------------------------------------------------------------
def wales():
    """舷側の赤茶の帯（上と下の 2 本）と丸窓"""
    strips, rims, glass = [], [], []
    for side in (-1, 1):
        for dz, r in ((-0.22, 0.075), (-0.95, 0.06)):
            pts = []
            for x in np.linspace(-L_HALF + 0.05, 9.6, 60):
                b = half_beam(x)
                s = sheer(x)
                # その高さでの船体の幅
                zk = keel(x)
                v = (s + dz - zk) / (s - zk)
                u = float(np.interp(v, [q[1] for q in HALF_SECTION], [q[0] for q in HALF_SECTION]))
                pts.append(Vector((x, side * (b * u + 0.02), s + dz)))
            t = geo.tube_along(pts, [r] * len(pts), sides=8, name=f"wale{side}{dz}")
            strips.append(t)
    for side in (-1, 1):
        for x in (4.8, 6.0, 7.2, -3.0, -4.2):
            s = sheer(x)
            z = s - 0.6
            zk = keel(x)
            v = (z - zk) / (s - zk)
            u = float(np.interp(v, [q[1] for q in HALF_SECTION], [q[0] for q in HALF_SECTION]))
            y = side * half_beam(x) * u
            prof = [(0.13, -0.02), (0.2, -0.02), (0.2, 0.04), (0.13, 0.05)]
            ring = T.lathe(f"port{side}{x}", prof + [prof[0]], segs=16, fix_normals=False)
            T.recalc_normals(ring)
            T.orient(ring, (x, y, z), (0, side, 0))
            rims.append(ring)
            g = T.lathe(f"portg{side}{x}", [(0.0, 0.0), (0.135, 0.0), (0.135, 0.02), (0.0, 0.02)], segs=16)
            T.orient(g, (x, y, z), (0, side, 0))
            glass.append(g)
    return strips, rims, glass


def iron_fence(name, pts, h=1.0, bar=0.13):
    """黒い鉄の柵（上下の横桟と縦の桟）"""
    parts = []
    pts = [Vector(p) for p in pts]
    for z, r in ((h, 0.03), (0.12, 0.02)):
        line = [p + Vector((0, 0, z)) for p in pts]
        parts.append(geo.tube_along(line, [r] * len(line), sides=8, name=f"{name}_r{z}"))
    acc = 0.0
    for a, b in zip(pts[:-1], pts[1:]):
        L = (b - a).length
        n = max(1, int(L / bar))
        for k in range(n):
            p = a.lerp(b, (k + 0.5) / n)
            parts.append(T.rod(f"{name}_b{acc}", p, p + Vector((0, 0, h)), 0.011, sides=5))
            acc += 1
        parts.append(T.rod(f"{name}_p{acc}", a, a + Vector((0, 0, h + 0.05)), 0.035, sides=8))
    parts.append(T.rod(f"{name}_pe", pts[-1], pts[-1] + Vector((0, 0, h + 0.05)), 0.035, sides=8))
    return parts


def balustrade(name, pts, h=0.95, pitch=0.2):
    """木の手すり（親柱・笠木・ろくろ挽きの手すり子）"""
    parts = []
    pts = [Vector(p) for p in pts]
    prof = [(0.0, 0.0), (0.045, 0.0), (0.045, 0.05), (0.03, 0.09), (0.028, 0.3), (0.05, 0.45), (0.03, 0.6),
            (0.028, h - 0.12), (0.045, h - 0.08), (0.045, h - 0.06), (0.0, h - 0.06)]
    for i, (a, b) in enumerate(zip(pts[:-1], pts[1:])):
        d = b - a
        L = d.length
        ang = math.atan2(d.y, d.x)
        rail = T.board(f"{name}_rail{i}", (L + 0.12, 0.14, 0.07), loc=(a + b) / 2 + Vector((0, 0, h - 0.03)),
                       rot=(0, 0, ang), axis="X")
        parts.append(rail)
        bot = T.board(f"{name}_bot{i}", (L, 0.1, 0.06), loc=(a + b) / 2 + Vector((0, 0, 0.03)), rot=(0, 0, ang),
                      axis="X")
        parts.append(bot)
        n = max(1, int(L / pitch))
        for k in range(n):
            p = a.lerp(b, (k + 0.5) / n)
            bal = T.lathe(f"{name}_bal{i}_{k}", prof, segs=8)
            bal.location = p
            T.wood_attrs(bal, random.Random(i * 100 + k), axis="Z")
            parts.append(bal)
    for i, p in enumerate(pts):
        post = T.board(f"{name}_post{i}", (0.14, 0.14, h + 0.12), loc=p + Vector((0, 0, (h + 0.12) / 2)), axis="Z")
        parts.append(post)
    return parts


def masts():
    wood, iron, nets, ropes = [], [], [], []
    tops = []
    for mi, (x, top, nest, yards) in enumerate(MASTS):
        base_z = sheer(x) + (STERN_RISE if x < STERN_X else 0.0)
        prof = [(0.0, base_z - 0.2), (0.17, base_z - 0.2), (0.17, base_z + 0.1), (0.15, base_z + 0.3),
                (0.14, nest or (top * 0.6)), (0.1, top - 0.6), (0.075, top - 0.05), (0.09, top), (0.0, top + 0.06)]
        m = T.lathe(f"mast{mi}", prof, segs=12)
        m.location = (x, 0, 0)
        T.wood_attrs(m, random.Random(mi), axis="Z", pith=(0.0, 0.02))
        wood.append(m)
        tops.append(Vector((x, 0, top)))
        for yi, (yz, yl) in enumerate(yards):
            brace = math.radians((-14, 10, -6)[yi % 3] + mi * 3)
            d = Vector((math.sin(brace), math.cos(brace), 0.0)) * (yl / 2)
            c = Vector((x + 0.2, 0, yz))
            y = T.rod(f"yard{mi}{yi}", c - d, c + d, 0.075, sides=10, radius1=0.075)
            T.wood_attrs(y, random.Random(mi * 10 + yi), axis="Z", pith=(0.0, 0.02))
            wood.append(y)
            # 帆桁の中央を細く、両端を絞る（2 本に分けて先細り）
            for s in (-1, 1):
                tip = T.rod(f"yardtip{mi}{yi}{s}", c + d * s, c + d * s * 1.12, 0.06, sides=8, radius1=0.035)
                T.wood_attrs(tip, random.Random(mi * 10 + yi + s), axis="Z")
                wood.append(tip)
            # 帆桁の吊り索
            ropes.append(geo.tube_along([c + d * 0.95 + Vector((0, 0, 0.05)), Vector((x, 0, yz + 1.0))], [0.012] * 2,
                                        sides=5, name=f"lift{mi}{yi}"))
        if nest:
            R = 0.8
            plat = T.lathe(f"nest{mi}", [(0.0, nest - 0.14), (R, nest - 0.14), (R, nest), (0.0, nest)], segs=20)
            plat.location = (x, 0, 0)
            T.wood_attrs(plat, random.Random(mi + 50), axis="X")
            wood.append(plat)
            # 支えの腕木
            for a in range(4):
                ang = a * math.pi / 2 + math.pi / 4
                p0 = Vector((x + math.cos(ang) * 0.15, math.sin(ang) * 0.15, nest - 0.9))
                p1 = Vector((x + math.cos(ang) * R * 0.9, math.sin(ang) * R * 0.9, nest - 0.14))
                wood.append(T.rod(f"nestarm{mi}{a}", p0, p1, 0.05, sides=6))
            # 網の囲い（円筒）と上の輪
            nets.append(_net_cylinder(f"nestnet{mi}", Vector((x, 0, nest)), R, 0.9))
            ring = [Vector((x + math.cos(a) * R, math.sin(a) * R, nest + 0.9)) for a in np.linspace(0, math.tau, 33)]
            iron.append(geo.tube_along(ring, [0.025] * len(ring), sides=6, name=f"nestring{mi}", cap_start=False,
                                       cap_end=False))
            for a in range(8):
                ang = a * math.tau / 8
                p = Vector((x + math.cos(ang) * R, math.sin(ang) * R, nest))
                iron.append(T.rod(f"nestpost{mi}{a}", p, p + Vector((0, 0, 0.9)), 0.018, sides=6))
            # 縄はしごの網（舷側から見張り台へ）
            for side in (-1, 1):
                yd = deck_y(x)
                s = sheer(x)
                quad = [Vector((x - 1.0, side * yd, s + 0.15)), Vector((x + 1.0, side * yd, s + 0.15)),
                        Vector((x + 0.35, side * R * 0.95, nest)), Vector((x - 0.35, side * R * 0.95, nest))]
                nets.append(_net_quad(f"shroud{mi}{side}", quad))
                # 見張り台から上の索
                for dx in (-0.3, 0.3):
                    ropes.append(geo.tube_along([Vector((x + dx, side * R * 0.95, nest + 0.05)),
                                                 Vector((x, side * 0.08, top - 0.8))], [0.014] * 2, sides=5,
                                                name=f"topshroud{mi}{side}{dx}"))
                # 網の縁の太い索
                for a, b in ((quad[0], quad[3]), (quad[1], quad[2])):
                    ropes.append(geo.tube_along([a, b], [0.022] * 2, sides=6, name=f"edge{mi}{side}{a.x:.1f}"))
        else:
            for side in (-1, 1):
                yd = deck_y(x)
                for dx in (-0.6, 0.0, 0.6):
                    ropes.append(geo.tube_along([Vector((x + dx, side * yd, sheer(x) + STERN_RISE + 0.9)),
                                                 Vector((x, side * 0.08, top - 1.2))], [0.016] * 2, sides=5,
                                                name=f"mshroud{side}{dx}"))
    return wood, iron, nets, ropes, tops


def _net_cylinder(name, c, R, h, segs=24):
    verts, faces, uvs = [], [], []
    for j in range(2):
        for i in range(segs + 1):
            a = i / segs * math.tau
            verts.append((c.x + math.cos(a) * R, c.y + math.sin(a) * R, c.z + j * h))
    for i in range(segs):
        a, b = i, i + 1
        faces.append((a, b, b + segs + 1, a + segs + 1))
        u0, u1 = i / segs * math.tau * R, (i + 1) / segs * math.tau * R
        uvs.append([(u0, 0.0), (u1, 0.0), (u1, h), (u0, h)])
    o = C.mesh_object(name, verts, faces, uvs)
    return o


def _net_quad(name, q, n=6):
    """4 点の網（UV は m 単位）"""
    verts, faces, uvs = [], [], []
    w0 = (q[1] - q[0]).length
    hgt = ((q[3] + q[2]) / 2 - (q[0] + q[1]) / 2).length
    for j in range(n + 1):
        t = j / n
        a = q[0].lerp(q[3], t)
        b = q[1].lerp(q[2], t)
        for i in range(n + 1):
            verts.append(tuple(a.lerp(b, i / n)))
    for j in range(n):
        for i in range(n):
            k = j * (n + 1) + i
            faces.append((k, k + 1, k + n + 2, k + n + 1))
            uvs.append([(w0 * i / n, hgt * j / n), (w0 * (i + 1) / n, hgt * j / n),
                        (w0 * (i + 1) / n, hgt * (j + 1) / n), (w0 * i / n, hgt * (j + 1) / n)])
    return C.mesh_object(name, verts, faces, uvs)


def bowsprit_and_stays(tops):
    wood, ropes = [], []
    p0 = Vector((8.6, 0, sheer(8.6) + FORE_RISE + 0.1))
    p1 = Vector((15.6, 0, 5.2))
    bs = T.rod("bowsprit", p0, p1, 0.17, sides=12, radius1=0.08)
    T.wood_attrs(bs, random.Random(77), axis="Z", pith=(0.0, 0.02))
    wood.append(bs)
    # 前の索（帆柱の先 → 前マストの上）、マストの上どうし、後ろのマスト → 船尾
    fore, main, mizzen = tops
    stays = [(p1, fore - Vector((0, 0, 0.3))), (p0.lerp(p1, 0.6), fore - Vector((0, 0, 3.0))),
             (fore - Vector((0, 0, 0.2)), main - Vector((0, 0, 1.6))), (main - Vector((0, 0, 0.2)), mizzen - Vector((0, 0, 0.6))),
             (mizzen - Vector((0, 0, 0.2)), Vector((-L_HALF + 0.3, 0, sheer(-L_HALF) + STERN_RISE + 0.9)))]
    for i, (a, b) in enumerate(stays):
        pts = [a.lerp(b, t) - Vector((0, 0, 0.12 * math.sin(math.pi * t))) for t in np.linspace(0, 1, 8)]
        ropes.append(geo.tube_along(pts, [0.016] * len(pts), sides=6, name=f"stay{i}"))
    # 帆柱の下の控え索
    ropes.append(geo.tube_along([p1, Vector((9.9, 0, keel(9.9) + 0.6))], [0.018] * 2, sides=6, name="bobstay"))
    return wood, ropes


def ship_wheel():
    """船首楼の舵輪"""
    parts = []
    x = 7.4
    z = sheer(x) + FORE_RISE + 1.05
    rim = [(0.42 + 0.03 * math.cos(a), 0.03 * math.sin(a)) for a in np.linspace(0, math.tau, 9)]
    w = T.lathe("wheelrim", rim, segs=20, fix_normals=False)
    T.recalc_normals(w)
    T.orient(w, (x, 0, z), (1, 0, 0))
    parts.append(w)
    for k in range(8):
        a = k * math.tau / 8
        d = Vector((0, math.cos(a), math.sin(a)))
        parts.append(T.rod(f"spoke{k}", Vector((x, 0, z)) + d * 0.05, Vector((x, 0, z)) + d * 0.6, 0.022, sides=6))
    parts.append(T.rod("wheelhub", (x - 0.06, 0, z), (x + 0.06, 0, z), 0.08, sides=10))
    parts.append(T.board("wheelpost", (0.18, 0.22, 1.05), loc=(x + 0.12, 0, z - 0.5), axis="Z"))
    for p in parts:
        if "WoodCo" not in p.data.color_attributes:
            T.wood_attrs(p, random.Random(len(p.name)), axis="Z")
    return parts


# ---------------------------------------------------------------------------
# 遊具（赤い鉄骨・すべり台・筒のすべり台・床）
# ---------------------------------------------------------------------------
def red_play():
    red = []
    deck_z = sheer(6.0)
    # 登り台（4 本柱の塔）。甲板と同じ高さの床
    tx, ty = 6.6, -4.5
    w = 1.6
    for sx in (-1, 1):
        for sy in (-1, 1):
            red.append(T.rod(f"tw_post{sx}{sy}", (tx + sx * w / 2, ty + sy * w / 2, 0.0),
                             (tx + sx * w / 2, ty + sy * w / 2, deck_z + 1.4), 0.06, sides=10))
    floor = geo.box("tw_floor", (w + 0.05, w + 0.05, 0.08), loc=(tx, ty, deck_z - 0.04))
    red.append(floor)
    roof = geo.cylinder("tw_roof", w * 0.85, 0.9, loc=(tx, ty, deck_z + 1.85), verts=4, radius2=0.02)
    roof.rotation_euler = (0, 0, math.pi / 4)
    red.append(roof)
    # 塔の手すり（三方）
    for a, b in (((-1, -1), (1, -1)), ((1, -1), (1, 1)), ((-1, -1), (-1, 1))):
        p0 = Vector((tx + a[0] * w / 2, ty + a[1] * w / 2, deck_z))
        p1 = Vector((tx + b[0] * w / 2, ty + b[1] * w / 2, deck_z))
        for z in (0.5, 1.0):
            red.append(T.rod(f"tw_rail{a}{b}{z}", p0 + Vector((0, 0, z)), p1 + Vector((0, 0, z)), 0.025, sides=8))
    # 塔から甲板への橋
    yd = deck_y(tx)
    bridge = geo.box("tw_bridge", (1.0, -yd - (ty + w / 2), 0.08), loc=(tx, (ty + w / 2 - yd) / 2, deck_z - 0.04))
    red.append(bridge)
    for sx in (-1, 1):
        a = Vector((tx + sx * 0.5, ty + w / 2, deck_z + 0.9))
        b = Vector((tx + sx * 0.5, -yd, deck_z + 0.9))
        red.append(T.rod(f"br_rail{sx}", a, b, 0.025, sides=8))
        for t in np.linspace(0, 1, 6):
            p = a.lerp(b, t)
            red.append(T.rod(f"br_bar{sx}{t:.2f}", p - Vector((0, 0, 0.9)), p, 0.015, sides=6))
    # 地面から塔への斜路（両側に縦桟の柵）
    g0 = Vector((14.0, -5.4, 0.25))
    g1 = Vector((tx + w / 2, ty, deck_z))
    d = g1 - g0
    L = d.length
    ang = math.atan2(d.y, d.x)
    pitch = math.atan2(d.z, math.hypot(d.x, d.y))
    ramp = geo.box("ramp", (L, 1.1, 0.08), loc=(g0 + g1) / 2)
    ramp.rotation_euler = (0, -pitch, ang)
    red.append(ramp)
    side = Vector((-math.sin(ang), math.cos(ang), 0))
    for s in (-1, 1):
        a = g0 + side * s * 0.55
        b = g1 + side * s * 0.55
        red.append(T.rod(f"ramp_rail{s}", a + Vector((0, 0, 0.95)), b + Vector((0, 0, 0.95)), 0.03, sides=8))
        for k, t in enumerate(np.linspace(0, 1, 24)):
            p = a.lerp(b, t)
            red.append(T.rod(f"ramp_bar{s}{k}", p, p + Vector((0, 0, 0.95)), 0.016, sides=6))
        # 斜路の下の支柱
        for k, t in enumerate((0.3, 0.65)):
            p = a.lerp(b, t)
            red.append(T.rod(f"ramp_leg{s}{k}", Vector((p.x, p.y, 0)), p, 0.05, sides=8))
    # 踏み桟
    for k, t in enumerate(np.linspace(0.04, 0.96, 18)):
        p = g0.lerp(g1, t) + Vector((0, 0, 0.05))
        red.append(T.rod(f"ramp_step{k}", p - side * 0.5, p + side * 0.5, 0.02, sides=6))
    # 塔から外へ下りる直線のすべり台（両側の壁付き）
    s0 = Vector((tx, ty - w / 2, deck_z))
    s1 = Vector((tx - 0.5, ty - 6.2, 0.35))
    pts = [s0.lerp(s1, t) + Vector((0, 0, 0.25 * math.sin(math.pi * t) * -1)) for t in np.linspace(0, 1, 14)]
    pts.append(s1 + Vector((0, -0.6, 0.0)))
    chute = _chute("slide", pts, 0.8)
    red.append(chute)
    for k, t in enumerate((0.45, 0.8)):
        p = s0.lerp(s1, t)
        for sx in (-1, 1):
            red.append(T.rod(f"slide_leg{k}{sx}", Vector((p.x + sx * 0.4, p.y, 0)), Vector((p.x + sx * 0.4, p.y, p.z - 0.15)),
                             0.04, sides=8))
    return red


def _chute(name, pts, width, wall=0.28):
    """U 字断面のすべり台を点列に沿って掃引"""
    prof = [(-width / 2 - 0.03, wall), (-width / 2 - 0.03, -0.05), (width / 2 + 0.03, -0.05), (width / 2 + 0.03, wall),
            (width / 2, wall), (width / 2, 0.0), (-width / 2, 0.0), (-width / 2, wall)]
    obj, _ = T.sweep(name, pts, prof)
    C.set_smooth(obj, True, angle=40)
    return obj


def tube_slide():
    """船尾の筒のすべり台（らせん）"""
    parts = []
    top = sheer(-8.5) + STERN_RISE
    c = Vector((-8.4, -5.0, 0))
    R = 1.5
    pts = []
    turns = 1.15
    n = 46
    for k in range(n):
        t = k / (n - 1)
        a = math.pi / 2 + t * turns * math.tau
        z = top - 0.1 - t * (top - 0.75)
        pts.append(Vector((c.x + math.cos(a) * R, c.y + math.sin(a) * R, z)))
    # 入口は船尾楼の縁、出口は地面へ水平に
    pts.insert(0, Vector((-8.4, -deck_y(-8.4) + 0.05, top - 0.05)))
    last = pts[-1]
    tangent = (pts[-1] - pts[-2]).normalized()
    tangent.z = 0
    tangent.normalize()
    pts.append(last + tangent * 0.8 + Vector((0, 0, -0.35)))
    pts.append(last + tangent * 1.6 + Vector((0, 0, -0.4)))
    t = geo.tube_along(pts, [0.45] * len(pts), sides=20, name="tube", cap_start=False, cap_end=False)
    # 筒の内側（裏面）も見えるように内壁を作る
    inner = geo.tube_along(pts, [0.41] * len(pts), sides=20, name="tube_in", cap_start=False, cap_end=False)
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(inner.data)
    bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.to_mesh(inner.data)
    bm.free()
    C.set_smooth(t, True)
    C.set_smooth(inner, True)
    parts += [t, inner]
    # 支柱
    for k in (12, 24, 36):
        p = pts[k]
        parts.append(T.rod(f"tube_leg{k}", Vector((p.x, p.y, 0)), p - Vector((0, 0, 0.4)), 0.07, sides=10))
    return parts


def rubber_floor():
    """船のまわりの青いゴムチップの床（角を丸めた長方形）"""
    x0, x1, y0, y1, r = -13.0, 16.5, -12.5, 5.0, 3.0
    pts = []
    for cx, cy, a0 in ((x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270)):
        for k in range(7):
            a = math.radians(a0 + k * 15)
            pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    bm_verts = [(x, y, -0.04) for x, y in pts]
    top = C.mesh_object("floor", bm_verts, [tuple(range(len(pts)))])
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(top.data)
    # 底の面を上へ押し出して上面にする（底の面は残さない）
    ret = bmesh.ops.extrude_face_region(bm, geom=bm.faces[:])
    vs = [g for g in ret["geom"] if isinstance(g, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=vs, vec=(0, 0, 0.08))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
    bm.to_mesh(top.data)
    bm.free()
    return top


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def hull_material():
    """風化した灰茶の板張り。板ごとの色ムラ、継ぎ目、下ほど砂と汚れ"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        ph = 0.24
        row = nb.math("FLOOR", nb.math("DIVIDE", Z, ph))
        fz = nb.math("FRACT", nb.math("DIVIDE", Z, ph))
        seam_h = nb.smooth(nb.math("MINIMUM", fz, nb.sub(1.0, fz)), 0.05, 0.015)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="2D")
        nb.link(nb.comb(row, 0.0, 0.0), wn.inputs["Vector"])
        shift = wn.outputs["Value"]
        u = nb.math("DIVIDE", nb.add(X, nb.mul(shift, 3.7)), 3.2)
        seg = nb.math("FLOOR", u)
        fu = nb.math("FRACT", u)
        butt = nb.smooth(nb.math("MINIMUM", fu, nb.sub(1.0, fu)), 0.006, 0.002)
        wn2 = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="2D")
        nb.link(nb.comb(row, seg, 0.0), wn2.inputs["Vector"])
        tone = wn2.outputs["Value"]
        grain = nb.noise(nb.mapping(co, scale=(0.6, 0.6, 18.0)), 2.0, 5, 0.6)
        base = nb.ramp(tone, [(0.0, srgb("#5a5047")), (0.4, srgb("#71665a")), (0.75, srgb("#867a6b")),
                              (1.0, srgb("#6a5a4a"))])
        col = nb.hsv(base, 0.5, 1.0, nb.maprange(grain, 0.3, 0.7, 0.85, 1.1))
        joint = nb.math("MAXIMUM", seam_h, butt)
        col = nb.mix(col, srgb("#2a241f"), nb.mul(joint, 0.85))
        # 釘の列
        streak = nb.noise(nb.mapping(co, scale=(12.0, 12.0, 0.6)), 1.5, 4, 0.55)
        col = nb.mix(col, srgb("#3b332c"), nb.mul(nb.smooth(streak, 0.55, 0.72), 0.35))
        # 地面際の砂と湿り
        col = nb.mix(col, srgb("#b4a68a"), nb.mul(nb.smooth(Z, 0.5, 0.0), 0.55))
        rough = nb.mixf(0.82, 0.95, joint)
        height = nb.add(nb.mul(joint, -1.0), nb.mul(grain, 0.3))
        return dict(color=col, rough=rough, height=height, height_scale=0.006,
                    cavity=nb.maprange(joint, 0, 1, 1.0, 0.5))

    return pbr_material("ShipHull", fn, res=2048, ao_distance=1.5)


def deck_material():
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        pw = 0.16
        row = nb.math("FLOOR", nb.math("DIVIDE", Y, pw))
        fy = nb.math("FRACT", nb.math("DIVIDE", Y, pw))
        seam = nb.smooth(nb.math("MINIMUM", fy, nb.sub(1.0, fy)), 0.05, 0.015)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="2D")
        nb.link(nb.comb(row, 0.0, 0.0), wn.inputs["Vector"])
        tone = wn.outputs["Value"]
        grain = nb.noise(nb.mapping(co, scale=(0.5, 20.0, 1.0)), 2.0, 5, 0.6)
        col = nb.ramp(tone, [(0.0, srgb("#7a6c5c")), (0.5, srgb("#958673")), (1.0, srgb("#a99a85"))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(grain, 0.3, 0.7, 0.88, 1.08))
        col = nb.mix(col, srgb("#3a322b"), nb.mul(seam, 0.8))
        sand = nb.smooth(nb.noise(co, 2.0, 4, 0.6), 0.55, 0.7)
        col = nb.mix(col, srgb("#cbbd9e"), nb.mul(sand, 0.35))
        return dict(color=col, rough=0.85, height=nb.add(nb.mul(seam, -1.0), nb.mul(grain, 0.3)), height_scale=0.004)

    return pbr_material("ShipDeck", fn, res=1024, ao_distance=0.8)


def dark_wood_material(name="ShipWood", res=1024):
    """マスト・帆桁・手すり（濃い茶の木）"""
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        g = T.wood_grain(nb, co, rings=16.0, knots=0.25, crack_amt=1.0)
        col = nb.ramp(g["var"], [(0.2, srgb("#3e3127")), (0.8, srgb("#5a4838"))])
        col = nb.mix(col, srgb("#2a211a"), nb.mul(g["ring"], 0.5))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(g["fib"], 0.25, 0.75, 0.85, 1.12))
        col = nb.mix(col, srgb("#18120e"), nb.mul(g["crack"], 0.9))
        h = nb.add(nb.add(nb.mul(g["fib"], 0.45), nb.mul(g["ring"], 0.5)), nb.mul(g["crack"], -1.5))
        return dict(color=col, rough=0.78, height=h, height_scale=0.003)

    return pbr_material(name, fn, res=res, ao_distance=0.5)


def paint_material(name, color, res=512, rough=0.45, chip=0.6):
    """塗装（褪せ・はげ）"""
    def fn(nb):
        co = nb.coord("Object")
        fade = nb.noise(co, 1.5, 3, 0.5)
        col = nb.mix(srgb(color), nb.hsv(srgb(color), 0.5, 0.8, 1.15), nb.smooth(fade, 0.4, 0.75))
        n = nb.noise(co, 10.0, 6, 0.65)
        chips = nb.mul(nb.smooth(n, 0.71, 0.73), chip)
        col = nb.mix(col, srgb("#5a3e2a"), chips)
        return dict(color=col, rough=nb.mixf(rough, 0.8, chips), height=nb.sub(1.0, chips), height_scale=0.001)

    return pbr_material(name, fn, res=res, ao_distance=0.4)


def rope_material():
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 30.0, 4, 0.6)
        col = nb.mix(srgb("#8b7d63"), srgb("#a39478"), n)
        return dict(color=col, rough=0.85, height=n, height_scale=0.002)

    return pbr_material("ShipRope", fn, res=256, ao_distance=0.1, uv="keep")


def net_material():
    """縄の網（25 cm 角の目。縄の外は抜く）"""
    def fn(nb):
        uv = nb.uv("Proc")
        u, v, _ = nb.sep(uv)
        gu = nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.math("DIVIDE", u, 0.25)), 0.5))
        gv = nb.math("ABSOLUTE", nb.sub(nb.math("FRACT", nb.math("DIVIDE", v, 0.25)), 0.5))
        du = nb.mul(nb.sub(0.5, gu), 0.25)
        dv = nb.mul(nb.sub(0.5, gv), 0.25)
        d = nb.math("MINIMUM", du, dv)
        rope = nb.smooth(d, 0.016, 0.011)
        knot = nb.mul(nb.smooth(du, 0.03, 0.018), nb.smooth(dv, 0.03, 0.018))
        alpha = nb.math("MAXIMUM", rope, knot)
        twist = nb.wave(nb.mapping(uv, scale=(1, 1, 1)), 60.0, 0.0, 0.0, kind="BANDS", direction="DIAGONAL")
        col = nb.mix(srgb("#7d6f55"), srgb("#a69676"), twist)
        return dict(color=col, rough=0.85, alpha=alpha, height=nb.add(alpha, nb.mul(twist, 0.3)), height_scale=0.002)

    return pbr_material("ShipNet", fn, res=512, uv="atlas", atlas_size=(1.0, 1.0), double_sided=True)


def rubber_material():
    def fn(nb):
        co = nb.coord("Object")
        fleck = nb.voronoi(co, 180.0, out="Color")
        f = nb.bw(fleck)
        col = nb.mix(srgb("#2f6fb3"), srgb("#4b8ccc"), nb.smooth(f, 0.4, 0.7))
        col = nb.mix(col, srgb("#1d4e86"), nb.mul(nb.smooth(f, 0.15, 0.05), 0.8))
        fade = nb.noise(co, 0.4, 3, 0.5)
        col = nb.mix(col, nb.hsv(col, 0.5, 0.75, 1.15), nb.mul(nb.smooth(fade, 0.45, 0.75), 0.6))
        sand = nb.smooth(nb.noise(co, 1.5, 4, 0.6), 0.58, 0.72)
        col = nb.mix(col, srgb("#c9bc9c"), nb.mul(sand, 0.4))
        return dict(color=col, rough=0.92, height=nb.voronoi(co, 180.0), height_scale=0.002)

    return pbr_material("ShipFloor", fn, res=1024, ao_distance=1.0)


# ---------------------------------------------------------------------------
# 石碑
# ---------------------------------------------------------------------------
def plaque_image():
    """銘板（黒御影に白い文字。本文は細かな行の並び）"""
    from PIL import Image, ImageDraw, ImageFont
    W, H = 900, 1200
    im = Image.new("RGB", (W, H), (22, 22, 24))
    d = ImageDraw.Draw(im)
    font = None
    for p in FONT_JA:
        if os.path.exists(p):
            font = ImageFont.truetype(p, 78)
            break
    font = font or ImageFont.load_default()
    title = "インディアン・オーク号"
    w = d.textlength(title, font=font)
    d.text(((W - w) / 2, 70), title, fill=(232, 230, 222), font=font)
    rnd = random.Random(3)
    y = 230
    while y < H - 120:
        x = 80
        while x < W - 90:
            seg = rnd.randint(30, 120)
            d.rectangle((x, y, min(W - 80, x + seg), y + 22), fill=(170, 168, 160))
            x += seg + rnd.randint(8, 18)
        y += 52
    d.rectangle((24, 24, W - 24, H - 24), outline=(150, 148, 140), width=5)
    path = os.path.join(tempfile.gettempdir(), "araha_indian_oak_plaque.png")
    im.save(path)
    return path


def monument():
    rnd = random.Random(21)
    off = Vector((rnd.uniform(-50, 50), rnd.uniform(-50, 50), rnd.uniform(-50, 50)))
    bm = geo.icosphere_bm(5)
    for v in bm.verts:
        dd = v.co.normalized()
        h = (dd.z + 1) / 2
        w = 0.42 * (1.0 - 0.25 * h) * (1.0 + 0.12 * geo.fbm(Vector((dd.x, dd.y, h * 2)) * 1.5 + off, 3))
        t = 0.22 * (1.0 - 0.2 * h)
        z = 0.25 + h * 1.15 + 0.08 * geo.fbm(Vector((dd.x * 2, dd.y * 2, 0)) + off, 2) * (h > 0.85)
        y = dd.y * t
        # 正面（-Y）は平らに磨いた面
        if dd.y < -0.35:
            y = -0.162
        v.co = Vector((dd.x * w, y, z))
    stone = C.from_bmesh("mon_stone", bm)
    geo.decimate(stone, target_tris=6000)
    C.set_smooth(stone, True, angle=50)
    plaque = C.mesh_object("mon_plaque", [(-0.24, -0.166, 0.5), (0.24, -0.166, 0.5), (0.24, -0.166, 1.14),
                                          (-0.24, -0.166, 1.14)], [(0, 1, 2, 3)], [[(0, 0), (1, 0), (1, 1), (0, 1)]])
    bake = plaque.data.uv_layers.new(name="Bake")
    for i, dd in enumerate(plaque.data.uv_layers["Proc"].data):
        bake.data[i].uv = dd.uv
    base = geo.box("mon_base", (1.1, 0.7, 0.25), loc=(0, 0, 0.125))
    geo.bevel(base, 0.02, 2)
    return stone, plaque, base


def monument_stone_material():
    def fn(nb):
        co = nb.coord("Object")
        n1 = nb.noise(co, 3.0, 5, 0.6)
        n2 = nb.noise(co, 25.0, 4, 0.6)
        col = nb.ramp(n1, [(0.3, srgb("#6c6a64")), (0.6, srgb("#8a877e")), (0.8, srgb("#9d998e"))])
        col = nb.mix(col, srgb("#3c3a35"), nb.mul(nb.smooth(nb.noise(co, 8.0, 4, 0.6), 0.6, 0.72), 0.5))
        return dict(color=col, rough=0.8, height=nb.add(n1, nb.mul(n2, 0.4)), height_scale=0.01)

    return pbr_material("MonumentStone", fn, res=1024, ao_distance=0.4)


def plaque_material(path):
    img = bpy.data.images.load(path, check_existing=True)

    def fn(nb):
        col = nb.image(img, nb.uv("Proc"), ext="EXTEND")["Color"]
        return dict(color=col, rough=0.18)

    return pbr_material("MonumentPlaque", fn, res=1024, uv="keep", ao_distance=0.2)


def monument_base_material():
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 8.0, 4, 0.6)
        col = nb.mix(srgb("#a7a399"), srgb("#bdb9ae"), n)
        return dict(color=col, rough=0.88, height=nb.noise(co, 40.0, 3, 0.6), height_scale=0.002)

    return pbr_material("MonumentBase", fn, res=512, ao_distance=0.3)


# ---------------------------------------------------------------------------
def build():
    hull = hull_mesh()
    sc = castle("stern_castle", -L_HALF, STERN_X, STERN_RISE)
    fc = castle("fore_castle", FORE_X, 9.3, FORE_RISE)
    hmat, dmat = hull_material(), deck_material()
    for o in (hull, sc, fc):
        o.data.materials.clear()
        o.data.materials.append(hmat)
        o.data.materials.append(dmat)
        split_deck_faces(o)
    strips, rims, glass = wales()
    # 甲板の柵（中央部の両舷と船首楼）、船尾楼の木の手すり
    iron = []
    for side in (-1, 1):
        pts = [(x, side * (deck_y(x) - 0.05), sheer(x)) for x in np.linspace(STERN_X + 0.1, FORE_X - 0.1, 9)]
        iron += iron_fence(f"fence{side}", pts)
        pts = [(x, side * (deck_y(x) - 0.05), sheer(x) + FORE_RISE) for x in np.linspace(FORE_X, 9.0, 4)]
        iron += iron_fence(f"ffence{side}", pts, h=0.9)
    bal_pts = [(STERN_X - 0.05, -(deck_y(STERN_X) - 0.08), sheer(STERN_X) + STERN_RISE)]
    bal_pts += [(x, -(deck_y(x) - 0.08), sheer(x) + STERN_RISE) for x in np.linspace(STERN_X - 1.5, -L_HALF + 0.1, 3)]
    bal_pts += [(x, (deck_y(x) - 0.08), sheer(x) + STERN_RISE) for x in np.linspace(-L_HALF + 0.1, STERN_X - 0.05, 4)]
    bal = balustrade("stern_bal", bal_pts)
    wood, m_iron, nets, ropes, tops = masts()
    bw, br = bowsprit_and_stays(tops)
    wheel = ship_wheel()
    red = red_play()
    tube = tube_slide()
    floor = rubber_floor()

    ship = [hull, sc, fc]
    rnd = random.Random(5)
    for o in bal + wood + bw + wheel:
        if "WoodCo" not in o.data.color_attributes:
            T.wood_attrs(o, rnd, axis="Z", pith=(0.0, 0.03))
    groups = [
        (strips + rims, paint_material("ShipWale", "#6b3a2a", res=512)),
        (glass, paint_material("ShipPortGlass", "#1c2328", res=128, rough=0.15, chip=0.0)),
        (iron + m_iron, paint_material("ShipIron", "#1e1f21", res=512, rough=0.5, chip=0.4)),
        (bal + wood + bw + wheel, dark_wood_material()),
        (ropes + br, rope_material()),
        (nets, net_material()),
        (red, paint_material("ShipRed", "#c8281e", res=1024, rough=0.4, chip=0.3)),
        (tube, paint_material("ShipTube", "#7c6a5a", res=1024, rough=0.45, chip=0.0)),
        ([floor], rubber_material()),
    ]
    out = list(ship)
    for objs, mat in groups:
        for o in objs:
            C.assign(o, mat)
            out.append(o)

    stone, plaque, base = monument()
    C.assign(stone, monument_stone_material())
    C.assign(plaque, plaque_material(plaque_image()))
    C.assign(base, monument_base_material())
    return {"IndianOak_Ship": out, "IndianOak_Monument": [stone, plaque, base]}
