"""街の建物の外壁と屋上のタイルテクスチャ（numpy で描く。BaseColor・Normal・ORM）

外壁は 1 階分の高さ × 12 m（3 m の間口 4 つ）を 1 枚にし、横にも縦にも継ぎ目なく繰り返す。
絵の上が上の階の側。描く単位は m（x = 左から、y = 床から）。
"""
import math
import os

import numpy as np
from PIL import Image

TILE_W = 12.0
PX_PER_M = 1024 / TILE_W
ROOF_TILE = 16.0

GLASS = np.array((58, 70, 80)) / 255.0
GLASS_SKY = np.array((120, 140, 156)) / 255.0
FRAME = np.array((192, 194, 192)) / 255.0
DARK = np.array((40, 42, 44)) / 255.0
WHITE_TRIM = np.array((236, 236, 230)) / 255.0
VILLAGE_COLORS = [(232, 122, 66), (64, 168, 170), (238, 198, 72), (214, 84, 78), (140, 188, 92), (92, 140, 206),
                  (230, 150, 170), (242, 236, 220), (250, 170, 60), (110, 196, 196)]
SIGN_COLORS = [(200, 40, 40), (30, 80, 170), (240, 200, 40), (30, 140, 70), (240, 120, 30), (245, 245, 240),
               (20, 20, 24), (150, 40, 120)]


def srgb(c):
    return np.array(c, float) / 255.0


class Canvas:
    def __init__(self, height_m, rng, width_m=TILE_W, px_per_m=PX_PER_M):
        self.wm = width_m
        self.hm = height_m
        self.W = int(round(width_m * px_per_m))
        self.H = int(round(height_m * px_per_m / 32.0)) * 32
        self.rng = rng
        self.col = np.zeros((self.H, self.W, 3))
        self.h = np.zeros((self.H, self.W))
        self.rough = np.full((self.H, self.W), 0.85)
        self.metal = np.zeros((self.H, self.W))
        self.shade = np.ones((self.H, self.W))

    def sl(self, x0, y0, x1, y1):
        """m の矩形 → 配列のスライス（y は床から上向き）"""
        c0 = int(round(x0 / self.wm * self.W))
        c1 = int(round(x1 / self.wm * self.W))
        r0 = int(round((1.0 - y1 / self.hm) * self.H))
        r1 = int(round((1.0 - y0 / self.hm) * self.H))
        return slice(max(r0, 0), min(r1, self.H)), slice(max(c0, 0), min(c1, self.W))

    def rect(self, x0, y0, x1, y1, color=None, h=None, rough=None, metal=None):
        s = self.sl(x0, y0, x1, y1)
        if color is not None:
            self.col[s] = color
        if h is not None:
            self.h[s] = h
        if rough is not None:
            self.rough[s] = rough
        if metal is not None:
            self.metal[s] = metal

    def fill(self, color, noise=0.04, scale=0.6):
        self.col[:] = color
        self.col *= (1.0 + self.noise(scale) * noise * 2)[..., None]

    def noise(self, cell_m):
        """横と縦に繰り返す値ノイズ（-0.5..0.5）"""
        cell = max(2, int(cell_m * self.W / self.wm))
        gw = max(2, self.W // cell)
        gh = max(2, self.H // cell)
        g = self.rng.random((gh, gw))
        g = np.concatenate([g, g[:1]], 0)
        g = np.concatenate([g, g[:, :1]], 1)
        im = Image.fromarray((g * 255).astype(np.uint8)).resize((self.W + self.W // gw, self.H + self.H // gh),
                                                               Image.BICUBIC)
        return np.asarray(im, float)[:self.H, :self.W] / 255.0 - 0.5

    def glass(self, x0, y0, x1, y1, recess=-0.1):
        s = self.sl(x0, y0, x1, y1)
        n = s[0].stop - s[0].start
        if n <= 0:
            return
        t = np.linspace(1.0, 0.0, n)[:, None, None]
        self.col[s] = GLASS * (1 - t * 0.6) + GLASS_SKY * t * 0.6
        self.h[s] = recess
        self.rough[s] = 0.08
        self.metal[s] = 0.0

    def frame_window(self, x0, y0, x1, y1, mullions=1, recess=-0.1, fw=0.05, color=FRAME):
        """枠付きの窓（引き違いの縦桟 mullions 本）"""
        self.glass(x0, y0, x1, y1, recess)
        for a, b, c, d in ((x0, y0, x1, y0 + fw), (x0, y1 - fw, x1, y1), (x0, y0, x0 + fw, y1), (x1 - fw, y0, x1, y1)):
            self.rect(a, b, c, d, color, recess + 0.03, 0.35, 0.7)
        for k in range(1, mullions + 1):
            x = x0 + (x1 - x0) * k / (mullions + 1)
            self.rect(x - fw / 2, y0, x + fw / 2, y1, color, recess + 0.03, 0.35, 0.7)

    def darken(self, x0, y0, x1, y1, amount, fade_down=True):
        """影（上が濃く下へ薄くなる帯）"""
        s = self.sl(x0, y0, x1, y1)
        n = s[0].stop - s[0].start
        if n <= 0:
            return
        t = np.linspace(0.0, 1.0, n)[:, None] if fade_down else np.ones((n, 1))
        self.shade[s] *= 1.0 - amount * (1.0 - t) ** 1.5

    def streaks(self, xs_m, y_top, length, amount):
        """雨だれの筋（xs_m の位置から下へ、縦に繰り返す）"""
        for x in xs_m:
            w = self.rng.uniform(0.04, 0.16)
            ln = length * self.rng.uniform(0.4, 1.0)
            a = amount * self.rng.uniform(0.5, 1.0)
            c0 = int((x % self.wm) / self.wm * self.W)
            c1 = c0 + max(1, int(w / self.wm * self.W))
            r_top = int((1.0 - y_top / self.hm) * self.H)
            n = int(ln / self.hm * self.H)
            prof = np.exp(-np.linspace(0, 3, max(n, 1)))
            rows = (r_top + np.arange(max(n, 1))) % self.H
            for k, r in enumerate(rows):
                self.shade[r, c0 % self.W:min(c1, self.W)] *= 1.0 - a * prof[k]

    def grime(self, top_amount=0.18, base_amount=0.08):
        """上の縁のくすみと全体のむら"""
        g = self.noise(1.5) * 0.12 + self.noise(0.25) * 0.05
        self.shade *= 1.0 + g
        n = int(self.H * 0.12)
        prof = np.linspace(1, 0, n)[:, None] ** 2
        self.shade[:n] *= 1.0 - top_amount * prof * (0.6 + 0.8 * (self.noise(0.8)[:n] + 0.5))
        self.shade[-max(1, int(self.H * 0.05)):] *= 1.0 - base_amount

    def maps(self):
        """BaseColor・Normal・ORM の配列（uint8）"""
        h = self.h
        hb = (h + np.roll(h, 1, 0) + np.roll(h, -1, 0) + np.roll(h, 1, 1) + np.roll(h, -1, 1)) / 5.0
        pm = self.wm / self.W
        dx = (np.roll(hb, -1, 1) - np.roll(hb, 1, 1)) / (2 * pm)
        dy = (np.roll(hb, 1, 0) - np.roll(hb, -1, 0)) / (2 * pm)
        k = 0.06
        n = np.stack([-dx * k, -dy * k, np.ones_like(dx)], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        big = hb.copy()
        for r in (3, 9, 21):
            acc = np.zeros_like(big)
            for sh in range(-r, r + 1, max(1, r // 3)):
                acc += np.roll(hb, sh, 0) + np.roll(hb, sh, 1)
            big = acc / (2 * len(range(-r, r + 1, max(1, r // 3))))
        ao = np.clip(1.0 - (big - hb) * 3.0, 0.45, 1.0)
        col = np.clip(self.col * self.shade[..., None], 0, 1)
        color = (col * 255 + 0.5).astype(np.uint8)
        normal = ((n * 0.5 + 0.5) * 255 + 0.5).astype(np.uint8)
        orm = np.stack([ao, self.rough, self.metal], -1)
        return color, normal, (np.clip(orm, 0, 1) * 255 + 0.5).astype(np.uint8)


# ---- 外壁 ----

def house(c, wall):
    """沖縄のコンクリートの戸建て（窓の上の庇・花ブロック・雨だれ）"""
    c.fill(wall, 0.05)
    c.rect(0, 0, TILE_W, 0.03, wall * 0.8, -0.01)
    wins = [(0.8, 1.7, 0.9, 1.15, 1), (4.0, 0.8, 1.35, 0.8, 0), (6.4, 1.8, 0.15, 2.0, 1), (9.6, 1.7, 0.9, 1.15, 1)]
    flower = c.rng.random() < 0.6
    for i, (x, w, sill, h, mull) in enumerate(wins):
        if flower and i == 1:
            continue
        c.frame_window(x, sill, x + w, sill + h, mull)
        top = sill + h
        c.rect(x - 0.18, top + 0.06, x + w + 0.18, top + 0.18, wall * 1.04, 0.3)
        c.darken(x - 0.15, top - 0.45, x + w + 0.15, top + 0.06, 0.45)
        c.rect(x - 0.05, sill - 0.06, x + w + 0.05, sill, wall * 0.95, 0.04)
        c.streaks([x + c.rng.uniform(-0.05, 0.1), x + w - c.rng.uniform(-0.05, 0.1)], sill - 0.06, 1.2, 0.35)
    if flower:
        x0, y0 = 3.7, 0.6
        for i in range(7):
            for j in range(10):
                bx, by = x0 + i * 0.2, y0 + j * 0.2
                c.rect(bx, by, bx + 0.2, by + 0.2, wall * 0.97, 0.05)
                c.rect(bx + 0.05, by + 0.05, bx + 0.15, by + 0.15, DARK * 1.5, -0.15, 0.9, 0.0)
                c.rect(bx + 0.085, by + 0.03, bx + 0.115, by + 0.17, wall * 0.97, 0.05)
                c.rect(bx + 0.03, by + 0.085, bx + 0.17, by + 0.115, wall * 0.97, 0.05)
    c.streaks(c.rng.uniform(0, TILE_W, 9), c.hm, 1.6, 0.3)
    c.grime(0.25)


def apartment(c, wall, accent, glass_rail=False):
    """ベランダ側のマンション（腰壁・スラブの縁・仕切り・掃き出し窓）"""
    c.fill(wall * 0.82, 0.03)
    c.h[:] = -1.2
    for unit in (0.0, 6.0):
        c.frame_window(unit + 0.9, 0.15, unit + 3.6, 2.15, 1, -1.25)
        c.frame_window(unit + 3.9, 0.9, unit + 5.4, 2.15, 1, -1.25)
    c.darken(0, 1.6, TILE_W, 2.72, 0.5)
    if glass_rail:
        s = c.sl(0, 0.05, TILE_W, 1.1)
        c.col[s] = c.col[s] * 0.55 + srgb((150, 175, 180)) * 0.45
        c.rough[s] = 0.1
        c.rect(0, 1.02, TILE_W, 1.1, FRAME, 0.0, 0.35, 0.7)
    else:
        c.rect(0, 0.0, TILE_W, 1.1, accent, 0.0)
        c.rect(0, 1.02, TILE_W, 1.1, accent * 1.06, 0.03)
        c.streaks(c.rng.uniform(0, TILE_W, 10), 1.02, 0.9, 0.3)
    c.rect(0, 2.72, TILE_W, 3.0, wall, 0.0)
    c.rect(0, 2.72, TILE_W, 2.76, wall * 0.9, 0.0)
    for x0, x1 in ((0.0, 0.1), (11.9, 12.0), (5.9, 6.1)):
        c.rect(x0, 1.1, x1, 2.72, wall * 0.95, -0.1)
    for unit in (0.0, 6.0):
        if c.rng.random() < 0.3:
            for k in range(c.rng.integers(1, 4)):
                x = unit + c.rng.uniform(1.0, 5.0)
                col = srgb(SIGN_COLORS[c.rng.integers(len(SIGN_COLORS))]) * 0.35 + 0.5
                c.rect(x, 1.15, x + c.rng.uniform(0.25, 0.45), 1.15 + c.rng.uniform(0.3, 0.55), col, -0.4, 0.95)
    c.streaks(c.rng.uniform(0, TILE_W, 6), c.hm, 0.8, 0.25)
    c.grime(0.12)


def tower(c, wall):
    """ホテル・高層マンション（白いスラブの縁・ガラスの手すり・4 m ごとの仕切り）"""
    c.fill(wall * 0.8, 0.02)
    c.h[:] = -1.5
    for x in (0.0, 4.0, 8.0):
        c.frame_window(x + 0.35, 0.1, x + 3.65, 2.5, 2, -1.55)
    c.darken(0, 1.8, TILE_W, 2.8, 0.45)
    s = c.sl(0, 0.05, TILE_W, 1.15)
    c.col[s] = c.col[s] * 0.5 + srgb((160, 190, 196)) * 0.5
    c.rough[s] = 0.08
    c.rect(0, 1.1, TILE_W, 1.15, FRAME, 0.0, 0.3, 0.8)
    c.rect(0, 2.8, TILE_W, 3.1, wall, 0.0)
    for x in (0.0, 4.0, 8.0):
        c.rect(x, 1.15, x + 0.18, 2.8, wall * 0.96, -0.05)
    c.streaks(c.rng.uniform(0, TILE_W, 4), 2.8, 0.6, 0.15)
    c.grime(0.06, 0.03)


def office(c, wall):
    """学校・役所・事務所（連続した窓と腰壁）"""
    c.fill(wall, 0.04)
    c.glass(0, 0.9, TILE_W, 2.9, -0.08)
    for k in range(9):
        x = k * 1.5
        c.rect(x - 0.04, 0.9, x + 0.04, 2.9, FRAME, -0.04, 0.35, 0.7)
    c.rect(0, 2.25, TILE_W, 2.29, FRAME, -0.04, 0.35, 0.7)
    c.rect(0, 0.84, TILE_W, 0.9, wall * 0.92, 0.05)
    c.darken(0, 2.4, TILE_W, 2.9, 0.35)
    c.streaks(c.rng.uniform(0, TILE_W, 8), 0.84, 0.8, 0.3)
    c.grime(0.18)


def shopfront(c, wall):
    """1 階の店（ガラスの店先・シャッター・色のついた看板）"""
    c.fill(wall, 0.04)
    for b in range(3):
        x = b * 4.0
        kind = c.rng.random()
        if kind < 0.6:
            c.frame_window(x + 0.2, 0.05, x + 3.8, 2.8, 2, -0.12, 0.07, FRAME * 0.9)
        elif kind < 0.85:
            s = c.sl(x + 0.2, 0.05, x + 3.8, 2.8)
            rows = np.arange(s[0].stop - s[0].start)
            rib = 0.5 + 0.5 * np.sin(rows / 3.0)
            c.col[s] = srgb((168, 170, 168)) * (0.85 + 0.15 * rib)[:, None, None]
            c.h[s] = (-0.05 + 0.01 * rib)[:, None]
            c.rough[s] = 0.5
            c.metal[s] = 0.5
        else:
            c.frame_window(x + 0.2, 0.05, x + 1.8, 2.4, 0, -0.12)
            c.rect(x + 1.8, 0.05, x + 3.8, 2.8, wall * 0.95)
            c.frame_window(x + 2.2, 1.0, x + 3.5, 2.3, 1)
        sign = srgb(SIGN_COLORS[c.rng.integers(len(SIGN_COLORS))])
        c.rect(x + 0.1, 2.95, x + 3.9, 3.75, sign, 0.08, 0.5)
        for k in range(c.rng.integers(3, 7)):
            lx = x + 0.4 + k * 0.5
            if lx > x + 3.5:
                break
            light = np.clip(1.0 - sign + 0.1, 0, 1) if sign.mean() > 0.5 else np.clip(sign * 0.2 + 0.85, 0, 1)
            c.rect(lx, 3.15, lx + 0.36, 3.55, light, 0.09, 0.5)
    c.darken(0, 2.3, TILE_W, 2.95, 0.4)
    c.grime(0.1, 0.12)


def bigbox(c, wall, accent):
    """大きな店・倉庫（縦のリブの金属の壁と色の帯）"""
    c.fill(wall, 0.03)
    xs = np.arange(c.W)
    rib = 0.5 + 0.5 * np.cos(xs / c.W * TILE_W / 0.3 * 2 * math.pi)
    c.h[:] = rib[None, :] * 0.02
    c.col *= (0.94 + 0.06 * rib)[None, :, None]
    c.rough[:] = 0.55
    c.rect(0, 3.6, TILE_W, 4.2, accent, 0.03, 0.5)
    if c.rng.random() < 0.5:
        c.rect(5.0, 0.0, 6.8, 2.4, srgb((120, 124, 128)), -0.05, 0.4, 0.6)
    c.streaks(c.rng.uniform(0, TILE_W, 6), c.hm, 2.0, 0.2)
    c.grime(0.15, 0.1)


def village(c):
    """アメリカンビレッジの色壁（間口ごとに違う色・白い窓枠・看板）"""
    for b in range(4):
        x = b * 3.0
        col = srgb(VILLAGE_COLORS[c.rng.integers(len(VILLAGE_COLORS))])
        c.rect(x, 0, x + 3.0, c.hm, col)
        c.col[c.sl(x, 0, x + 3.0, c.hm)] *= (1.0 + c.noise(0.5)[c.sl(x, 0, x + 3.0, c.hm)] * 0.06)[..., None]
        c.frame_window(x + 0.8, 0.9, x + 2.2, 2.6, 1, -0.1, 0.07, WHITE_TRIM)
        c.rect(x + 0.6, 0.9, x + 0.8, 2.6, col * 0.7, 0.03)
        c.rect(x + 2.2, 0.9, x + 2.4, 2.6, col * 0.7, 0.03)
        c.rect(x + 0.7, 2.6, x + 2.3, 2.75, WHITE_TRIM, 0.05)
        if c.rng.random() < 0.35:
            sign = srgb(SIGN_COLORS[c.rng.integers(len(SIGN_COLORS))])
            c.rect(x + 0.4, 2.95, x + 2.6, 3.45, sign, 0.06, 0.5)
    c.rect(0, 0, TILE_W, 0.12, WHITE_TRIM, 0.02)
    c.rect(0, c.hm - 0.12, TILE_W, c.hm, WHITE_TRIM, 0.02)
    c.grime(0.06, 0.05)


def base(c, wall):
    """米軍施設・住宅（ベージュのスタッコ・白い窓枠の小さな窓）"""
    c.fill(wall, 0.05, 0.3)
    for k in range(4):
        x = k * 3.0 + 0.7
        c.frame_window(x, 1.0, x + 1.6, 2.4, 1, -0.08, 0.06, WHITE_TRIM)
        c.rect(x - 0.05, 0.92, x + 1.65, 1.0, WHITE_TRIM * 0.95, 0.03)
    c.streaks(c.rng.uniform(0, TILE_W, 5), 0.92, 0.7, 0.15)
    c.grime(0.1)


# 外壁の種類: 名前 → (1 階の高さ m, 描く関数, 色の候補, 屋上の手すり壁に使う v の範囲)
STYLES = {
    "House": (3.0, "house", [("White", (232, 230, 222)), ("Cream", (233, 222, 196)), ("Pink", (231, 212, 202)),
                             ("Mint", (212, 224, 212)), ("Gray", (206, 207, 204))], (0.0, 0.25)),
    "Apt": (3.0, "apartment", [("White", (230, 229, 224)), ("Beige", (222, 210, 188)), ("Gray", (202, 203, 200)),
                               ("Brown", (226, 218, 204))], (0.0, 0.3)),
    "AptGlass": (3.0, "apartment_glass", [("White", (234, 234, 230)), ("Beige", (224, 214, 194))], (0.75, 0.95)),
    "Tower": (3.1, "tower", [("White", (238, 238, 234)), ("Beige", (226, 216, 196))], (0.92, 1.0)),
    "Office": (3.6, "office", [("White", (226, 226, 222)), ("Gray", (192, 194, 192))], (0.0, 0.22)),
    "Shop": (4.0, "shopfront", [("A", (214, 210, 200)), ("B", (196, 194, 190))], (0.95, 1.0)),
    "BigBox": (5.0, "bigbox", [("Gray", (198, 200, 198)), ("Ivory", (228, 224, 210))], (0.86, 0.98)),
    "Village": (3.6, "village", [("A", None), ("B", None)], (0.0, 0.03)),
    "Base": (3.2, "base", [("Beige", (214, 199, 168)), ("Cream", (228, 218, 194))], (0.0, 0.28)),
}
ACCENTS = {"Brown": (150, 112, 82), "Gray": (60, 90, 140), "Ivory": (40, 120, 70)}


def paint_facade(style, palette, seed):
    floor_h, fn, palettes, _ = STYLES[style]
    rng = np.random.default_rng(seed)
    c = Canvas(floor_h, rng)
    wall = None
    for name, col in palettes:
        if name == palette:
            wall = srgb(col) if col else None
    accent = srgb(ACCENTS.get(palette, (0, 0, 0))) if palette in ACCENTS else (wall * 1.03 if wall is not None else None)
    if fn == "house":
        house(c, wall)
    elif fn == "apartment":
        apartment(c, wall, accent)
    elif fn == "apartment_glass":
        apartment(c, wall, wall, glass_rail=True)
    elif fn == "tower":
        tower(c, wall)
    elif fn == "office":
        office(c, wall)
    elif fn == "shopfront":
        shopfront(c, wall)
    elif fn == "bigbox":
        bigbox(c, wall, accent if palette in ACCENTS else srgb((180, 60, 50)))
    elif fn == "village":
        village(c)
    elif fn == "base":
        base(c, wall)
    return c.maps()


# ---- 屋上 ----

def paint_roof(kind, seed):
    rng = np.random.default_rng(seed)
    c = Canvas(ROOF_TILE, rng, ROOF_TILE, 512 / ROOF_TILE)
    if kind == "Concrete":
        c.fill(srgb((168, 167, 160)), 0.06, 1.0)
        c.shade *= 1.0 - np.clip(c.noise(2.5) * 0.9 + c.noise(0.6) * 0.4, 0, 1) * 0.35
        c.rough[:] = 0.92
        for _ in range(8):
            x, y = rng.uniform(0, ROOF_TILE, 2)
            c.rect(x, y, x + 0.25, y + 0.25, DARK * 2, -0.02)
    elif kind == "Coated":
        c.fill(srgb((150, 158, 148)), 0.05, 1.5)
        c.shade *= 1.0 - np.clip(c.noise(3.0), 0, 1) * 0.25
        c.rough[:] = 0.75
        for k in range(5):
            y = k * ROOF_TILE / 5
            c.rect(0, y, ROOF_TILE, y + 0.06, srgb((136, 144, 134)), 0.01)
    else:
        c.fill(srgb((186, 188, 188)), 0.03, 2.0)
        xs = np.arange(c.W)
        rib = np.maximum(0, np.cos(xs / c.W * ROOF_TILE / 0.6 * 2 * math.pi)) ** 6
        c.h[:] = rib[None, :] * 0.04
        c.col *= (0.92 + 0.08 * rib)[None, :, None]
        c.rough[:] = 0.5
        c.shade *= 1.0 - np.clip(c.noise(4.0), 0, 1) * 0.2
    return c.maps()


def save_maps(out_dir, prefix, maps):
    color, normal, orm = maps
    paths = {}
    for key, arr in (("BaseColor", color), ("Normal", normal), ("ORM", orm)):
        p = os.path.join(out_dir, f"{prefix}_{key}.png")
        Image.fromarray(arr, "RGB").save(p, optimize=True)
        paths[key] = p
    return paths
