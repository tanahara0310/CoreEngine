"""安良波公園の管理棟とトイレ（白い壁に赤瓦の寄棟屋根の平屋）

  ArahaHouse: アラハビーチの管理棟。長さの違う 2 棟がずれて並び、間を平らな屋根の通路でつなぐ。
              海側の棟には柱の並ぶ深い軒（片流れの赤瓦）。窓・扉・換気ガラリ・看板、
              軒先の自動販売機 3 台、外のシャワー 6 本
  ParkToilet: 公園のトイレ。1 棟で、両端の入口の前に目隠しの壁

座標: 原点 = 建物の中心の地面。+Y が棟の長手、-X が海側（軒・シャワーの側）
"""
import math
import os
import tempfile

import bpy
from mathutils import Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from ..materials import concrete, galvanized, painted_metal
from . import _timber as T

PREVIEW = dict(cam_dir=(-1.0, -0.7, 0.55), lens=50)

WALL_H = 3.0
BASE_H = 0.35
PITCH = math.radians(24)
OVER = 0.6
FONT_JA = ["C:/Windows/Fonts/YuGothB.ttc", "C:/Windows/Fonts/meiryob.ttc",
           "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf"]
FONT_EN = ["C:/Windows/Fonts/arialbd.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def wall_material():
    """白い吹き付けの外壁。軒下の雨だれ、地面際の泥はね"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        big = nb.noise(co, 0.8, 4, 0.55)
        col = nb.ramp(big, [(0.3, srgb("#d9d3c3")), (0.55, srgb("#e8e3d5")), (0.8, srgb("#f0ece2"))])
        stip = nb.noise(co, 35.0, 3, 0.6)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(stip, 0.35, 0.65, 0.96, 1.03))
        streak = nb.noise(nb.mapping(co, scale=(14.0, 14.0, 0.6)), 1.2, 4, 0.55)
        top = nb.smooth(Z, WALL_H - 1.4, WALL_H)
        col = nb.mix(col, srgb("#a8a294"), nb.mul(nb.mul(nb.smooth(streak, 0.52, 0.7), top), 0.5))
        mold = nb.smooth(nb.noise(co, 3.0, 4, 0.6), 0.62, 0.75)
        col = nb.mix(col, srgb("#7a7a68"), nb.mul(mold, nb.mul(top, 0.3)))
        dirt = nb.smooth(Z, BASE_H + 0.5, BASE_H)
        col = nb.mix(col, srgb("#a89a80"), nb.mul(dirt, 0.5))
        return dict(color=col, rough=0.9, height=stip, height_scale=0.0015)

    return pbr_material("HouseWall", fn, res=2048, ao_distance=0.8)


def roof_material():
    """赤瓦。丸瓦の列の継ぎ目を白い漆喰で固め、日焼けと黒い苔のむら"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        nrm = nb.node("ShaderNodeNewGeometry").outputs["Normal"]
        nx, ny, nz = nb.sep(nrm)
        across = nb.mixf(X, Y, nb.math("GREATER_THAN", nb.math("ABSOLUTE", nx), nb.math("ABSOLUTE", ny)))
        down = Z
        pitch = 0.27
        f = nb.math("FRACT", nb.math("DIVIDE", across, pitch))
        d = nb.math("ABSOLUTE", nb.sub(f, 0.5))
        cap = nb.smooth(d, 0.22, 0.12)            # 丸瓦（男瓦）
        mortar = nb.mul(nb.smooth(d, 0.27, 0.22), nb.smooth(d, 0.08, 0.13))
        row = nb.math("FRACT", nb.math("DIVIDE", down, 0.11))
        step = nb.smooth(row, 0.9, 1.0)
        cid = nb.math("FLOOR", nb.math("DIVIDE", across, pitch))
        rid = nb.math("FLOOR", nb.math("DIVIDE", down, 0.11))
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(nb.comb(cid, rid, 0.0), wn.inputs["Vector"])
        h1 = wn.outputs["Value"]
        tile = nb.ramp(h1, [(0.0, srgb("#a8462a")), (0.4, srgb("#bc5634")), (0.75, srgb("#c86a40")),
                            (1.0, srgb("#d07c4e"))])
        fade = nb.noise(co, 0.7, 4, 0.55)
        tile = nb.mix(tile, srgb("#d89a7a"), nb.mul(nb.smooth(fade, 0.45, 0.75), 0.4))
        moss = nb.smooth(nb.noise(co, 2.5, 5, 0.6), 0.6, 0.72)
        tile = nb.mix(tile, srgb("#3e3a30"), nb.mul(moss, 0.45))
        col = nb.mix(tile, nb.mix(srgb("#e6e2d6"), srgb("#b8b4a8"), moss), mortar)
        height = nb.add(nb.add(nb.mul(cap, 1.0), nb.mul(mortar, 0.7)), nb.mul(step, -0.3))
        return dict(color=col, rough=nb.mixf(0.75, 0.9, mortar), height=height, height_scale=0.02,
                    cavity=nb.maprange(nb.mul(nb.sub(1.0, cap), nb.sub(1.0, mortar)), 0, 1, 1.0, 0.75))

    return pbr_material("HouseRoof", fn, res=2048, ao_distance=0.5)


def mortar_material():
    """棟と隅棟の白い漆喰"""
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 4.0, 4, 0.6)
        col = nb.mix(srgb("#d8d4c8"), srgb("#eeeae0"), nb.smooth(n, 0.35, 0.7))
        moss = nb.smooth(nb.noise(co, 3.0, 4, 0.6), 0.6, 0.72)
        col = nb.mix(col, srgb("#6a6a5a"), nb.mul(moss, 0.4))
        return dict(color=col, rough=0.85, height=nb.noise(co, 30.0, 3, 0.6), height_scale=0.002)

    return pbr_material("HouseMortar", fn, res=512, ao_distance=0.4)


def glass_material():
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        grad = nb.smooth(Z, 0.8, 2.6)
        col = nb.mix(srgb("#1a252c"), srgb("#4a5e6a"), grad)
        smudge = nb.smooth(nb.noise(co, 5.0, 4, 0.6), 0.55, 0.7)
        col = nb.mix(col, srgb("#7a8288"), nb.mul(smudge, 0.2))
        return dict(color=col, rough=nb.mixf(0.05, 0.25, smudge))

    return pbr_material("HouseGlass", fn, res=512, ao_distance=0.3)


def louver_material():
    """換気ガラリ（横の羽板の影）"""
    def fn(nb):
        co = nb.coord("Object")
        Z = nb.sep(co)[2]
        f = nb.math("FRACT", nb.math("DIVIDE", Z, 0.06))
        slat = nb.smooth(f, 0.25, 0.4)
        col = nb.mix(srgb("#2c2e30"), srgb("#9a9c98"), slat)
        dirt = nb.smooth(nb.noise(co, 3.0, 4, 0.6), 0.5, 0.7)
        col = nb.mix(col, srgb("#6a665c"), nb.mul(dirt, 0.3))
        return dict(color=col, rough=0.6, metal=nb.mul(slat, 0.6), height=slat, height_scale=0.006)

    return pbr_material("HouseLouver", fn, res=512, ao_distance=0.3)


def dark_material():
    """開口の奥（暗い室内）"""
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 2.0, 3, 0.5)
        col = nb.mix(srgb("#141414"), srgb("#2a2826"), n)
        return dict(color=col, rough=0.9)

    return pbr_material("HouseDark", fn, res=256, ao_strength=0.0)


def print_material(name, img_path):
    img = bpy.data.images.load(img_path, check_existing=True)

    def fn(nb):
        col = nb.image(img, nb.uv("Proc"), ext="EXTEND")["Color"]
        co = nb.coord("Object")
        fade = nb.noise(co, 1.5, 3, 0.5)
        col = nb.mix(col, srgb("#f2f2ee"), nb.mul(nb.smooth(fade, 0.5, 0.8), 0.12))
        return dict(color=col, rough=0.45, height=nb.noise(co, 40.0, 3, 0.5), height_scale=0.0005)

    return pbr_material(name, fn, res=1024, uv="keep", ao_distance=0.3)


def vending_material():
    """自動販売機（赤・白・青の 3 台。前面に商品見本の列）"""
    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        idx = nb.math("FLOORED_MODULO", nb.math("FLOOR", nb.math("DIVIDE", Y, 1.1)), 3.0)
        body = nb.mix(srgb("#c41e2a"), srgb("#e8e6e0"), nb.math("GREATER_THAN", idx, 0.5))
        body = nb.mix(body, srgb("#1e4a9a"), nb.math("GREATER_THAN", idx, 1.5))
        front = nb.smooth(X, -0.36, -0.4)
        win = nb.mul(nb.mul(nb.smooth(Z, 0.95, 1.0), nb.smooth(Z, 1.75, 1.7)), front)
        cell_y = nb.math("FRACT", nb.math("DIVIDE", Y, 0.11))
        cell_z = nb.math("FRACT", nb.math("DIVIDE", nb.sub(Z, 0.95), 0.27))
        can = nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(cell_y, 0.5)), 0.32, 0.26),
                     nb.smooth(nb.math("ABSOLUTE", nb.sub(cell_z, 0.45)), 0.36, 0.3))
        cid = nb.comb(nb.math("FLOOR", nb.math("DIVIDE", Y, 0.11)), nb.math("FLOOR", nb.math("DIVIDE", Z, 0.27)), 0.0)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(cid, wn.inputs["Vector"])
        cc = nb.hsv(wn.outputs["Color"], 0.5, 1.4, 1.1)
        disp = nb.mix(srgb("#d8dcdc"), cc, can)
        col = nb.mix(body, disp, win)
        slot = nb.mul(nb.mul(nb.smooth(Z, 0.25, 0.28), nb.smooth(Z, 0.5, 0.47)), front)
        col = nb.mix(col, srgb("#202020"), nb.mul(slot, 0.85))
        return dict(color=col, rough=nb.mixf(0.4, 0.1, win), height=can, height_scale=0.004)

    return pbr_material("HouseVending", fn, res=1024, ao_distance=0.4)


def _font(paths, size):
    from PIL import ImageFont
    for p in paths:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def sign_image(fname, lines, size_mm, bg=(28, 72, 140), fg=(250, 250, 248)):
    """看板の印刷（1 px = 2 mm）。lines = [(文字, 高さ px, 日本語か)]"""
    from PIL import Image, ImageDraw
    W, H = int(size_mm[0] / 2), int(size_mm[1] / 2)
    im = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(im)
    total = sum(h for _, h, _ in lines) + 18 * (len(lines) - 1)
    y = (H - total) / 2
    for text, h, ja in lines:
        f = _font(FONT_JA if ja else FONT_EN, h)
        w = d.textlength(text, font=f)
        d.text(((W - w) / 2, y - h * 0.12), text, fill=fg, font=f)
        y += h + 18
    d.rectangle((6, 6, W - 7, H - 7), outline=fg, width=4)
    path = os.path.join(tempfile.gettempdir(), fname)
    im.save(path)
    return path


# ---------------------------------------------------------------------------
# 部品
# ---------------------------------------------------------------------------
class Parts:
    """材質ごとの部品の入れ物"""

    def __init__(self):
        self.g = {}

    def add(self, key, *objs):
        self.g.setdefault(key, []).extend(o for o in objs if o is not None)


def quad(name, pts, uvs=None):
    """四角形 1 枚。uvs を渡すと Proc と同じ値の Bake UV も作る（uv="keep" の焼き先）"""
    obj = C.mesh_object(name, [tuple(p) for p in pts], [(0, 1, 2, 3)], [uvs] if uvs else None)
    if uvs:
        me = obj.data
        bake = me.uv_layers.new(name="Bake")
        for i, d in enumerate(me.uv_layers["Proc"].data):
            bake.data[i].uv = d.uv
    return obj


def hip_roof(P, name, cx, cy, L, W, z_e, over=OVER, pitch=PITCH):
    """寄棟屋根（長手 = Y）。屋根面・軒裏・鼻隠し・棟と隅棟の漆喰"""
    w2, l2 = W / 2 + over, L / 2 + over
    rl = max(0.0, l2 - w2)
    zr = z_e + w2 * math.tan(pitch)
    c = Vector((cx, cy, 0))
    A = [c + Vector(v) for v in ((-w2, -l2, z_e), (w2, -l2, z_e), (w2, l2, z_e), (-w2, l2, z_e))]
    R0, R1 = c + Vector((0, -rl, zr)), c + Vector((0, rl, zr))
    faces = [(A[1], A[2], R1, R0), (A[3], A[0], R0, R1), (A[0], A[1], R0), (A[2], A[3], R1)]
    verts, fl = [], []
    for f in faces:
        fl.append(tuple(range(len(verts), len(verts) + len(f))))
        verts += [tuple(v) for v in f]
    P.add("roof", C.mesh_object(name + "_roof", verts, fl))
    t = 0.12
    B = [Vector((v.x, v.y, z_e - t)) for v in A]
    # 鼻隠し（外向き）と軒裏（下向き）
    fas, soff = [], []
    for i in range(4):
        a, b = A[i], A[(i + 1) % 4]
        fas.append(quad(f"{name}_fas{i}", [B[i], B[(i + 1) % 4], b, a]))
    inner = [c + Vector(v) for v in ((-W / 2, -L / 2, z_e - t), (W / 2, -L / 2, z_e - t), (W / 2, L / 2, z_e - t),
                                     (-W / 2, L / 2, z_e - t))]
    for i in range(4):
        soff.append(quad(f"{name}_soff{i}", [inner[i], inner[(i + 1) % 4], B[(i + 1) % 4], B[i]]))
    P.add("trim", *fas)
    P.add("wall", *soff)
    ridge = [geo.tube_along([R0 - Vector((0, 0.05, 0)), R1 + Vector((0, 0.05, 0))], [0.1, 0.1], sides=8,
                            name=name + "_ridge")]
    for a, r in ((A[0], R0), (A[1], R0), (A[2], R1), (A[3], R1)):
        ridge.append(geo.tube_along([r, a + (r - a).normalized() * 0.05], [0.075, 0.075], sides=8, name=name + "_hip"))
    P.add("mortar", *ridge)
    return zr


def wing(P, name, cx, cy, L, W):
    """1 棟分の壁・基礎・屋根"""
    body = geo.box(name + "_wall", (W, L, WALL_H - BASE_H), loc=(cx, cy, BASE_H + (WALL_H - BASE_H) / 2))
    P.add("wall", body)
    base = geo.box(name + "_base", (W + 0.08, L + 0.08, BASE_H + 0.05), loc=(cx, cy, (BASE_H - 0.05) / 2))
    geo.bevel(base, 0.01, 1)
    P.add("base", base)
    return hip_roof(P, name, cx, cy, L, W, WALL_H + 0.12)


def window(P, name, c, normal, w, h):
    """壁面の窓（ガラス・アルミの枠・中桟・水切り）。c = 窓の中心、normal = 壁の外向き"""
    n = Vector(normal)
    side = Vector((-n.y, n.x, 0))
    c = Vector(c)
    glass = geo.box(name + "_g", (abs(side.x) * w + abs(n.x) * 0.02, abs(side.y) * w + abs(n.y) * 0.02, h),
                    loc=c + n * 0.01)
    P.add("glass", glass)
    fw = 0.05
    fr = []
    for s in (-1, 1):
        fr.append(geo.box(f"{name}_v{s}", (abs(side.x) * fw + abs(n.x) * 0.06, abs(side.y) * fw + abs(n.y) * 0.06, h + 2 * fw),
                          loc=c + side * s * (w / 2 + fw / 2) + n * 0.03))
        fr.append(geo.box(f"{name}_h{s}", (abs(side.x) * (w + 2 * fw) + abs(n.x) * 0.06,
                                           abs(side.y) * (w + 2 * fw) + abs(n.y) * 0.06, fw),
                          loc=c + Vector((0, 0, s * (h / 2 + fw / 2))) + n * 0.03))
    fr.append(geo.box(name + "_m", (abs(side.x) * 0.035 + abs(n.x) * 0.05, abs(side.y) * 0.035 + abs(n.y) * 0.05, h),
                      loc=c + n * 0.03))
    sill = geo.box(name + "_sill", (abs(side.x) * (w + 0.2) + abs(n.x) * 0.12, abs(side.y) * (w + 0.2) + abs(n.y) * 0.12,
                                    0.04), loc=c + Vector((0, 0, -h / 2 - fw - 0.02)) + n * 0.06)
    P.add("frame", *fr, sill)


def panel(P, key, name, c, normal, w, h, depth=0.02):
    """壁面に重ねる板（扉・ガラリ・開口の奥）"""
    n = Vector(normal)
    side = Vector((-n.y, n.x, 0))
    b = geo.box(name, (abs(side.x) * w + abs(n.x) * depth, abs(side.y) * w + abs(n.y) * depth, h),
                loc=Vector(c) + n * (depth / 2))
    P.add(key, b)
    return b


def door(P, name, c_floor, normal, w=0.9, h=2.05):
    n = Vector(normal)
    c = Vector(c_floor) + Vector((0, 0, h / 2))
    panel(P, "door", name, c, n, w, h, 0.03)
    side = Vector((-n.y, n.x, 0))
    fr = []
    for s in (-1, 1):
        fr.append(panel(P, "frame", f"{name}_j{s}", c + side * s * (w / 2 + 0.03), n, 0.06, h + 0.06, 0.05))
    panel(P, "frame", name + "_head", c + Vector((0, 0, h / 2 + 0.03)), n, w + 0.12, 0.06, 0.05)
    knob = T.rod(name + "_knob", c + side * (w * 0.38) + n * 0.03 + Vector((0, 0, -0.05)),
                 c + side * (w * 0.38) + n * 0.09 + Vector((0, 0, -0.05)), 0.02, sides=8)
    P.add("metal", knob)


def sign(P, name, c, normal, w, h, img):
    """看板（文字を印刷した板と枠）"""
    n = Vector(normal)
    side = Vector((-n.y, n.x, 0))
    c = Vector(c)
    pts = [c - side * w / 2 - Vector((0, 0, h / 2)), c + side * w / 2 - Vector((0, 0, h / 2)),
           c + side * w / 2 + Vector((0, 0, h / 2)), c - side * w / 2 + Vector((0, 0, h / 2))]
    face = quad(name, [p + n * 0.05 for p in pts], [(0, 0), (1, 0), (1, 1), (0, 1)])
    P.add(("print", img), face)
    panel(P, "frame", name + "_back", c, n, w + 0.08, h + 0.08, 0.045)


def shower(P, name, base):
    """外のシャワー（亜鉛めっきの管・シャワーヘッド・足洗いの蛇口）"""
    b = Vector(base)
    top = b + Vector((0, 0, 2.25))
    P.add("metal", T.rod(name + "_post", b, top, 0.03, sides=10))
    arm_end = top + Vector((-0.35, 0, -0.05))
    P.add("metal", geo.tube_along([top, top + Vector((-0.05, 0, 0.04)), arm_end], [0.022] * 3, sides=8,
                                  name=name + "_arm"))
    head = T.lathe(name + "_head", [(0.0, 0.0), (0.05, 0.0), (0.075, -0.05), (0.075, -0.06), (0.0, -0.06)], segs=12)
    head.location = arm_end
    P.add("metal", head)
    P.add("metal", T.rod(name + "_tap", b + Vector((0, 0, 0.45)), b + Vector((-0.15, 0, 0.45)), 0.015, sides=6))
    P.add("metal", T.rod(name + "_valve", b + Vector((-0.02, 0, 1.2)), b + Vector((-0.1, 0, 1.2)), 0.025, sides=8))


def lean_to(P, name, x_wall, x_out, y0, y1, z_hi, z_lo, cols):
    """片流れの軒（-X へ下がる）と柱と軒下の土間"""
    roof = C.mesh_object(name + "_roof", [(x_wall, y0, z_hi), (x_wall, y1, z_hi), (x_out, y1, z_lo), (x_out, y0, z_lo)],
                         [(0, 1, 2, 3)])
    P.add("roof", roof)
    t = 0.12
    under = C.mesh_object(name + "_soff", [(x_wall, y0, z_hi - t), (x_out, y0, z_lo - t), (x_out, y1, z_lo - t),
                                           (x_wall, y1, z_hi - t)], [(0, 1, 2, 3)])
    P.add("wall", under)
    P.add("trim", quad(name + "_fas", [(x_out, y0, z_lo), (x_out, y1, z_lo), (x_out, y1, z_lo - t),
                                       (x_out, y0, z_lo - t)]))
    for s, y in ((-1, y0), (1, y1)):
        P.add("trim", C.mesh_object(f"{name}_end{s}", [(x_wall, y, z_hi - t), (x_out, y, z_lo - t), (x_out, y, z_lo),
                                                        (x_wall, y, z_hi)], [(3, 2, 1, 0) if s < 0 else (0, 1, 2, 3)]))
    beam = geo.box(name + "_beam", (0.25, y1 - y0, 0.3), loc=(x_out + 0.35, (y0 + y1) / 2, z_lo - t - 0.15))
    P.add("base", beam)
    for k in range(cols):
        y = y0 + 0.3 + (y1 - y0 - 0.6) * k / (cols - 1)
        col = geo.box(f"{name}_col{k}", (0.28, 0.28, z_lo - 0.3), loc=(x_out + 0.35, y, (z_lo - 0.3) / 2))
        geo.bevel(col, 0.015, 1)
        P.add("base", col)
    slab = geo.box(name + "_slab", (x_wall - x_out + 0.2, y1 - y0, 0.12), loc=((x_wall + x_out) / 2, (y0 + y1) / 2, 0.06))
    P.add("slab", slab)


def finish(P, variant):
    mats = {
        "wall": wall_material(), "roof": roof_material(), "mortar": mortar_material(),
        "base": concrete("HouseConcrete", res=1024, tint="#a9a59b", salt=0.1),
        "slab": concrete("HouseSlab", res=1024, tint="#b4b0a6", salt=0.05, scale=0.8),
        "trim": painted_metal("HouseTrim", "#e4e0d4", res=512, chip=0.08, gloss=0.5),
        "frame": painted_metal("HouseFrame", "#b6b8b4", res=512, chip=0.05, gloss=0.35),
        "door": painted_metal("HouseDoor", "#8fa8b0", res=512, chip=0.12, gloss=0.4),
        "glass": glass_material(), "louver": louver_material(), "dark": dark_material(),
        "metal": galvanized("HouseMetal", res=512), "vending": vending_material(),
    }
    out = []
    for key, objs in P.g.items():
        if isinstance(key, tuple):
            mat = print_material(f"{variant}Sign", key[1])
        else:
            mat = mats[key]
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return out


# ---------------------------------------------------------------------------
# 建物
# ---------------------------------------------------------------------------
def araha_house():
    P = Parts()
    WB, LB, xb, yb = 6.6, 19.0, -4.5, -2.0     # 海側の長い棟
    WA, LA, xa, ya = 6.6, 15.0, 4.5, 2.0       # 陸側の短い棟
    wing(P, "B", xb, yb, LB, WB)
    wing(P, "A", xa, ya, LA, WA)
    # 2 棟の間の通路（平らな屋根）
    y0, y1 = ya - LA / 2 + 1.0, yb + LB / 2 - 1.0
    slab = geo.box("pass_roof", (2.6, y1 - y0, 0.18), loc=(0.0, (y0 + y1) / 2, WALL_H - 0.2))
    P.add("trim", slab)
    P.add("slab", geo.box("pass_floor", (2.4, y1 - y0, 0.1), loc=(0.0, (y0 + y1) / 2, 0.05)))
    # 海側の軒
    xw = xb - WB / 2
    lean_to(P, "veranda", xw, xw - 2.6, yb - LB / 2, yb + LB / 2, WALL_H - 0.1, 2.55, 7)
    # 海側の壁: 窓・扉・ガラリ・シャワー室の入口
    n = (-1, 0, 0)
    for k, y in enumerate((-9.0, -6.6)):
        window(P, f"wB{k}", (xw, yb + y + 0.0, 1.55), n, 1.6, 1.1)
    door(P, "dB0", (xw, yb - 3.6, BASE_H), n, 1.6, 2.1)
    panel(P, "dark", "openB1", (xw, yb + 0.5, BASE_H + 1.05), n, 1.8, 2.1)
    panel(P, "dark", "openB2", (xw, yb + 4.5, BASE_H + 1.05), n, 1.8, 2.1)
    for k, y in enumerate((2.5, 6.5)):
        panel(P, "louver", f"lvB{k}", (xw, yb + y, 2.3), n, 1.4, 0.6)
    window(P, "wB2", (xw, yb + 8.0, 1.55), n, 1.2, 1.1)
    sign(P, "signB", (xw, yb - 7.8, 2.55), n, 2.6, 0.55,
         sign_image("araha_house_sign.png", [("アラハビーチ管理棟", 120, True), ("ARAHA BEACH HOUSE", 70, False)],
                    (2600, 550)))
    # 陸側の棟と残りの壁
    xe = xa + WA / 2
    for k, y in enumerate((-4.5, -1.5, 1.5, 4.5)):
        window(P, f"wAe{k}", (xe, ya + y, 1.6), (1, 0, 0), 1.6, 1.1)
    door(P, "dA0", (xe, ya + 6.2, BASE_H), (1, 0, 0), 0.9)
    for k, y in enumerate((-5.0, 0.0, 5.0)):
        window(P, f"wAw{k}", (xa - WA / 2, ya + y, 1.6), (-1, 0, 0), 1.2, 1.0)
    for k, x in enumerate((-1.5, 1.5)):
        window(P, f"wAn{k}", (xa + x, ya + LA / 2, 1.6), (0, 1, 0), 1.2, 1.0)
        window(P, f"wBs{k}", (xb + x, yb - LB / 2, 1.6), (0, -1, 0), 1.2, 1.0)
    door(P, "dAs", (xa, ya - LA / 2, BASE_H), (0, -1, 0), 1.6, 2.1)
    for k, y in enumerate((-6.0, -2.0, 2.0, 6.0)):
        panel(P, "louver", f"lvBe{k}", (xb + WB / 2, yb + y, 2.35), (1, 0, 0), 1.0, 0.5)
    panel(P, "dark", "openBn", (xb, yb + LB / 2, BASE_H + 1.05), (0, 1, 0), 1.6, 2.1)
    # 軒下の自動販売機
    for k in range(3):
        vm = geo.box(f"vend{k}", (0.8, 1.02, 1.85), loc=(xw - 0.45, yb + LB / 2 - 1.5 - k * 1.1, 0.12 + 0.925))
        geo.bevel(vm, 0.02, 2)
        P.add("vending", vm)
    # 外のシャワー（軒の外の土間）
    x_sh = xw - 4.0
    P.add("slab", geo.box("shower_pad", (2.2, 11.6, 0.12), loc=(x_sh, yb - 1.0, 0.06)))
    P.add("metal", geo.box("drain", (0.25, 11.0, 0.02), loc=(x_sh + 0.6, yb - 1.0, 0.125)))
    for k in range(6):
        shower(P, f"sh{k}", (x_sh - 0.4, yb - 5.6 + k * 1.85, 0.12))
    return finish(P, "ArahaHouse")


def park_toilet():
    P = Parts()
    W, L = 6.4, 10.0
    wing(P, "T", 0.0, 0.0, L, W)
    for s in (-1, 1):
        y = s * L / 2
        n = (0, s, 0)
        panel(P, "dark", f"open{s}", (-1.4, y, BASE_H + 1.05), n, 1.6, 2.1)
        panel(P, "dark", f"open2{s}", (1.4, y, BASE_H + 1.05), n, 1.6, 2.1)
        # 目隠しの壁と、その上の小屋根
        scr = geo.box(f"screen{s}", (W - 0.6, 0.18, 2.2), loc=(0.0, y + s * 1.4, BASE_H + 1.1))
        P.add("wall", scr)
        P.add("base", geo.box(f"screen_base{s}", (W - 0.5, 0.26, 0.3), loc=(0.0, y + s * 1.4, 0.15)))
        for k, x in enumerate((-W / 2 + 0.6, W / 2 - 0.6)):
            P.add("wall", geo.box(f"wing{s}{k}", (0.18, 1.3, 2.2), loc=(x * 0.98, y + s * 0.7, BASE_H + 1.1)))
        P.add("slab", geo.box(f"apron{s}", (W, 2.0, 0.1), loc=(0.0, y + s * 1.0, 0.05)))
        sign(P, f"sign{s}", (0.0, y + s * 1.49, 2.05), n, 1.4, 0.45,
             sign_image("park_toilet_sign.png", [("トイレ", 110, True), ("TOILET", 60, False)], (1400, 450)))
    for s in (-1, 1):
        for k, y in enumerate((-3.0, 0.0, 3.0)):
            panel(P, "louver", f"lv{s}{k}", (s * W / 2, y, 2.4), (s, 0, 0), 1.6, 0.5)
    for k in range(2):
        P.add("metal", T.rod(f"tap{k}", (-W / 2 - 0.05, -1.0 + k * 2.0, 0.8), (-W / 2 - 0.2, -1.0 + k * 2.0, 0.8), 0.015,
                             sides=6))
    return finish(P, "ParkToilet")


def build():
    return {"ArahaHouse": araha_house(), "ParkToilet": park_toilet()}
