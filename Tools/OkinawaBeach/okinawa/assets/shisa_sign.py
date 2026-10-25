"""安良波公園の入口の看板（大きなシーサーが「ようこそ」の看板を抱える）

石張りの台の上に、額縁の付いた看板。シーサーは看板の後ろに寝そべり、左に頭を出し、
前に白い足を 3 本そろえ、右に尾を立てる。塗装した造形物（茶の体、青緑の顔、白い歯と足）。

座標: z=0 が地面。原点は台の底の中心。正面（看板の文字の面）は -Y。
"""
import math
import os
import tempfile

import bpy
import numpy as np
from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from . import _timber as T

PREVIEW = dict(cam_dir=(-0.35, -1.0, 0.25), lens=45)

BASE_W, BASE_D, BASE_H = 4.6, 1.4, 0.3
BASE_X = -0.2
SIGN_W, SIGN_H = 2.3, 1.5          # 板面（額縁の内側）
FRAME = 0.13
SIGN_Z0 = 0.62                     # 額縁の下端
SIGN_Y = 0.0                       # 板面の前の面
HEAD = Vector((-1.74, -0.02, 1.86))
PAW_X = (-1.84, -0.5, 0.66)

FONT_JA = ["C:/Windows/Fonts/YuGothB.ttc", "C:/Windows/Fonts/meiryob.ttc",
           "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf"]
FONT_EN = ["C:/Windows/Fonts/arialbi.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf"]


# ---------------------------------------------------------------------------
# 看板の文字の画像
# ---------------------------------------------------------------------------
def _font(paths, size):
    from PIL import ImageFont
    for p in paths:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def sign_image():
    """板面の印刷（1 px = 1 mm）。上が白、下へ水色のグラデーション"""
    from PIL import Image, ImageDraw
    W, H = int(SIGN_W * 1000), int(SIGN_H * 1000)
    g = np.linspace(0.0, 1.0, H)[:, None]
    top = np.array([250, 252, 252], float)
    mid = np.array([196, 232, 244], float)
    bot = np.array([88, 178, 224], float)
    t1 = np.clip(g / 0.55, 0, 1)
    t2 = np.clip((g - 0.55) / 0.45, 0, 1)
    rgb = np.where(g < 0.55, top * (1 - t1) + mid * t1, mid * (1 - t2) + bot * t2)
    arr = np.repeat(rgb[:, None, :], W, axis=1)
    im = Image.fromarray(arr.astype(np.uint8), "RGB")
    d = ImageDraw.Draw(im)
    navy = (22, 44, 104)

    def center(text, y, size, fnt):
        f = _font(fnt, size)
        w = d.textlength(text, font=f)
        d.text(((W - w) / 2, y), text, fill=navy, font=f)

    center("ようこそ", 70, 120, FONT_JA)
    center("安良波公園", 230, 300, FONT_JA)
    center("アラハビーチへ", 560, 250, FONT_JA)
    # 英語は斜体を左寄せで 3 行
    f_small = _font(FONT_EN, 105)
    f_big = _font(FONT_EN, 135)
    d.text((620, 870), "welcome to", fill=navy, font=f_small)
    d.text((620, 990), "ARAHA PARK", fill=navy, font=f_big)
    d.text((620, 1140), "ARAHA BEACH", fill=navy, font=f_big)
    # 下端の濃い帯
    d.rectangle((0, H - 95, W, H), fill=(28, 52, 110))
    path = os.path.join(tempfile.gettempdir(), "araha_shisa_sign_print.png")
    im.save(path)
    return path


# ---------------------------------------------------------------------------
# SDF（numpy, p: (N,3)）
# ---------------------------------------------------------------------------
def sd_ellipsoid(p, c, r):
    q = (p - c) / r
    k0 = np.linalg.norm(q, axis=-1)
    k1 = np.linalg.norm((p - c) / (r * r), axis=-1)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-6)


def sd_capsule(p, a, b, ra, rb):
    pa = p - a
    ba = b - a
    h = np.clip((pa @ ba) / (ba @ ba), 0.0, 1.0)
    return np.linalg.norm(pa - h[:, None] * ba, axis=-1) - (ra + (rb - ra) * h)


def smin(a, b, k):
    h = np.maximum(k - np.abs(a - b), 0.0) / k
    return np.minimum(a, b) - h * h * k * 0.25


class BodySDF:
    """茶色の体（胴・頭の後ろ・たてがみ・腕・尾）"""

    def __init__(self):
        A = np.array
        hc = A(HEAD)
        self.ell = [
            (A((0.05, 0.6, 1.0)), A((1.3, 0.42, 0.62))),         # 胴（看板の後ろ）
            (hc + A((0.0, 0.08, 0.0)), A((0.56, 0.52, 0.55))),  # 頭
        ]
        # 足は看板の後ろを下り、看板の下端をくぐって前へ出る
        self.caps = [(hc + A((0.1, 0.1, -0.45)), A((PAW_X[0], -0.2, 0.42)), 0.22, 0.19)]
        for x0, x in ((-0.2, PAW_X[1]), (1.0, PAW_X[2])):
            self.caps.append((A((x0, 0.3, 0.95)), A((x, 0.2, 0.42)), 0.2, 0.17))
            self.caps.append((A((x, 0.2, 0.42)), A((x, -0.2, 0.42)), 0.17, 0.17))
        # 尾（右に立ち上がって先が巻く）
        tail = [A((1.15, 0.35, 0.9)), A((1.45, 0.25, 1.3)), A((1.55, 0.2, 1.7)), A((1.42, 0.15, 2.0)),
                A((1.22, 0.15, 2.05)), A((1.18, 0.15, 1.88))]
        rads = [0.2, 0.18, 0.15, 0.12, 0.1, 0.08]
        for i in range(len(tail) - 1):
            self.caps.append((tail[i], tail[i + 1], rads[i], rads[i + 1]))
        # たてがみの巻き毛（顔のまわりと頭の上）
        self.curls = []
        for k in range(11):
            a = math.radians(-200 + k * 22)
            self.curls.append((hc + A((math.cos(a) * 0.55, -0.28, math.sin(a) * 0.5)), 0.17))
        for k in range(3):
            self.curls.append((hc + A((-0.25 + k * 0.25, 0.05, 0.58)), 0.16))

    def __call__(self, p):
        d = None
        for c, r in self.ell:
            e = sd_ellipsoid(p, c, r)
            d = e if d is None else smin(d, e, 0.15)
        for a, b, ra, rb in self.caps:
            d = smin(d, sd_capsule(p, a, b, ra, rb), 0.12)
        for c, r in self.curls:
            d = smin(d, np.linalg.norm(p - c, axis=-1) - r, 0.06)
        return d

    def grad(self, p, e=1e-3):
        g = np.zeros_like(p)
        for i in range(3):
            o = np.zeros(3)
            o[i] = e
            g[:, i] = (self(p + o) - self(p - o)) / (2 * e)
        return g


def _verts_np(me):
    a = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)


def _project(obj, sdf, iters=6, disp=None):
    me = obj.data
    p = _verts_np(me)
    for _ in range(iters):
        d = sdf(p)
        g = sdf.grad(p)
        gl2 = np.maximum((g * g).sum(-1), 1e-8)
        p = p - (d / gl2)[:, None] * g
    if disp is not None:
        g = sdf.grad(p)
        n = g / np.linalg.norm(g, axis=-1, keepdims=True)
        p = p + n * disp(p)[:, None]
    me.vertices.foreach_set("co", p.ravel())
    me.update()


def _remesh(obj, voxel):
    m = obj.modifiers.new("Remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = voxel
    m.adaptivity = 0.0
    C.apply_modifiers(obj)


def body_mesh(target_tris=30000):
    sdf = BodySDF()
    parts = []
    for c, r in sdf.ell:
        o = _ellipsoid("_b", c, r)
        parts.append(o)
    for a, b, ra, rb in sdf.caps:
        parts.append(T.rod("_c", a, b, max(ra, rb) * 1.1, sides=12))
    for c, r in sdf.curls:
        parts.append(_ellipsoid("_cu", c, (r, r, r)))
    bpy.context.view_layer.update()
    obj = C.join(parts, "shisa_body")
    _remesh(obj, 0.04)
    _project(obj, sdf)
    _remesh(obj, 0.022)
    off = Vector((3.0, 8.0, 1.0))

    def disp(p):
        return np.array([geo.fbm(Vector(q) * 3.0 + off, 3) for q in p]) * 0.012

    _project(obj, sdf, disp=disp)
    geo.decimate(obj, target_tris=target_tris)
    C.set_smooth(obj, True)
    return obj


def _ellipsoid(name, c, r, subdiv=3):
    bm = geo.icosphere_bm(subdiv)
    for v in bm.verts:
        v.co = Vector((v.co.x * r[0], v.co.y * r[1], v.co.z * r[2]))
    o = C.from_bmesh(name, bm)
    o.location = Vector(c)
    return o


def _local_attr(obj, name):
    """配置前のローカル座標を属性に残す（目や足の模様をその部品の向きで描く）"""
    T.set_attr(obj, name, lambda co, n: (co.x, co.y, co.z))


# ---------------------------------------------------------------------------
# 顔の部品
# ---------------------------------------------------------------------------
def face_parts():
    face, eyes, teeth, mouth, dark, ears_in = [], [], [], [], [], []
    h = HEAD
    # 鼻づらと額（青緑）
    face.append(_ellipsoid("snout", h + Vector((0.0, -0.36, -0.12)), (0.44, 0.26, 0.3)))
    face.append(_ellipsoid("brow", h + Vector((0.0, -0.3, 0.17)), (0.42, 0.24, 0.2)))
    for o in face:
        C.set_smooth(o, True)
    # 目（大きな丸い目。ローカルの -Y が正面）
    for sx in (-1, 1):
        e = _ellipsoid(f"eye{sx}", (0, 0, 0), (0.13, 0.07, 0.13))
        _local_attr(e, "EyeCo")
        e.matrix_world = Matrix.Translation(h + Vector((sx * 0.17, -0.5, 0.12))) @ Matrix.Rotation(sx * 0.25, 4, "Z")
        C.set_smooth(e, True)
        eyes.append(e)
        # 太い眉（茶）
        b = T.rod(f"brow{sx}", h + Vector((sx * 0.05, -0.52, 0.29)), h + Vector((sx * 0.3, -0.48, 0.25)), 0.05, sides=10)
        C.set_smooth(b, True)
        dark.append(b)
    # 鼻
    n = _ellipsoid("nose", h + Vector((0.0, -0.64, -0.02)), (0.12, 0.07, 0.08))
    C.set_smooth(n, True)
    dark.append(n)
    # 口（暗い赤）と歯
    m = _ellipsoid("mouth", h + Vector((0.0, -0.55, -0.27)), (0.36, 0.09, 0.14))
    C.set_smooth(m, True)
    mouth.append(m)
    for row, (z, hgt) in enumerate(((-0.2, 0.11), (-0.34, 0.1))):
        for k in range(6):
            t = (k - 2.5) / 2.5
            x = t * 0.29
            y = -0.6 - 0.05 * (1 - t * t) + (0.0 if row == 0 else 0.01)
            tooth = geo.box(f"tooth{row}{k}", (0.085, 0.05, hgt), loc=h + Vector((x, y, z)),
                            rot=(0, 0, -t * 0.5))
            geo.bevel(tooth, 0.012, 2)
            teeth.append(tooth)
    # 耳（先が尖る。内側は赤）
    for sx in (-1, 1):
        prof = [(0.0, 0.0), (0.16, 0.0), (0.15, 0.08), (0.11, 0.2), (0.05, 0.3), (0.0, 0.34)]
        ear = T.lathe(f"ear{sx}", prof, segs=12)
        ear.data.transform(Matrix.Diagonal((1.0, 0.5, 1.0, 1.0)))
        ear.matrix_world = Matrix.Translation(h + Vector((sx * 0.36, -0.05, 0.42))) @ Matrix.Rotation(-sx * 0.45, 4, "Y")
        C.set_smooth(ear, True, angle=60)
        dark.append(ear)
        inner = T.lathe(f"earin{sx}", [(0.0, 0.0), (0.1, 0.0), (0.07, 0.14), (0.0, 0.24)], segs=10)
        inner.data.transform(Matrix.Diagonal((1.0, 0.3, 1.0, 1.0)))
        inner.matrix_world = Matrix.Translation(h + Vector((sx * 0.36, -0.12, 0.45))) @ Matrix.Rotation(-sx * 0.45, 4, "Y")
        ears_in.append(inner)
    return face, eyes, teeth, mouth, dark, ears_in


def paws():
    """前にそろえた白い足 3 本（丸みのある塊に爪の溝）"""
    out = []
    for k, x in enumerate(PAW_X):
        bm = geo.icosphere_bm(3)
        for v in bm.verts:
            d = v.co.normalized()
            p = 3.0
            s = (abs(d.x) ** p + abs(d.y) ** p + abs(d.z) ** p) ** (-1.0 / p)
            q = d * (0.5 + 0.5 * s)
            v.co = Vector((q.x * 0.18, q.y * 0.22, max(q.z, -0.6) * 0.15))
        o = C.from_bmesh(f"paw{k}", bm)
        _local_attr(o, "PawCo")
        o.location = (x, -0.32, BASE_H + 0.12)
        C.set_smooth(o, True)
        out.append(o)
    return out


# ---------------------------------------------------------------------------
# 看板・台
# ---------------------------------------------------------------------------
def sign_parts():
    panel_t = 0.05
    y = SIGN_Y + panel_t / 2
    zc = SIGN_Z0 + FRAME + SIGN_H / 2
    # 板面（前の面だけ Proc UV で印刷を貼る）
    x0, x1 = -SIGN_W / 2, SIGN_W / 2
    z0, z1 = zc - SIGN_H / 2, zc + SIGN_H / 2
    front = C.mesh_object("panel_front", [(x0, SIGN_Y, z0), (x1, SIGN_Y, z0), (x1, SIGN_Y, z1), (x0, SIGN_Y, z1)],
                          [(0, 1, 2, 3)], [[(0, 0), (1, 0), (1, 1), (0, 1)]])
    bake = front.data.uv_layers.new(name="Bake")
    for i, d in enumerate(front.data.uv_layers["Proc"].data):
        bake.data[i].uv = d.uv
    back = geo.box("panel_back", (SIGN_W + 0.02, panel_t, SIGN_H + 0.02), loc=(0, y + 0.001, zc))
    frame = []
    ow, oh = SIGN_W + 2 * FRAME, SIGN_H + 2 * FRAME
    for name, size, loc in (("fr_b", (ow, 0.12, FRAME), (0, y - 0.01, SIGN_Z0 + FRAME / 2)),
                            ("fr_t", (ow, 0.12, FRAME), (0, y - 0.01, SIGN_Z0 + oh - FRAME / 2)),
                            ("fr_l", (FRAME, 0.12, SIGN_H), (-ow / 2 + FRAME / 2, y - 0.01, zc)),
                            ("fr_r", (FRAME, 0.12, SIGN_H), (ow / 2 - FRAME / 2, y - 0.01, zc))):
        f = T.board(name, size, loc=loc, bevel=0.025, axis="X" if size[0] > size[2] else "Z")
        frame.append(f)
    return [front], [back], frame


def base():
    b = geo.box("base", (BASE_W, BASE_D, BASE_H), loc=(BASE_X, 0.05, BASE_H / 2))
    geo.bevel(b, 0.03, 2)
    return b


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def sculpture_paint(name, color, color2=None, res=1024, rough=0.55):
    """塗装した造形物（コテ跡の凹凸・褪せ・汚れ）"""
    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        n1 = nb.noise(co, 2.5, 4, 0.55)
        n2 = nb.noise(co, 14.0, 5, 0.6)
        c2 = srgb(color2) if color2 else nb.hsv(srgb(color), 0.5, 1.0, 1.25)
        col = nb.mix(srgb(color), c2, nb.smooth(n1, 0.35, 0.75))
        # 凹みに溜まった汚れ
        dirt = nb.smooth(nb.noise(co, 6.0, 4, 0.6), 0.55, 0.72)
        col = nb.mix(col, nb.hsv(col, 0.5, 0.8, 0.65), nb.mul(dirt, 0.35))
        # 下の方の砂ぼこり
        col = nb.mix(col, srgb("#b5a88b"), nb.mul(nb.smooth(z, BASE_H + 0.25, BASE_H), 0.35))
        chip = nb.smooth(nb.noise(co, 9.0, 6, 0.65), 0.7, 0.72)
        col = nb.mix(col, srgb("#bdb6a6"), nb.mul(chip, 0.6))
        height = nb.add(nb.mul(n2, 0.6), nb.mul(chip, -0.4))
        return dict(color=col, rough=nb.mixf(rough, 0.85, chip), height=height, height_scale=0.004)

    return pbr_material(name, fn, res=res, ao_distance=0.5)


def eye_material():
    def fn(nb):
        e = nb.attr("EyeCo", "Vector")
        x, y, z = nb.sep(e)
        r = nb.vmath("LENGTH", nb.comb(x, 0.0, z), out=1)
        col = nb.mix(srgb("#f1ead2"), srgb("#e2a72a"), nb.smooth(r, 0.095, 0.085))
        col = nb.mix(col, srgb("#111111"), nb.smooth(r, 0.06, 0.05))
        hl = nb.vmath("LENGTH", nb.comb(nb.add(x, 0.025), 0.0, nb.sub(z, 0.03)), out=1)
        col = nb.mix(col, srgb("#ffffff"), nb.smooth(hl, 0.018, 0.012))
        # 縁取り
        col = nb.mix(col, srgb("#1b1b1b"), nb.smooth(r, 0.115, 0.125))
        return dict(color=col, rough=0.25)

    return pbr_material("ShisaSignEye", fn, res=256, ao_distance=0.2)


def paw_material():
    def fn(nb):
        p = nb.attr("PawCo", "Vector")
        x, y, z = nb.sep(p)
        co = nb.coord("Object")
        col = srgb("#ebe7dc")
        claws = None
        for cx in (-0.06, 0.0, 0.06):
            s = nb.smooth(nb.math("ABSOLUTE", nb.sub(x, cx + 0.0)), 0.012, 0.006)
            claws = s if claws is None else nb.math("MAXIMUM", claws, s)
        # 溝は前の面と上の面だけ
        claws = nb.mul(claws, nb.smooth(y, 0.05, -0.08))
        claws = nb.mul(claws, nb.math("GREATER_THAN", nb.math("ABSOLUTE", x), 0.03))
        col = nb.mix(col, srgb("#1a1a1a"), claws)
        dirt = nb.smooth(nb.noise(co, 7.0, 4, 0.6), 0.55, 0.72)
        col = nb.mix(col, srgb("#9a917c"), nb.mul(dirt, 0.4))
        return dict(color=col, rough=0.55, height=nb.mul(claws, -1.0), height_scale=0.006)

    return pbr_material("ShisaSignPaw", fn, res=512, ao_distance=0.3)


def print_material(img_path):
    img = bpy.data.images.load(img_path, check_existing=True)

    def fn(nb):
        uv = nb.uv("Proc")
        col = nb.image(img, uv, ext="EXTEND")["Color"]
        co = nb.coord("Object")
        # 日焼けで少し白っぽく、雨だれの筋
        fade = nb.noise(co, 1.5, 3, 0.5)
        col = nb.mix(col, srgb("#f2f2ee"), nb.mul(nb.smooth(fade, 0.5, 0.8), 0.15))
        streak = nb.noise(nb.mapping(co, scale=(30, 30, 1.0)), 1.5, 4, 0.55)
        col = nb.mix(col, nb.hsv(col, 0.5, 1.0, 0.85), nb.mul(nb.smooth(streak, 0.6, 0.75), 0.4))
        return dict(color=col, rough=0.4, height=nb.noise(co, 40.0, 3, 0.5), height_scale=0.0005)

    return pbr_material("ShisaSignPrint", fn, res=2048, uv="keep", ao_distance=0.3)


def frame_material():
    """額縁（明るい木目の塗装）"""
    def fn(nb):
        co = nb.attr("WoodCo", "Vector")
        g = T.wood_grain(nb, co, rings=14.0, knots=0.2, crack_amt=0.5)
        col = nb.ramp(g["var"], [(0.2, srgb("#c49a5a")), (0.8, srgb("#dcb878"))])
        col = nb.mix(col, srgb("#8a6232"), nb.mul(g["ring"], 0.5))
        col = nb.mix(col, srgb("#5a3d1e"), nb.mul(g["crack"], 0.8))
        h = nb.add(nb.mul(g["fib"], 0.4), nb.mul(g["ring"], 0.5))
        return dict(color=col, rough=0.6, height=h, height_scale=0.002)

    return pbr_material("ShisaSignFrame", fn, res=512, ao_distance=0.3)


def stone_base_material():
    """乱石張りの台（灰色の石とモルタルの目地）"""
    def fn(nb):
        co = nb.coord("Object")
        f1 = nb.voronoi(co, 5.0, feature="F1")
        f2 = nb.voronoi(co, 5.0, feature="F2")
        edge = nb.sub(f2, f1)
        mortar = nb.smooth(edge, 0.06, 0.02)
        sc = nb.bw(nb.voronoi(co, 5.0, out="Color"))
        stone = nb.ramp(sc, [(0.0, srgb("#6f6d68")), (0.5, srgb("#8d8a83")), (1.0, srgb("#a8a499"))])
        stone = nb.hsv(stone, 0.5, 1.0, nb.maprange(nb.noise(co, 20.0, 4, 0.6), 0.3, 0.7, 0.85, 1.1))
        col = nb.mix(stone, srgb("#b8b2a2"), mortar)
        h = nb.sub(nb.smooth(edge, 0.0, 0.12), nb.mul(mortar, 0.5))
        return dict(color=col, rough=nb.mixf(0.75, 0.92, mortar), height=h, height_scale=0.012,
                    cavity=nb.maprange(mortar, 0, 1, 1.0, 0.6))

    return pbr_material("ShisaSignBase", fn, res=1024, ao_distance=0.5)


def build():
    body = body_mesh()
    face, eyes, teeth, mouth, dark, ears_in = face_parts()
    pw = paws()
    front, back, frame = sign_parts()
    b = base()
    groups = [
        ([body], sculpture_paint("ShisaSignBody", "#7a3e18", "#9a5a28", res=2048)),
        (face, sculpture_paint("ShisaSignFace", "#4f8798", "#6fa6b4", res=1024)),
        (dark, sculpture_paint("ShisaSignDark", "#45250f", "#5a3218", res=512)),
        (teeth, sculpture_paint("ShisaSignTeeth", "#f1eee4", "#e2ddcf", res=256, rough=0.45)),
        (mouth, sculpture_paint("ShisaSignMouth", "#7a1d22", "#5e161a", res=256)),
        (ears_in, sculpture_paint("ShisaSignEarIn", "#c84a5c", "#d86a78", res=256)),
        (eyes, eye_material()),
        (pw, paw_material()),
        (front, print_material(sign_image())),
        (back, sculpture_paint("ShisaSignBack", "#9aa7ad", "#b3bec2", res=512, rough=0.5)),
        (frame, frame_material()),
        ([b], stone_base_material()),
    ]
    out = []
    for objs, mat in groups:
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return {"ShisaSign": out}
