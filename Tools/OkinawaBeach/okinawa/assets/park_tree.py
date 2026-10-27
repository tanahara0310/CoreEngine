"""安良波公園の木 7 バリエーション（公園の植栽から）

  Gajumaru     ガジュマル: 数本の幹が寄り合い、枝から気根が垂れる。濃い緑の小さな葉のこんもりした大きな樹冠
  Kobateishi_A コバテイシ: まっすぐな幹から枝が段ごとに水平に出る。枝先に大きな倒卵形の葉が輪に付き、紅葉が混じる
  Kobateishi_B コバテイシの若い木（段が少なく小さい）
  Fukugi       フクギ: まっすぐな幹に短い枝が密に付く細長い樹冠。厚く光沢のある長楕円形の葉が対に付く
  Terihaboku   テリハボク: 短い幹から太い枝が斜めに伸びる丸く密な樹冠。光沢のある楕円形の葉に細い平行脈
  Hououboku    ホウオウボク: 低い位置から枝が横へ広がる傘形の樹冠。細かい羽状の葉と、樹冠の上の朱色の花の房
  Sotetsu      ソテツ: 短い幹の先に硬い羽状葉のロゼット（2 株立ち）

幹・枝: 樹種ごとの決まりで枝分かれするチューブ（BarkPacker で 1 枚に詰めた樹皮テクスチャ）
葉: 小枝ごと描いた「葉の房」アトラス（2x2。hibiscus.py の Canvas で描く）を V 字に少し折ったカードにして、
    枝先に置いた葉の塊（楕円体）の外殻に散らす。右上のセルは同じ房を暗く描いたもので、塊の奥と下側に使う。
    ソテツの葉はヤシ（palm.py）と同じ作りの葉カード
揺れ: 株全体は高さの 2 乗で曲がり、葉カードは付け根から先へしなる（okinawa/anim.py）
"""
import math
import os
import random

import numpy as np
from mathutils import Vector

from .. import anim
from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from .adan import BarkPacker, MeshAcc, perp, register_cutout_aliases, rotate_toward
from .hibiscus import Canvas, _bez2, _card, _fit, _lin, _sstep, image_material
from .palm import _frond

PREVIEW = dict(cam_dir=(0.3, -1.0, 0.18), lens=80, spacing=1.0)

# エンジンの頂点アニメーションの種類（glTF のマテリアルの extras に書く。置くだけで風に揺れる）
VERTEX_ANIMATION = "plant"

UP = Vector((0, 0, 1))
RES_SCALE = float(os.environ.get("OKI_RES_SCALE", "1"))
# 葉の粗さの下限
LEAF_ROUGH_MIN = 0.6
CELLS = [(0.0, 0.0), (0.5, 0.0), (0.0, 0.5), (0.5, 0.5)]
DARK = (0.5, 0.5)
_MATS = {}


def _mat(key, fn):
    """同じマテリアル（アトラス描画）をバリエーション間で使い回す"""
    if key not in _MATS:
        _MATS[key] = fn()
    return _MATS[key]


# ---------------------------------------------------------------------------
# 葉の形（u: 付け根 0 → 先 1 の半幅。単位は葉の長さ）
# ---------------------------------------------------------------------------
def ellip_hw(W, a=0.85, b=0.8):
    def f(u):
        return W * np.sin(np.pi * np.clip(u, 0, 1) ** a) ** b
    return f


def oblong_hw(W, p=5.0):
    """両端の丸い長楕円"""
    def f(u):
        x = np.clip(u, 0, 1) * 2 - 1
        return W * np.clip(1 - np.abs(x) ** p, 0, 1) ** 0.5
    return f


def obovate_hw(W):
    """先の方が幅広い倒卵形"""
    def f(u):
        u = np.clip(u, 0, 1)
        return W * np.sin(np.pi * u ** 1.7) ** 0.6
    return f


def petal_hw(W):
    """細い爪から丸く開く花弁"""
    def f(u):
        u = np.clip(u, 0, 1)
        tip = np.where(u > 0.6, np.sqrt(np.clip(1 - ((u - 0.6) / 0.4) ** 2, 0, 1)), 1.0)
        return W * (0.18 + 0.82 * _sstep(0.15, 0.55, u)) * tip
    return f


def leaf_shade(dark, light, vein, W, rnd, rough=0.35, vein_amt=0.3, n_veins=8.0, vein_w=0.05, gloss=0.0,
               lift=1.0):
    """羽状脈の葉。gloss は中肋の片側の明るい帯、lift は明るさの倍率"""
    lit = rnd.uniform(0.05, 0.12) * rnd.choice([-1, 1])
    k = rnd.random()
    base = (dark * (1 - k) + light * k) * lift
    ph0 = rnd.random()

    def f(u, v, hw):
        q = np.clip(np.abs(v) / hw, 0, 1)
        phase = u * n_veins - np.abs(v) / W * 1.3 + ph0
        vn = np.exp(-((np.mod(phase, 1.0) - 0.5) / vein_w) ** 2) * (1 - q) ** 0.6 * _sstep(0.05, 0.15, u)
        mid = np.exp(-(np.abs(v) / (0.012 * (1 - u) + 0.005)) ** 2)
        shade = 1 + lit * np.sign(v) - 0.14 * q ** 3 + 0.05 * (1 - u)
        shine = gloss * np.exp(-((v * np.sign(lit) / hw - 0.4) / 0.28) ** 2) * (1 - q) * _sstep(0.05, 0.3, u)
        col = base * (shade + shine)[..., None]
        col = col * (1 - (vn * vein_amt)[..., None]) + vein * lift * (vn * vein_amt)[..., None]
        col = col * (1 - (mid * 0.55)[..., None]) + vein * lift * (mid * 0.55)[..., None]
        h = np.sqrt(np.clip(1 - q ** 2, 0, 1)) * 0.7 - vn * 0.3 - mid * 0.3
        r = rough + vn * 0.1 - shine * 0.15
        cav = 1 - 0.1 * vn - 0.15 * mid
        return col, h, r, cav
    return f


def _at(line, t):
    i = min(len(line) - 2, int(t * (len(line) - 1)))
    d = line[i + 1] - line[i]
    return line[i], d / np.linalg.norm(d)


def _rot2(d, deg):
    a = math.radians(deg)
    return np.array([d[0] * math.cos(a) - d[1] * math.sin(a), d[0] * math.sin(a) + d[1] * math.cos(a)])


# ---------------------------------------------------------------------------
# 葉の房の描画（セル (x0, y0, cs)。付け根はセル下端中央）
# ---------------------------------------------------------------------------
def draw_branchlets(cv, rnd, cell, sp, lift=1.0):
    """枝分かれした小枝に葉が互生・対生する房（ガジュマル・フクギ・テリハボク）"""
    x0, y0, cs = cell
    lo = np.array([x0, y0]) + cs * 0.06
    hi = np.array([x0, y0]) + cs * 0.94

    def P(x, y):
        return np.array([x0 + x * cs, y0 + y * cs])

    twigs = []  # (点列, 太さ 0, 太さ 1, 葉を付け始める位置)
    bend = rnd.uniform(-0.12, 0.12)
    main = _bez2(P(0.5, 0.0), P(0.5 + bend, 0.45), P(0.5 - bend * 0.5, sp.get("top", 0.88)), 30)
    tr = sp["twig_r"]
    twigs.append((main, tr, tr * 0.45, sp.get("main_from", 0.2)))
    n_side = sp["n_side"]
    for k in range(n_side):
        t = 0.1 + 0.72 * (k + rnd.uniform(0.2, 0.8)) / n_side
        p, d = _at(main, t)
        side = 1 if k % 2 == 0 else -1
        sd = _rot2(d, side * rnd.uniform(28, 55))
        L = cs * (0.95 - t * 0.7) * rnd.uniform(0.5, 0.72)
        e = np.clip(p + sd * L, lo, hi)
        tw = _bez2(p, np.clip(p + sd * L * 0.5 + d * L * 0.12, lo, hi), e, 16)
        twigs.append((tw, tr * 0.7, tr * 0.35, 0.08))
        for _ in range(sp.get("sub", 0)):
            if L < cs * 0.22:
                break
            ts = rnd.uniform(0.3, 0.7)
            q, dq = _at(tw, ts)
            sq = _rot2(dq, rnd.choice([-1, 1]) * rnd.uniform(30, 50))
            L2 = L * (1 - ts) * rnd.uniform(0.6, 0.9)
            e2 = np.clip(q + sq * L2, lo, hi)
            twigs.append((_bez2(q, (q + e2) / 2 + dq * L2 * 0.1, e2, 10), tr * 0.45, tr * 0.3, 0.0))
    tc0, tc1 = (np.asarray(c) * lift for c in sp["twig_col"])
    for pts, r0, r1, _ in twigs:
        cv.stroke(pts, r0 * cs, r1 * cs, tc0, tc1, shadow=0.25)
    leaves = []
    Lf = sp["leaf_len"] * cs
    for pts, r0, r1, t0 in twigs:
        seg = float(np.linalg.norm(np.diff(pts, axis=0), axis=1).sum())
        n = max(2, int(seg * (1 - t0) / (Lf * sp["density"])))
        side = rnd.choice([-1, 1])
        for i in range(n):
            t = t0 + (1 - t0) * (i + rnd.uniform(0.25, 0.75)) / n
            p, d = _at(pts, min(t, 0.99))
            nrm = np.array([-d[1], d[0]])
            sides = (-1, 1) if sp.get("opposite") else (side,)
            side = -side
            for s in sides:
                ang = math.radians(rnd.uniform(*sp.get("angle", (35, 70))) * (1 - 0.3 * t))
                ld = d * math.cos(ang) + nrm * s * math.sin(ang)
                L = Lf * rnd.uniform(0.8, 1.1) * (1 - 0.3 * t ** 2)
                base = p + ld * sp.get("petiole", 0.08) * L
                A = ld * L
                B = np.array([-ld[1], ld[0]]) * L * rnd.uniform(0.6, 1.0)
                A, B = _fit(base, A, B, sp["W"], cell)
                leaves.append((p, base, A, B, t))
    leaves.sort(key=lambda lf: lf[4])
    shade = sp["shade"]
    for p, base, A, B, t in leaves:
        cv.stroke([p, base], 0.004 * cs, 0.003 * cs, tc1, tc1, shadow=0.15)
        cv.blade(base, A, B, sp["hw"], shade(rnd, t, lift))
    fruit = sp.get("fruit")
    if fruit:
        col, n, rad = fruit
        for _ in range(n):
            pts, *_ = rnd.choice(twigs[1:] or twigs)
            p, d = _at(pts, rnd.uniform(0.2, 0.8))
            c = p + np.array([-d[1], d[0]]) * rnd.choice([-1, 1]) * rad * cs * 1.2
            cv.disc(c, rad * cs, _lin(rnd.choice(col)) * lift, h=0.8, rough=0.4, shadow=0.3)


def _twig_tree(rnd, P, lo, hi, n_side, top=0.72, side_len=(0.22, 0.36)):
    """セルの下端中央から伸びる小枝と側枝。戻り値 (枝の点列のリスト, 枝先 [(位置, 向き)])"""
    main = _bez2(P(0.5, 0.0), P(0.5 + rnd.uniform(-0.08, 0.08), top * 0.5), P(0.5 + rnd.uniform(-0.1, 0.1), top), 20)
    branches = [main]
    ends = [(main[-1], (main[-1] - main[-2]) / np.linalg.norm(main[-1] - main[-2]))]
    cs = P(1, 0)[0] - P(0, 0)[0]
    for k in range(n_side):
        t = 0.18 + 0.66 * (k + rnd.uniform(0.2, 0.8)) / n_side
        p, d = _at(main, t)
        s = 1 if k % 2 == 0 else -1
        sd = _rot2(d, s * rnd.uniform(35, 65))
        e = np.clip(p + sd * cs * rnd.uniform(*side_len), lo, hi)
        b = _bez2(p, (p + e) / 2 + d * 0.03 * cs, e, 10)
        branches.append(b)
        ends.append((e, (b[-1] - b[-2]) / max(1e-9, np.linalg.norm(b[-1] - b[-2]))))
    return branches, ends


def draw_rosettes(cv, rnd, cell, sp, lift=1.0, red=0.0):
    """枝先ごとに大きな倒卵形の葉が輪に付く房（コバテイシ）。red は紅葉した葉の割合"""
    x0, y0, cs = cell
    m = sp["leaf_len"] * 0.9 + 0.03
    lo = np.array([x0, y0]) + cs * m
    hi = np.array([x0, y0]) + cs * (1 - m)

    def P(x, y):
        return np.array([x0 + x * cs, y0 + y * cs])

    branches, ends = _twig_tree(rnd, P, lo, hi, sp["n_side"])
    tc0, tc1 = (np.asarray(c) * lift for c in sp["twig_col"])
    for b in branches:
        cv.stroke(b, sp["twig_r"] * cs, sp["twig_r"] * 0.6 * cs, tc0, tc1, shadow=0.3)
    shade = sp["shade"]
    rnd.shuffle(ends)
    for c, _ in ends:
        n = rnd.randint(8, 11)
        tilt = rnd.uniform(0.35, 0.8)
        ta = rnd.uniform(0, math.tau)
        td = np.array([math.cos(ta), math.sin(ta)])
        a0 = rnd.uniform(0, math.tau)
        L0 = cs * sp["leaf_len"] * rnd.uniform(0.8, 1.0)
        items = []
        for k in range(n):
            a = a0 + k * math.tau / n + rnd.uniform(-0.15, 0.15)
            dd = np.array([math.cos(a), math.sin(a)])

            def sq(v, td=td, tilt=tilt):
                return v - td * (v @ td) * (1 - tilt)
            L = L0 * rnd.uniform(0.75, 1.05)
            A = sq(dd * L)
            B = sq(np.array([-dd[1], dd[0]]) * L * rnd.uniform(0.85, 1.0))
            items.append((float(dd @ td), A, B, rnd.random() < red))
        items.sort(key=lambda it: it[0])
        for _, A, B, is_red in items:
            A, B = _fit(c, A, B, sp["W"], cell)
            cv.blade(c + A * 0.04, A, B, sp["hw"], shade(rnd, is_red, lift))


def _compound_leaf(cv, rnd, cell, sp, p0, d0, L, lift, lo, hi):
    """2 回羽状複葉 1 枚（葉軸・羽片・小葉）"""
    cs = cell[2]
    rc0, rc1 = (np.asarray(c) * lift for c in sp["rachis_col"])
    e = np.clip(p0 + d0 * L, lo, hi)
    rach = _bez2(p0, p0 + d0 * L * 0.5 + _rot2(d0, 90) * rnd.uniform(-0.04, 0.04) * cs, e, 16)
    cv.stroke(rach, 0.004 * cs, 0.0015 * cs, rc0, rc1, shadow=0.2)
    leaflet = oblong_hw(0.17, p=3.0)
    n_p = rnd.randint(8, 10)
    for k in range(n_p):
        t = 0.1 + 0.88 * k / (n_p - 1)
        p, d = _at(rach, min(t, 0.99))
        env = math.sin(math.pi * min(1.0, 0.25 + t * 0.85)) ** 0.6
        for s in (-1, 1):
            pd = _rot2(d, s * rnd.uniform(58, 72))
            pl = cs * sp["pinna_len"] * env * rnd.uniform(0.85, 1.05)
            pe = np.clip(p + pd * pl, lo, hi)
            pinna = _bez2(p, p + pd * pl * 0.5 + d * pl * 0.08, pe, 8)
            cv.stroke(pinna, 0.0018 * cs, 0.001 * cs, rc1, rc1, shadow=0.1)
            n_l = 11
            for j in range(n_l):
                tj = 0.06 + 0.9 * j / (n_l - 1)
                q, dq = _at(pinna, min(tj, 0.99))
                for s2 in (-1, 1):
                    ld = _rot2(dq, s2 * rnd.uniform(62, 80))
                    ll = cs * sp["leaflet_len"] * (1 - 0.35 * tj) * rnd.uniform(0.85, 1.1)
                    A = ld * ll
                    B = np.array([-ld[1], ld[0]]) * ll * rnd.uniform(0.7, 1.0)
                    cv.blade(q, A, B, leaflet, sp["shade"](rnd, lift), shadow=0.18)


def draw_bipinnate(cv, rnd, cell, sp, lift=1.0, n_side=5):
    """小枝の先ごとに 2 回羽状複葉が扇に開く房（ホウオウボク）"""
    x0, y0, cs = cell
    lo = np.array([x0, y0]) + cs * 0.04
    hi = np.array([x0, y0]) + cs * 0.96

    def P(x, y):
        return np.array([x0 + x * cs, y0 + y * cs])

    branches, ends = _twig_tree(rnd, P, P(0.2, 0.15), P(0.8, 0.75), n_side, top=0.55, side_len=(0.15, 0.25))
    rc0 = np.asarray(sp["rachis_col"][0]) * lift
    for b in branches:
        cv.stroke(b, 0.007 * cs, 0.004 * cs, _lin("#6a6040") * lift, rc0, shadow=0.25)
    for c, d in ends:
        n = rnd.randint(3, 4)
        for k in range(n):
            ang = (k - (n - 1) / 2) * rnd.uniform(30, 45) + rnd.uniform(-10, 10)
            _compound_leaf(cv, rnd, cell, sp, c, _rot2(d, ang), cs * rnd.uniform(0.28, 0.36), lift, lo, hi)


def draw_flame_flowers(cv, rnd, cell, sp, lift=1.0):
    """ホウオウボクの花の房（朱色の 4 枚と、白黄に赤い筋の旗弁 1 枚）。後ろに羽状の葉"""
    x0, y0, cs = cell
    draw_bipinnate(cv, rnd, cell, sp, lift=lift * 0.95, n_side=3)

    def P(x, y):
        return np.array([x0 + x * cs, y0 + y * cs])

    hw = petal_hw(0.42)
    red = _lin("#d8361a") * lift
    red2 = _lin("#ee5a1e") * lift
    pale = _lin("#f1e2b0") * lift
    for ci in range(3):
        cc = np.array([P(0.3, 0.55), P(0.68, 0.5), P(0.5, 0.75)][ci]) + np.array([rnd.uniform(-0.05, 0.05),
                                                                                  rnd.uniform(-0.05, 0.05)]) * cs
        stem = _bez2(P(0.5, 0.08), (P(0.5, 0.08) + cc) / 2 + np.array([0.0, 0.05]) * cs, cc, 10)
        cv.stroke(stem, 0.005 * cs, 0.003 * cs, _lin("#6a5a3a") * lift, _lin("#7a4a30") * lift, shadow=0.25)
        flowers = []
        for k in range(rnd.randint(7, 10)):
            a = rnd.uniform(0, math.tau)
            r = rnd.uniform(0.0, 0.14) * cs
            flowers.append(np.clip(cc + np.array([math.cos(a), math.sin(a)]) * r, P(0.1, 0.12), P(0.9, 0.9)))
        flowers.sort(key=lambda c: c[1])
        for c in flowers:
            cv.stroke([cc, c], 0.0018 * cs, 0.0012 * cs, _lin("#8a5a30") * lift, _lin("#a04a2a") * lift, shadow=0.1)
            a0 = rnd.uniform(0, math.tau)
            tilt = rnd.uniform(0.55, 1.0)
            td = np.array([math.cos(a0 * 1.3), math.sin(a0 * 1.3)])
            L = cs * sp["petal_len"] * rnd.uniform(0.85, 1.1)
            std = rnd.randrange(5)
            for k in range(5):
                a = a0 + k * math.tau / 5 + rnd.uniform(-0.1, 0.1)
                dd = np.array([math.cos(a), math.sin(a)])

                def sq(v, td=td, tilt=tilt):
                    return v - td * (v @ td) * (1 - tilt)
                A = sq(dd * L * (1.1 if k == std else 1.0))
                B = sq(np.array([-dd[1], dd[0]]) * L)
                if k == std:
                    def shade(u, v, h):
                        streak = np.exp(-((np.mod(v * 9.0 + u * 2.0, 1.0) - 0.5) / 0.12) ** 2) * _sstep(0.2, 0.7, u)
                        col = pale * (1 - streak[..., None] * 0.7) + red * (streak[..., None] * 0.7)
                        return col * (1 + 0.08 * np.sign(v))[..., None], 0.5, 0.5, 1.0
                else:
                    col = red if rnd.random() < 0.5 else red2

                    def shade(u, v, h, col=col):
                        q = np.clip(np.abs(v) / h, 0, 1)
                        edge = _sstep(0.6, 1.0, q) * 0.15
                        cc2 = col * (1 - edge[..., None]) + _lin("#f08a3a") * lift * edge[..., None]
                        return cc2 * (1.04 - 0.1 * q ** 2)[..., None], 0.5, 0.55, 1.0
                cv.blade(c, A, B, hw, shade, shadow=0.3)
            for k in range(rnd.randint(5, 8)):
                a = rnd.uniform(0, math.tau)
                e = c + np.array([math.cos(a), math.sin(a)]) * L * rnd.uniform(0.6, 1.0)
                cv.stroke([c, e], 0.0012 * cs, 0.0008 * cs, _lin("#b0281a") * lift, _lin("#c8401e") * lift,
                          shadow=0.05)
        for _ in range(rnd.randint(2, 4)):
            c = np.clip(cc + np.array([rnd.uniform(-0.12, 0.12), rnd.uniform(-0.1, 0.14)]) * cs, P(0.1, 0.12),
                        P(0.9, 0.9))
            cv.disc(c, 0.008 * cs, _lin("#8a2a18") * lift, h=0.7, rough=0.5, shadow=0.2)


def _canvas():
    return Canvas(max(64, int(2048 * RES_SCALE)))


def _branchlet_atlas(name, sp, seed, card):
    rnd = random.Random(seed)
    cv = _canvas()
    draw_branchlets(cv, rnd, (0.0, 0.0, 0.5), sp)
    draw_branchlets(cv, rnd, (0.5, 0.0, 0.5), sp)
    draw_branchlets(cv, rnd, (0.0, 0.5, 0.5), dict(sp, n_side=max(2, sp["n_side"] - 2)), lift=1.12)
    draw_branchlets(cv, rnd, (0.5, 0.5, 0.5), sp, lift=0.6)
    cv.finish()
    return image_material(name, cv, 2048, (card * 2, card * 2), rough_floor=LEAF_ROUGH_MIN)


# --- 樹種ごとの葉 --------------------------------------------------------------
GAJUMARU_CARD = 1.3
KOBATEISHI_CARD = 1.4
FUKUGI_CARD = 0.9
TERIHA_CARD = 1.15
HOUOU_CARD = 1.5


def gajumaru_leaf():
    dark, light, vein = _lin("#284a1c"), _lin("#4a7430"), _lin("#6c8c48")
    young = _lin("#7fa040")

    def shade(rnd, t, lift):
        lt = light * (1 - _sstep(0.75, 1.0, t)) + young * _sstep(0.75, 1.0, t)
        return leaf_shade(dark, lt, vein, 0.26, rnd, rough=0.3, vein_amt=0.15, n_veins=9.0, gloss=0.14, lift=lift)

    sp = dict(leaf_len=0.07, W=0.26, hw=ellip_hw(0.26, 0.8, 0.7), shade=shade, n_side=10, sub=3, density=0.26,
              angle=(30, 65), twig_r=0.008, twig_col=(_lin("#6d6656"), _lin("#7a8048")), petiole=0.1,
              fruit=(["#d8b84a", "#c86a3a", "#a83a3a"], 14, 0.005))
    return _branchlet_atlas("GajumaruLeaf", sp, 41, GAJUMARU_CARD)


def fukugi_leaf():
    dark, light, vein = _lin("#25461a"), _lin("#46702a"), _lin("#9aa860")
    young = _lin("#9cb040")

    def shade(rnd, t, lift):
        lt = light * (1 - _sstep(0.8, 1.0, t)) + young * _sstep(0.8, 1.0, t)
        return leaf_shade(dark, lt, vein, 0.17, rnd, rough=0.25, vein_amt=0.08, n_veins=14.0, vein_w=0.04,
                          gloss=0.18, lift=lift)

    sp = dict(leaf_len=0.135, W=0.19, hw=oblong_hw(0.19, 4.0), shade=shade, n_side=7, sub=1, density=0.32,
              opposite=True, angle=(40, 62), twig_r=0.009, twig_col=(_lin("#5e5444"), _lin("#6a7040")),
              petiole=0.06)
    return _branchlet_atlas("FukugiLeaf", sp, 43, FUKUGI_CARD)


def terihaboku_leaf():
    dark, light, vein = _lin("#2b5420"), _lin("#55862e"), _lin("#86a85a")

    def shade(rnd, t, lift):
        return leaf_shade(dark, light, vein, 0.25, rnd, rough=0.25, vein_amt=0.22, n_veins=42.0, vein_w=0.09,
                          gloss=0.16, lift=lift)

    sp = dict(leaf_len=0.11, W=0.25, hw=ellip_hw(0.25, 0.95, 0.6), shade=shade, n_side=8, sub=1, density=0.34,
              opposite=True, angle=(45, 70), twig_r=0.009, twig_col=(_lin("#5a4a3a"), _lin("#687040")),
              petiole=0.08)
    return _branchlet_atlas("TerihaLeaf", sp, 47, TERIHA_CARD)


def kobateishi_leaf():
    green_d, green_l, vein = _lin("#36622a"), _lin("#66963a"), _lin("#b0c27a")
    reds = [_lin("#b0301c"), _lin("#c8502a"), _lin("#d8862e"), _lin("#d8b040")]

    def shade(rnd, is_red, lift):
        if is_red:
            c = rnd.choice(reds)
            return leaf_shade(c * 0.8, c, _lin("#e0b070"), 0.27, rnd, rough=0.45, vein_amt=0.25, n_veins=9.0,
                              lift=lift)
        return leaf_shade(green_d, green_l, vein, 0.27, rnd, rough=0.4, vein_amt=0.3, n_veins=9.0, gloss=0.08,
                          lift=lift)

    sp = dict(leaf_len=0.2, W=0.27, hw=obovate_hw(0.27), shade=shade, twig_r=0.009, n_side=7,
              twig_col=(_lin("#6a5a48"), _lin("#76704c")))
    rnd = random.Random(53)
    cv = _canvas()
    draw_rosettes(cv, rnd, (0.0, 0.0, 0.5), sp, red=0.05)
    draw_rosettes(cv, rnd, (0.5, 0.0, 0.5), sp, red=0.1)
    draw_rosettes(cv, rnd, (0.0, 0.5, 0.5), sp, red=0.7)
    draw_rosettes(cv, rnd, (0.5, 0.5, 0.5), sp, lift=0.6, red=0.05)
    cv.finish()
    return image_material("KobateishiLeaf", cv, 2048, (KOBATEISHI_CARD * 2, KOBATEISHI_CARD * 2),
                          rough_floor=LEAF_ROUGH_MIN)


def hououboku_leaf():
    dark, light = _lin("#34602a"), _lin("#62923a")

    def shade(rnd, lift):
        k = rnd.random()
        c = (dark * (1 - k) + light * k) * lift
        lit = rnd.uniform(-0.08, 0.08)

        def f(u, v, hw):
            q = np.clip(np.abs(v) / hw, 0, 1)
            return c * (1 + lit * np.sign(v) - 0.12 * q ** 2)[..., None], np.sqrt(np.clip(1 - q ** 2, 0, 1)) * 0.5, 0.5, 1.0
        return f

    sp = dict(shade=shade, pinna_len=0.095, leaflet_len=0.015, petal_len=0.04,
              rachis_col=(_lin("#7a8a48"), _lin("#8a9a52")))
    rnd = random.Random(59)
    cv = _canvas()
    draw_bipinnate(cv, rnd, (0.0, 0.0, 0.5), sp)
    draw_bipinnate(cv, rnd, (0.5, 0.0, 0.5), sp)
    draw_flame_flowers(cv, rnd, (0.0, 0.5, 0.5), sp)
    draw_bipinnate(cv, rnd, (0.5, 0.5, 0.5), sp, lift=0.6)
    cv.finish()
    return image_material("HououbokuLeaf", cv, 2048, (HOUOU_CARD * 2, HOUOU_CARD * 2),
                          rough_floor=LEAF_ROUGH_MIN)


SOTETSU_W = 0.62
SOTETSU_L = 1.35


def sotetsu_frond_material():
    """ソテツの羽状葉のアトラス（左半分 = 濃い緑の葉 / 右半分 = 古い黄ばんだ葉）。細く硬い羽片が密に並ぶ"""
    def white(nb, w):
        n = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="1D")
        nb.link(w, n.inputs["W"])
        return n.outputs["Value"]

    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        old = nb.math("GREATER_THAN", U, 0.5)
        x = nb.math("FRACT", nb.mul(U, 2.0))
        s = nb.mul(nb.math("ABSOLUTE", nb.sub(x, 0.5)), 2.0)
        env = nb.mul(nb.smooth(V, 0.06, 0.2), nb.maprange(V, 0.7, 1.0, 1.0, 0.35))
        n_leaflets = 85.0
        t = nb.sub(nb.mul(V, n_leaflets), nb.mul(s, 1.4))
        side = nb.math("GREATER_THAN", x, 0.5)
        lid = nb.add(nb.add(nb.math("FLOOR", t), nb.mul(side, 1000.0)), nb.mul(old, 5000.0))
        rnd = white(nb, lid)
        f = nb.math("FRACT", t)
        length = nb.mul(env, nb.maprange(rnd, 0, 1, 0.9, 1.02))
        q = nb.math("DIVIDE", s, nb.math("MAXIMUM", length, 0.001))
        width = nb.mul(0.42, nb.pow(nb.math("MAXIMUM", nb.sub(1.0, nb.pow(q, 6.0)), 0.0), 0.35))
        d = nb.math("ABSOLUTE", nb.sub(f, 0.5))
        leaf = nb.mul(nb.smooth(d, nb.mul(width, 0.5), nb.sub(nb.mul(width, 0.5), 0.04)),
                      nb.math("LESS_THAN", q, 1.0))
        rw = nb.maprange(V, 0.0, 1.0, 0.03, 0.01)
        rachis = nb.mul(nb.smooth(s, rw, nb.mul(rw, 0.6)), nb.math("LESS_THAN", V, 0.985))
        alpha = nb.math("MAXIMUM", leaf, rachis)
        g = nb.mix(srgb("#1f3e18"), srgb("#3a6424"), rnd)
        g = nb.mix(g, srgb("#5a7a34"), nb.mul(nb.smooth(d, 0.05, 0.0), 0.5))
        o = nb.mix(srgb("#7a7a34"), srgb("#a89a48"), rnd)
        o = nb.mix(o, srgb("#6a5a30"), nb.mul(nb.smooth(q, 0.6, 1.0), 0.6))
        col = nb.mix(g, o, old)
        col = nb.mix(col, nb.mix(srgb("#8a8a50"), srgb("#8a7a48"), old), rachis)
        rough = nb.mixf(0.6, 0.78, old)
        height = nb.add(nb.mul(nb.sub(1.0, nb.math("DIVIDE", d, nb.math("MAXIMUM", nb.mul(width, 0.5), 0.001))),
                               leaf), nb.mul(rachis, 1.5))
        return dict(color=col, rough=rough, alpha=alpha, height=height, height_scale=0.003,
                    cavity=nb.maprange(nb.smooth(d, 0.0, 0.2), 0, 1, 0.85, 1.0))

    return pbr_material("SotetsuFrond", fn, res=1024, uv="atlas", double_sided=True,
                        atlas_size=(SOTETSU_W * 2, SOTETSU_L))


# ---------------------------------------------------------------------------
# 樹皮
# ---------------------------------------------------------------------------
def bark_material(name, dark, base, light, crack="#3a3028", fiss=0.5, fiss_scale=(7.0, 0.9), lent=0.0,
                  lichen=0.3, rough=0.85, res=2048, scales=False):
    """樹皮。fiss = 縦の割れ目の強さ、lent = 横長の皮目、lichen = 白っぽい地衣類、scales = ソテツの葉の付け根の鱗"""
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        a = nb.mul(nb.math("FRACT", U), math.tau)
        R = 0.22
        cyl = nb.comb(nb.mul(nb.math("COSINE", a), R), nb.mul(nb.math("SINE", a), R), V)
        big = nb.noise(cyl, 2.0, 4, 0.6)
        col = nb.ramp(big, [(0.3, srgb(dark)), (0.55, srgb(base)), (0.8, srgb(light))])
        f = nb.noise(nb.mapping(cyl, scale=(fiss_scale[0], fiss_scale[0], fiss_scale[1])), 4.0, 5, 0.6)
        cr = nb.smooth(nb.math("ABSOLUTE", nb.sub(f, 0.5)), 0.06, 0.0)
        col = nb.mix(col, srgb(crack), nb.mul(cr, fiss))
        height = nb.add(nb.mul(cr, -fiss), nb.mul(big, 0.3))
        if lent:
            le = nb.smooth(nb.voronoi(nb.mapping(cyl, scale=(1.0, 1.0, 3.5)), 26.0), 0.12, 0.0)
            col = nb.mix(col, srgb(light), nb.mul(le, lent))
            height = nb.add(height, nb.mul(le, lent * 0.4))
        if scales:
            gu = nb.mul(nb.math("FRACT", U), 14.0)
            gv = nb.mul(V, 9.0)
            d1 = nb.math("FRACT", nb.add(gu, gv))
            d2 = nb.math("FRACT", nb.sub(gu, gv))
            dia = nb.math("MINIMUM", nb.math("ABSOLUTE", nb.sub(d1, 0.5)), nb.math("ABSOLUTE", nb.sub(d2, 0.5)))
            edge = nb.smooth(dia, 0.08, 0.0)
            col = nb.mix(col, srgb(crack), nb.mul(edge, 0.8))
            height = nb.add(height, nb.mul(nb.smooth(dia, 0.0, 0.3), 1.2))
        if lichen:
            li = nb.smooth(nb.noise(cyl, 7.0, 4, 0.6), 0.6, 0.7)
            col = nb.mix(col, srgb("#b8b4a0"), nb.mul(li, lichen))
        z = nb.sep(nb.coord("Object"))[2]
        col = nb.mix(col, srgb("#5d5444"), nb.mul(nb.smooth(z, 0.35, 0.0), 0.5))
        return dict(color=col, rough=rough, height=height, height_scale=0.022,
                    cavity=nb.maprange(cr, 0, 1, 1.0, 1.0 - 0.3 * fiss))

    return pbr_material(name, fn, res=res, ao_distance=0.6, uv="keep")


# ---------------------------------------------------------------------------
# 枝の骨格
# ---------------------------------------------------------------------------
def _tilt(d, ang):
    """d を鉛直面内で ang [rad] 起こす（負なら下げる）"""
    h = Vector((d.x, d.y, 0.0))
    if h.length < 1e-4:
        h = perp(d)
    h.normalize()
    e = math.asin(max(-1.0, min(1.0, d.z)))
    e = max(-1.45, min(1.45, e + ang))
    return (h * math.cos(e) + UP * math.sin(e)).normalized()


def _flatten(d, k):
    """d の上下成分を k 倍にした向き（枝を水平に寝かせる）"""
    v = Vector((d.x, d.y, d.z * k))
    return v.normalized() if v.length > 1e-4 else d


def _sides(r):
    return 14 if r > 0.15 else 10 if r > 0.07 else 7 if r > 0.03 else 5 if r > 0.012 else 4


class Tree:
    """幹・枝のチューブ（BarkPacker）と、葉の塊を置く枝先を集める"""

    def __init__(self, rnd, name):
        self.rnd = rnd
        self.name = name
        self.bp = BarkPacker(chunk_len=0.7)
        self.segs = []   # (点列, 半径, 深さ)
        self.ends = []   # (位置, 向き, 深さ, 半径)
        self.n = 0

    def limb(self, p0, d0, L, r0, r1, bend=0.0, wobble=0.08, d_end=None, rfun=None, ring_fn=None, step=None,
             depth=0):
        """p0 から d0 向きに長さ L の枝。終わりの向きは d0 を bend だけ起こした向き（d_end で指定も可）"""
        rnd = self.rnd
        d0 = d0.normalized()
        de = d_end.normalized() if d_end is not None else _tilt(d0, bend)
        wob = Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-0.4, 0.4))) * wobble * L
        p3 = p0 + (d0 * 0.5 + de * 0.5).normalized() * L + wob
        p1 = p0 + d0 * L * 0.38
        p2 = p3 - de * L * 0.35
        step = step or (0.3 if r0 > 0.08 else 0.45 if r0 > 0.02 else 0.6)
        m = max(3, int(L / step) + 1)
        pts = geo.bezier_points(p0, p1, p2, p3, m)
        radii = [(r0 + (r1 - r0) * i / (m - 1)) * (rfun(i / (m - 1)) if rfun else 1.0) for i in range(m)]
        self.n += 1
        self.bp.tube(pts, radii, _sides(r0), f"{self.name}_b{self.n}", cap_end=True, ring_fn=ring_fn)
        self.segs.append((pts, radii, depth))
        return pts, radii

    def grow(self, p0, d, L, r, depth, P):
        """再帰の枝分かれ。P: children / angle / bend は深さの関数、ratio（長さ比）、taper（先の太さ比）、
        lateral_from（側枝を出す位置）、dir_fn（子の向きの補正）"""
        rnd = self.rnd
        pts, radii = self.limb(p0, d, L, r, r * P["taper"], bend=P["bend"](depth), wobble=P.get("wobble", 0.08),
                               depth=depth)
        de = (pts[-1] - pts[-2]).normalized()
        self.ends.append((pts[-1], de, depth, radii[-1]))
        if depth == 0:
            return
        lo, hi = P["children"](depth)
        nc = rnd.randint(lo, hi)
        az0 = rnd.uniform(0, math.tau)
        for c in range(nc):
            apical = c == 0 and P.get("apical", True)
            if apical:
                k = len(pts) - 1
                ang = math.radians(rnd.uniform(*P.get("apical_angle", (5, 20))))
            else:
                f0, f1 = P.get("lateral_from", (0.45, 0.95))
                k = max(1, min(len(pts) - 1, int(round((len(pts) - 1) * rnd.uniform(f0, f1)))))
                ang = math.radians(rnd.uniform(*P["angle"](depth)))
            dk = (pts[k] - pts[k - 1]).normalized()
            a = az0 + c * 2.39996 + rnd.uniform(-0.3, 0.3)
            pa = perp(dk)
            pb = dk.cross(pa)
            cd = rotate_toward(dk, pa * math.cos(a) + pb * math.sin(a), ang)
            if "dir_fn" in P:
                cd = P["dir_fn"](cd, depth)
            ratio = P["ratio"] * rnd.uniform(0.85, 1.12) * (1.0 if apical else P.get("lateral_len", 0.85))
            rc = radii[k] * (P.get("apical_r", 0.8) if apical else P.get("lateral_r", 0.62))
            self.grow(pts[k] - dk * min(0.03, radii[k]), cd, L * ratio, max(0.006, rc), depth - 1, P)

    def clumps(self, depths, radii, lift=0.25, push=0.3, jitter=0.2):
        """指定した深さの枝先に葉の塊（中心, 半径）を置く"""
        rnd = self.rnd
        out = []
        for p, d, dep, r in self.ends:
            if dep not in depths:
                continue
            k = rnd.uniform(1 - jitter, 1 + jitter)
            c = p + d * push + UP * lift
            out.append((c, (radii[0] * k, radii[1] * k, radii[2] * k)))
        return out


def clump_anchors(rnd, clumps, n, rmin=0.55, rmax=1.0, zmin=0.4, hide=0.8):
    """葉の塊の外殻付近に葉カードの付け根を散らす。ほかの塊の奥に隠れる点は捨てる。戻り値 [(位置, 外向き, 深さ 0..1)]"""
    C_ = np.array([tuple(c) for c, _ in clumps])
    R_ = np.array([r for _, r in clumps])
    area = R_[:, 0] * R_[:, 1] + R_[:, 0] * R_[:, 2] + R_[:, 1] * R_[:, 2]
    prob = area / area.sum()
    nr = np.random.RandomState(rnd.randrange(1 << 30))
    out = []
    tries = 0
    while len(out) < n and tries < n * 30:
        tries += 1
        i = int(nr.choice(len(clumps), p=prob))
        v = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1)))
        if v.length < 1e-3:
            continue
        v.normalize()
        k = rnd.uniform(rmin, rmax)
        rx, ry, rz = R_[i]
        p = np.array(C_[i]) + np.array([v.x * rx, v.y * ry, v.z * rz]) * k
        if p[2] < zmin:
            continue
        q = ((p - C_) / R_) ** 2
        inside = q.sum(1) < hide * hide
        inside[i] = False
        if inside.any():
            continue
        nrm = Vector((v.x / rx, v.y / ry, v.z / rz)).normalized()
        out.append((Vector(p), nrm, (k - rmin) / max(1e-6, rmax - rmin)))
    return out


def shell_anchors(rnd, n, center, radii, zmin, rmin=0.6, rmax=1.0):
    """1 つの楕円体の外殻付近の付け根（フクギの細長い樹冠）"""
    return clump_anchors(rnd, [(center, radii)], n, rmin, rmax, zmin)


def dress(acc, rnd, anchors, size, wind, droop=0.15, up_bias=0.3, out_bias=0.9, flat=0.0, cell_fn=None,
          rows=4):
    """付け根ごとに外向き・上向きの葉カードを付ける。奥の付け根と下向きの付け根には暗いセルを使う。
    wind = dict(key, bend_at, b, r)"""
    for k, (p, nrm, depth) in enumerate(anchors):
        jit = Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1))) * 0.45
        dd = (nrm * out_bias + UP * up_bias + jit).normalized()
        if flat:
            dd = _flatten(dd, 1.0 - flat)
        hint = (UP * 0.8 + nrm * 0.6 + jit * 0.6).normalized()
        s = size * rnd.uniform(0.8, 1.15)
        if cell_fn:
            cell = cell_fn(p, nrm, depth, rnd)
        elif depth < 0.3 or nrm.z < -0.45:
            cell = DARK
        else:
            cell = rnd.choice(CELLS[:3])
        base = p - dd * s * 0.2
        w = dict(G=anim.hash01(wind["key"], k), A=wind["bend_at"](p.z), b=wind["b"] * s / size, r=wind["r"])
        _card(acc, base, dd, hint, cell, rnd, size=s, droop=droop * rnd.uniform(0.5, 1.5), rows=rows, wind=w)


def _finish(prefix, tree, bark, leaves_acc, leaf_mat, H, main, extra=()):
    """樹皮を詰め、幹・枝と葉に揺れを書いて部品を返す"""
    parts = []
    for o in tree.bp.pack(margin=0.006):
        C.set_smooth(o, True)
        C.assign(o, bark)
        anim.write(o, 0.0, anim.hash01(o.name), 0.0, anim.main_bend(anim.world_co(o)[:, 2], H, main))
        parts.append(o)
    lo = leaves_acc.build(f"{prefix}_leaves")
    C.set_smooth(lo, True)
    C.assign(lo, leaf_mat)
    parts.append(lo)
    parts += list(extra)
    print(f"  {prefix}: {tree.n} branches, {len(leaves_acc.faces) // 6} cards")
    return parts


def _bend_fn(H, main):
    def bend_at(z):
        return float(anim.main_bend(z, H, main))
    return bend_at


def _point_at(pts, z):
    """点列のうち高さ z に最も近い点の番号"""
    return min(range(len(pts)), key=lambda i: abs(pts[i].z - z))


# ---------------------------------------------------------------------------
# 樹種
# ---------------------------------------------------------------------------
def gajumaru(prefix, seed, H=8.5):
    rnd = random.Random(seed)
    T = Tree(rnd, prefix)
    P = dict(children=lambda d: (2, 3), angle=lambda d: (25, 48), ratio=0.68, bend=lambda d: 0.12, taper=0.62,
             lateral_from=(0.4, 0.95), wobble=0.1)

    def flare(t):
        return 1.0 + 0.45 * math.exp(-t / 0.08)

    n_st = 5
    for s in range(n_st):
        az = s * math.tau / n_st + rnd.uniform(-0.3, 0.3)
        off = Vector((math.cos(az), math.sin(az), 0.0)) * rnd.uniform(0.12, 0.32)
        lean = math.radians(rnd.uniform(6, 20))
        d = Vector((math.cos(az) * math.sin(lean), math.sin(az) * math.sin(lean), math.cos(lean)))
        L = rnd.uniform(2.3, 3.0)
        r0 = rnd.uniform(0.17, 0.25)
        pts, radii = T.limb(off + Vector((0, 0, -0.3)), d, L + 0.3, r0, r0 * 0.8, bend=-0.05, rfun=flare,
                            depth=5)
        top = pts[-1]
        for c in range(rnd.randint(2, 3)):
            a = az + rnd.uniform(-1.0, 1.0)
            el = math.radians(rnd.uniform(15, 42))
            cd = Vector((math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el)))
            T.grow(top - d * 0.05, cd, rnd.uniform(3.0, 4.0), r0 * 0.68, 3, P)
    # 気根: 枝から垂れる細い房と、地面まで届いて太った柱
    pool = [(pts, radii) for pts, radii, dep in T.segs if 1 <= dep <= 3]
    w = np.array([sum((pts[i] - pts[i - 1]).length for i in range(1, len(pts))) for pts, _ in pool])
    nr = np.random.RandomState(seed)
    for k in nr.choice(len(pool), 46, p=w / w.sum()):
        pts, radii = pool[k]
        i = rnd.randrange(1, len(pts))
        p = pts[i]
        if p.z < 2.0:
            continue
        for j in range(rnd.randint(3, 7)):
            o = Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), 0)) * radii[i] * 0.8
            L = rnd.uniform(0.5, min(p.z - 0.4, 2.6))
            wx, wy = rnd.uniform(-0.08, 0.08), rnd.uniform(-0.08, 0.08)
            q = [p + o + Vector((wx * s * s, wy * s * s, -L * s)) for s in (0.0, 0.35, 0.7, 1.0)]
            r = rnd.uniform(0.004, 0.009)
            T.bp.tube(q, [r * 1.3, r, r * 0.8, r * 0.4], 4, f"{prefix}_root{k}_{j}", cap_end=True)
    pillars = 0
    for pts, radii in pool:
        if pillars >= 4:
            break
        i = len(pts) // 2
        p = pts[i]
        if p.z < 2.5 or Vector((p.x, p.y)).length < 2.2 or rnd.random() < 0.5:
            continue
        pillars += 1
        r = rnd.uniform(0.035, 0.07)
        q = [p + Vector((rnd.uniform(-0.05, 0.05), rnd.uniform(-0.05, 0.05), 0)) * s - Vector((0, 0, (p.z + 0.15) * s))
             for s in (0.0, 0.25, 0.5, 0.75, 0.9, 1.0)]
        T.bp.tube(q, [r * 0.7, r * 0.8, r * 0.9, r, r * 1.2, r * 1.6], 7, f"{prefix}_pillar{pillars}", cap_end=True)
    clumps = T.clumps((1,), (1.3, 1.3, 0.95), lift=0.35, push=0.25)
    main = 0.02 * H
    bend_at = _bend_fn(H, main)
    acc = MeshAcc()
    anchors = clump_anchors(rnd, clumps, 1550, rmin=0.5, zmin=2.0)
    dress(acc, rnd, anchors, GAJUMARU_CARD, dict(key=(prefix, "leaf"), bend_at=bend_at, b=0.05, r=0.015))
    bark = _mat("gaju_bark", lambda: bark_material("GajumaruBark", "#77746a", "#99958a", "#b5b1a4",
                                                   crack="#5a564c", fiss=0.2, fiss_scale=(4.0, 2.0), lent=0.5,
                                                   lichen=0.45, rough=0.8))
    return _finish(prefix, T, bark, acc, _mat("gaju_leaf", gajumaru_leaf), H, main)


def kobateishi(prefix, seed, H, tiers, spread):
    rnd = random.Random(seed)
    T = Tree(rnd, prefix)
    r0 = 0.06 + 0.022 * H
    trunk, tr = T.limb(Vector((0, 0, -0.3)), Vector((rnd.uniform(-0.04, 0.04), rnd.uniform(-0.04, 0.04), 1)),
                       H * 0.9 + 0.3, r0, r0 * 0.25, wobble=0.03, rfun=lambda t: 1.0 + 0.3 * math.exp(-t / 0.05),
                       depth=4)
    P = dict(children=lambda d: (2, 3), angle=lambda d: (32, 55), ratio=0.6, bend=lambda d: 0.16, taper=0.6,
             lateral_from=(0.35, 0.9), wobble=0.06, dir_fn=lambda v, d: _flatten(v, 0.35), apical_angle=(5, 15))
    z0 = 2.0 + 0.1 * H
    top = H * 0.86
    for k in range(tiers):
        z = z0 + (top - z0) * k / max(1, tiers - 1)
        i = _point_at(trunk, z)
        n = rnd.randint(4, 5)
        az0 = rnd.uniform(0, math.tau)
        f = (z - z0) / max(0.1, top - z0)
        for b in range(n):
            az = az0 + b * math.tau / n + rnd.uniform(-0.3, 0.3)
            el = math.radians(rnd.uniform(4, 14))
            d = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
            L = (spread * (1 - f) ** 0.65 + 0.5) * rnd.uniform(0.85, 1.1)
            T.grow(trunk[i], d, L, tr[i] * 0.48, 2, P)
    T.grow(trunk[-1], UP, 0.9, tr[-1], 1, dict(P, dir_fn=lambda v, d: v))
    clumps = T.clumps((0, 1), (0.85, 0.85, 0.32), lift=0.15, push=0.2)
    main = 0.022 * H
    bend_at = _bend_fn(H, main)
    acc = MeshAcc()
    anchors = clump_anchors(rnd, clumps, int(72 * spread * tiers ** 0.5), rmin=0.4, zmin=1.5, hide=0.75)
    def cell_fn(p, nrm, depth, r):
        if depth < 0.3 or nrm.z < -0.45:
            return DARK
        return (0.0, 0.5) if r.random() < 0.08 else r.choice(CELLS[:2])

    dress(acc, rnd, anchors, KOBATEISHI_CARD, dict(key=(prefix, "leaf"), bend_at=bend_at, b=0.06, r=0.02),
          up_bias=0.5, out_bias=0.7, flat=0.55, droop=0.1, cell_fn=cell_fn)
    bark = _mat("koba_bark", lambda: bark_material("KobateishiBark", "#5e564a", "#7a7062", "#958a78",
                                                   crack="#3e362e", fiss=0.45, fiss_scale=(9.0, 0.6),
                                                   lichen=0.3, rough=0.85))
    return _finish(prefix, T, bark, acc, _mat("koba_leaf", kobateishi_leaf), H, main)


def fukugi(prefix, seed, H=7.5):
    rnd = random.Random(seed)
    T = Tree(rnd, prefix)
    trunk, tr = T.limb(Vector((0, 0, -0.3)), Vector((rnd.uniform(-0.03, 0.03), rnd.uniform(-0.03, 0.03), 1)),
                       H * 0.97 + 0.3, 0.2, 0.04, wobble=0.02, rfun=lambda t: 1.0 + 0.25 * math.exp(-t / 0.05),
                       depth=3)
    P = dict(children=lambda d: (2, 3), angle=lambda d: (25, 45), ratio=0.55, bend=lambda d: 0.2, taper=0.55,
             lateral_from=(0.4, 0.9), wobble=0.08)
    z0 = 1.1
    z = z0
    a = rnd.uniform(0, math.tau)
    while z < H * 0.94:
        i = _point_at(trunk, z)
        t = (z - z0) / (H - z0)
        L = 0.35 + 1.25 * math.sin(math.pi * min(1.0, t * 0.9 + 0.08)) ** 0.7
        el = math.radians(rnd.uniform(30, 50))
        d = Vector((math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el)))
        T.grow(trunk[i], d, L * rnd.uniform(0.85, 1.1), max(0.012, tr[i] * 0.4), 1, P)
        a += 2.39996
        z += rnd.uniform(0.28, 0.42)
    cz = (H + z0) / 2 + 0.25
    rz = (H - z0) / 2 + 0.35
    main = 0.018 * H
    bend_at = _bend_fn(H, main)
    acc = MeshAcc()
    anchors = shell_anchors(rnd, 520, Vector((0, 0, cz)), (1.45, 1.45, rz - 0.1), zmin=z0 - 0.2, rmin=0.6)
    tips = [c for c in T.clumps((0,), (0.45, 0.45, 0.45), lift=0.05, push=0.1) if c[0].z < cz + rz * 0.8]
    anchors += clump_anchors(rnd, tips, 180, rmin=0.6, zmin=z0)
    dress(acc, rnd, anchors, FUKUGI_CARD, dict(key=(prefix, "leaf"), bend_at=bend_at, b=0.04, r=0.012),
          up_bias=0.4)
    bark = _mat("fuku_bark", lambda: bark_material("FukugiBark", "#4a4239", "#625849", "#7c7160",
                                                   crack="#2e2822", fiss=0.55, fiss_scale=(6.0, 1.4),
                                                   lichen=0.35, rough=0.9))
    return _finish(prefix, T, bark, acc, _mat("fuku_leaf", fukugi_leaf), H, main)


def terihaboku(prefix, seed, H=7.0):
    rnd = random.Random(seed)
    T = Tree(rnd, prefix)
    lean = Vector((rnd.uniform(-0.12, 0.12), rnd.uniform(-0.12, 0.12), 1))
    trunk, tr = T.limb(Vector((0, 0, -0.3)), lean, 2.6, 0.3, 0.24, wobble=0.04,
                       rfun=lambda t: 1.0 + 0.4 * math.exp(-t / 0.08), depth=5)
    P = dict(children=lambda d: (2, 3), angle=lambda d: (25, 45), ratio=0.66, bend=lambda d: 0.1, taper=0.6,
             lateral_from=(0.4, 0.95), wobble=0.1)
    n = 4
    az0 = rnd.uniform(0, math.tau)
    for b in range(n):
        az = az0 + b * math.tau / n + rnd.uniform(-0.35, 0.35)
        el = math.radians(rnd.uniform(38, 58))
        d = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
        T.grow(trunk[-1] - lean.normalized() * 0.1, d, rnd.uniform(2.4, 3.1), 0.15, 3, P)
    clumps = T.clumps((1,), (1.15, 1.15, 0.95), lift=0.35, push=0.25)
    main = 0.02 * H
    bend_at = _bend_fn(H, main)
    acc = MeshAcc()
    anchors = clump_anchors(rnd, clumps, 1000, rmin=0.5, zmin=1.8)
    dress(acc, rnd, anchors, TERIHA_CARD, dict(key=(prefix, "leaf"), bend_at=bend_at, b=0.05, r=0.015))
    bark = _mat("teri_bark", lambda: bark_material("TerihaBark", "#3e352c", "#5a4c3e", "#76675a",
                                                   crack="#241e19", fiss=0.8, fiss_scale=(5.0, 0.7),
                                                   lichen=0.2, rough=0.92))
    return _finish(prefix, T, bark, acc, _mat("teri_leaf", terihaboku_leaf), H, main)


def hououboku(prefix, seed, H=6.5):
    rnd = random.Random(seed)
    T = Tree(rnd, prefix)
    trunk, tr = T.limb(Vector((0, 0, -0.3)), Vector((rnd.uniform(-0.08, 0.08), rnd.uniform(-0.08, 0.08), 1)), 2.3,
                       0.28, 0.22, wobble=0.04, rfun=lambda t: 1.0 + 0.35 * math.exp(-t / 0.08), depth=5)
    P = dict(children=lambda d: (2, 3), angle=lambda d: (30, 55), ratio=0.64, bend=lambda d: 0.14, taper=0.6,
             lateral_from=(0.25, 0.95), wobble=0.08, dir_fn=lambda v, d: _flatten(v, 0.5))
    n = 6
    az0 = rnd.uniform(0, math.tau)
    for b in range(n):
        az = az0 + b * math.tau / n + rnd.uniform(-0.3, 0.3)
        el = math.radians(rnd.uniform(12, 26))
        d = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
        T.grow(trunk[-1], d, rnd.uniform(3.6, 4.4), 0.14, 3, P)
    clumps = T.clumps((0, 1), (1.05, 1.05, 0.5), lift=0.3, push=0.2)
    main = 0.022 * H
    bend_at = _bend_fn(H, main)
    acc = MeshAcc()
    anchors = clump_anchors(rnd, clumps, 1150, rmin=0.45, zmin=2.0)

    def cell_fn(p, nrm, depth, r):
        if depth < 0.3 or nrm.z < -0.45:
            return DARK
        if nrm.z > 0.35 and r.random() < 0.35:
            return (0.0, 0.5)
        return r.choice(CELLS[:2])

    dress(acc, rnd, anchors, HOUOU_CARD, dict(key=(prefix, "leaf"), bend_at=bend_at, b=0.06, r=0.02),
          up_bias=0.45, flat=0.3, cell_fn=cell_fn)
    bark = _mat("houou_bark", lambda: bark_material("HououbokuBark", "#6a6458", "#8a8272", "#a59c8a",
                                                    crack="#4a443a", fiss=0.15, fiss_scale=(4.0, 2.0), lent=0.35,
                                                    lichen=0.35, rough=0.8))
    return _finish(prefix, T, bark, acc, _mat("houou_leaf", hououboku_leaf), H, main)


def sotetsu(prefix, seed):
    rnd = random.Random(seed)
    T = Tree(rnd, prefix)
    frond_mat = _mat("sotetsu_frond", sotetsu_frond_material)
    heads = [((0.0, 0.0), 1.15, (0.06, 0.04)), ((0.42, 0.28), 0.62, (0.12, 0.08))]
    H = 2.4
    main = 0.012 * H
    bend_at = _bend_fn(H, main)
    extra = []
    for hi, ((x, y), h, (lx, ly)) in enumerate(heads):
        base = Vector((x, y, -0.2))
        top = Vector((x + lx, y + ly, h))
        pts = [base.lerp(top, s) for s in np.linspace(0, 1, 6)]
        r = 0.19 if hi == 0 else 0.16
        radii = [r * (1.12 if i == 0 else 1.0 + 0.08 * math.sin(i)) for i in range(len(pts))]
        T.bp.tube(pts, radii, 12, f"{prefix}_trunk{hi}", cap_end=True)
        T.n += 1
        crown = top + Vector((0, 0, 0.05))
        n_f = 22 if hi == 0 else 16
        for k in range(n_f):
            az = k * 2.39996 + rnd.uniform(-0.15, 0.15)
            age = k / max(1, n_f - 1)
            elev = math.radians(62 - 70 * age + rnd.uniform(-6, 6))
            L = SOTETSU_L * rnd.uniform(0.85, 1.0) * (1.0 if hi == 0 else 0.85)
            wind = dict(G=anim.hash01(prefix, "frond", hi, k), A=bend_at(crown.z), b=0.025 * L, r=0.008)
            for o in _frond(f"{prefix}_frond{hi}_{k}", crown, az, elev, L, 0.12 + 0.35 * age,
                            SOTETSU_W * rnd.uniform(0.9, 1.05), dry=(age > 0.9 and rnd.random() < 0.6),
                            twist=rnd.uniform(-0.2, 0.2), rnd=rnd, wind=wind):
                C.assign(o, frond_mat)
                extra.append(o)
    parts = []
    bark = _mat("sotetsu_bark", lambda: bark_material("SotetsuBark", "#3e3830", "#5a5144", "#746a58",
                                                      crack="#26211c", fiss=0.2, lichen=0.15, rough=0.9, res=1024,
                                                      scales=True))
    for o in T.bp.pack(margin=0.01):
        C.set_smooth(o, True)
        C.assign(o, bark)
        anim.write(o, 0.0, anim.hash01(o.name), 0.0, anim.main_bend(anim.world_co(o)[:, 2], H, main))
        parts.append(o)
    print(f"  {prefix}: {len(extra) // 2} fronds")
    return parts + extra


def build():
    _MATS.clear()
    return register_cutout_aliases({
        "Gajumaru": gajumaru("Gaju", 11),
        "Kobateishi_A": kobateishi("KobaA", 13, 9.0, 5, 4.2),
        "Kobateishi_B": kobateishi("KobaB", 17, 6.0, 3, 2.8),
        "Fukugi": fukugi("Fuku", 19),
        "Terihaboku": terihaboku("Teri", 23),
        "Hououboku": hououboku("Houou", 29),
        "Sotetsu": sotetsu("Sotetsu", 31),
    })
