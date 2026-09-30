"""リュウキュウスガモ（Thalassia hemprichii）の海草藻場 2 バリエーション

Seagrass_Patch_A: 約 4 x 3 m の不定形の藻場（ジュゴンの食み跡が 2 本通る）
Seagrass_Patch_B: 約 1.5 m の小さな株立ち

葉  : 帯状の葉を 1〜4 節の短冊（幅方向は 1 枚。節数は長さと曲がりで決める）で作り、両面化する
      葉アトラス（16 列: 若葉 3 / 成葉 6 / 古葉 4 / 枯れかけ 3）を numpy で描いて焼く。
      縦の葉脈、タンニン細胞の褐色の短い線、付着した石灰藻の白い斑点と珪藻の褐色のくすみ、
      先端の枯れ・ちぎれ、丸い葉先（アルファ）。下ほど暗い（群落の中の陰。AO の代わり）
株  : 砂の中の地下茎から立ち上がる短い葉鞘（三角形 1 枚）から、2〜5 枚の葉を扇状（葉の面内）に出す。
      葉は鎌形にわずかに反り、流れの向きへ面外になびく。内側の若い葉ほど短く明るい
配置: 密度場（ノイズで崩した楕円 + むら + 食み跡）に沿って地下茎をランダムウォークさせ、
      株を置く。縁ほど疎らで葉も短く、砂地へ溶け込む
原点: パッチ中心の砂面（z=0）。葉鞘の付け根は砂の中（z=-0.025）
揺れ: 葉は付け根から先へしなる（振幅は葉の長さに比例）。株全体の曲げ A は 0（okinawa/anim.py）
"""
import math
import os
import random

import numpy as np
from mathutils import Matrix, Vector
from mathutils import noise as mnoise
from scipy import ndimage

from .. import anim
from .. import common as C
from ..common import srgb
from .adan import MeshAcc
from .hibiscus import Canvas, image_material

PREVIEW_CAM = dict(cam_dir=(0.2, -1.0, 0.9), lens=50)

# エンジンの頂点アニメーションの種類（glTF のマテリアルの extras に書く。置くだけで波に寄せ返す）
VERTEX_ANIMATION = "seagrass"

RES_SCALE = float(os.environ.get("OKI_RES_SCALE", "1"))
ATLAS_RES = 2048
N_COL = 16         # アトラスの列数（葉の種類）
LEAF_L = 0.2       # アトラス 1 列の長さ(m)。実際の葉は長さに合わせて伸縮する
COL_W = 0.0105     # 1 列の幅(m)
FILL = 0.88        # 列幅のうち葉身が占める割合（残りは透明の余白）
BASE_Z = -0.025    # 葉鞘の付け根（砂の中）
UP = Vector((0, 0, 1))
CURRENT = Vector((math.cos(0.45), math.sin(0.45), 0.0))  # 流れの向き（葉がなびく向き）

# 列ごとの (年齢 0=若葉..1=枯れかけ, 葉先 0=丸 1=褐変 2=ちぎれ)
COLS = [(0.02, 0), (0.08, 0), (0.14, 0),
        (0.26, 0), (0.31, 1), (0.36, 0), (0.41, 2), (0.46, 0), (0.5, 0),
        (0.6, 1), (0.66, 2), (0.72, 0), (0.78, 1),
        (0.86, 1), (0.93, 2), (1.0, 1)]
YOUNG, MATURE, OLD, DYING = range(0, 3), range(3, 9), range(9, 13), range(13, 16)
AGE_STOPS = [(0.0, "#7fbd4c"), (0.28, "#5e9f3e"), (0.52, "#6f963f"), (0.76, "#a09f4e"), (1.0, "#ad8a4e")]


# ---------------------------------------------------------------------------
# 葉アトラス（numpy で描く）
# ---------------------------------------------------------------------------
def _ss(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def _lin(h):
    return np.array(srgb(h), np.float32)


def _mix(a, b, t):
    t = np.asarray(t, np.float32)[..., None]
    return a * (1 - t) + b * t


def _age_color(age):
    ages = [a for a, _ in AGE_STOPS]
    cols = np.array([srgb(h) for _, h in AGE_STOPS], np.float32)
    return np.stack([np.interp(age, ages, cols[:, k]) for k in range(3)], -1)


def _field(rng, n, cells_y, cells_x):
    """なめらかなノイズ場（粗い乱数格子を 3 次補間で拡大）。平均 0・標準偏差 ≈1"""
    g = rng.standard_normal((cells_y + 3, cells_x + 3)).astype(np.float32)
    f = ndimage.zoom(g, (n / cells_y, n / cells_x), order=3)
    oy = (f.shape[0] - n) // 2
    ox = (f.shape[1] - n) // 2
    f = f[oy:oy + n, ox:ox + n]
    return (f - f.mean()) / (f.std() + 1e-6)


class _Leaves:
    """列ごとに mm 単位で描くための座標系"""

    def __init__(self, n):
        self.n = n
        self.cpx = n / N_COL                    # 1 列の画素数
        self.sx = self.cpx / (COL_W * 1000.0)   # 横 1mm あたりの画素数
        self.sy = n / (LEAF_L * 1000.0)         # 縦 1mm あたりの画素数

    def to_px(self, ci, xm, ym):
        return (ci + 0.5) * self.cpx + xm * self.sx, ym * self.sy

    def blob(self, arrs, ci, xm, ym, rx, ry, col, opa, hgt=0.0, rough=None):
        """楕円の斑点（mm 指定）。画素より小さいものは薄くして面積の見た目を保つ"""
        col_a, h_a, r_a = arrs
        cx, cy = self.to_px(ci, xm, ym)
        rxp, ryp = rx * self.sx, ry * self.sy
        k = min(1.0, rxp / 0.7) * min(1.0, ryp / 0.7)
        rxp, ryp = max(rxp, 0.7), max(ryp, 0.7)
        j0, j1 = max(int(cx - rxp - 1), 0), min(int(cx + rxp + 2), self.n)
        i0, i1 = max(int(cy - ryp - 1), 0), min(int(cy + ryp + 2), self.n)
        if j1 <= j0 or i1 <= i0:
            return
        X, Y = np.meshgrid(np.arange(j0, j1) + 0.5, np.arange(i0, i1) + 0.5)
        q = np.sqrt(((X - cx) / rxp) ** 2 + ((Y - cy) / ryp) ** 2)
        m = (np.clip((1.0 - q) * min(rxp, ryp) + 0.5, 0, 1) * (opa * k)).astype(np.float32)
        sl = (slice(i0, i1), slice(j0, j1))
        col_a[sl] = col_a[sl] * (1 - m[..., None]) + np.asarray(col) * m[..., None]
        if hgt:
            h_a[sl] += hgt * np.sqrt(np.clip(1 - q * q, 0, 1)).astype(np.float32) * m
        if rough is not None:
            r_a[sl] = r_a[sl] * (1 - m) + rough * m


def leaf_canvas(res):
    """葉アトラス。U: 列(16)×幅, V: 付け根 0 → 先端 1。戻り値は hibiscus.Canvas"""
    rng = np.random.default_rng(11)
    n = res
    L = _Leaves(n)
    iy, ix = np.mgrid[0:n, 0:n]
    U = (ix + 0.5) / n
    V = ((iy + 0.5) / n).astype(np.float32)
    ci = np.minimum((U * N_COL).astype(np.int32), N_COL - 1)
    X = (U * N_COL - ci - 0.5) * 2.0
    xm = (X * COL_W * 500.0).astype(np.float32)   # 葉の中心からの横位置(mm)
    ym = (V * LEAF_L * 1000.0).astype(np.float32)  # 付け根からの位置(mm)
    hw = FILL * COL_W * 500.0                      # 葉身の半幅(mm)
    Lmm = LEAF_L * 1000.0
    px = max(1.0 / L.sx, 1.0 / L.sy)               # 1 画素(mm)
    ages = np.array([c[0] for c in COLS], np.float32)
    tips = np.array([c[1] for c in COLS])
    age = ages[ci]
    tip = tips[ci]
    phc = rng.uniform(0, 100, (N_COL, 6)).astype(np.float32)

    def ph(k):
        return phc[ci, k]

    # --- 輪郭（mm の符号付き距離、内側が正） ---
    apex = Lmm - 1.5
    cy = apex - hw
    d_side = hw * (1 + 0.006 * np.sin(ym * 0.35 + ph(0))) - np.abs(xm)
    d_tip = hw - np.hypot(xm, np.maximum(ym - cy, 0))
    # 丸い葉先の縁のごく細かい鋸歯
    d_tip = d_tip - 0.06 * (0.5 + 0.5 * np.sin(np.arctan2(xm, np.maximum(ym - cy, 1e-3)) * 22 + ph(1))) \
        * _ss(cy, apex, ym)
    d = np.minimum(d_side, d_tip)
    cut_y = (apex - rng.uniform(14, 34, N_COL).astype(np.float32))[ci]
    # ちぎれ口: やや斜めで不揃い
    jag = (1.3 * np.sin(xm * 1.1 + ph(2)) + 0.5 * np.sin(xm * 2.9 + ph(3)) + 0.18 * np.sin(xm * 7.3 + ph(4))
           + 0.3 * xm * np.sign(np.sin(ph(4))))
    d_cut = cut_y + jag - ym
    broken = tip == 2
    d = np.where(broken, np.minimum(d, d_cut), d)
    alpha = np.clip(d / px + 0.5, 0, 1).astype(np.float32)

    # --- 地色 ---
    tone = rng.uniform(-1, 1, N_COL).astype(np.float32)[ci]
    col = _age_color(age + 0.04 * tone)
    col = col * (1.0 + 0.07 * tone)[..., None]
    # 列内のむら（縦に長い斑）
    mott = _field(rng, n, 28, N_COL * 4)
    col = col * (1.0 + 0.06 * mott)[..., None]
    hue = _field(rng, n, 12, N_COL * 2)
    col = _mix(col, col * _lin("#d8e8a0") / _lin("#a8c878"), 0.25 * np.clip(hue, 0, 1))
    # 葉は付け根で伸びるので先端ほど古い組織: 黄化
    yell = _ss(0.35, 1.0, V) * (0.12 + 0.55 * age) * (0.7 + 0.3 * np.clip(mott, -1, 1))
    col = _mix(col, _lin("#b4a655"), yell * 0.55)
    # 付け根（葉鞘・砂の際）は白っぽい
    col = _mix(col, _lin("#dcdcb2"), _ss(24.0, 7.0, ym))
    col = _mix(col, _lin("#d8c4ae"), _ss(8.0, 0.0, ym) * 0.6)
    # 縦の葉脈（約 0.7mm 間隔、13 本）と中肋
    q = xm / 0.7
    vein = np.exp(-((q - np.round(q)) / 0.17) ** 2) * _ss(hw, hw - 0.5, np.abs(xm))
    mid = np.exp(-(xm / 0.2) ** 2)
    col = col * (1.0 + 0.07 * vein + 0.06 * mid)[..., None]
    # 縁は薄く明るい
    edge = _ss(0.45, 0.0, d_side)
    col = col * (1.0 + 0.1 * edge)[..., None]
    # 珪藻・微細藻の褐色のくすみ（古い葉ほど、先ほど）
    film = _ss(0.0, 1.2, _field(rng, n, 30, N_COL * 3)) * _ss(0.25, 0.85, V) * age ** 1.3
    col = _mix(col, _lin("#8d7d52"), film * 0.55)
    # 砂泥のうっすらとした付着
    silt = _ss(0.3, 1.5, _field(rng, n, 60, N_COL * 5)) * age * _ss(0.15, 0.7, V)
    col = _mix(col, _lin("#bdb49a"), silt * 0.3)
    hgt = (0.45 + 0.12 * vein + 0.12 * mid - 0.3 * _ss(0.9, 0.0, d)).astype(np.float32)
    rgh = (0.5 + 0.14 * age + 0.1 * film).astype(np.float32)

    # --- 先端の枯れ（褐変）とちぎれ口 ---
    yb = (Lmm * rng.uniform(0.9, 0.965, N_COL)).astype(np.float32)[ci] \
        + 2.2 * np.sin(xm * 0.7 + ph(5)) + 0.6 * np.sin(xm * 2.3 + ph(1))
    tipc = (tip == 1).astype(np.float32)
    # 緑 → 黄 → 褐色へ枯れ込む
    col = _mix(col, _lin("#bba452"), _ss(yb - 14.0, yb - 1.5, ym) * tipc * 0.7)
    brown = _ss(yb - 2.0, yb + 3.0, ym) * tipc
    bcol = _mix(_lin("#9a7440"), _lin("#6e4e2c"), _ss(yb + 2, apex, ym))
    col = _mix(col, bcol, brown * 0.9)
    col = _mix(col, _lin("#5a3a20"), np.exp(-((ym - yb - 0.5) / 0.8) ** 2) * 0.2 * tipc)
    torn = _ss(3.0, 0.4, d_cut) * broken
    col = _mix(col, _lin("#7d6038"), torn * 0.85)
    rgh = np.where(brown + torn > 0.5, 0.7, rgh)

    arrs = (col, hgt, rgh)
    # --- タンニン細胞（褐色の短い線） ---
    for c in range(N_COL):
        a = COLS[c][0]
        for _ in range(int((0.3 + 1.1 * a) * 170)):
            y = Lmm * (0.08 + 0.9 * rng.random() ** 0.8)
            x = rng.uniform(-hw * 0.85, hw * 0.85)
            L.blob(arrs, c, x, y, rng.uniform(0.06, 0.1), rng.uniform(0.25, 0.7), _lin("#57261a"),
                   rng.uniform(0.45, 0.85))
    # --- 石灰藻の白い斑点（古い葉ほど多く、先ほど密。群がって付く） ---
    spk = [_lin(h) for h in ("#e9ddd4", "#dcc6c6", "#f0eade", "#cbbfae", "#e2cfc9")]
    for c in range(N_COL):
        a = COLS[c][0]
        n_cl = int(1 + 9 * a)
        for _ in range(n_cl):
            cy0 = Lmm * (0.25 + 0.72 * rng.random() ** 0.6)
            cx0 = rng.uniform(-hw * 0.6, hw * 0.6)
            sig = rng.uniform(2.0, 9.0)
            for _ in range(int(rng.integers(3, 8 + int(55 * a * a)))):
                y = cy0 + rng.normal(0, sig)
                x = cx0 + rng.normal(0, min(sig, hw) * 0.6)
                if not (8 < y < apex - 1 and abs(x) < hw - 0.3):
                    continue
                r = float(np.clip(rng.lognormal(math.log(0.28), 0.5), 0.1, 1.1))
                L.blob(arrs, c, x, y, r, r, spk[int(rng.integers(len(spk)))], rng.uniform(0.7, 1.0),
                       hgt=0.35, rough=0.85)
        # 若い葉にもまばらな小さな点
        for _ in range(int(10 + 30 * a)):
            y = Lmm * rng.uniform(0.2, 0.97)
            x = rng.uniform(-hw * 0.9, hw * 0.9)
            L.blob(arrs, c, x, y, 0.1, 0.1, spk[2], rng.uniform(0.4, 0.8), hgt=0.25, rough=0.8)
    col, hgt, rgh = arrs
    hgt = np.clip(hgt, 0.0, 1.0)

    # 群落の中の陰（下ほど暗い）。atlas は AO を焼かないので cavity に入れる
    cav = (0.6 + 0.4 * _ss(0.0, 0.55, V)).astype(np.float32)

    # 透明部分は最寄りの葉の色で埋める（ミップのにじみ対策）
    mask = alpha > 0.5
    _, (jy, jx) = ndimage.distance_transform_edt(~mask, return_indices=True)
    cv = Canvas(n)
    cv.col = col[jy, jx].astype(np.float32)
    cv.h = hgt[jy, jx].astype(np.float32)
    cv.r = rgh[jy, jx].astype(np.float32)
    cv.cav = cav
    cv.a = alpha
    return cv


_MAT = {}


def leaf_material():
    if "leaf" not in _MAT:
        cv = leaf_canvas(max(64, int(ATLAS_RES * RES_SCALE)))
        _MAT["leaf"] = image_material("SeagrassLeaf", cv, ATLAS_RES, (COL_W * N_COL, LEAF_L), height_scale=0.0004)
    return _MAT["leaf"]


# ---------------------------------------------------------------------------
# 形状
# ---------------------------------------------------------------------------
def _sss(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def _leaf(acc, base, t_dir, n_dir, length, width, col, bend, twist, falc, segs, v0=0.0, G=None):
    """帯状の葉 1 枚を acc に追加（片面。両面化はマテリアル側）

    t_dir: 付け根の向き, n_dir: 葉の面の法線（bend > 0 でこちらへなびく）
    bend: 面外のなびきの全角, falc: 面内の鎌形の反りの全角, twist: ねじれの全角（ラジアン）
    v0: 付け根の V（葉鞘の上から出る葉は淡色部の途中から始める）
    G: 揺れの位相（None なら揺れデータを書かない）
    """
    T = t_dir.normalized()
    N = (n_dir - T * n_dir.dot(T)).normalized()
    W = T.cross(N)
    ds = length / segs
    wts = [0.85 + 0.12 * i for i in range(segs)]  # 葉鞘の上から寝はじめ、先ほどやや強く曲がる
    tot = sum(wts)
    P = Vector(base)
    rows, uvs = [], []
    hw = width / FILL * 0.5
    u0 = (col + 0.004) / N_COL
    u1 = (col + 0.996) / N_COL

    def row(P, W, s):
        v = v0 + (1.0 - v0) * min(1.0, s / length)
        rows.append([P - W * hw, P + W * hw])
        uvs.append([(u0, v), (u1, v)])

    row(P, W, 0.0)
    for i in range(segs):
        k = wts[i] / tot
        R = Matrix.Rotation(bend * k, 3, W)
        T, N = R @ T, R @ N
        R = Matrix.Rotation(falc * k, 3, N)
        T, W = R @ T, R @ W
        R = Matrix.Rotation(twist * k, 3, T)
        W, N = R @ W, R @ N
        Q = P + T * ds
        if Q.z < 0.01:
            # 砂に届いた葉は砂の上に寝かせる
            T = Vector((T.x, T.y, max(T.z, 0.03))).normalized()
            N = (N - T * N.dot(T)).normalized()
            W = T.cross(N)
            Q = P + T * ds
            Q.z = max(Q.z, 0.01)
        P = Q
        row(P, W, ds * (i + 1))
    if G is not None:
        # 付け根から先へ s^1.5 でしなる。長い葉ほど大きく揺れる
        acc.set_anim(len(acc.verts), [(0.03 * length * (i / segs), G, 0.3 * length * (i / segs) ** 1.5, 0.0)
                                      for i in range(segs + 1) for _ in range(2)])
    acc.grid(rows, uvs)


def _pick_col(rnd, rank, oldness):
    """葉の位置（0=内側の若い葉 → 1=外側の古い葉）と株の古さからアトラスの列を選ぶ"""
    r = rnd.random()
    if rank < 0.3:
        return rnd.choice(YOUNG) if r < 0.6 else rnd.choice(MATURE)
    if rank < 0.8:
        return rnd.choice(MATURE) if r < 0.75 - 0.25 * oldness else rnd.choice(OLD)
    if r < 0.45 - 0.25 * oldness:
        return rnd.choice(MATURE)
    if r < 0.85:
        return rnd.choice(OLD)
    return rnd.choice(DYING)


def _shoot_spec(rnd, p, d, fan_az):
    """株 1 つ分の葉の設定（形はまだ作らない。三角形数の見積もりに使う）"""
    n = rnd.choices([2, 3, 4, 5], [0.18, 0.42, 0.3, 0.1])[0]
    Lmax = min(0.3, max(0.09, (0.1 + 0.14 * d ** 0.7) * rnd.uniform(0.85, 1.18)))
    wmax = (0.0065 + 0.0033 * d) * rnd.uniform(0.92, 1.08)
    oldness = min(1.0, max(0.0, 0.25 + 0.5 * d + rnd.uniform(-0.25, 0.25)))
    leaves = []
    spread = rnd.uniform(0.1, 0.32)
    bend0 = rnd.uniform(0.55, 1.45)
    for k in range(n):
        pos = (k - (n - 1) / 2) / max(1.0, (n - 1) / 2)
        rank = abs(pos)
        L = Lmax * (0.55 + 0.45 * rank) * rnd.uniform(0.85, 1.08)
        col = _pick_col(rnd, rank, oldness)
        if col in DYING or (col in OLD and rnd.random() < 0.3):
            L *= rnd.uniform(0.6, 0.95)  # 古い葉は先が欠けて短い
        L = max(0.06, L)
        bend = max(0.15, bend0 + rnd.uniform(-0.3, 0.3)) * (L / 0.2) ** 0.6
        # 節数は長さと曲がりで決める（短い若葉はまっすぐ 1 節）
        segs = 1 if L < 0.1 else (2 if L < 0.19 else (4 if L > 0.26 and bend > 1.2 else 3))
        if segs == 1:
            bend *= 0.5
        leaves.append(dict(pos=pos, L=L, w=wmax * rnd.uniform(0.9, 1.05), col=col, bend=bend,
                           twist=rnd.uniform(-0.8, 0.8), falc=math.copysign(rnd.uniform(0.04, 0.26), pos)
                           if pos else rnd.uniform(-0.12, 0.12), segs=segs, fan=pos * spread + rnd.gauss(0, 0.06)))
    return dict(p=p, d=d, az=fan_az, leaves=leaves, tilt=(rnd.gauss(0, 0.1), rnd.gauss(0, 0.08)),
                flip=rnd.random() < 0.2, sheath=rnd.uniform(0.035, 0.05), wmax=wmax)


def _shoot_tris(sp):
    """両面化後の三角形数（葉鞘の三角形 1 枚 + 葉）"""
    return 2 + 4 * sum(lf["segs"] for lf in sp["leaves"])


def _build_shoot(acc, rnd, sp, key=None):
    x, y = sp["p"]
    Fh = Vector((math.cos(sp["az"]), math.sin(sp["az"]), 0.0))  # 扇の広がる向き（葉の面内・水平）
    Nf = Vector((-Fh.y, Fh.x, 0.0))                               # 葉の面の法線
    tilt = Matrix.Rotation(sp["tilt"][0], 3, Fh) @ Matrix.Rotation(sp["tilt"][1], 3, Nf)
    axis = tilt @ UP
    W = tilt @ Fh
    # 葉鞘: 砂の中から 1〜2cm 立ち上がる淡色のくさび（三角形 1 枚）。葉はこの上から扇状に出る
    base = Vector((x, y, BASE_Z))
    top = base + axis * sp["sheath"]
    hw = sp["wmax"] / FILL * 0.55
    col0 = sp["leaves"][0]["col"]
    u0, u1 = (col0 + 0.004) / N_COL, (col0 + 0.996) / N_COL
    G0 = anim.hash01(key) if key is not None else None
    if key is not None:
        acc.set_anim(len(acc.verts), [(0.0, G0, 0.0, 0.0)] * 3)  # 葉鞘は動かない
    acc.add([base, top + W * hw, top - W * hw], [(0, 1, 2)], [[((u0 + u1) / 2, 0.0), (u1, 0.07), (u0, 0.07)]])
    # 面外のなびきは流れの向きへ（2 割は逆向き＝渦や揺り戻し）
    sgn = 1.0 if Nf.dot(CURRENT) >= 0 else -1.0
    if sp["flip"]:
        sgn = -sgn
    n = len(sp["leaves"])
    for k, lf in enumerate(sp["leaves"]):
        rot = Matrix.Rotation(lf["fan"], 3, Nf)
        t_dir = tilt @ (rot @ UP)
        # 重なった葉の面がちらつかないよう、面の向きを少しずつ変えて厚み方向にずらす
        n_dir = tilt @ (Matrix.Rotation(rnd.uniform(-0.14, 0.14), 3, UP) @ Nf)
        start = top - axis * 0.006 + W * (lf["pos"] * hw * 0.45) + tilt @ Nf * ((k - (n - 1) / 2) * 0.0012)
        # 同じ株の葉は少しずつ位相をずらす
        _leaf(acc, start, t_dir, n_dir, lf["L"], lf["w"], lf["col"], sgn * lf["bend"], lf["twist"], lf["falc"],
              lf["segs"], v0=0.08, G=None if G0 is None else (G0 + 0.07 * k) % 1.0)


def _dist_polyline(x, y, pts):
    best = 1e9
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / max(dx * dx + dy * dy, 1e-9)))
        best = min(best, math.hypot(x - (ax + t * dx), y - (ay + t * dy)))
    return best


def _meander(start, heading, length, amp, wave, phase=0.0, step=0.05):
    """蛇行する食み跡の中心線"""
    d = Vector((math.cos(heading), math.sin(heading)))
    nrm = Vector((-d.y, d.x))
    out = []
    for i in range(int(length / step) + 1):
        s = i * step
        p = Vector(start) + d * s + nrm * (amp * math.sin(s / wave * math.tau + phase)
                                           + 0.35 * amp * math.sin(s / wave * 2.7 * math.tau + phase * 1.7))
        out.append((p.x, p.y))
    return out


def _noise2(x, y, z, s=1.0):
    return mnoise.noise(Vector((x * s, y * s, z)))


# ジュゴンの食み跡（幅 15〜20cm の砂の帯）
TRAILS_A = [_meander((-1.75, -0.55), math.radians(14), 2.7, 0.16, 1.15, 0.4),
            _meander((0.55, 1.05), math.radians(-72), 1.05, 0.08, 0.8, 1.3)]


def density_a(x, y):
    """Patch_A: 約 4 x 3 m の不定形。縁は疎らに、中にむらと食み跡"""
    ex, ey = x / 2.2, y / 1.55
    r = math.hypot(ex, ey)
    a = math.atan2(ey, ex)
    wob = 0.24 * _noise2(math.cos(a), math.sin(a), 7.1, 1.3) + 0.12 * _noise2(x, y, 2.3, 1.2)
    d = 1.0 - _sss(0.7, 1.0, r * (1.0 + wob))
    # 縁の外にちぎれた小さな株の群れ
    for (cx, cy, rr) in ((2.05, 0.75, 0.24), (1.0, -1.3, 0.2)):
        d = max(d, 0.7 * (1.0 - _sss(0.35, 1.0, math.hypot(x - cx, y - cy) / rr)))
    # 中の砂の穴（くっきり）とむら
    d *= _sss(-0.6, -0.4, _noise2(x, y, 4.4, 1.1)) * (0.8 + 0.2 * _sss(-0.3, 0.3, _noise2(x, y, 9.1, 2.3)))
    for tr in TRAILS_A:
        d *= _sss(0.075, 0.15, _dist_polyline(x, y, tr))
    return d


def density_b(x, y):
    """Patch_B: 約 1.5 m の株立ち"""
    r = math.hypot(x, y) / 0.72
    a = math.atan2(y, x)
    wob = 0.25 * _noise2(math.cos(a), math.sin(a), 3.7, 1.4) + 0.1 * _noise2(x, y, 5.5, 2.0)
    d = 1.0 - _sss(0.62, 1.0, r * (1.0 + wob))
    d = max(d, 0.7 * (1.0 - _sss(0.3, 1.0, math.hypot(x - 0.66, y + 0.42) / 0.18)))
    d *= 0.8 + 0.2 * _sss(-0.3, 0.3, _noise2(x, y, 8.8, 1.8))
    return d


def _rhizome(rnd, density, p, step=0.065, max_steps=11):
    """地下茎 1 本（枝分かれ込み）のランダムウォーク。密度に応じて株を出す"""
    stack = [(p, rnd.uniform(0, math.tau), max_steps)]
    out = []
    while stack:
        p, ang, steps = stack.pop()
        miss = 0
        for _ in range(steps):
            d = density(*p)
            if d < 0.03:
                miss += 1
                if miss > 2:
                    break
            elif rnd.random() < d ** 1.3:   # 芯ほど株が混む（縁は疎ら）
                out.append((p, d))
            if rnd.random() < 0.08:
                stack.append((p, ang + rnd.choice([-1, 1]) * rnd.uniform(0.7, 1.2), steps // 2))
            ang += rnd.gauss(0, 0.28)
            L = step * rnd.uniform(0.75, 1.25)
            p = (p[0] + math.cos(ang) * L, p[1] + math.sin(ang) * L)
    return out


def _start(rnd, density, bounds, starts, rmin):
    """地下茎の出発点: 密度に比例し、既存の出発点から rmin 以上離す（だめなら条件をゆるめる）"""
    x0, y0, x1, y1 = bounds
    for k in range(3000):
        p = (rnd.uniform(x0, x1), rnd.uniform(y0, y1))
        if rnd.random() >= density(*p) ** 1.3:
            continue
        r = rmin * (0.9 ** (k // 150))
        if all((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 >= r * r for q in starts):
            starts.append(p)
            return p
    return p


def patch(name, seed, density, bounds, budget):
    """地下茎を 1 本ずつ伸ばし、三角形数の予算（両面化後）に達するまで株を置く"""
    rnd = random.Random(seed)
    specs, tris, idle, starts = [], 0, 0, []
    while tris < budget - 12 and idle < 30:
        added = 0
        for p, d in _rhizome(rnd, density, _start(rnd, density, bounds, starts, 0.3)):
            # 葉の面は流れに向くもの（なびきやすい）が多い
            az = math.atan2(CURRENT.y, CURRENT.x) + math.pi / 2 + rnd.gauss(0, 0.9)
            sp = _shoot_spec(rnd, p, d, az)
            t = _shoot_tris(sp)
            if tris + t > budget:
                continue
            specs.append(sp)
            tris += t
            added += 1
        idle = 0 if added else idle + 1
    acc = MeshAcc()
    n_leaves = 0
    for i, sp in enumerate(specs):
        _build_shoot(acc, rnd, sp, key=(name, i))
        n_leaves += len(sp["leaves"])
    obj = acc.build(name)
    C.set_smooth(obj, True)
    C.assign(obj, leaf_material())
    print(f"  {name}: {len(specs)} shoots, {n_leaves} leaves, {tris} tris (double-sided)")
    return obj


# ---------------------------------------------------------------------------
# プレビュー
# ---------------------------------------------------------------------------
def _preview_extra(objs):
    """背の低いパッチなので寄って撮る。OKI_DEBUG_DIR があれば近景も撮る"""
    import bpy
    scene = bpy.context.scene
    cam = scene.camera
    meshes = [o for o in objs if o.type == "MESH"]
    mn, mx = C.bounds(meshes)
    tgt = Vector(((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, 0.0))
    cam.location = tgt + (cam.location - tgt) * 0.7
    C.look_at(cam, tgt)
    dbg = os.environ.get("OKI_DEBUG_DIR")
    if not dbg:
        return
    keep = (cam.location.copy(), cam.rotation_euler.copy(), cam.data.lens)
    for o in objs:
        if o.type != "MESH":
            continue
        mn, mx = C.bounds([o])
        c = (mn + mx) / 2
        tag = o.name.split(".")[0]
        for k, (off, tgt_off, lens) in enumerate([((0.25, -0.9, 0.32), (0.1, 0.0, 0.05), 40),
                                                  ((0.0, -0.05, 3.2), (0.0, 0.0, 0.0), 30)]):
            tgt = Vector((c.x + tgt_off[0], c.y + tgt_off[1], tgt_off[2]))
            cam.location = tgt + Vector(off) * (1.0 if "A" in tag[-1] else 0.55)
            cam.data.lens = lens
            C.look_at(cam, tgt)
            C.render(os.path.join(dbg, f"seagrass_{tag}_{k}.jpg"), res=(800, 450), samples=16)
    cam.location, cam.rotation_euler, cam.data.lens = keep


PREVIEW = dict(PREVIEW_CAM, extra=_preview_extra)


def build():
    _MAT.clear()
    return {
        "Seagrass_Patch_A": [patch("SeagrassA", 5, density_a, (-2.6, -1.9, 2.6, 1.9), 29400)],
        "Seagrass_Patch_B": [patch("SeagrassB", 9, density_b, (-1.0, -1.0, 1.0, 1.0), 9800)],
    }
