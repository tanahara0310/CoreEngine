"""安良波公園の多目的広場（3on3 のバスケットコート）

  ArahaCourts: 長さ 75.7 m・幅 20.7 m（角を落とした長方形）の舗装の広場。西の辺に沿って 3x3 の半面コートが 4 面、
               北の端に 1 面。青い面に橙の制限区域と白い線。ゴールは支柱から腕が伸びる形で、
               バックボードはコートごとに違う色の飾り（印刷の画像）。白い網

座標: 原点 = 広場の中心の地面。+Y が長手（北）、-X が西（ゴールの側）
"""
import math
import os
import tempfile

import bpy
import numpy as np
from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import concrete, painted_metal
from . import _timber as T

PREVIEW = dict(cam_dir=(-0.9, -0.6, 0.75), lens=40)

L, W = 75.7, 20.7
CHAMFER = 4.0
SLAB = 0.08
PX = 100           # 印刷の 1 m あたりの画素
COURT_W, COURT_D = 15.0, 11.0
# 半面コート: (バックボードの位置 (x, y), コートの奥へ向かう向き)。エンドラインはその 1.2 m 後ろ
COURTS = [((-W / 2 + 1.6, y), (1.0, 0.0)) for y in (-28.5, -13.5, 1.5, 16.5)] + [((0.0, L / 2 - 1.6), (0.0, -1.0))]
BOARD_THEMES = [("#c8102e", "#f1bf00"), ("#ffffff", "#bc002d"), ("#ed0a3f", "#0050f0"), ("#078930", "#da121a"),
                ("#239f40", "#da0000")]


def outline():
    c, hw, hl = CHAMFER, W / 2, L / 2
    return [(-hw + c, -hl), (hw - c, -hl), (hw, -hl + c), (hw, hl - c), (hw - c, hl), (-hw + c, hl), (-hw, hl - c),
            (-hw, -hl + c)]


# ---------------------------------------------------------------------------
# 印刷の画像
# ---------------------------------------------------------------------------
def court_image():
    """広場全体の塗装（1 cm = 1 px）。行 0 が北端"""
    from PIL import Image, ImageDraw
    Wp, Hp = int(W * PX), int(L * PX)
    im = Image.new("RGB", (Wp, Hp), (62, 96, 158))
    d = ImageDraw.Draw(im)

    def P(x, y):
        return ((x + W / 2) * PX, (L / 2 - y) * PX)

    white = (240, 240, 236)
    lw = max(2, int(0.05 * PX))
    for (hx, hy), (dx, dy) in COURTS:
        rim = Vector((hx, hy)) + Vector((dx, dy)) * 1.575
        base = Vector((hx, hy)) - Vector((dx, dy)) * 1.2
        side = Vector((-dy, dx))
        # コートの面（明るい青）
        corners = [base + side * COURT_W / 2, base - side * COURT_W / 2,
                   base - side * COURT_W / 2 + Vector((dx, dy)) * COURT_D, base + side * COURT_W / 2 + Vector((dx, dy)) * COURT_D]
        d.polygon([P(*c) for c in corners], fill=(70, 122, 196))
        # 2 点の線の内側（さらに明るい青）
        r2 = 6.75
        arc = [rim + Vector((dx, dy)) * (r2 * math.cos(a)) + side * (r2 * math.sin(a))
               for a in np.linspace(-math.pi / 2, math.pi / 2, 64)]
        d.polygon([P(*p) for p in [base + side * r2] + arc + [base - side * r2]], fill=(92, 146, 214))
        # 制限区域（橙）
        key = [base + side * 2.45, base - side * 2.45, base - side * 2.45 + Vector((dx, dy)) * 5.8,
               base + side * 2.45 + Vector((dx, dy)) * 5.8]
        d.polygon([P(*c) for c in key], fill=(214, 108, 44))
        # 白い線
        d.line([P(*c) for c in corners + corners[:1]], fill=white, width=lw)
        d.line([P(*p) for p in [base + side * r2] + arc + [base - side * r2]], fill=white, width=lw)
        d.line([P(*c) for c in key + key[:1]], fill=white, width=lw)
        ft = base + Vector((dx, dy)) * 5.8
        semi = [ft + Vector((dx, dy)) * (1.8 * math.cos(a)) + side * (1.8 * math.sin(a))
                for a in np.linspace(-math.pi / 2, math.pi / 2, 32)]
        d.line([P(*p) for p in semi], fill=white, width=lw)
        nc = [rim + Vector((dx, dy)) * (1.25 * math.cos(a)) + side * (1.25 * math.sin(a))
              for a in np.linspace(-math.pi / 2, math.pi / 2, 24)]
        d.line([P(*p) for p in nc], fill=white, width=lw)
    # 外周の白線と広場の縁の帯
    o = outline()
    d.line([P(*c) for c in o + o[:1]], fill=(200, 200, 196), width=int(0.12 * PX))
    path = os.path.join(tempfile.gettempdir(), "araha_courts_print.png")
    im.save(path)
    return path


def board_image():
    """バックボード 5 枚の飾り（横に並べる。1 枚 180 x 105 cm、1 cm = 4 px）"""
    from PIL import Image, ImageDraw
    bw, bh = 720, 420
    im = Image.new("RGB", (bw * 5, bh), (245, 245, 242))
    d = ImageDraw.Draw(im)
    for k, (c1, c2) in enumerate(BOARD_THEMES):
        x0 = k * bw

        def hexrgb(h):
            return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))
        a, b = hexrgb(c1), hexrgb(c2)
        for i in range(6):
            y = int(bh * i / 6)
            d.polygon([(x0, y), (x0 + bw * 0.28, y + bh * 0.08), (x0 + bw * 0.28, y + bh / 6 + bh * 0.08),
                       (x0, y + bh / 6)], fill=a if i % 2 == 0 else b)
            d.polygon([(x0 + bw, y), (x0 + bw * 0.72, y + bh * 0.08), (x0 + bw * 0.72, y + bh / 6 + bh * 0.08),
                       (x0 + bw, y + bh / 6)], fill=b if i % 2 == 0 else a)
        d.rectangle((x0 + 8, 8, x0 + bw - 9, bh - 9), outline=(30, 30, 30), width=20)
        sx0, sx1 = x0 + bw / 2 - 118, x0 + bw / 2 + 118
        d.rectangle((sx0, bh - 60 - 180, sx1, bh - 60), outline=(30, 30, 30), width=20)
    path = os.path.join(tempfile.gettempdir(), "araha_courts_boards.png")
    im.save(path)
    return path


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def court_material(img_path):
    """塗装したコートの舗装（印刷を Proc UV で読み、すり減りと砂ぼこりを足す）"""
    img = bpy.data.images.load(img_path, check_existing=True)

    def fn(nb):
        col = nb.image(img, nb.uv("Proc"), ext="EXTEND")["Color"]
        co = nb.coord("Object")
        grit = nb.noise(co, 80.0, 3, 0.6)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(grit, 0.3, 0.7, 0.95, 1.04))
        wear = nb.smooth(nb.noise(co, 0.4, 4, 0.6), 0.55, 0.75)
        col = nb.mix(col, nb.hsv(col, 0.5, 0.7, 1.12), nb.mul(wear, 0.5))
        sand = nb.smooth(nb.noise(co, 1.5, 4, 0.6), 0.64, 0.76)
        col = nb.mix(col, srgb("#c8bc9e"), nb.mul(sand, 0.3))
        crack = nb.smooth(nb.math("ABSOLUTE", nb.sub(nb.noise(co, 0.6, 5, 0.6), 0.5)), 0.006, 0.0)
        col = nb.mix(col, srgb("#3a3e48"), nb.mul(crack, 0.5))
        return dict(color=col, rough=nb.maprange(grit, 0.3, 0.7, 0.62, 0.78),
                    height=nb.sub(grit, nb.mul(crack, 2.0)), height_scale=0.0015)

    return pbr_material("CourtPaint", fn, res=4096, uv="keep", ao_distance=0.5)


def board_material(img_path):
    img = bpy.data.images.load(img_path, check_existing=True)

    def fn(nb):
        col = nb.image(img, nb.uv("Proc"), ext="EXTEND")["Color"]
        co = nb.coord("Object")
        fade = nb.noise(co, 2.0, 3, 0.5)
        col = nb.mix(col, srgb("#f0f0ec"), nb.mul(nb.smooth(fade, 0.5, 0.8), 0.2))
        return dict(color=col, rough=0.3)

    return pbr_material("CourtBoard", fn, res=2048, uv="keep", ao_distance=0.3)


def net_material():
    """ゴールの網（菱形の目。Proc UV: u = 周、v = 上 1 → 下 0）"""
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        a = nb.math("FRACT", nb.add(nb.mul(U, 12.0), nb.mul(V, 4.0)))
        b = nb.math("FRACT", nb.sub(nb.mul(U, 12.0), nb.mul(V, 4.0)))
        line = nb.math("MAXIMUM", nb.smooth(nb.math("ABSOLUTE", nb.sub(a, 0.5)), 0.42, 0.47),
                       nb.smooth(nb.math("ABSOLUTE", nb.sub(b, 0.5)), 0.42, 0.47))
        top = nb.smooth(V, 0.93, 0.97)
        alpha = nb.math("MAXIMUM", line, top)
        col = nb.mix(srgb("#d8d8d2"), srgb("#f2f2ee"), nb.noise(nb.comb(U, V, 0.0), 8.0, 2, 0.5))
        return dict(color=col, rough=0.8, alpha=alpha)

    return pbr_material("CourtNet", fn, res=512, uv="atlas", double_sided=True, atlas_size=(1.4, 0.45))


# ---------------------------------------------------------------------------
# 形
# ---------------------------------------------------------------------------
def slab():
    """広場の板（上面の UV: Proc = 印刷全体、Bake = 南北 2 つに分けて横に並べる）"""
    poly = outline()
    n = len(poly)
    # 上面は格子に切る（縁の多角形の内側だけ）
    nx, ny = 12, 40
    verts, faces, proc, bake = [], [], [], []
    idx = {}

    def inside(x, y):
        c = CHAMFER
        return not ((abs(x) > W / 2 - c) and (abs(y) > L / 2 - c) and (abs(x) - (W / 2 - c) + abs(y) - (L / 2 - c) > c))

    for j in range(ny + 1):
        for i in range(nx + 1):
            x = -W / 2 + W * i / nx
            y = -L / 2 + L * j / ny
            idx[(i, j)] = len(verts)
            verts.append((x, y, SLAB))
    for j in range(ny):
        for i in range(nx):
            cx = -W / 2 + W * (i + 0.5) / nx
            cy = -L / 2 + L * (j + 0.5) / ny
            if not inside(cx, cy):
                continue
            faces.append((idx[(i, j)], idx[(i + 1, j)], idx[(i + 1, j + 1)], idx[(i, j + 1)]))
    # 角は格子の頂点を縁の斜めの線へ寄せる
    for k, (x, y, z) in enumerate(verts):
        c = CHAMFER
        ex = abs(x) - (W / 2 - c)
        ey = abs(y) - (L / 2 - c)
        if ex > 0 and ey > 0 and ex + ey > c:
            s = c / (ex + ey)
            verts[k] = (math.copysign(W / 2 - c + ex * s, x), math.copysign(L / 2 - c + ey * s, y), z)
    top = C.mesh_object("court_top", verts, faces)
    me = top.data
    pl = me.uv_layers.new(name="Proc")
    bl = me.uv_layers.new(name="Bake")
    for p in me.polygons:
        for li in p.loop_indices:
            x, y, _ = me.vertices[me.loops[li].vertex_index].co
            pl.data[li].uv = ((x + W / 2) / W, (y + L / 2) / L)
            half = 0 if y < 0 or (y == 0 and p.center.y < 0) else 1
            yy = (y + L / 2) - half * L / 2
            bl.data[li].uv = (0.004 + 0.992 * yy / (L / 2), 0.004 + half * 0.5 + 0.492 * (x + W / 2) / W)
    verts = [(x, y, -0.05) for x, y in poly] + [(x, y, SLAB) for x, y in poly]
    side = C.mesh_object("court_side", verts, [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)])
    return top, side


def hoop(k, pos, d):
    """支柱・腕・バックボード・リング・網"""
    metal, pad, board, rim, net = [], [], [], [], []
    p = Vector((pos[0], pos[1], 0))
    d = Vector((d[0], d[1], 0))
    side = Vector((-d.y, d.x, 0))
    pole = p - d * 1.5
    metal.append(geo.box(f"pole{k}", (0.14, 0.14, 3.4), loc=pole + Vector((0, 0, 1.7))))
    metal.append(geo.box(f"base{k}", (0.5, 0.5, 0.04), loc=pole + Vector((0, 0, 0.02))))
    arm_end = p - d * 0.05 + Vector((0, 0, 3.2))
    for z in (3.2, 2.85):
        a0 = pole + Vector((0, 0, z))
        a1 = Vector((arm_end.x, arm_end.y, z))
        metal.append(T.rod(f"arm{k}{z}", a0, a1, 0.045, sides=8))
    padding = geo.box(f"pad{k}", (0.3, 0.3, 1.9), loc=pole + Vector((0, 0, 0.95)))
    geo.bevel(padding, 0.04, 2)
    pad.append(padding)
    # バックボード（印刷の UV はアトラスの k 番目）
    bc = p + d * 0.0 + Vector((0, 0, 2.9 + 1.05 / 2))
    hw, hh = 0.9, 0.525
    u0, u1 = k / 5, (k + 1) / 5
    pts = [bc - side * hw - Vector((0, 0, hh)), bc + side * hw - Vector((0, 0, hh)), bc + side * hw + Vector((0, 0, hh)),
           bc - side * hw + Vector((0, 0, hh))]
    front = C.mesh_object(f"board{k}", [tuple(q + d * 0.03) for q in pts], [(3, 2, 1, 0)],
                          [[(u0, 1), (u1, 1), (u1, 0), (u0, 0)]])
    if front.data.polygons[0].normal.dot(d) < 0:
        front.data.polygons[0].flip()
    # 印刷の UV を焼き先（Bake UV）にも写す
    bake = front.data.uv_layers.new(name="Bake")
    for i, dd in enumerate(front.data.uv_layers["Proc"].data):
        bake.data[i].uv = dd.uv
    board.append(front)
    back = geo.box(f"boardback{k}", (abs(side.x) * 1.84 + abs(d.x) * 0.04, abs(side.y) * 1.84 + abs(d.y) * 0.04, 1.09),
                   loc=bc + d * 0.0)
    metal.append(back)
    # リングと網
    rc = p + d * 0.375 + Vector((0, 0, 3.05))
    ring = geo.tube_along([rc + (d * math.cos(a) + side * math.sin(a)) * 0.23 for a in np.linspace(0, math.tau, 25)],
                          [0.01] * 25, sides=6, name=f"ring{k}", cap_start=False, cap_end=False)
    rim.append(ring)
    rim.append(geo.box(f"bracket{k}", (abs(d.x) * 0.36 + abs(side.x) * 0.12, abs(d.y) * 0.36 + abs(side.y) * 0.12, 0.05),
                       loc=p + d * 0.17 + Vector((0, 0, 3.05))))
    nu, nv = 16, 4
    verts, faces, uvs = [], [], []
    for j in range(nv + 1):
        v = j / nv
        r = 0.15 + 0.08 * v
        z = 3.05 - 0.42 * (1 - v)
        for i in range(nu + 1):
            a = i / nu * math.tau
            verts.append(tuple(rc + (d * math.cos(a) + side * math.sin(a)) * r + Vector((0, 0, z - 3.05))))
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i
            faces.append((a, a + 1, a + nu + 2, a + nu + 1))
            uvs.append([(i / nu, j / nv), ((i + 1) / nu, j / nv), ((i + 1) / nu, (j + 1) / nv), (i / nu, (j + 1) / nv)])
    net.append(C.mesh_object(f"net{k}", verts, faces, uvs))
    return metal, pad, board, rim, net


def build():
    top, side = slab()
    metal, pad, board, rim, net = [], [], [], [], []
    for k, (pos, d) in enumerate(COURTS):
        m, p, b, r, n = hoop(k, pos, d)
        metal += m
        pad += p
        board += b
        rim += r
        net += n
    groups = [
        ([top], court_material(court_image())),
        ([side], concrete("CourtCurb", res=1024, tint="#aeaaa0", salt=0.05)),
        (metal, painted_metal("CourtPole", "#2a4a8a", res=512, chip=0.15, gloss=0.4)),
        (pad, painted_metal("CourtPad", "#1e3a7a", res=256, chip=0.05, gloss=0.6)),
        (board, board_material(board_image())),
        (rim, painted_metal("CourtRim", "#e05a1e", res=256, chip=0.2, gloss=0.4)),
        (net, net_material()),
    ]
    out = []
    for objs, mat in groups:
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return {"ArahaCourts": out}
