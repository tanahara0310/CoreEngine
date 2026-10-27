"""街の建物の外壁・屋上と海岸の護岸のタイルテクスチャ（numpy で描く。BaseColor・Normal・ORM）

外壁は 1 階分の高さ × 12 m（3 m の間口 4 つ）を 1 枚にし、横にも縦にも継ぎ目なく繰り返す。
絵の上が上の階の側。描く単位は m（x = 左から、y = 床から）。
凹凸は実寸の高さ [m] で描き、法線はその勾配、AO は周りより低い所の深さから求める。
古さ（黒カビの雨だれ・錆の筋・ひび割れと補修・塗装の剥がれと塗り直しのむら）は最後に重ねる。
"""
import math
import os

import numpy as np
from PIL import Image

TILE_W = 12.0
PX_PER_M = 2048 / TILE_W
ROOF_TILE = 16.0

ALU = np.array((186, 188, 186)) / 255.0
ALU_BRONZE = np.array((92, 78, 62)) / 255.0
ALU_BLACK = np.array((48, 48, 50)) / 255.0
CONCRETE = np.array((164, 160, 152)) / 255.0
MOLD = np.array((52, 56, 50)) / 255.0
RUST = np.array((128, 74, 40)) / 255.0
WHITE_TRIM = np.array((232, 232, 226)) / 255.0
CURTAINS = [(226, 218, 196), (236, 234, 226), (188, 204, 214), (222, 196, 186), (168, 168, 160), (210, 200, 160),
            (150, 170, 150)]
VILLAGE_COLORS = [(222, 118, 66), (62, 160, 164), (232, 192, 74), (204, 82, 76), (136, 182, 92), (90, 136, 200),
                  (224, 146, 166), (238, 232, 216), (244, 166, 62), (106, 190, 190)]
SIGN_COLORS = [(196, 42, 40), (32, 80, 168), (236, 196, 44), (32, 136, 72), (236, 118, 32), (242, 242, 236),
               (24, 24, 28), (146, 42, 118)]


def srgb(c):
    return np.array(c, float) / 255.0


class Canvas:
    """外壁 1 枚分の配列（色・高さ [m]・粗さ・金属・色の陰・AO の陰）"""

    def __init__(self, width_m, height_m, px_per_m, rng):
        self.wm = width_m
        self.hm = height_m
        self.W = max(32, int(round(width_m * px_per_m / 32.0)) * 32)
        self.H = max(32, int(round(height_m * px_per_m / 32.0)) * 32)
        self.pm = width_m / self.W
        self.rng = rng
        self.col = np.zeros((self.H, self.W, 3))
        self.h = np.zeros((self.H, self.W))
        self.rough = np.full((self.H, self.W), 0.88)
        self.metal = np.zeros((self.H, self.W))
        self.shade = np.ones((self.H, self.W))
        self.occ = np.ones((self.H, self.W))
        self.sources = []     # 雨だれの出どころ (x, y, 長さ, 強さ, 色)

    # ---- 範囲 ----

    def sl(self, x0, y0, x1, y1):
        """m の矩形 → 配列のスライス（y は床から上向き）"""
        c0 = int(round(x0 / self.wm * self.W))
        c1 = int(round(x1 / self.wm * self.W))
        r0 = int(round((1.0 - y1 / self.hm) * self.H))
        r1 = int(round((1.0 - y0 / self.hm) * self.H))
        r0, r1 = min(max(r0, 0), self.H), min(max(r1, 0), self.H)
        c0, c1 = min(max(c0, 0), self.W), min(max(c1, 0), self.W)
        return slice(r0, max(r0, r1)), slice(c0, max(c0, c1))

    def rect(self, x0, y0, x1, y1, color=None, h=None, rough=None, metal=None, add_h=None, blend=1.0):
        s = self.sl(x0, y0, x1, y1)
        if color is not None:
            self.col[s] = self.col[s] * (1 - blend) + np.asarray(color) * blend
        if h is not None:
            self.h[s] = h
        if add_h is not None:
            self.h[s] += add_h
        if rough is not None:
            self.rough[s] = rough
        if metal is not None:
            self.metal[s] = metal

    def ring_mask(self, cx, cy, r0, r1):
        """円環（中心 cx, cy、半径 r0..r1 [m]）の 0/1"""
        y = (1.0 - (np.arange(self.H) + 0.5) / self.H) * self.hm
        x = (np.arange(self.W) + 0.5) / self.W * self.wm
        d = np.hypot(x[None, :] - cx, y[:, None] - cy)
        return (d >= r0) & (d <= r1)

    # ---- ノイズ ----

    def noise(self, cell_m, aniso=1.0):
        """横と縦に継ぎ目なく繰り返す値ノイズ（-0.5..0.5）。aniso > 1 で縦に伸びる"""
        gw = max(2, int(round(self.wm / cell_m)))
        gh = max(2, int(round(self.hm / (cell_m * aniso))))
        g = self.rng.random((gh, gw)).astype(np.float32)
        big = np.tile(g, (3, 3))
        im = Image.fromarray((big * 255).astype(np.uint8)).resize((self.W * 3, self.H * 3), Image.BICUBIC)
        a = np.asarray(im, np.float32)[self.H:2 * self.H, self.W:2 * self.W] / 255.0
        return a - 0.5

    def fbm(self, cell_m, octaves=4, gain=0.5, aniso=1.0):
        out = np.zeros((self.H, self.W), np.float32)
        amp = 1.0
        total = 0.0
        for k in range(octaves):
            c = cell_m / (2 ** k)
            if c < self.pm * 1.5:
                break
            out += self.noise(c, aniso) * amp
            total += amp
            amp *= gain
        return out / max(total, 1e-6)

    # ---- 部品 ----

    def streak(self, x, y_top, length, width, amount, color=MOLD):
        """上から垂れる筋（縦に継ぎ目なく繰り返す）。色へ寄せ、少し粗くする"""
        r_top = int(round((1.0 - y_top / self.hm) * self.H))
        n = max(2, int(length / self.pm))
        rows = (r_top + np.arange(n)) % self.H
        wpx = max(1, int(width / self.pm))
        prof = np.exp(-np.linspace(0, 3.2, n)) * (0.85 + 0.3 * (self.rng.random(n) - 0.5))
        wob = np.cumsum(self.rng.normal(0, 0.25, n)) * 0.6
        c0 = int((x % self.wm) / self.pm)
        across = np.linspace(-1, 1, wpx)
        hp = np.clip(1.0 - across ** 2, 0, 1) ** 0.7
        for k, r in enumerate(rows):
            cols = (c0 + int(wob[k]) + np.arange(wpx)) % self.W
            a = amount * prof[k] * hp
            self.col[r, cols] = self.col[r, cols] * (1 - a[:, None]) + color * a[:, None]

    def crack(self, x, y, length, repaired=False):
        """ひび（細い暗い線）。補修したものは明るい幅広の線の上に細い線"""
        pts = [(x, y)]
        ang = self.rng.choice([-1.0, 1.0]) * self.rng.uniform(0.9, 1.4) - math.pi / 2
        step = self.pm * 1.5
        for _ in range(int(length / step)):
            ang += self.rng.normal(0, 0.06)
            x += math.cos(ang) * step
            y += math.sin(ang) * step
            pts.append((x, y))
        for px_, py_ in pts:
            c = int((px_ % self.wm) / self.pm)
            r = int(((1.0 - (py_ % self.hm) / self.hm) * self.H)) % self.H
            if repaired:
                rr = (r + np.arange(-3, 4)) % self.H
                cc = (c + np.arange(-3, 4)) % self.W
                self.col[np.ix_(rr, cc)] = self.col[np.ix_(rr, cc)] * 0.6 + srgb((176, 174, 168)) * 0.4
                self.h[np.ix_(rr, cc)] += 0.0004
            self.col[r, c] *= 0.45
            self.h[r, c] -= 0.002
            self.occ[r, c] *= 0.7

    def peel(self, amount):
        """塗装の剥がれ（下のコンクリートが出る）"""
        m = self.fbm(0.6, 4) + self.fbm(0.15, 3) * 0.4
        thr = 0.5 - amount
        mask = np.clip((m - thr) / 0.02, 0, 1)
        bare = CONCRETE * (0.9 + 0.2 * (self.noise(0.05) + 0.5))[..., None]
        self.col = self.col * (1 - mask[..., None]) + bare * mask[..., None]
        self.h -= 0.0006 * mask
        self.rough = np.maximum(self.rough, 0.92 * mask)

    def glass(self, x0, y0, x1, y1, recess, interior=True):
        """ガラス（空の映り込み＋部屋の中のカーテン・ブラインド・暗い部屋）"""
        s = self.sl(x0, y0, x1, y1)
        n = s[0].stop - s[0].start
        m = s[1].stop - s[1].start
        if n <= 0 or m <= 0:
            return
        t = np.linspace(1.0, 0.0, n)[:, None]
        sky = srgb((150, 172, 190)) * (0.55 + 0.45 * t[..., None]) + srgb((210, 214, 216)) * 0.1
        room = np.full((n, m, 3), 0.06)
        if interior:
            kind = self.rng.random()
            if kind < 0.55:
                cur = srgb(CURTAINS[self.rng.integers(len(CURTAINS))])
                folds = 0.8 + 0.2 * np.sin(np.arange(m) / max(2.0, 0.06 / self.pm) * 2.0)
                cover = self.rng.uniform(0.4, 1.0)
                cm = int(m * cover)
                side = self.rng.random() < 0.5
                cs = slice(0, cm) if side else slice(m - cm, m)
                room[:, cs] = cur * 0.55 * folds[cs, None]
            elif kind < 0.75:
                lines = (np.arange(n) % max(2, int(0.04 / self.pm))) < max(1, int(0.03 / self.pm))
                room[:] = np.where(lines[:, None, None], srgb((200, 200, 194)) * 0.5, 0.08)
            else:
                room *= 0.6
        fres = 0.35 + 0.25 * t[..., None]
        self.col[s] = room * (1 - fres) + sky * fres
        self.h[s] = recess
        self.rough[s] = 0.05
        self.metal[s] = 0.0

    def window(self, x0, y0, x1, y1, mullions=1, recess=-0.08, frame=ALU, hood=True, sill=True, grille=False,
               wall=None, interior=True):
        """沖縄のアルミサッシの窓（枠・中桟・水切り・庇・面格子）"""
        fw = 0.045
        self.rect(x0 - 0.03, y0 - 0.03, x1 + 0.03, y1 + 0.03, h=recess + 0.02)
        self.glass(x0, y0, x1, y1, recess, interior)
        metal = 0.85 if frame is ALU else 0.4
        for a, b, c, d in ((x0, y0, x1, y0 + fw), (x0, y1 - fw, x1, y1), (x0, y0, x0 + fw, y1), (x1 - fw, y0, x1, y1)):
            self.rect(a, b, c, d, frame, recess + 0.035, 0.35, metal)
        for k in range(1, mullions + 1):
            x = x0 + (x1 - x0) * k / (mullions + 1)
            self.rect(x - fw * 0.6, y0, x + fw * 0.6, y1, frame * 0.95, recess + 0.04, 0.35, metal)
        if sill:
            self.rect(x0 - 0.06, y0 - 0.07, x1 + 0.06, y0 - 0.02, ALU * 0.92, 0.03, 0.4, 0.8)
            for xs in (x0 + 0.02, x1 - 0.06, (x0 + x1) / 2):
                self.sources.append((xs, y0 - 0.07, self.rng.uniform(0.4, 1.4), self.rng.uniform(0.12, 0.3), MOLD))
        if hood and wall is not None:
            top = y1 + 0.04
            self.rect(x0 - 0.2, top, x1 + 0.2, top + 0.11, wall * 1.03, 0.25)
            self.rect(x0 - 0.2, top, x1 + 0.2, top + 0.015, wall * 0.8, 0.2)
            s = self.sl(x0 - 0.3, y1 - 0.5, x1 + 0.3, top)
            k = s[0].stop - s[0].start
            m = s[1].stop - s[1].start
            if k > 0 and m > 0:
                g = np.linspace(0.0, 1.0, k)[:, None] ** 1.8
                edge = np.clip(np.minimum(np.arange(m), np.arange(m)[::-1]) / max(1.0, 0.2 / self.pm), 0, 1)[None, :]
                self.shade[s] *= 1.0 - 0.15 * g * edge
                self.occ[s] *= 1.0 - 0.45 * g * edge
            for xs in (x0 - 0.18, x1 + 0.14):
                self.sources.append((xs, top, self.rng.uniform(0.5, 1.8), self.rng.uniform(0.18, 0.4), MOLD))
        if grille:
            bars = np.arange(x0 + 0.06, x1 - 0.03, 0.11)
            for xb in bars:
                self.rect(xb - 0.012, y0 - 0.02, xb + 0.012, y1 + 0.02, ALU * 0.9, 0.06, 0.4, 0.8)
            for yb in (y0 + 0.08, y1 - 0.08):
                self.rect(x0 - 0.02, yb - 0.015, x1 + 0.02, yb + 0.015, ALU * 0.9, 0.065, 0.4, 0.8)
            self.sources.append((x0, y0 - 0.02, 0.5, 0.15, RUST))

    def ac_unit(self, x, y, w=0.8, hgt=0.58):
        """室外機（ファンの丸い格子・横の通風口・配管と下の排水の跡）"""
        body = srgb((214, 212, 202)) * (0.9 + 0.1 * self.rng.random())
        self.rect(x, y, x + w, y + hgt, body, 0.28, 0.6, 0.1)
        s = self.sl(x - 0.04, y - 0.12, x + w + 0.04, y)
        self.shade[s] *= 0.75
        self.occ[s] *= 0.7
        cx, cy, r = x + w * 0.38, y + hgt * 0.5, hgt * 0.38
        disk = self.ring_mask(cx, cy, 0.0, r)
        self.col[disk] = srgb((40, 42, 44))
        self.h[disk] = 0.22
        for rr in np.arange(r * 0.2, r, 0.03):
            ring = self.ring_mask(cx, cy, rr - 0.005, rr + 0.005)
            self.col[ring] = body * 0.8
            self.h[ring] = 0.27
        for k in range(8):
            yy = y + 0.08 + k * (hgt - 0.16) / 7
            self.rect(x + w * 0.78, yy, x + w * 0.95, yy + 0.02, body * 0.6, 0.26)
        self.rect(x + w * 0.8, y - 0.9, x + w * 0.86, y, srgb((228, 226, 216)), 0.08, 0.5)
        self.sources.append((x + w * 0.5, y, self.rng.uniform(0.6, 1.6), 0.1, MOLD))

    def finish(self, mold=0.35, top_mold=0.3, cracks=4, peel=0.0, rust=1.0):
        """古さを重ねる（雨だれ・上の縁の黒カビ・ひび・剥がれ・全体のくすみ）"""
        if peel > 0:
            self.peel(peel)
        for x, y, ln, a, color in self.sources:
            if color is RUST:
                self.streak(x, y, ln, 0.05, min(0.6, a * 2.0) * rust, RUST)
            else:
                self.streak(x, y, ln, self.rng.uniform(0.04, 0.12), a * mold * 2.0, color)
        for _ in range(int(14 * top_mold / 0.3)):
            self.streak(self.rng.uniform(0, self.wm), self.hm, self.rng.uniform(0.3, 1.6),
                        self.rng.uniform(0.05, 0.3), self.rng.uniform(0.1, 0.4) * top_mold * 2.0)
        for _ in range(cracks):
            self.crack(self.rng.uniform(0, self.wm), self.rng.uniform(0.2, self.hm - 0.2), self.rng.uniform(0.2, 1.0),
                       repaired=self.rng.random() < 0.4)
        g = self.fbm(1.8, 3) * 0.18 + self.fbm(0.3, 3) * 0.06
        self.shade *= 1.0 + g
        n = int(self.H * 0.14)
        prof = np.linspace(1, 0, n)[:, None] ** 2
        self.shade[:n] *= 1.0 - top_mold * 0.5 * prof * (0.5 + (self.noise(0.7)[:n] + 0.5))
        # 壁の細かな凹凸（吹き付けの粒・コンクリートの気泡）
        self.h += self.fbm(0.02, 3) * 0.0015
        self.rough = np.clip(self.rough + self.fbm(0.5, 3) * 0.08, 0.03, 1.0)

    def maps(self, strength=1.0, ao_scale=7.0):
        """BaseColor・Normal・ORM の配列（uint8）"""
        h = self.h
        hb = (h * 2 + np.roll(h, 1, 0) + np.roll(h, -1, 0) + np.roll(h, 1, 1) + np.roll(h, -1, 1)) / 6.0
        dx = (np.roll(hb, -1, 1) - np.roll(hb, 1, 1)) / (2 * self.pm)
        dy = (np.roll(hb, 1, 0) - np.roll(hb, -1, 0)) / (2 * self.pm)
        n = np.stack([-dx * strength, -dy * strength, np.ones_like(dx)], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        big = hb.copy()
        for r in (2, 6, 18):
            acc = np.zeros_like(big)
            cnt = 0
            for sh in range(-r, r + 1, max(1, r // 2)):
                acc += np.roll(big, sh, 0) + np.roll(big, sh, 1)
                cnt += 2
            big = acc / cnt
        ao = np.clip(1.0 - np.maximum(big - hb, 0.0) * ao_scale, 0.35, 1.0) * self.occ
        col = np.clip(self.col * self.shade[..., None], 0, 1)
        color = (col * 255 + 0.5).astype(np.uint8)
        normal = ((n * 0.5 + 0.5) * 255 + 0.5).astype(np.uint8)
        orm = np.stack([ao, np.clip(self.rough, 0, 1), np.clip(self.metal, 0, 1)], -1)
        return color, normal, (np.clip(orm, 0, 1) * 255 + 0.5).astype(np.uint8)


def wall_base(c, color, mottle=1.0):
    """塗装した壁（塗りのむら・吹き付けのむら）"""
    c.col[:] = color
    v = c.fbm(2.2, 3) * 0.08 + c.fbm(0.4, 3) * 0.04 + c.noise(0.03) * 0.015
    c.col *= (1.0 + v * mottle)[..., None]


# ---- 外壁 ----

def house(c, wall):
    """沖縄のコンクリートの戸建て（窓の庇・面格子・花ブロック・室外機・黒カビ）"""
    wall_base(c, wall)
    c.rect(0, 0, TILE_W, 0.02, wall * 0.75, -0.003)
    frame = [ALU, ALU, ALU_BRONZE][c.rng.integers(3)]
    flower = c.rng.random() < 0.55
    wins = [(0.8, 1.7, 0.95, 1.1, 1, True), (4.0, 0.75, 1.35, 0.75, 0, False), (6.4, 1.8, 0.12, 2.0, 1, False),
            (9.6, 1.7, 0.95, 1.1, 1, c.rng.random() < 0.5)]
    for i, (x, w, sill, h, mull, grille) in enumerate(wins):
        if flower and i == 1:
            continue
        c.window(x, sill, x + w, sill + h, mull, frame=frame, wall=wall, grille=grille, sill=i != 2)
    if flower:
        x0, y0 = 3.75, 0.85
        for i in range(7):
            for j in range(9):
                bx, by = x0 + i * 0.2, y0 + j * 0.2
                c.rect(bx, by, bx + 0.2, by + 0.2, wall * 0.98, 0.04)
                c.rect(bx + 0.04, by + 0.04, bx + 0.16, by + 0.16, srgb((30, 30, 30)), -0.12)
                c.occ[c.sl(bx + 0.04, by + 0.04, bx + 0.16, by + 0.16)] *= 0.4
                c.rect(bx + 0.09, by + 0.02, bx + 0.11, by + 0.18, wall * 0.96, 0.04)
                c.rect(bx + 0.02, by + 0.09, bx + 0.18, by + 0.11, wall * 0.96, 0.04)
        c.sources.append((x0 + 0.2, y0, 0.8, 0.3, MOLD))
        c.sources.append((x0 + 1.2, y0, 0.8, 0.3, MOLD))
    if c.rng.random() < 0.7:
        c.ac_unit(2.85, 1.15)
    c.finish(mold=0.45, top_mold=0.4, cracks=5, peel=c.rng.uniform(0.0, 0.08))


def apartment(c, wall, accent, glass_rail=False):
    """ベランダ側のマンション（腰壁・水切り・仕切り板・掃き出し窓・室外機）"""
    wall_base(c, wall * 0.84, 0.6)
    c.h[:] = -1.2
    for unit in (0.0, 6.0):
        frame = ALU if c.rng.random() < 0.7 else ALU_BRONZE
        c.window(unit + 0.9, 0.15, unit + 3.6, 2.2, 1, -1.25, frame, hood=False, sill=False)
        c.window(unit + 3.9, 0.95, unit + 5.4, 2.2, 1, -1.25, frame, hood=False, sill=False)
        if c.rng.random() < 0.8:
            c.ac_unit(unit + 4.0, 0.15, 0.8, 0.6)
    s = c.sl(0, 1.6, TILE_W, 2.74)
    k = s[0].stop - s[0].start
    g = np.linspace(1.0, 0.0, k)[:, None]
    c.shade[s] *= 1.0 - 0.32 * g ** 1.3
    c.occ[s] *= 1.0 - 0.5 * g ** 1.3
    if glass_rail:
        s = c.sl(0, 0.05, TILE_W, 1.1)
        c.col[s] = c.col[s] * 0.5 + srgb((150, 172, 176)) * 0.5
        c.rough[s] = 0.08
        c.rect(0, 1.03, TILE_W, 1.1, ALU, 0.02, 0.35, 0.85)
        for x in np.arange(0.0, TILE_W, 1.5):
            c.rect(x - 0.02, 0.05, x + 0.02, 1.1, ALU, 0.02, 0.35, 0.85)
    else:
        par = accent * 1.0
        c.rect(0, 0.0, TILE_W, 1.1, par, 0.0)
        c.col[c.sl(0, 0, TILE_W, 1.1)] *= (1.0 + c.fbm(0.8, 3)[c.sl(0, 0, TILE_W, 1.1)] * 0.06)[..., None]
        c.rect(0, 1.02, TILE_W, 1.1, ALU * 0.95, 0.04, 0.35, 0.8)
        for x in np.arange(0.3, TILE_W, 0.75):
            c.sources.append((x + c.rng.uniform(-0.2, 0.2), 1.02, c.rng.uniform(0.3, 1.0), c.rng.uniform(0.2, 0.45),
                              MOLD))
        for x in (1.0, 7.0):
            c.rect(x, 0.04, x + 0.06, 0.1, srgb((40, 40, 40)), -0.03)
            c.sources.append((x + 0.03, 0.04, 0.9, 0.5, MOLD))
    c.rect(0, 2.74, TILE_W, 3.0, wall, 0.0)
    c.rect(0, 2.74, TILE_W, 2.77, wall * 0.82, 0.0)
    for x0, x1 in ((0.0, 0.08), (11.92, 12.0), (5.92, 6.08)):
        c.rect(x0, 1.1, x1, 2.74, srgb((238, 236, 228)), -0.2, 0.6)
    for x in np.arange(0.4, TILE_W, 1.2):
        c.sources.append((x + c.rng.uniform(-0.3, 0.3), 2.74, c.rng.uniform(0.2, 0.7), 0.3, MOLD))
    c.finish(mold=0.4, top_mold=0.25, cracks=3, peel=0.0)


def tower(c, wall):
    """ホテル・高層マンション（白いスラブの縁・ガラスの手すり・4 m ごとの仕切り）"""
    wall_base(c, wall * 0.82, 0.5)
    c.h[:] = -1.5
    for x in (0.0, 4.0, 8.0):
        c.window(x + 0.35, 0.1, x + 3.65, 2.55, 2, -1.55, ALU, hood=False, sill=False)
    s = c.sl(0, 1.8, TILE_W, 2.82)
    k = s[0].stop - s[0].start
    g = np.linspace(1.0, 0.0, k)[:, None]
    c.shade[s] *= 1.0 - 0.3 * g ** 1.3
    c.occ[s] *= 1.0 - 0.45 * g ** 1.3
    s = c.sl(0, 0.05, TILE_W, 1.15)
    c.col[s] = c.col[s] * 0.45 + srgb((156, 184, 190)) * 0.55
    c.rough[s] = 0.06
    c.rect(0, 1.1, TILE_W, 1.15, ALU, 0.0, 0.3, 0.85)
    c.rect(0, 2.82, TILE_W, 3.1, wall, 0.0)
    for x in (0.0, 4.0, 8.0):
        c.rect(x, 1.15, x + 0.16, 2.82, wall * 0.95, -0.05)
    for x in np.arange(0.5, TILE_W, 2.0):
        c.sources.append((x, 2.82, c.rng.uniform(0.2, 0.5), 0.2, MOLD))
    c.finish(mold=0.25, top_mold=0.12, cracks=1)


def office(c, wall):
    """学校・役所・事務所（連続した窓と腰壁、窓の上の庇）"""
    wall_base(c, wall)
    c.glass(0, 0.95, TILE_W, 2.85, -0.1)
    for k in range(9):
        x = k * 1.5
        c.rect(x - 0.035, 0.95, x + 0.035, 2.85, ALU, -0.06, 0.35, 0.85)
    c.rect(0, 2.2, TILE_W, 2.24, ALU, -0.06, 0.35, 0.85)
    c.rect(0, 0.9, TILE_W, 0.95, ALU * 0.9, 0.02, 0.4, 0.8)
    c.rect(0, 2.95, TILE_W, 3.07, wall * 1.03, 0.45)
    s = c.sl(0, 2.3, TILE_W, 2.95)
    k = s[0].stop - s[0].start
    c.shade[s] *= 1.0 - 0.28 * np.linspace(0.0, 1.0, k)[:, None] ** 1.5
    c.occ[s] *= 1.0 - 0.4 * np.linspace(0.0, 1.0, k)[:, None] ** 1.5
    for x in np.arange(0.2, TILE_W, 0.6):
        c.sources.append((x + c.rng.uniform(-0.1, 0.1), 0.9, c.rng.uniform(0.3, 0.8), 0.35, MOLD))
        c.sources.append((x + c.rng.uniform(-0.2, 0.2), 2.95, c.rng.uniform(0.2, 0.6), 0.3, MOLD))
    c.finish(mold=0.4, top_mold=0.3, cracks=4)


def shopfront(c, wall):
    """1 階の店（ガラスの店先・錆びたシャッター・色のついた看板）"""
    wall_base(c, wall)
    for b in range(3):
        x = b * 4.0
        kind = c.rng.random()
        if kind < 0.55:
            c.window(x + 0.2, 0.05, x + 3.8, 2.75, 2, -0.15, ALU_BLACK if c.rng.random() < 0.4 else ALU, hood=False,
                     sill=False)
        elif kind < 0.85:
            s = c.sl(x + 0.2, 0.05, x + 3.8, 2.75)
            rows = np.arange(s[0].stop - s[0].start)
            rib = 0.5 + 0.5 * np.sin(rows / max(1.0, 0.05 / c.pm) * math.pi)
            base_col = srgb((166, 168, 166)) * (0.86 + 0.14 * rib)[:, None, None]
            c.col[s] = base_col
            c.h[s] = (-0.06 + 0.012 * rib)[:, None]
            c.rough[s] = 0.5
            c.metal[s] = 0.6
            for k in range(6):
                c.sources.append((x + c.rng.uniform(0.3, 3.6), 2.75, c.rng.uniform(0.6, 2.2), 0.3, RUST))
        else:
            c.window(x + 0.2, 0.05, x + 1.8, 2.4, 0, -0.15, hood=False, sill=False)
            c.window(x + 2.2, 1.0, x + 3.5, 2.3, 1, wall=wall)
        sign = srgb(SIGN_COLORS[c.rng.integers(len(SIGN_COLORS))])
        c.rect(x + 0.1, 2.95, x + 3.9, 3.75, sign, 0.08, 0.45)
        light = np.clip(1.0 - sign + 0.12, 0, 1) if sign.mean() > 0.5 else np.clip(sign * 0.15 + 0.85, 0, 1)
        for k in range(c.rng.integers(3, 7)):
            lx = x + 0.4 + k * 0.5
            if lx > x + 3.5:
                break
            c.rect(lx, 3.12, lx + 0.36, 3.58, light, 0.09, 0.45)
        c.sources.append((x + 0.3, 2.95, 1.0, 0.35, MOLD))
        c.sources.append((x + 3.6, 2.95, 1.0, 0.35, MOLD))
    s = c.sl(0, 2.3, TILE_W, 2.95)
    k = s[0].stop - s[0].start
    c.shade[s] *= 1.0 - 0.25 * np.linspace(0.0, 1.0, k)[:, None] ** 1.5
    c.occ[s] *= 1.0 - 0.4 * np.linspace(0.0, 1.0, k)[:, None] ** 1.5
    c.shade[c.sl(0, 0, TILE_W, 0.3)] *= 0.82
    c.finish(mold=0.35, top_mold=0.2, cracks=3)


def bigbox(c, wall, accent):
    """大きな店・倉庫（縦のリブの金属の壁と色の帯、留め具の錆）"""
    wall_base(c, wall, 0.5)
    xs = np.arange(c.W)
    rib = np.maximum(0, np.cos(xs / c.W * TILE_W / 0.3 * 2 * math.pi)) ** 3
    c.h[:] = rib[None, :] * 0.025
    c.col *= (0.93 + 0.07 * rib)[None, :, None]
    c.rough[:] = 0.5
    c.metal[:] = 0.35
    c.rect(0, 3.6, TILE_W, 4.2, accent, 0.03, 0.45)
    if c.rng.random() < 0.5:
        c.rect(5.0, 0.0, 6.8, 2.4, srgb((118, 122, 126)), -0.05, 0.4, 0.7)
    for x in np.arange(0.15, TILE_W, 0.6):
        for y in (1.2, 2.4, 3.6, 4.8):
            if c.rng.random() < 0.35:
                c.sources.append((x, y, c.rng.uniform(0.2, 0.9), 0.3, RUST))
    c.finish(mold=0.3, top_mold=0.35, cracks=0, rust=0.8)


def village(c):
    """アメリカンビレッジの色壁（間口ごとに違う色・白い窓枠と鎧戸・看板）"""
    for b in range(4):
        x = b * 3.0
        col = srgb(VILLAGE_COLORS[c.rng.integers(len(VILLAGE_COLORS))])
        s = c.sl(x, 0, x + 3.0, c.hm)
        c.col[s] = col
        c.col[s] *= (1.0 + c.fbm(0.8, 3)[s] * 0.06)[..., None]
        c.window(x + 0.8, 0.9, x + 2.2, 2.6, 1, -0.1, WHITE_TRIM, hood=False)
        c.rect(x + 0.55, 0.9, x + 0.78, 2.6, col * 0.65, 0.03, 0.6)
        c.rect(x + 2.22, 0.9, x + 2.45, 2.6, col * 0.65, 0.03, 0.6)
        for yy in np.arange(0.95, 2.6, 0.09):
            c.rect(x + 0.55, yy, x + 0.78, yy + 0.02, col * 0.5, 0.02)
            c.rect(x + 2.22, yy, x + 2.45, yy + 0.02, col * 0.5, 0.02)
        c.rect(x + 0.7, 2.6, x + 2.3, 2.75, WHITE_TRIM, 0.05)
        if c.rng.random() < 0.35:
            sign = srgb(SIGN_COLORS[c.rng.integers(len(SIGN_COLORS))])
            c.rect(x + 0.4, 2.95, x + 2.6, 3.45, sign, 0.06, 0.45)
    c.rect(0, 0, TILE_W, 0.12, WHITE_TRIM, 0.02)
    c.rect(0, c.hm - 0.12, TILE_W, c.hm, WHITE_TRIM, 0.02)
    c.finish(mold=0.2, top_mold=0.12, cracks=1)


def base(c, wall):
    """米軍施設・住宅（ベージュのスタッコ・白い窓枠・窓の冷房機）"""
    wall_base(c, wall, 1.2)
    c.h += c.fbm(0.03, 3) * 0.003
    for k in range(4):
        x = k * 3.0 + 0.7
        c.window(x, 1.0, x + 1.6, 2.4, 1, -0.08, WHITE_TRIM, hood=False)
        c.rect(x - 0.05, 0.9, x + 1.65, 1.0, WHITE_TRIM * 0.95, 0.04)
        if c.rng.random() < 0.3:
            c.rect(x + 0.2, 1.0, x + 0.9, 1.45, srgb((200, 198, 190)), 0.2, 0.6)
            c.sources.append((x + 0.5, 1.0, 0.8, 0.4, RUST))
    c.finish(mold=0.25, top_mold=0.2, cracks=2)


# 外壁の種類: 名前 → (1 階の高さ m, 描く関数, 色の候補, 屋上の手すり壁に使う v の範囲)
STYLES = {
    "House": (3.0, "house", [("White", (226, 224, 216)), ("Cream", (228, 216, 190)), ("Pink", (226, 206, 196)),
                             ("Mint", (206, 218, 206)), ("Gray", (198, 199, 196))], (0.0, 0.25)),
    "Apt": (3.0, "apartment", [("White", (226, 225, 220)), ("Beige", (216, 204, 182)), ("Gray", (196, 197, 194)),
                               ("Brown", (220, 212, 198))], (0.0, 0.3)),
    "AptGlass": (3.0, "apartment_glass", [("White", (232, 232, 228)), ("Beige", (220, 210, 190))], (0.92, 1.0)),
    "Tower": (3.1, "tower", [("White", (236, 236, 232)), ("Beige", (222, 212, 192))], (0.92, 1.0)),
    "Office": (3.6, "office", [("White", (222, 222, 218)), ("Gray", (186, 188, 186))], (0.0, 0.22)),
    "Shop": (4.0, "shopfront", [("A", (210, 206, 196)), ("B", (190, 188, 184))], (0.95, 1.0)),
    "BigBox": (5.0, "bigbox", [("Gray", (192, 194, 192)), ("Ivory", (222, 218, 204))], (0.86, 0.98)),
    "Village": (3.6, "village", [("A", None), ("B", None)], (0.05, 0.22)),
    "Base": (3.2, "base", [("Beige", (208, 194, 164)), ("Cream", (222, 212, 188))], (0.0, 0.28)),
}
ACCENTS = {"Brown": (146, 110, 80), "Gray": (60, 88, 136), "Ivory": (40, 116, 70)}


def paint_facade(style, palette, seed):
    floor_h, fn, palettes, _ = STYLES[style]
    rng = np.random.default_rng(seed)
    c = Canvas(TILE_W, floor_h, PX_PER_M, rng)
    wall = None
    for name, col in palettes:
        if name == palette:
            wall = srgb(col) if col else None
    accent = srgb(ACCENTS[palette]) if palette in ACCENTS else (wall * 1.02 if wall is not None else None)
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
        bigbox(c, wall, accent if palette in ACCENTS else srgb((176, 58, 48)))
    elif fn == "village":
        village(c)
    elif fn == "base":
        base(c, wall)
    return c.maps()


# ---- 屋上 ----

def paint_roof(kind, seed):
    """屋上（押さえコンクリートと目地・防水塗装の剥がれ・水たまりの跡・黒カビ／金属の折板屋根と錆）"""
    rng = np.random.default_rng(seed)
    c = Canvas(ROOF_TILE, ROOF_TILE, 1024 / ROOF_TILE, rng)
    if kind in ("Concrete", "Coated"):
        base_col = srgb((160, 158, 150)) if kind == "Concrete" else srgb((150, 162, 150))
        c.col[:] = base_col
        c.col *= (1.0 + c.fbm(3.0, 4) * 0.12 + c.noise(0.05) * 0.04)[..., None]
        # 伸縮目地（3 m 角）
        for k in range(int(ROOF_TILE / 3.2)):
            v = k * 3.2
            c.rect(v - 0.02, 0, v + 0.02, ROOF_TILE, srgb((40, 40, 40)), -0.004, 0.7)
            c.rect(0, v - 0.02, ROOF_TILE, v + 0.02, srgb((40, 40, 40)), -0.004, 0.7)
        if kind == "Coated":
            worn = np.clip((c.fbm(1.5, 4) - 0.1) / 0.05, 0, 1)
            c.col = c.col * (1 - worn[..., None]) + srgb((158, 156, 148)) * worn[..., None]
            c.rough = 0.7 + 0.2 * worn
        else:
            c.rough[:] = 0.92
        mold = np.clip((c.fbm(2.0, 5) + 0.05) / 0.25, 0, 1) ** 2
        c.col = c.col * (1 - 0.45 * mold[..., None]) + MOLD * 0.45 * mold[..., None]
        puddle = np.clip((c.fbm(4.0, 3) - 0.18) / 0.04, 0, 1)
        c.col *= (1.0 - 0.18 * puddle)[..., None]
        for _ in range(30):
            x, y = rng.uniform(0, ROOF_TILE, 2)
            c.crack(x, y, rng.uniform(0.3, 1.5), repaired=rng.random() < 0.3)
        c.h += c.fbm(0.03, 3) * 0.002
    else:
        c.col[:] = srgb((178, 180, 180))
        c.col *= (1.0 + c.fbm(4.0, 3) * 0.08)[..., None]
        xs = np.arange(c.W)
        rib = np.maximum(0, np.cos(xs / c.W * ROOF_TILE / 0.6 * 2 * math.pi)) ** 6
        c.h[:] = rib[None, :] * 0.05
        c.col *= (0.9 + 0.1 * rib)[None, :, None]
        c.rough[:] = 0.5
        c.metal[:] = 0.4
        rust = np.clip((c.fbm(1.2, 5) - 0.12) / 0.08, 0, 1)
        c.col = c.col * (1 - 0.6 * rust[..., None]) + RUST * 0.6 * rust[..., None]
        c.rough = np.maximum(c.rough, 0.85 * rust)
    return c.maps(ao_scale=4.0)


def save_maps(out_dir, prefix, maps):
    color, normal, orm = maps
    paths = {}
    for key, arr in (("BaseColor", color), ("Normal", normal), ("ORM", orm)):
        p = os.path.join(out_dir, f"{prefix}_{key}.png")
        Image.fromarray(arr, "RGB").save(p, optimize=True)
        paths[key] = p
    return paths


# ---- 護岸 ----

SEAWALL_W = 8.0      # 横の周期 [m]
SEAWALL_Z0 = -3.0    # 絵の下端の高さ [m]（絵の縦は SEAWALL_W と同じ 8 m）


def paint_seawall(seed):
    """海岸の護岸のコンクリート（縦は高さ SEAWALL_Z0..+5 m に固定）

    上から、雨だれの黒い筋・塩の白い筋・波しぶきで濡れて黒ずむ帯・潮間帯のフジツボの群れ・水の下の藻。
    """
    rng = np.random.default_rng(seed)
    c = Canvas(SEAWALL_W, SEAWALL_W, 2048 / SEAWALL_W, rng)
    c.col[:] = srgb((170, 166, 156))
    c.col *= (1.0 + c.fbm(2.0, 4) * 0.14 + c.noise(0.04) * 0.05)[..., None]
    z = SEAWALL_Z0 + (1.0 - (np.arange(c.H) + 0.5) / c.H) * SEAWALL_W
    zc = z[:, None]
    # 目地（4 m ごとの伸縮目地と型枠の段、型枠の穴）
    for x in (0.0, 4.0):
        c.rect(x - 0.015, 0, x + 0.015, SEAWALL_W, srgb((70, 68, 64)), -0.02, 0.95)
    for k in range(int(SEAWALL_W / 0.6) + 1):
        y = k * 0.6
        c.rect(0, y, SEAWALL_W, y + 0.008, None, add_h=-0.003)
        for x in np.arange(0.3, SEAWALL_W, 0.6):
            c.rect(x - 0.012, y + 0.3 - 0.012, x + 0.012, y + 0.3 + 0.012, srgb((90, 88, 84)), -0.01)
    # 気泡（あばた）
    pits = (c.noise(0.012) > 0.42) & (c.noise(0.3) > -0.1)
    c.h[pits] -= 0.004
    c.col[pits] *= 0.75
    for _ in range(40):
        c.streak(rng.uniform(0, SEAWALL_W), SEAWALL_W, rng.uniform(1.0, 4.5), rng.uniform(0.04, 0.3),
                 rng.uniform(0.15, 0.45))
    salt = np.clip(c.fbm(0.5, 4) * 2.4, 0, 1) * np.clip((zc - 0.6) / 1.2, 0, 1) * np.clip((3.5 - zc) / 1.5, 0, 1)
    c.col = c.col + (srgb((226, 224, 216)) - c.col) * (salt * 0.45)[..., None]
    wet = np.clip((1.3 - zc) / 1.0, 0, 1) * (0.7 + 0.3 * (c.fbm(0.6, 3) + 0.5))
    c.col *= (1.0 - 0.5 * wet)[..., None]
    c.rough = 0.9 - 0.62 * wet
    algae_m = np.clip((0.35 - zc) / 0.5, 0, 1)
    algae = srgb((48, 60, 36)) * (0.7 + 0.6 * (c.fbm(0.25, 4) + 0.5))[..., None]
    algae = algae * (1 - 0.3 * np.clip((zc + 1.5) / 1.0, 0, 1))[..., None] + srgb((70, 62, 44)) * 0.3 * np.clip(
        (zc + 1.5) / 1.0, 0, 1)[..., None]
    c.col = c.col + (algae - c.col) * (algae_m * 0.9)[..., None]
    c.rough = np.minimum(c.rough, 0.9 - 0.6 * algae_m)
    c.h += algae_m * c.fbm(0.04, 3) * 0.006
    # フジツボ（群れになって付く。大小、白い殻と暗い口）
    dens = np.exp(-((zc - 0.15) / 0.45) ** 2) * np.clip(c.fbm(0.35, 4) + 0.35, 0, 1)
    ys = (1.0 - (np.arange(c.H) + 0.5) / c.H) * SEAWALL_W
    xs = (np.arange(c.W) + 0.5) / c.W * SEAWALL_W
    n = 26000
    px = rng.uniform(0, SEAWALL_W, n)
    pz = rng.normal(0.15, 0.4, n)
    keep = (pz > -0.8) & (pz < 1.0)
    px, pz = px[keep], pz[keep]
    ci = np.clip((px / SEAWALL_W * c.W).astype(int), 0, c.W - 1)
    ri = np.clip(((1.0 - (pz - SEAWALL_Z0) / SEAWALL_W) * c.H).astype(int), 0, c.H - 1)
    accept = rng.random(len(px)) < dens[ri, ci]
    for x, zz in zip(px[accept], pz[accept]):
        r = rng.uniform(0.006, 0.02)
        y = zz - SEAWALL_Z0
        s = c.sl(x - r * 1.6, y - r * 1.6, x + r * 1.6, y + r * 1.6)
        if s[0].stop <= s[0].start or s[1].stop <= s[1].start:
            continue
        yy = ys[s[0]][:, None]
        xx = xs[s[1]][None, :]
        d = np.hypot(xx - x, yy - y) / r
        cone = np.clip(1.0 - d, 0, 1)
        shell = srgb((206, 200, 186)) * rng.uniform(0.7, 1.0)
        m = (d < 1.0)[..., None]
        c.col[s] = np.where(m, shell * (0.75 + 0.25 * cone[..., None]), c.col[s])
        c.col[s] = np.where((d < 0.3)[..., None], srgb((60, 56, 50)), c.col[s])
        c.h[s] = np.maximum(c.h[s], cone * r * 0.9)
        c.rough[s] = np.where(d < 1.0, 0.85, c.rough[s])
        c.occ[s] *= np.where((d > 1.0) & (d < 1.5), 0.75, 1.0)
    return c.maps(ao_scale=12.0)
