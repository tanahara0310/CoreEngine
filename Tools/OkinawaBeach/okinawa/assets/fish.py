"""熱帯魚の群れ（静止メッシュ）3 バリエーション

FishSchool_Blue : ルリスズメダイ（Chrysiptera cyanea）5〜7cm × 34 匹のゆるい群れ
                  （オスは尾びれが橙黄、メスは尾びれが淡い青で背びれ後端の付け根に黒点）
FishSchool_Green: デバスズメダイ（Chromis viridis）6〜8cm × 52 匹の密な群れ（枝サンゴの上でホバリング）
Fish_Butterfly  : トゲチョウチョウウオ（Chaetodon auriga）約 15cm のペア

魚 1 匹: 側扁した胴（断面 8 頂点のリングを吻端から尾柄まで繋ぐ）+ 背・臀・尾・胸・腹びれ
         （薄い板を表裏 2 枚。輪郭はアルファで切り抜く）。1 匹 約 220〜300 三角形
アトラス: 種ごとに 1 枚（上半分 = 胴の側面展開、下半分 = ひれ）を numpy で描いて焼く。
         胴は左右で同じ UV を共有し、V は断面の周長比。模様は実寸の側面座標 (s, z) で描くので
         目・鱗・縞が歪まない
群れ: 楕円体の雲にポアソン円盤で散らし、向きはなめらかなノイズ場で揃えつつばらつかせる。
      体はわずかに C 字・S 字に曲げ、大きさも変える
原点: 群れの中心（水中に浮かせて置く）
"""
import math
import os
import random

import numpy as np
from mathutils import Matrix, Vector
from mathutils import noise as mnoise
from scipy import ndimage
from scipy.interpolate import PchipInterpolator

from .. import common as C
from ..common import pbr_material, srgb
from .hibiscus import Canvas

RES_SCALE = float(os.environ.get("OKI_RES_SCALE", "1"))
ATLAS_RES = 1024
UP = Vector((0, 0, 1))

# アトラスの領域 (u0, v0, u1, v1)。背びれ・尾びれは 2 種（A=メス等 / B=オス等）
R_BODY = (0.01, 0.43, 0.99, 0.99)
R_DORSAL = ((0.01, 0.30, 0.60, 0.415), (0.01, 0.17, 0.60, 0.285))
R_CAUDAL = ((0.62, 0.17, 0.795, 0.415), (0.815, 0.17, 0.99, 0.415))
R_ANAL = (0.01, 0.015, 0.40, 0.155)
R_PECT = (0.42, 0.015, 0.60, 0.155)
R_PELV = (0.62, 0.015, 0.78, 0.155)
R_FIL = (0.80, 0.015, 0.84, 0.155)


# ---------------------------------------------------------------------------
# 種のデータ（長さの単位は標準体長 SL = 吻端〜尾柄）
#   prof: (s, 背縁 z, 腹縁 z, 半幅)  s = 0 吻端 → 1 尾柄
#   rings: 胴のリングの位置, th: 片側の断面頂点の角度（度、-90=腹 → 90=背）
#   ひれの shape: (σ, 高さ) σ = ひれの付け根に沿った 0..1。rake = 鰭条の後傾(ラジアン, 前端→後端)
# ---------------------------------------------------------------------------
CYANEA = dict(
    key="cyanea",
    prof=[(0.0, 0.004, -0.01, 0.0), (0.03, 0.045, -0.036, 0.026), (0.08, 0.094, -0.074, 0.05),
          (0.15, 0.143, -0.122, 0.069), (0.25, 0.186, -0.168, 0.082), (0.37, 0.204, -0.19, 0.086),
          (0.5, 0.198, -0.186, 0.081), (0.63, 0.17, -0.158, 0.069), (0.76, 0.13, -0.117, 0.052),
          (0.88, 0.094, -0.086, 0.036), (1.0, 0.078, -0.072, 0.024)],
    rings=[0.03, 0.08, 0.15, 0.25, 0.37, 0.5, 0.63, 0.76, 0.88, 1.0],
    th=(-48, -4, 46),
    eye=(0.118, 0.05, 0.048),
    scale=(0.036, 0.034),
    dorsal=dict(s0=0.27, s1=0.92, n=6, rake=(0.5, 0.9), spines=13, soft_at=0.56,
                shape=[(0, 0.02), (0.05, 0.1), (0.25, 0.112), (0.5, 0.12), (0.57, 0.13), (0.66, 0.165),
                       (0.8, 0.205), (0.89, 0.215), (0.95, 0.16), (1.0, 0.03)]),
    anal=dict(s0=0.6, s1=0.92, n=4, rake=(0.6, 0.9), spines=2, soft_at=0.18,
              shape=[(0, 0.02), (0.08, 0.1), (0.25, 0.13), (0.6, 0.175), (0.8, 0.185), (0.93, 0.13),
                     (1.0, 0.03)]),
    caudal=dict(len=0.3, span=0.5, fork=0.24, pointed=False),
    pect=dict(s=0.29, zf=0.44, len=0.23, w=0.13, spread=0.5),
    pelv=dict(s=0.31, len=0.2, w=0.075, drop=0.7),
    tl=(0.05, 0.07),
)

VIRIDIS = dict(
    key="viridis",
    prof=[(0.0, 0.006, -0.01, 0.0), (0.03, 0.05, -0.04, 0.027), (0.08, 0.11, -0.086, 0.054),
          (0.15, 0.17, -0.142, 0.073), (0.25, 0.224, -0.198, 0.087), (0.37, 0.248, -0.228, 0.091),
          (0.5, 0.24, -0.222, 0.087), (0.63, 0.204, -0.188, 0.074), (0.76, 0.15, -0.134, 0.055),
          (0.88, 0.1, -0.09, 0.037), (1.0, 0.074, -0.068, 0.024)],
    rings=[0.03, 0.08, 0.15, 0.25, 0.37, 0.5, 0.63, 0.76, 0.88, 1.0],
    th=(-48, -4, 46),
    eye=(0.122, 0.058, 0.052),
    scale=(0.038, 0.036),
    dorsal=dict(s0=0.28, s1=0.9, n=6, rake=(0.5, 0.95), spines=12, soft_at=0.55,
                shape=[(0, 0.02), (0.05, 0.11), (0.3, 0.13), (0.5, 0.135), (0.57, 0.15), (0.7, 0.2),
                       (0.83, 0.24), (0.9, 0.24), (0.96, 0.15), (1.0, 0.03)]),
    anal=dict(s0=0.6, s1=0.9, n=4, rake=(0.6, 0.95), spines=2, soft_at=0.18,
              shape=[(0, 0.02), (0.08, 0.11), (0.3, 0.15), (0.65, 0.2), (0.82, 0.21), (0.94, 0.13),
                     (1.0, 0.03)]),
    caudal=dict(len=0.38, span=0.64, fork=0.62, pointed=True),
    pect=dict(s=0.3, zf=0.45, len=0.25, w=0.13, spread=0.55),
    pelv=dict(s=0.32, len=0.22, w=0.075, drop=0.7),
    tl=(0.06, 0.08),
)

AURIGA = dict(
    key="auriga",
    prof=[(0.0, 0.012, -0.009, 0.0), (0.03, 0.03, -0.028, 0.014), (0.07, 0.052, -0.05, 0.024),
          (0.12, 0.092, -0.084, 0.036), (0.19, 0.166, -0.144, 0.053), (0.27, 0.244, -0.21, 0.066),
          (0.36, 0.296, -0.262, 0.075), (0.46, 0.314, -0.286, 0.077), (0.56, 0.302, -0.282, 0.073),
          (0.66, 0.262, -0.248, 0.064), (0.76, 0.2, -0.19, 0.05), (0.86, 0.13, -0.122, 0.035),
          (0.94, 0.092, -0.085, 0.025), (1.0, 0.076, -0.07, 0.02)],
    rings=[0.03, 0.07, 0.12, 0.19, 0.27, 0.36, 0.46, 0.56, 0.66, 0.76, 0.86, 0.94, 1.0],
    th=(-50, -4, 46),
    eye=(0.185, 0.066, 0.044),
    scale=(0.027, 0.029),
    dorsal=dict(s0=0.25, s1=0.95, n=8, rake=(0.55, 0.95), spines=13, soft_at=0.5,
                shape=[(0, 0.02), (0.05, 0.08), (0.2, 0.13), (0.4, 0.17), (0.5, 0.19), (0.62, 0.22),
                       (0.74, 0.23), (0.83, 0.21), (0.92, 0.14), (1.0, 0.03)]),
    anal=dict(s0=0.55, s1=0.95, n=5, rake=(0.55, 0.95), spines=3, soft_at=0.25,
              shape=[(0, 0.02), (0.08, 0.1), (0.3, 0.16), (0.6, 0.2), (0.78, 0.2), (0.92, 0.13),
                     (1.0, 0.03)]),
    caudal=dict(len=0.24, span=0.42, fork=0.0, pointed=False),
    pect=dict(s=0.33, zf=0.4, len=0.22, w=0.12, spread=0.45),
    pelv=dict(s=0.34, len=0.24, w=0.08, drop=0.75),
    filament=dict(at=0.78, len=0.34, w=0.028),
    tl=(0.14, 0.155),
)


def _prep(sp):
    """輪郭の補間と、断面の周長比（UV の V）⇔ 高さ z の対応表を作る"""
    if "_top" in sp:
        return sp
    P = np.array(sp["prof"], np.float64)
    sp["_top"] = PchipInterpolator(P[:, 0], P[:, 1])
    sp["_bot"] = PchipInterpolator(P[:, 0], P[:, 2])
    sp["_hw"] = PchipInterpolator(P[:, 0], P[:, 3])
    S = np.linspace(0, 1, 257)
    TH = np.linspace(-np.pi / 2, np.pi / 2, 241)
    Y, Z = _section(sp, S[:, None], TH[None, :])
    seg = np.hypot(np.diff(Y, axis=1), np.diff(Z, axis=1))
    arc = np.concatenate([np.zeros((len(S), 1)), np.cumsum(seg, axis=1)], 1)
    arc = arc / np.maximum(arc[:, -1:], 1e-12)
    arc[0] = arc[1]
    sp["_S"], sp["_TH"], sp["_arc"] = S, TH, arc
    # 逆引き: (s, v) → θ
    VG = np.linspace(0, 1, 241)
    sp["_VG"] = VG
    sp["_thv"] = np.stack([np.interp(VG, arc[i], TH) for i in range(len(S))])
    sp["TL"] = 1.0 + sp["caudal"]["len"]
    return sp


def _section(sp, s, th):
    """断面の点 (y, z)（SL 単位）。腹側はふくらみ、背側は細い"""
    top, bot, hw = sp["_top"](s), sp["_bot"](s), sp["_hw"](s)
    zc, hh = (top + bot) / 2, (top - bot) / 2
    c = np.abs(np.cos(th))
    c = np.where(th < 0, c ** 0.8, c ** 1.2)
    return hw * c, zc + hh * np.sin(th)


def _arc_at(sp, s, th):
    S, TH, arc = sp["_S"], sp["_TH"], sp["_arc"]
    f = s * (len(S) - 1)
    i = min(int(f), len(S) - 2)
    t = f - i
    return (1 - t) * np.interp(th, TH, arc[i]) + t * np.interp(th, TH, arc[i + 1])


def _body_coords(sp, s, v):
    """アトラスの胴の局所座標 (s, v) → 側面の高さ z と、面の法線の上向き成分 nz"""
    S, VG, thv = sp["_S"], sp["_VG"], sp["_thv"]
    ci = np.clip(s, 0, 1) * (len(S) - 1)
    cj = np.clip(v, 0, 1) * (len(VG) - 1)
    th = ndimage.map_coordinates(thv, [ci, cj], order=1, mode="nearest")
    _, z = _section(sp, s, th)
    # 断面の接線から法線の z 成分を求める
    y1, z1 = _section(sp, s, th - 0.01)
    y2, z2 = _section(sp, s, th + 0.01)
    ty, tz = y2 - y1, z2 - z1
    nz = -ty / np.maximum(np.hypot(ty, tz), 1e-9)
    return z, nz


def _outline(fin, sig):
    P = np.array(fin["shape"], np.float64)
    return np.interp(sig, P[:, 0], P[:, 1])


def _caudal_edge(cf, zeta):
    """尾びれの後縁（c 方向の長さ、0..1）。zeta = -1..1（下葉〜上葉）"""
    a = np.abs(zeta)
    if cf["pointed"]:
        e = 1.0 - cf["fork"] * np.clip(1.0 - a / 0.93, 0, 1) ** 1.15
        e = e * np.clip(1.0 - np.clip(a - 0.93, 0, None) / 0.07, 0, 1) ** 0.5
    elif cf["fork"] > 0:
        e = 1.0 - cf["fork"] * np.clip(1.0 - a / 0.8, 0, 1) ** 1.6
        e = e * np.sqrt(np.clip(1.0 - (np.clip(a - 0.72, 0, None) / 0.28) ** 2, 0, 1))
    else:
        e = 0.97 - 0.1 * a ** 2
        e = e * np.sqrt(np.clip(1.0 - (np.clip(a - 0.8, 0, None) / 0.2) ** 2, 0, 1))
    return e


def _caudal_spread(sp, c):
    """尾びれの上下の広がり（zeta の最大、0..1）。付け根は尾柄の高さ"""
    cf = sp["caudal"]
    ped = (sp["_top"](1.0) - sp["_bot"](1.0)) / cf["span"] * 0.92
    k = 0.8 if cf["pointed"] else 0.75
    return ped + (1.0 - ped) * np.clip(c / k, 0, 1) ** 0.75


# ---------------------------------------------------------------------------
# 1 匹分の形状（SL 単位、吻端 x=0、尾は -x、背 +z）
# ---------------------------------------------------------------------------
class _Mesh:
    def __init__(self):
        self.v, self.f, self.uv = [], [], []

    def add_v(self, p):
        self.v.append(tuple(p))
        return len(self.v) - 1

    def face(self, idx, uvs):
        self.f.append(tuple(idx))
        self.uv.append([tuple(u) for u in uvs])

    def grid2(self, rows, uvs):
        """表裏 2 枚の格子（ひれ）。rows[i][j] = 頂点"""
        for flip in (False, True):
            R = [r[::-1] for r in rows] if flip else rows
            U = [u[::-1] for u in uvs] if flip else uvs
            n, m = len(R), len(R[0])
            ids = [[self.add_v(p) for p in r] for r in R]
            for i in range(n - 1):
                for j in range(m - 1):
                    self.face((ids[i][j], ids[i][j + 1], ids[i + 1][j + 1], ids[i + 1][j]),
                              (U[i][j], U[i][j + 1], U[i + 1][j + 1], U[i + 1][j]))


def _ruv(rect, x, y):
    u0, v0, u1, v1 = rect
    return (u0 + (u1 - u0) * x, v0 + (v1 - v0) * y)


def _fin_env(fin, n):
    """ひれの板の外周（各節で前後の区間の輪郭の最大 + 余白）"""
    sig = np.linspace(0, 1, n)
    dense = np.linspace(0, 1, 201)
    out = _outline(fin, dense)
    env = []
    for k in range(n):
        a = sig[max(0, k - 1)]
        b = sig[min(n - 1, k + 1)]
        env.append(out[(dense >= a) & (dense <= b)].max() * 1.04 + 0.008)
    return sig, np.array(env)


def fish_mesh(sp, variant=0):
    """1 匹分の頂点・面・UV（variant: 背びれ・尾びれのアトラス領域 0/1）"""
    _prep(sp)
    M = _Mesh()
    top, bot = sp["_top"], sp["_bot"]
    th_side = [math.radians(t) for t in sp["th"]]
    ring_th = [-math.pi / 2] + th_side + [math.pi / 2] + th_side[::-1]
    ring_sd = [0, 1, 1, 1, 0, -1, -1, -1]
    m = len(ring_th)

    def pt(s, th, sd):
        y, z = _section(sp, s, th)
        return Vector((-s, sd * float(y), float(z)))

    def uv(s, th):
        return _ruv(R_BODY, s, float(_arc_at(sp, s, th)))

    # --- 胴 ---
    rings = []
    for s in sp["rings"]:
        rings.append([M.add_v(pt(s, th, sd)) for th, sd in zip(ring_th, ring_sd)])
    ruv = [[uv(s, th) for th in ring_th] for s in sp["rings"]]

    def axis_pt(p):
        s = min(1.0, max(0.0, -p.x))
        return Vector((p.x, 0.0, float((top(s) + bot(s)) / 2)))

    def oriented(ids, uvs, outward):
        a, b, c = (Vector(M.v[i]) for i in ids[:3])
        nrm = (b - a).cross(c - a)
        if len(ids) == 4:
            d = Vector(M.v[ids[3]])
            nrm = nrm + (c - a).cross(d - a)
        if nrm.dot(outward) < 0:
            return ids[::-1], uvs[::-1]
        return ids, uvs

    for i in range(len(rings) - 1):
        for j in range(m):
            k = (j + 1) % m
            ids = (rings[i][j], rings[i][k], rings[i + 1][k], rings[i + 1][j])
            uvs = (ruv[i][j], ruv[i][k], ruv[i + 1][k], ruv[i + 1][j])
            cen = sum((Vector(M.v[q]) for q in ids), Vector()) / 4
            M.face(*oriented(ids, uvs, cen - axis_pt(cen)))
    # 吻端（口）
    s0 = 0.0
    tip = M.add_v(Vector((0.0, 0.0, float((top(s0) + bot(s0)) / 2))))
    tip_uv = _ruv(R_BODY, 0.0, float(_arc_at(sp, 0.02, 0.0)))
    for j in range(m):
        k = (j + 1) % m
        M.face(*oriented((tip, rings[0][j], rings[0][k]), (tip_uv, ruv[0][j], ruv[0][k]), Vector((1, 0, 0))))
    # 尾柄の端
    cz = float((top(1.0) + bot(1.0)) / 2)
    end = M.add_v(Vector((-1.004, 0.0, cz)))
    end_uv = _ruv(R_BODY, 1.0, float(_arc_at(sp, 1.0, 0.0)))
    for j in range(m):
        k = (j + 1) % m
        M.face(*oriented((end, rings[-1][j], rings[-1][k]), (end_uv, ruv[-1][j], ruv[-1][k]), Vector((-1, 0, 0))))

    # --- 背びれ・臀びれ ---
    def median_fin(fin, rect, dorsal):
        sig, env = _fin_env(fin, fin["n"])
        href = env.max()
        rows, uvs = [], []
        for k, sg in enumerate(sig):
            s = fin["s0"] + sg * (fin["s1"] - fin["s0"])
            rk = fin["rake"][0] + (fin["rake"][1] - fin["rake"][0]) * sg
            if dorsal:
                b = Vector((-s, 0.0, float(top(s)) - 0.006))
                d = Vector((-math.sin(rk), 0.0, math.cos(rk)))
            else:
                b = Vector((-s, 0.0, float(bot(s)) + 0.006))
                d = Vector((-math.sin(rk), 0.0, -math.cos(rk)))
            rows.append([b, b + d * env[k]])
            uvs.append([_ruv(rect, sg, 0.0), _ruv(rect, sg, env[k] / href)])
        M.grid2(rows, uvs)
        return href

    sp["_href_d"] = median_fin(sp["dorsal"], R_DORSAL[variant], True)
    sp["_href_a"] = median_fin(sp["anal"], R_ANAL, False)

    # --- 尾びれ ---
    cf = sp["caudal"]
    rect = R_CAUDAL[variant]
    rows, uvs = [], []
    cs = [0.0, 0.45, 1.0]
    dense = np.linspace(0, 1, 101)
    spread = _caudal_spread(sp, dense)
    for k, c in enumerate(cs):
        a = cs[max(0, k - 1)]
        b = cs[min(len(cs) - 1, k + 1)]
        zm = min(1.0, spread[(dense >= a) & (dense <= b)].max() * 1.03 + 0.02)
        x = -0.97 - c * cf["len"]
        rows.append([Vector((x, 0.0, cz + zz * zm * cf["span"] / 2)) for zz in (-1.0, 0.0, 1.0)])
        uvs.append([_ruv(rect, c, 0.5 + zz * zm * 0.5) for zz in (-1.0, 0.0, 1.0)])
    M.grid2(rows, uvs)

    # --- 胸びれ・腹びれ（左右） ---
    pf, vf = sp["pect"], sp["pelv"]
    for sd in (1, -1):
        s = pf["s"]
        z = float(bot(s) + (top(s) - bot(s)) * pf["zf"])
        zc, hh = float((top(s) + bot(s)) / 2), float((top(s) - bot(s)) / 2)
        th = math.asin(max(-1.0, min(1.0, (z - zc) / hh)))
        y, _ = _section(sp, s, th)
        A = Vector((-s, sd * (float(y) - 0.006), z))
        D = Vector((-math.cos(pf["spread"]), sd * math.sin(pf["spread"]), -0.12)).normalized()
        E = (UP - D * UP.dot(D)).normalized()
        E = Matrix.Rotation(sd * 0.35, 3, D) @ E  # 付け根はやや斜め
        rows = [[A - E * pf["w"] / 2, A + E * pf["w"] / 2],
                [A - E * pf["w"] / 2 + D * pf["len"], A + E * pf["w"] / 2 + D * pf["len"]]]
        uvs = [[_ruv(R_PECT, 0, 0), _ruv(R_PECT, 0, 1)], [_ruv(R_PECT, 1, 0), _ruv(R_PECT, 1, 1)]]
        M.grid2(rows, uvs)
        s = vf["s"]
        A = Vector((-s, sd * 0.012, float(bot(s)) + 0.014))
        D = Vector((-math.cos(vf["drop"]), sd * 0.22, -math.sin(vf["drop"]))).normalized()
        E = D.cross(Vector((0.0, sd, 0.0))).normalized()
        if E.z < 0:
            E = -E
        rows = [[A - E * vf["w"] * 0.2, A + E * vf["w"] * 0.8],
                [A - E * vf["w"] * 0.2 + D * vf["len"], A + E * vf["w"] * 0.8 + D * vf["len"]]]
        uvs = [[_ruv(R_PELV, 0, 0), _ruv(R_PELV, 0, 1)], [_ruv(R_PELV, 1, 0), _ruv(R_PELV, 1, 1)]]
        M.grid2(rows, uvs)

    # --- 背びれの糸状の軟条（トゲチョウチョウウオ） ---
    fl = sp.get("filament")
    if fl:
        fin = sp["dorsal"]
        sg = fl["at"]
        s = fin["s0"] + sg * (fin["s1"] - fin["s0"])
        rk = fin["rake"][0] + (fin["rake"][1] - fin["rake"][0]) * sg
        d = Vector((-math.sin(rk), 0.0, math.cos(rk)))
        P0 = Vector((-s, 0.0, float(top(s)) - 0.006)) + d * float(_outline(fin, sg)) * 0.92
        pts = [P0]
        dirv = (d * 0.35 + Vector((-1, 0, 0)) * 0.65).normalized()
        for i in range(3):
            dirv = (Matrix.Rotation(-0.22, 3, Vector((0, 1, 0))) @ dirv).normalized()
            pts.append(pts[-1] + dirv * fl["len"] / 3)
        rows, uvs = [], []
        for i, p in enumerate(pts):
            t = (pts[min(i + 1, 3)] - pts[max(i - 1, 0)]).normalized()
            w = Vector((0, 1, 0)).cross(t).normalized() * fl["w"] / 2
            rows.append([p - w, p + w])
            uvs.append([_ruv(R_FIL, 0, i / 3), _ruv(R_FIL, 1, i / 3)])
        M.grid2(rows, uvs)
    return M


# ---------------------------------------------------------------------------
# アトラスの描画
# ---------------------------------------------------------------------------
def _ss(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def _lin(h):
    return np.array(srgb(h), np.float32)


def _mix(a, b, t):
    t = np.asarray(t, np.float32)[..., None]
    return a * (1 - t) + b * t


def _grid(cv, rect):
    n = cv.n
    u0, v0, u1, v1 = rect
    j0, j1 = int(math.floor(u0 * n)), int(math.ceil(u1 * n))
    i0, i1 = int(math.floor(v0 * n)), int(math.ceil(v1 * n))
    X, Y = np.meshgrid((np.arange(j0, j1) + 0.5) / n, (np.arange(i0, i1) + 0.5) / n)
    return (slice(i0, i1), slice(j0, j1)), (X - u0) / (u1 - u0), (Y - v0) / (v1 - v0), \
        ((u1 - u0) * n, (v1 - v0) * n)


def _put(cv, sl, col, alpha, h, r, cav=1.0):
    a = np.clip(alpha, 0, 1).astype(np.float32)
    cv.col[sl] = np.where(a[..., None] > 0, col, cv.col[sl])
    cv.a[sl] = np.maximum(cv.a[sl], a)
    cv.h[sl] = np.where(a > 0, h, cv.h[sl])
    cv.r[sl] = np.where(a > 0, r, cv.r[sl])
    cv.cav[sl] = np.where(a > 0, cav, cv.cav[sl])


def _scales(S, Z, sx, sz):
    """瓦状に重なった鱗（前の鱗の後縁が後ろの鱗に重なる）

    戻り値 (高さ 0..1: 鱗の後縁へ向かって盛り上がる, 縁 0..1: 後縁の弧, 影 0..1: 後縁のすぐ後ろ)
    """
    R = 0.72
    best = np.full(S.shape, np.inf)
    dsv = np.zeros(S.shape)
    qv = np.ones(S.shape)
    cand = []
    row = np.floor(Z / sz)
    for dr in (-1, 0, 1):
        r = row + dr
        off = np.where(np.mod(r, 2) == 0, 0.0, 0.5)
        cz = (r + 0.5) * sz
        k = np.floor(S / sx - off)
        for dk in (-2, -1, 0, 1):
            cs = (k + dk + off + 0.5) * sx
            ds = (S - cs) / (R * sx)
            q = np.hypot(ds, (Z - cz) / (0.62 * sz))
            cand.append((cs, q))
            take = (q < 1.0) & (cs < best)
            best = np.where(take, cs, best)
            dsv = np.where(take, ds, dsv)
            qv = np.where(take, q, qv)
    # 見えている鱗より前にある鱗の後縁からの距離（すぐ後ろは影になる）
    gap = np.full(S.shape, 9.0)
    for cs, q in cand:
        gap = np.where(cs < best, np.minimum(gap, q - 1.0), gap)
    h = np.clip(0.5 + 0.5 * dsv, 0, 1) * (1.0 - _ss(0.9, 1.0, qv))
    rim = _ss(0.8, 0.97, qv) * (dsv > 0)
    shadow = _ss(0.22, 0.0, gap)
    return h, rim, shadow


def _eye(S, Z, eye):
    s, z, r = eye
    return np.hypot(S - s, Z - z) / r


def _seg_dist(S, Z, a, b):
    ax, az = a
    bx, bz = b
    dx, dz = bx - ax, bz - az
    t = np.clip(((S - ax) * dx + (Z - az) * dz) / (dx * dx + dz * dz), 0, 1)
    return np.hypot(S - (ax + t * dx), Z - (az + t * dz))


def _rays(sig, soft_at, spines, n_soft, length):
    """鰭条までの距離（SL）。棘条は付け根〜soft_at に等間隔、軟条はその後ろ"""
    sp_pos = (np.arange(spines) + 0.5) / spines * soft_at
    so_pos = soft_at + (np.arange(n_soft) + 0.5) / n_soft * (1 - soft_at)
    pos = np.concatenate([sp_pos, so_pos])
    d = np.min(np.abs(sig[..., None] - pos[None, None, :]), axis=-1) * length
    spine = sig < soft_at
    # 棘の間の膜の切れ込み（0 = 棘、1 = 棘の中間）
    gap = soft_at / max(spines, 1)
    notch = np.where(spine, np.abs(np.mod(sig / gap, 1.0) - 0.5) * 2, 0.0)
    return d, spine, 1.0 - notch


def _median_fin(cv, sp, fin, rect, href, paint):
    """背びれ・臀びれ。paint(sig, H, F, ray, spine) → (col, rough)"""
    sl, SG, ET, (pw, ph) = _grid(cv, rect)
    H = ET * href
    F = _outline(fin, SG)
    length = fin["s1"] - fin["s0"]
    d, spine, notch = _rays(SG, fin["soft_at"], fin["spines"], int(12 + 10 * (1 - fin["soft_at"])), length)
    # 棘の間は膜が切れ込み、棘の先が突き出る
    Fm = F * np.where(spine, 1.0 - 0.22 * notch ** 1.5, 1.0)
    ray = np.exp(-(d / np.where(spine, 0.0045, 0.003)) ** 2)
    Fe = np.maximum(Fm, F * ray * spine)
    px = href / ph
    alpha = np.clip((Fe - H) / px + 0.5, 0, 1) * np.clip(SG * pw + 0.5, 0, 1) * np.clip((1 - SG) * pw + 0.5, 0, 1)
    col, rough = paint(SG, H, F, ray, spine)
    h = 0.35 + 0.5 * ray - 0.15 * _ss(0.7, 1.0, H / np.maximum(F, 1e-4))
    _put(cv, sl, col, alpha, h, rough, 1.0 - 0.15 * _ss(0.04, 0.0, H))


def _caudal(cv, sp, rect, paint):
    cf = sp["caudal"]
    sl, CU, VV, (pw, ph) = _grid(cv, rect)
    zeta = (VV - 0.5) * 2
    edge = _caudal_edge(cf, zeta)
    spread = _caudal_spread(sp, CU)
    px_c = 1.0 / pw
    px_z = 2.0 / ph
    alpha = np.clip((edge - CU) / px_c + 0.5, 0, 1) * np.clip((spread - np.abs(zeta)) / px_z + 0.5, 0, 1) \
        * np.clip(CU / px_c + 0.5, 0, 1)
    # 鰭条: 付け根の中心から扇状
    ang = np.arctan2(zeta * cf["span"] / 2, CU * cf["len"] + 0.06)
    ray = np.exp(-((np.mod(ang * 9.5 / (np.pi / 2), 1.0) - 0.5) / 0.14) ** 2) * _ss(0.0, 0.15, CU)
    col, rough = paint(CU, zeta, edge, ray)
    h = 0.35 + 0.45 * ray
    _put(cv, sl, col, alpha, h, rough, 1.0 - 0.12 * _ss(0.12, 0.0, CU))


def _paddle(cv, rect, paint, kind):
    """胸びれ（丸いうちわ形）/ 腹びれ（細く尖る、前縁に棘）。局所座標 a: 付け根→先, b: 横"""
    sl, A, B, (pw, ph) = _grid(cv, rect)
    if kind == "pect":
        hw = 0.13 + 0.36 * np.sqrt(np.clip(A, 0, 1))
        tipc = 0.97 * np.sqrt(np.clip(1.0 - ((B - 0.55) / 0.55) ** 4, 0, 1))
        inside_d = np.minimum((hw - np.abs(B - 0.5)) * ph, (tipc - A) * pw)
        ang = np.arctan2(B - 0.5, A + 0.08)
        ray = np.exp(-((np.mod(ang * 16 / np.pi, 1.0) - 0.5) / 0.16) ** 2) * _ss(0.02, 0.2, A)
    else:
        # 前縁（b 大）が棘でまっすぐ、後縁へ細く尖る
        lead = 0.93 - 0.2 * A
        trail = 0.2 + 0.72 * A ** 0.8
        inside_d = np.minimum((lead - B) * ph, (B - trail) * ph)
        inside_d = np.minimum(inside_d, (0.99 - A) * pw)
        ang = np.arctan2(B - 0.75, A + 0.05)
        ray = np.exp(-((np.mod(ang * 7 / np.pi, 1.0) - 0.5) / 0.18) ** 2)
    alpha = np.clip(inside_d + 0.5, 0, 1) * np.clip(A * pw + 0.5, 0, 1)
    col, rough = paint(A, B, ray)
    _put(cv, sl, col, alpha, 0.35 + 0.4 * ray, rough, 1.0 - 0.15 * _ss(0.15, 0.0, A))


def _finish(cv):
    """透明部分の色・高さを最寄りの不透明部で埋める（ミップのにじみ・縁の法線対策）"""
    mask = cv.a > 0.5
    _, (iy, ix) = ndimage.distance_transform_edt(~mask, return_indices=True)
    cv.col = cv.col[iy, ix]
    cv.h = cv.h[iy, ix]
    cv.r = cv.r[iy, ix]
    cv.cav = cv.cav[iy, ix]


def _body_grid(cv, sp):
    sl, S, V, _ = _grid(cv, R_BODY)
    S = np.clip(S, 0.0, 1.0)
    V = np.clip(V, 0.0, 1.0)
    Z, NZ = _body_coords(sp, S, V)
    return sl, S, V, Z, NZ


def _eye_paint(col, h, r, cav, S, Z, eye, iris, ring):
    q = _eye(S, Z, eye)
    e = _ss(1.05, 0.95, q)
    pupil = _ss(0.6, 0.52, q)
    ic = _mix(iris, ring, _ss(0.62, 0.9, q))
    ic = _mix(ic, _lin("#050608"), pupil)
    ic = _mix(ic, _lin("#101418"), _ss(0.88, 1.0, q) * 0.8)
    col = _mix(col, ic, e)
    h = h * (1 - e) + (0.55 + 0.45 * np.sqrt(np.clip(1 - q * q, 0, 1))) * e
    r = r * (1 - e) + 0.08 * e
    cav = cav * (1 - 0.25 * _ss(1.35, 1.0, q) * (1 - e))
    return col, h, r, cav


def _common_body(sp, S, Z, NZ, col, rough=0.35, scale_amt=1.0):
    """鱗・鰓蓋・口・目の周りの陰影（色 col に重ねる）"""
    sx, sz = sp["scale"]
    # 頭部（鰓蓋より前）は鱗が細かく目立たない
    sh, rim, shade = _scales(S, Z, sx, sz)
    body = _ss(0.2, 0.27, S) * _ss(0.99, 0.9, S)
    body = body * scale_amt
    sh, rim, shade = sh * body + 0.5 * (1 - body), rim * body, shade * body
    col = col * (1.0 + 0.035 * (sh - 0.5) + 0.04 * rim - 0.07 * shade)[..., None]
    h = 0.35 + 0.3 * sh - 0.12 * shade
    r = rough - 0.06 * rim
    cav = 1.0 - 0.1 * shade
    # 鰓蓋の後縁（弧）
    top, bot = sp["_top"](S), sp["_bot"](S)
    zc = (top + bot) / 2
    hh = np.maximum((top - bot) / 2, 1e-4)
    s_op = sp["eye"][0] + 0.12 + 0.05 * ((Z - zc) / hh) ** 2
    op = np.exp(-((S - s_op) / 0.006) ** 2) * _ss(0.95, 0.6, np.abs(Z - zc) / hh)
    col = col * (1.0 - 0.28 * op)[..., None]
    h = h - 0.25 * op + 0.12 * _ss(s_op, s_op - 0.02, S) * _ss(0.9, 0.5, np.abs(Z - zc) / hh)
    cav = cav * (1.0 - 0.25 * op)
    # 口
    mouth = _ss(0.035, 0.0, S) * np.exp(-((Z - float(sp["_top"](0.0) + sp["_bot"](0.0)) / 2) / 0.006) ** 2)
    col = col * (1.0 - 0.5 * mouth)[..., None]
    return col, h, r, cav


# --- ルリスズメダイ ----------------------------------------------------------
def paint_cyanea(cv, sp):
    rng = np.random.default_rng(3)
    sl, S, V, Z, NZ = _body_grid(cv, sp)
    t = _ss(-0.75, 0.95, NZ)
    col = _mix(_lin("#5aa2ff"), _lin("#2b7dff"), _ss(0.0, 0.5, t))
    col = _mix(col, _lin("#1756e6"), _ss(0.5, 1.0, t))
    # 鱗ごとのきらめき（わずかな色相差）
    sheen = ndimage.zoom(rng.standard_normal((24, 60)), (col.shape[0] / 24, col.shape[1] / 60), order=1)
    sheen = sheen[:col.shape[0], :col.shape[1]]
    col = _mix(col, _lin("#3fb0ff"), np.clip(sheen, 0, 1) * 0.25)
    col, h, r, cav = _common_body(sp, S, Z, NZ, col, 0.3)
    # 吻端から目を通る黒い線
    e = sp["eye"]
    zm = float((sp["_top"](0.0) + sp["_bot"](0.0)) / 2)
    line = _ss(0.012, 0.006, _seg_dist(S, Z, (0.0, zm + 0.012), (e[0] + e[2] * 1.6, e[1] + 0.004)))
    col = _mix(col, _lin("#0a1a4a"), line * 0.9)
    col, h, r, cav = _eye_paint(col, h, r, cav, S, Z, e, _lin("#0e1e4a"), _lin("#2656b0"))
    _put(cv, sl, col, np.ones_like(S), h, r, cav)

    def fin_col(spot):
        def paint(sig, H, F, ray, spine):
            q = H / np.maximum(F, 1e-4)
            c = _mix(_lin("#2c78ff"), _lin("#1d5ae8"), _ss(0.2, 0.7, q))
            c = _mix(c, _lin("#08183c"), np.exp(-((q - 0.86) / 0.06) ** 2) * 0.85)   # 縁の内側の黒い線
            c = _mix(c, _lin("#9ccaff"), _ss(0.92, 1.0, q) * 0.8)                    # 薄く透ける縁
            c = _mix(c, _lin("#1846b8"), ray * 0.4)
            if spot:
                d = np.hypot((sig - 0.9) * (fin["s1"] - fin["s0"]), H - 0.035) / 0.03
                c = _mix(c, _lin("#05070c"), _ss(1.0, 0.8, d))
                c = _mix(c, _lin("#5aa8ff"), np.exp(-((d - 1.1) / 0.12) ** 2) * 0.6)
            return c, 0.35 + 0.1 * _ss(0.8, 1.0, q)
        return paint

    fin = sp["dorsal"]
    _median_fin(cv, sp, fin, R_DORSAL[0], sp["_href_d"], fin_col(True))
    _median_fin(cv, sp, fin, R_DORSAL[1], sp["_href_d"], fin_col(False))
    fin = sp["anal"]
    _median_fin(cv, sp, fin, R_ANAL, sp["_href_a"], fin_col(False))

    def tail(c0, c1, c2):
        def paint(cu, z, edge, ray):
            q = cu / np.maximum(edge, 1e-4)
            c = _mix(c0, c1, _ss(0.1, 0.9, q))
            c = _mix(c, c2, _ss(0.85, 1.0, q) * 0.7)
            c = c * (1.0 - 0.18 * ray)[..., None]
            return c, 0.4
        return paint

    _caudal(cv, sp, R_CAUDAL[0], tail(_lin("#3f86ff"), _lin("#8cc0ff"), _lin("#cfe6ff")))   # メス: 淡い青
    _caudal(cv, sp, R_CAUDAL[1], tail(_lin("#ff8414"), _lin("#ffb02a"), _lin("#ffd470")))   # オス: 橙黄

    def pect(a, b, ray):
        c = _mix(_lin("#5c9cff"), _lin("#a6caff"), _ss(0.0, 0.8, a))
        return c * (1.0 - 0.15 * ray)[..., None], 0.4

    def pelv(a, b, ray):
        c = _mix(_lin("#2a70ff"), _lin("#8fc4ff"), _ss(0.3, 1.0, a))
        c = _mix(c, _lin("#dff0ff"), _ss(0.8, 0.9, b) * 0.7)   # 前縁の棘は白っぽい
        return c, 0.4

    _paddle(cv, R_PECT, pect, "pect")
    _paddle(cv, R_PELV, pelv, "pelv")


# --- デバスズメダイ ----------------------------------------------------------
def paint_viridis(cv, sp):
    rng = np.random.default_rng(5)
    sl, S, V, Z, NZ = _body_grid(cv, sp)
    t = _ss(-0.75, 0.95, NZ)
    col = _mix(_lin("#bcf2d8"), _lin("#62d8ae"), _ss(0.0, 0.5, t))
    col = _mix(col, _lin("#1fa29c"), _ss(0.5, 1.0, t))
    sheen = ndimage.zoom(rng.standard_normal((24, 60)), (col.shape[0] / 24, col.shape[1] / 60), order=1)
    sheen = sheen[:col.shape[0], :col.shape[1]]
    col = _mix(col, _lin("#9df0c8"), np.clip(sheen, 0, 1) * 0.3)
    col = _mix(col, _lin("#5ab8e8"), np.clip(-sheen, 0, 1) * 0.2)
    col, h, r, cav = _common_body(sp, S, Z, NZ, col, 0.3)
    e = sp["eye"]
    zm = float((sp["_top"](0.0) + sp["_bot"](0.0)) / 2)
    # 吻端から目への青い線
    line = _ss(0.01, 0.004, _seg_dist(S, Z, (0.0, zm + 0.014), (e[0] - e[2] * 0.9, e[1] + 0.006)))
    col = _mix(col, _lin("#2c8ee0"), line * 0.8)
    col, h, r, cav = _eye_paint(col, h, r, cav, S, Z, e, _lin("#2a3a36"), _lin("#9cc8b4"))
    _put(cv, sl, col, np.ones_like(S), h, r, cav)

    def fin_col(tint):
        def paint(sig, H, F, ray, spine):
            q = H / np.maximum(F, 1e-4)
            c = _mix(_lin("#8fe0d2"), _lin("#b8f0e6"), _ss(0.3, 1.0, q))
            c = _mix(c, tint, _ss(0.85, 1.0, q) * 0.5)
            c = c * (1.0 - 0.12 * ray)[..., None]
            return c, 0.4
        return paint

    _median_fin(cv, sp, sp["dorsal"], R_DORSAL[0], sp["_href_d"], fin_col(_lin("#58b0e8")))
    _median_fin(cv, sp, sp["dorsal"], R_DORSAL[1], sp["_href_d"], fin_col(_lin("#c8f8ec")))
    _median_fin(cv, sp, sp["anal"], R_ANAL, sp["_href_a"], fin_col(_lin("#58b0e8")))

    def tail(tint):
        def paint(cu, z, edge, ray):
            q = cu / np.maximum(edge, 1e-4)
            c = _mix(_lin("#86dccc"), _lin("#c2f2ea"), _ss(0.2, 1.0, q))
            c = _mix(c, tint, _ss(0.8, 1.0, np.abs(z)) * 0.5)   # 上下の葉の縁
            return c * (1.0 - 0.14 * ray)[..., None], 0.4
        return paint

    _caudal(cv, sp, R_CAUDAL[0], tail(_lin("#4aa6e0")))
    _caudal(cv, sp, R_CAUDAL[1], tail(_lin("#d8fff2")))

    def pect(a, b, ray):
        c = _mix(_lin("#a8e8dc"), _lin("#dcf8f2"), _ss(0.0, 0.8, a))
        return c * (1.0 - 0.12 * ray)[..., None], 0.4

    def pelv(a, b, ray):
        c = _mix(_lin("#8ad8c8"), _lin("#d8f6ee"), _ss(0.3, 1.0, a))
        return c, 0.4

    _paddle(cv, R_PECT, pect, "pect")
    _paddle(cv, R_PELV, pelv, "pelv")


# --- トゲチョウチョウウオ ------------------------------------------------------
def _chevrons(S, Z):
    """直交する 2 組の細い斜線。上側は尾へ向かって上がり、下側は下がる（頭へ向いた「く」の字）"""
    # 2 組の境目（「く」の頂点が並ぶ線）: 胸びれの付け根の上から尾柄へ、ゆるく上がる
    zb = -0.035 + 0.1 * (S - 0.3)
    side = Z - zb                                     # >0: 上側
    out = np.zeros(S.shape)
    for phi, sp_, sel in ((math.radians(50), 0.042, side > 0), (math.radians(-40), 0.038, side <= 0)):
        nx, nz = -math.sin(phi), math.cos(phi)
        q = (S * nx + Z * nz) / sp_
        d = np.abs(q - np.round(q)) * sp_
        out = np.where(sel, _ss(0.005, 0.0027, d), out)
    return out * _ss(0.0, 0.01, np.abs(side))


def paint_auriga(cv, sp):
    sl, S, V, Z, NZ = _body_grid(cv, sp)
    white = _lin("#f4f3ec")
    col = _mix(_lin("#fbfaf4"), white, _ss(-0.5, 0.8, NZ))
    col = _mix(col, _lin("#dcdad2"), _ss(0.4, 1.0, NZ) * 0.35)
    # 後半部の黄色（背びれの軟条部の付け根から臀びれの前へ斜めに境界）
    sb = 0.665 - 0.38 * Z
    yel = _ss(sb - 0.03, sb + 0.07, S)
    col = _mix(col, _lin("#ffc40c"), yel)
    col = _mix(col, _lin("#ffa800"), yel * _ss(0.8, 1.0, S) * 0.5)
    chev = _chevrons(S, Z) * _ss(0.24, 0.3, S)
    col = _mix(col, _lin("#34322c"), chev * (0.85 - 0.45 * yel))
    col, h, r, cav = _common_body(sp, S, Z, NZ, col, 0.35, scale_amt=0.45)
    # 頭部: 吻は灰白色、目の帯は黒（背から喉まで、上端がやや後ろ）
    e = sp["eye"]
    band_c = e[0] + 0.1 * (Z - e[1])
    band = _ss(0.052, 0.042, np.abs(S - band_c))
    col = _mix(col, _lin("#d9d6cc"), _ss(0.16, 0.05, S) * 0.6)
    col = _mix(col, _lin("#111111"), band)
    col, h, r, cav = _eye_paint(col, h, r, cav, S, Z, e, _lin("#1a1a1a"), _lin("#4a4a44"))
    _put(cv, sl, col, np.ones_like(S), h, r, cav)

    fin = sp["dorsal"]
    L = fin["s1"] - fin["s0"]

    def dorsal(sig, H, F, ray, spine):
        q = H / np.maximum(F, 1e-4)
        soft = _ss(fin["soft_at"] - 0.06, fin["soft_at"] + 0.04, sig)
        c = _mix(_lin("#f2f1ea"), _lin("#ffc40c"), soft)
        c = _mix(c, _lin("#34322c"), (1 - soft) * ray * 0.3)
        c = _mix(c, _lin("#1c1a16"), np.exp(-((q - 0.9) / 0.05) ** 2) * soft * 0.7)
        c = _mix(c, _lin("#fffbe8"), _ss(0.94, 1.0, q) * 0.8)
        # 軟条部の黒い眼状斑（白い縁取り）
        d = np.hypot((sig - 0.8) * L, H - 0.12) / 0.028
        c = _mix(c, _lin("#fff6d8"), _ss(1.45, 1.2, d))
        c = _mix(c, _lin("#0a0a0a"), _ss(1.05, 0.9, d))
        return c, 0.4

    _median_fin(cv, sp, fin, R_DORSAL[0], sp["_href_d"], dorsal)
    _median_fin(cv, sp, fin, R_DORSAL[1], sp["_href_d"], dorsal)

    def anal(sig, H, F, ray, spine):
        q = H / np.maximum(F, 1e-4)
        c = _mix(_lin("#f2f1ea"), _lin("#ffbc0a"), _ss(0.05, 0.3, sig))
        c = _mix(c, _lin("#1c1a16"), np.exp(-((q - 0.88) / 0.05) ** 2) * 0.75)
        c = _mix(c, _lin("#fffbe8"), _ss(0.93, 1.0, q) * 0.85)
        return c * (1.0 - 0.1 * ray)[..., None], 0.4

    _median_fin(cv, sp, sp["anal"], R_ANAL, sp["_href_a"], anal)

    def tail(cu, z, edge, ray):
        q = cu / np.maximum(edge, 1e-4)
        c = _mix(_lin("#ffbc0a"), _lin("#ffcc3a"), _ss(0.2, 0.6, q))
        c = _mix(c, _lin("#1a1a16"), np.exp(-((q - 0.74) / 0.05) ** 2) * 0.9)   # 黒い帯
        c = _mix(c, _lin("#eef2ee"), _ss(0.8, 0.9, q))                          # 透ける白い縁
        return c * (1.0 - 0.1 * ray)[..., None], 0.4

    _caudal(cv, sp, R_CAUDAL[0], tail)
    _caudal(cv, sp, R_CAUDAL[1], tail)

    def pect(a, b, ray):
        c = _mix(_lin("#e8ecea"), _lin("#f6f8f6"), _ss(0.0, 0.8, a))
        return c * (1.0 - 0.1 * ray)[..., None], 0.4

    def pelv(a, b, ray):
        return _mix(_lin("#f4f3ee"), _lin("#fffdf4"), _ss(0.3, 1.0, a)), 0.4

    _paddle(cv, R_PECT, pect, "pect")
    _paddle(cv, R_PELV, pelv, "pelv")
    # 糸状の軟条: 付け根は黄、先は白く細る
    sl, U, Vf, (pw, ph) = _grid(cv, R_FIL)
    hw = 0.45 * (1.0 - 0.75 * np.clip(Vf, 0, 1))
    alpha = np.clip((hw - np.abs(U - 0.5)) * pw + 0.5, 0, 1) * np.clip((0.99 - Vf) * ph + 0.5, 0, 1)
    c = _mix(_lin("#ffc40c"), _lin("#fff4d0"), _ss(0.2, 0.9, Vf))
    _put(cv, sl, c, alpha, np.full(U.shape, 0.5), 0.4)


PAINTERS = {"cyanea": paint_cyanea, "viridis": paint_viridis, "auriga": paint_auriga}


def atlas_material(sp, name):
    """種ごとのアトラス（片面のマテリアル。ひれは形状側で表裏 2 枚にしてある）"""
    n = max(64, int(ATLAS_RES * RES_SCALE))
    cv = Canvas(n)
    cv.r[:] = 0.4
    PAINTERS[sp["key"]](cv, sp)
    cv.h = np.clip(cv.h, 0, 1)
    _finish(cv)
    ic, iahc, ir = cv.images(name)
    tl = sp["tl"][1]

    def fn(nb):
        uv = nb.uv("Proc")
        col = nb.image(ic, uv, interp="Closest", ext="CLIP")["Color"]
        # 高さは Linear でないとバンプの微分が 0 になる
        ahc = nb.sep(nb.image(iahc, uv, interp="Linear", ext="CLIP")["Color"])
        rough = nb.sep(nb.image(ir, uv, interp="Closest", ext="CLIP")["Color"])[0]
        return dict(color=col, alpha=ahc[0], height=ahc[1], height_scale=tl * 0.004, rough=rough,
                    cavity=ahc[2])

    return pbr_material(name, fn, res=ATLAS_RES, uv="atlas", double_sided=False, atlas_size=(tl, tl))


# ---------------------------------------------------------------------------
# 群れ
# ---------------------------------------------------------------------------
def _cloud(rnd, n, radii, dmin, weight=None):
    """楕円体の中にポアソン円盤で n 点。weight(p) で密度に偏りを付ける"""
    pts = []
    fails = 0
    while len(pts) < n:
        p = Vector((rnd.uniform(-1, 1) * radii[0], rnd.uniform(-1, 1) * radii[1], rnd.uniform(-1, 1) * radii[2]))
        if (p.x / radii[0]) ** 2 + (p.y / radii[1]) ** 2 + (p.z / radii[2]) ** 2 > 1.0:
            continue
        if weight and rnd.random() > weight(p):
            continue
        if all((p - q).length >= dmin for q in pts):
            pts.append(p)
            fails = 0
        else:
            fails += 1
            if fails > 400:
                dmin *= 0.95
                fails = 0
    return pts


def _place(mesh, sp, SL, pos, yaw, pitch, roll, bend_c, bend_s, phase, depth=1.0):
    """正準形（SL 単位）を曲げ・回転・移動した頂点配列"""
    V = np.array(mesh.v, np.float64)
    TL = sp["TL"]
    x = V[:, 0] + TL / 2           # 全長の中央を原点に
    t = (TL / 2 - x) / TL          # 0 = 吻端 → 1 = 尾の先
    u = np.clip(t - 0.22, 0, None)
    y = V[:, 1] + TL * (bend_c * u ** 2 + bend_s * u ** 1.5 * np.sin(2 * np.pi * u * 1.3 + phase))
    z = V[:, 2] * depth
    P = np.stack([x, y, z], 1) * SL
    R = np.array(Matrix.Rotation(yaw, 3, "Z") @ Matrix.Rotation(-pitch, 3, "Y") @ Matrix.Rotation(roll, 3, "X"))
    return P @ R.T + np.array(pos)


_SHOW = {}


def school(name, sp, n, radii, dmin, heading_sd, pitch=(0.0, 0.12), roll_sd=0.12, male_frac=0.0,
           seed=0, weight=None, fixed=None):
    """群れ 1 つ分のオブジェクト。fixed = [(pos, yaw, TL), ...] なら配置を指定"""
    _prep(sp)
    rnd = random.Random(seed)
    meshes = [fish_mesh(sp, 0), fish_mesh(sp, 1)]
    if fixed:
        items = fixed
    else:
        pts = _cloud(rnd, n, radii, dmin, weight)
        items = []
        for p in pts:
            # 近くの魚ほど向きがそろう（なめらかなノイズ場）+ 個体のばらつき
            f = mnoise.noise(Vector((p.x * 1.6, p.y * 1.6, p.z * 1.6 + seed)))
            yaw = heading_sd * (1.5 * f + 0.45 * rnd.gauss(0, 1))
            items.append((p, yaw, rnd.uniform(*sp["tl"])))
    verts, faces, uvs = [], [], []
    for k, (p, yaw, TL) in enumerate(items):
        male = rnd.random() < male_frac
        if male:
            TL = max(TL, rnd.uniform(sp["tl"][0] + 0.6 * (sp["tl"][1] - sp["tl"][0]), sp["tl"][1]))
        mesh = meshes[1 if (male or (male_frac == 0 and rnd.random() < 0.5)) else 0]
        SL = TL / sp["TL"]
        pit = rnd.gauss(*pitch)
        P = _place(mesh, sp, SL, p, yaw, pit, rnd.gauss(0, roll_sd), rnd.gauss(0, 0.1), rnd.gauss(0, 0.035),
                   rnd.uniform(0, math.tau), depth=rnd.uniform(0.95, 1.05))
        o = len(verts)
        verts += [tuple(v) for v in P]
        faces += [tuple(i + o for i in f) for f in mesh.f]
        uvs += mesh.uv
        if k == 0:
            _SHOW[name] = (Vector(p), yaw, TL)
    obj = C.mesh_object(name, verts, faces, uvs)
    C.set_smooth(obj, True)
    tris = sum(len(f) - 2 for f in faces)
    print(f"  {name}: {len(items)} fish, {tris} tris ({tris // max(1, len(items))} / fish)")
    return obj


# ---------------------------------------------------------------------------
# プレビュー
# ---------------------------------------------------------------------------
def _preview_extra(objs):
    """3 種を寄せて並べ（青の群れ・緑の群れ・手前にチョウチョウウオ）、斜め横から撮る。
    OKI_DEBUG_DIR があれば 1 匹ずつの近景も撮る"""
    import bpy
    meshes = [o for o in objs if o.type == "MESH"]
    place = {"FishSchool_Blue": (-0.75, 0.3, 0.05), "FishSchool_Green": (0.75, 0.35, 0.0),
             "Fish_Butterfly": (0.05, -0.75, -0.12)}
    for o in meshes:
        tag = o.name.split(".")[0]
        if tag in place:
            mn, mx = C.bounds([o])
            o.location += Vector(place[tag]) - (mn + mx) / 2
    bpy.context.view_layer.update()
    mn, mx = C.bounds(meshes)
    g = bpy.data.objects.get("PreviewGround")
    if g:
        g.location.z = mn.z - 1.2
    cam = bpy.context.scene.camera
    cam.location = Vector((0.55, -3.1, 0.75))
    cam.data.lens = 36
    C.look_at(cam, Vector((0.0, 0.0, -0.05)))
    dbg = os.environ.get("OKI_DEBUG_DIR")
    if not dbg:
        return
    keep = (cam.location.copy(), cam.rotation_euler.copy(), cam.data.lens)
    for o in meshes:
        tag = o.name.split(".")[0]
        if tag not in _SHOW:
            continue
        p, yaw, TL = _SHOW[tag]
        c = o.matrix_world @ p
        fwd = Vector((math.cos(yaw), math.sin(yaw), 0))
        left = Vector((-fwd.y, fwd.x, 0))
        for k, (d, dist, lens) in enumerate([(left * 1.0 + UP * 0.15, TL * 3.2, 50),
                                             (left * 0.7 + fwd * 0.6 + UP * 0.35, TL * 3.2, 50)]):
            cam.location = c + d.normalized() * dist
            cam.data.lens = lens
            C.look_at(cam, c)
            C.render(os.path.join(dbg, f"fish_{tag}_{k}.jpg"), res=(800, 450), samples=16)
    cam.location, cam.rotation_euler, cam.data.lens = keep


PREVIEW = dict(cam_dir=(0.2, -1.0, 0.3), lens=50, extra=_preview_extra)


def build():
    _SHOW.clear()
    for sp in (CYANEA, VIRIDIS, AURIGA):
        for k in [k for k in sp if k.startswith("_")]:
            del sp[k]
    blue = school("FishSchool_Blue", CYANEA, 34, (0.74, 0.54, 0.32), 0.1, math.radians(24),
                  pitch=(0.0, math.radians(7)), roll_sd=math.radians(8), male_frac=0.35, seed=3)
    C.assign(blue, atlas_material(CYANEA, "SapphireDevil"))
    # デバスズメダイ: 枝サンゴの上にかたまる（下ほど密）
    green = school("FishSchool_Green", VIRIDIS, 52, (0.52, 0.46, 0.34), 0.085, math.radians(32),
                   pitch=(math.radians(4), math.radians(10)), roll_sd=math.radians(9), seed=7,
                   weight=lambda p: 0.45 + 0.55 * (0.5 - p.z / 0.68))
    C.assign(green, atlas_material(VIRIDIS, "BlueGreenChromis"))
    pair = school("Fish_Butterfly", AURIGA, 2, None, None, 0.0, pitch=(0.0, math.radians(3)),
                  roll_sd=math.radians(3), seed=11,
                  fixed=[(Vector((0.05, -0.06, 0.0)), math.radians(4), 0.152),
                         (Vector((-0.09, 0.08, 0.035)), math.radians(-6), 0.141)])
    C.assign(pair, atlas_material(AURIGA, "ThreadfinButterfly"))
    return {"FishSchool_Blue": [blue], "FishSchool_Green": [green], "Fish_Butterfly": [pair]}
