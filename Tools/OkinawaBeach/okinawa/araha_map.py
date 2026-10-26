"""アラハビーチの地図と高さ（Blender を使わない。地形の生成とシーンの書き出しで共有する）

data/araha_map.json（OpenStreetMap の浜・海岸線）から、浜・陸・海の範囲と、地形の高さを求める。
座標はシーンの原点（浜の中央）からの平面。x = 東 [m]、y = 北 [m]、z=0 が平均水面。
エンジンへは (x, z, y) で対応する。

高さの基準: 遊歩道の舗装面 = PROMENADE（護岸 seawall の z=0 をここに置く）。浜の砂は護岸の下端で SAND_TOE。
地図の浜の陸側の縁を護岸の天端の前の線、海側の縁を波打ち際（z=0）とする。
"""
import json
import math
import os

import numpy as np

TOOL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP_PATH = os.path.join(TOOL_DIR, "data", "araha_map.json")
PROMENADE = 2.5
SAND_TOE = PROMENADE - 0.9
LAWN = PROMENADE + 0.05
# 護岸の線から陸側でまだ浜の下端の高さにしておく幅（陸の高さへ上がる面が遊歩道の舗装の下に入る）
WALL_SKIRT = 2.0
SEABED_MAX = -9.5
# 浜の海側の縁・陸側の縁の両端（地図の浜の頂点のうち、この点にいちばん近いもの）
WATER_ENDS = ((-108.2, -276.4), (51.7, 278.0))
WALL_ENDS = ((-46.1, -310.2), (124.1, 257.5))
# 浜の北と南の外の海岸線（突堤の付け根から先）の始まり
NORTH_COAST_FROM = (133.6, 286.7)
SOUTH_COAST_TO = (-68.5, -323.3)
# 公園の施設のモデルと野外ステージの裏の広場: (中心, 方位 [度。北から時計回り], 占める範囲)。方位は野外ステージと遊具ではモデルの +X、
# ほかはモデルの +Y（長手）が向く方位。範囲は半径か (方位の向きの半分の長さ, 横の半分の長さ)。園路と木はこの範囲を避ける
PARK_FACILITIES = {
    "ArahaAmphitheater": ((9.5, -213.0), 102.0, 37.0),
    "ArahaAmphitheater_Plaza": ((57.9, -223.3), 102.0, (10.0, 15.0)),
    "ArahaCourts": ((93.5, 79.5), 14.8, (38.5, 11.0)),
    "ArahaHouse": ((44.5, -113.5), 18.0, (12.5, 13.0)),
    "ParkToilet": ((91.7, 8.5), 17.7, (7.5, 4.0)),
    "PlayTower": ((68.0, 12.0), 20.0, (7.5, 5.5)),
}


def _nearest(pts, p):
    return int(np.argmin(((pts - np.array(p)) ** 2).sum(-1)))


def _chain(pts, a, b):
    """閉じた多角形 pts（最後の点 = 最初の点）を a から b まで順にたどった点列"""
    n = len(pts) - 1
    out = [pts[a % n]]
    i = a
    while i % n != b % n:
        i += 1
        out.append(pts[i % n])
    return np.array(out)


class Map:
    def __init__(self, path=MAP_PATH):
        data = json.load(open(path, encoding="utf-8"))
        beach = np.array(data["beach"]["points"], float)
        if not np.allclose(beach[0], beach[-1]):
            beach = np.vstack([beach, beach[:1]])
        w0, w1 = _nearest(beach[:-1], WATER_ENDS[0]), _nearest(beach[:-1], WATER_ENDS[1])
        water = _chain(beach, w0, w1)
        # 海側の縁が南 → 北の順に、浜の頂点の半分より短くたどれる向きにそろえる
        if len(water) > len(beach) * 0.7:
            beach = beach[::-1].copy()
            w0, w1 = _nearest(beach[:-1], WATER_ENDS[0]), _nearest(beach[:-1], WATER_ENDS[1])
            water = _chain(beach, w0, w1)
        self.beach = beach
        s0, s1 = _nearest(beach[:-1], WALL_ENDS[1]), _nearest(beach[:-1], WALL_ENDS[0])
        self.water = water                         # 波打ち際（南 → 北）
        # 護岸の線（南 → 北）。地図の角をならし、護岸のモジュールもこの線に並べる
        self.wall = smooth_curve(resample(_chain(beach, s0, s1)[::-1], 0.5), passes=800)
        # 浜の範囲（地図の浜の、陸側の縁をならした護岸の線に置き換えたもの）
        north_end = _chain(beach, w1, s0)
        south_end = _chain(beach, s1, w0)
        self.beach_area = np.vstack([water, north_end[1:-1], self.wall[::-1], south_end[1:-1], water[:1]])
        north = south = None
        for c in data["coastline"]:
            pts = np.array(c["points"], float)
            if ((pts - np.array(NORTH_COAST_FROM)) ** 2).sum(-1).min() < 1.0:
                i = _nearest(pts, NORTH_COAST_FROM)
                north = pts[:i + 1][::-1] if pts[0][1] > pts[-1][1] else pts[i:]
            if ((pts - np.array(SOUTH_COAST_TO)) ** 2).sum(-1).min() < 1.0:
                i = _nearest(pts, SOUTH_COAST_TO)
                south = pts[i:] if pts[0][1] > pts[-1][1] else pts[:i + 1][::-1]
        self.north_coast = north                   # 北の突堤の付け根から北へ
        self.south_coast = south                   # 南の突堤の付け根から南へ
        far = 900.0
        self.land = np.vstack([self.wall, self.north_coast,
                               [[self.north_coast[-1][0], far], [far, far], [far, -far],
                                [self.south_coast[-1][0], -far]],
                               self.south_coast[::-1], self.wall[:1]])
        self.attribution = data["attribution"]


def inside(px, py, poly):
    res = np.zeros(np.shape(px), bool)
    x, y = poly[:, 0], poly[:, 1]
    for i in range(len(poly)):
        j = i - 1
        xi, yi, xj, yj = x[i], y[i], x[j], y[j]
        if yi == yj:
            continue
        res ^= ((yi > py) != (yj > py)) & (px < (xj - xi) * (py - yi) / (yj - yi) + xi)
    return res


def dist(px, py, pts):
    d = np.full(np.shape(px), 1e9)
    for a, b in zip(pts[:-1], pts[1:]):
        vx, vy = b[0] - a[0], b[1] - a[1]
        L2 = vx * vx + vy * vy
        if L2 < 1e-9:
            continue
        t = np.clip(((px - a[0]) * vx + (py - a[1]) * vy) / L2, 0.0, 1.0)
        d = np.minimum(d, np.hypot(px - (a[0] + t * vx), py - (a[1] + t * vy)))
    return d


def in_facility(px, py, margin=0.0):
    """公園の施設の範囲（PARK_FACILITIES）に入る点"""
    px = np.asarray(px, float)
    py = np.asarray(py, float)
    res = np.zeros(np.shape(px), bool)
    for (cx, cy), az, ext in PARK_FACILITIES.values():
        dx, dy = px - cx, py - cy
        if np.isscalar(ext):
            res |= np.hypot(dx, dy) < ext + margin
            continue
        a = math.radians(az)
        along = dx * math.sin(a) + dy * math.cos(a)
        across = dx * math.cos(a) - dy * math.sin(a)
        res |= (np.abs(along) < ext[0] + margin) & (np.abs(across) < ext[1] + margin)
    return res


def park_footways(m, wall_clear=8.0, margin=1.5):
    """公園の中の歩道の線。地図の歩道を 1 m おきに取り、護岸の遊歩道の近くと施設の範囲を除いて切った点列のリスト"""
    data = json.load(open(MAP_PATH, encoding="utf-8"))
    park = np.array(data["park"]["points"], float)
    out = []
    for fw in data["footways"]:
        pts = resample(np.array(fw["points"], float), 1.0)
        ok = inside(pts[:, 0], pts[:, 1], park) & inside(pts[:, 0], pts[:, 1], m.land)
        ok &= dist(pts[:, 0], pts[:, 1], m.wall) > wall_clear
        ok &= ~in_facility(pts[:, 0], pts[:, 1], margin=margin)
        run = []
        for p, k in zip(pts, ok):
            if k:
                run.append(p)
                continue
            if len(run) >= 4:
                out.append(np.array(run))
            run = []
        if len(run) >= 4:
            out.append(np.array(run))
    return out


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def waves(px, py, comps):
    """なだらかな起伏（いくつかの向きの正弦の和）"""
    v = np.zeros(np.shape(px))
    for wl, ang, ph, amp in comps:
        a = math.radians(ang)
        v = v + amp * np.sin((px * math.cos(a) + py * math.sin(a)) * math.tau / wl + ph)
    return v


class Field:
    """高さと面の種類"""

    def __init__(self, m):
        self.m = m
        self.sea_edges = [m.beach_area, m.north_coast, m.south_coast]
        self.coasts = [m.north_coast, m.south_coast]

    def classify(self, px, py):
        """0 = 海, 1 = 浜, 2 = 陸"""
        land = inside(px, py, self.m.land)
        beach = inside(px, py, self.m.beach_area) & ~land
        return np.where(land, 2, np.where(beach, 1, 0))

    def height(self, px, py, region=None):
        m = self.m
        px = np.asarray(px, float)
        py = np.asarray(py, float)
        if region is None:
            region = self.classify(px, py)
        d_wall = dist(px, py, m.wall)
        d_water = dist(px, py, m.water)
        d_sea = np.minimum.reduce([dist(px, py, e) for e in self.sea_edges])
        d_coast = np.minimum.reduce([dist(px, py, e) for e in self.coasts])
        # 浜: 波打ち際から上がり、護岸の下端で SAND_TOE
        z_beach = SAND_TOE * (1.0 - np.exp(-d_water / 12.0))
        z_beach = z_beach + 0.05 * waves(px, py, [(37.0, 20, 0.3, 1.0), (23.0, 110, 1.1, 0.6)]) \
            * smoothstep(3.0, 10.0, d_water)
        z_beach = z_beach + (SAND_TOE - z_beach) * smoothstep(3.0, 0.0, d_wall)
        z_beach = np.minimum(z_beach, SAND_TOE)
        # 海: 遠浅の砂地、170 m あたりから深くなる
        z_sea = -(1.8 * (1.0 - np.exp(-d_sea / 25.0)) + 0.012 * d_sea) - 3.0 * smoothstep(110.0, 170.0, d_sea)
        z_sea = z_sea + 0.18 * waves(px, py, [(41.0, 75, 0.0, 1.0), (17.0, 160, 2.0, 0.5), (63.0, 10, 4.0, 0.7)]) \
            * smoothstep(8.0, 40.0, d_sea)
        z_sea = np.maximum(z_sea, SEABED_MAX)
        # 陸: 遊歩道の下は舗装のすぐ下、その先の公園は平ら。浜の外の海岸は護岸で下がる
        z_land = np.where(d_wall < 4.6, PROMENADE - 0.05, LAWN)
        z_land = z_land + (-1.0 - z_land) * smoothstep(5.0, 0.0, d_coast) * (d_wall > 4.6)
        z = np.where(region == 2, z_land, np.where(region == 1, z_beach, z_sea))
        # 護岸の線から陸側 WALL_SKIRT までは浜の下端の高さ（護岸の本体と遊歩道の下に隠れる）
        return np.where((region == 2) & (d_wall < WALL_SKIRT), SAND_TOE, z)

    def surface(self, region, z):
        """面の種類の番号（0 乾いた砂, 1 濡れた砂, 2 水中の砂, 3 深い海底, 4 芝生）"""
        s = np.full(np.shape(z), 4)
        s = np.where(region == 1, np.where(z > 0.3, 0, 1), s)
        return np.where(region == 0, np.where(z > -0.5, 1, np.where(z > -3.0, 2, 3)), s)


# ---------------------------------------------------------------------------
# 線の補助
# ---------------------------------------------------------------------------
def resample(pts, step):
    """点列を step [m] おきに取り直す（最後の点も含める）"""
    seg = np.hypot(*(pts[1:] - pts[:-1]).T)
    acc = np.concatenate([[0.0], np.cumsum(seg)])
    s = np.append(np.arange(0.0, acc[-1], step), acc[-1])
    return np.stack([np.interp(s, acc, pts[:, 0]), np.interp(s, acc, pts[:, 1])], -1)


def smooth_curve(pts, passes=60):
    p = pts.copy()
    for _ in range(passes):
        p[1:-1] = p[1:-1] * 0.5 + (p[:-2] + p[2:]) * 0.25
    return p


def normals(c):
    """点列の左（南 → 北の線なら西）の向き"""
    tang = np.gradient(c, axis=0)
    tang /= np.linalg.norm(tang, axis=-1, keepdims=True)
    return np.stack([-tang[:, 1], tang[:, 0]], -1)


def grid_points(m, ds=2.0, ext=(250.0, 250.0)):
    """波打ち際に沿って曲がる格子の点 [ns, nt, 2]。s は南 → 北、t は海側が正"""
    base = smooth_curve(resample(m.water, 2.0))
    t0 = base[1] - base[0]
    t0 /= np.linalg.norm(t0)
    t1 = base[-1] - base[-2]
    t1 /= np.linalg.norm(t1)
    pre = np.array([base[0] - t0 * d for d in np.arange(ext[0], 0.0, -2.0)])
    post = np.array([base[-1] + t1 * d for d in np.arange(2.0, ext[1], 2.0)])
    curve = smooth_curve(resample(np.vstack([pre, base, post]), ds), passes=20)
    n_near = normals(curve)
    n_far = normals(smooth_curve(curve, passes=6000))
    t = np.concatenate([np.arange(-420.0, -100.0, 8.0), np.arange(-100.0, -20.0, 1.5), np.arange(-20.0, 40.0, 1.0),
                        np.arange(40.0, 220.01, 4.0)])
    P = np.empty((len(curve), len(t), 2))
    for j, tj in enumerate(t):
        # 浜から離れた行ほど、伸ばす向きを全体でならした向きへ寄せる（曲がりの内側で格子が折り重ならない）
        w = float(smoothstep(20.0, 120.0, abs(tj)))
        n = n_near * (1.0 - w) + n_far * w
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        P[:, j] = curve + n * tj
    return P


def offset_line(pts, d, step=2.0, passes=60):
    """線を左（海側）へ d [m] ずらした点列（ならしてから）"""
    c = smooth_curve(resample(pts, step), passes)
    return c + normals(c) * d
