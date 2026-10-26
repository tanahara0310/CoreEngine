"""アラハビーチのまわりの街（OpenStreetMap の建物・道路・面と標高）から、街の地面と建物の形を作る

Blender を使わない（numpy と Pillow だけ）。build_araha_town.py が使う。
座標は araha_map と同じ（x = 東 [m]、y = 北 [m]、z=0 が平均水面）。
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw

from . import araha_map as AM

TOWN_PATH = os.path.join(AM.TOOL_DIR, "data", "araha_town.json")
# 外側の地面の範囲（標高の範囲と同じ）と、細かい格子の範囲 [m]
FAR_BOX = (-3000.0, 5600.0, -4200.0, 5400.0)
INNER_BOX = (-1024.0, 2048.0, -1536.0, 2560.0)
INNER_STEP = 16.0
OUTER_STEP = 64.0
# 地面のテクスチャ: 細かい格子の範囲は 1024 m 四方のタイル、今の地形の格子の中の街は 512 m 四方のタイル
MID_TILE = 1024.0
NEAR_TILE = 512.0
TILE_PX = 1024
NEAR_PX = 2048
TILE_MARGIN = 16.0
FAR_PX = 2048
# 海と陸を分ける絵の解像度 [m/px]、海岸線の左右で陸と海を決め直す幅 [m]、海の深さを決める距離の絵の解像度
LAND_RES = 4.0
COAST_STRIP = 24.0
SEA_DIST_RES = 8.0
# 陸から離れた海の格子は作らない [m]
SEA_KEEP = 500.0

ROAD_WIDTH = {"motorway": 22.0, "trunk": 24.0, "primary": 16.0, "secondary": 11.0, "tertiary": 9.0,
              "motorway_link": 7.0, "trunk_link": 7.0, "primary_link": 7.0, "secondary_link": 6.5,
              "tertiary_link": 6.0, "unclassified": 6.0, "residential": 6.0, "living_street": 5.0, "service": 4.0,
              "track": 3.0, "pedestrian": 4.0, "footway": 2.0, "path": 1.8, "cycleway": 2.0, "steps": 2.0}
MAJOR = ("motorway", "trunk", "primary", "secondary", "tertiary")
SIDEWALK = {"trunk": 3.0, "primary": 3.0, "secondary": 2.5, "tertiary": 2.0}
PAVED_PATHS = ("footway", "path", "cycleway", "steps", "pedestrian")

# 面の色（sRGB）。後ろほど上に描く
AREA_ORDER = ["residential", "commercial", "industrial", "military", "farmland", "cemetery", "school", "grass",
              "park", "golf", "pitch", "track", "wood", "scrub", "beach", "water", "parking"]
AREA_COLOR = {"residential": (176, 171, 160), "commercial": (170, 168, 162), "industrial": (165, 163, 158),
              "military": (122, 142, 86), "farmland": (132, 148, 88), "cemetery": (168, 165, 156),
              "school": (192, 170, 134), "grass": (108, 140, 74), "park": (100, 136, 70), "golf": (118, 158, 80),
              "pitch": (96, 144, 76), "track": (170, 92, 70), "wood": (70, 98, 52), "scrub": (96, 116, 64),
              "beach": (224, 212, 184), "water": (70, 118, 128), "parking": (92, 93, 90)}
LAND_COLOR = (180, 175, 165)
LOT_COLOR = (142, 139, 132)
ASPHALT = (104, 105, 102)
GRASS_PATCH = (122, 140, 86)
TREE = (50, 76, 40)
WILD = (92, 118, 66)
# 木立の濃さ（面の種類ごと、0..1）。地図の範囲の外の陸は WILD の色に木立 0.55
TREE_DENSITY = {"wood": 0.85, "scrub": 0.5, "park": 0.55, "military": 0.35, "grass": 0.3, "cemetery": 0.25,
                "golf": 0.12, "farmland": 0.08, "residential": 0.1, "school": 0.08}
WALK = (172, 168, 160)
PAVING = (190, 182, 166)
LINE_WHITE = (226, 226, 218)
LINE_YELLOW = (214, 176, 70)
SEABED = (196, 186, 150)


def smoothstep(e0, e1, x):
    return AM.smoothstep(e0, e1, x)


class Town:
    """OSM の街のデータ（建物・道路・面・海岸線）"""

    def __init__(self, path=TOWN_PATH):
        data = json.load(open(path, encoding="utf-8"))
        self.attribution = data["attribution"]
        self.buildings = data["buildings"]
        self.roads = data["roads"]
        self.areas = data["areas"]
        self.coastline = [np.array(c["points"] if "points" in c else c["pts"], float) for c in data["coastline"]]
        self.box = tuple(data["box"])
        for group in (self.buildings, self.roads, self.areas):
            for f in group:
                a = np.asarray(f["pts"], float)
                f["_a"] = a
                f["_box"] = (a[:, 0].min(), a[:, 0].max(), a[:, 1].min(), a[:, 1].max())
        self._land = None
        self._sea_d = None

    # ---- 海と陸 ----

    def land_raster(self):
        """陸 = True の絵（FAR_BOX を LAND_RES で、北が上）

        標高が 0 より上を陸とし、海岸線から COAST_STRIP 以内だけは海岸線の左（陸）と右（海）で決め直す。
        """
        if self._land is not None:
            return self._land
        x0, x1, y0, y1 = FAR_BOX
        w = int(round((x1 - x0) / LAND_RES))
        h = int(round((y1 - y0) / LAND_RES))
        gx = x0 + (np.arange(w) + 0.5) * LAND_RES
        gy = y1 - (np.arange(h) + 0.5) * LAND_RES
        X, Y = np.meshgrid(gx, gy)
        land = AM.dem_height(X, Y) > 0.05
        im = Image.new("L", (w, h), 0)
        dr = ImageDraw.Draw(im)

        def P(pts):
            return [((x - x0) / LAND_RES, (y1 - y) / LAND_RES) for x, y in pts]

        for value, side in ((1, -1.0), (2, 1.0)):
            for c in self.coastline:
                for a, b in zip(c[:-1], c[1:]):
                    t = b - a
                    n = np.hypot(*t)
                    if n < 1e-6:
                        continue
                    left = np.array([-t[1], t[0]]) / n * COAST_STRIP * side
                    dr.polygon(P([a, b, b + left, a + left]), fill=value)
        o = np.array(im)
        land = np.where(o == 1, False, np.where(o == 2, True, land))
        self._land = land
        return land

    def is_land(self, px, py):
        land = self.land_raster()
        x0, x1, y0, y1 = FAR_BOX
        ix = np.clip(((np.asarray(px) - x0) / LAND_RES).astype(int), 0, land.shape[1] - 1)
        iy = np.clip(((y1 - np.asarray(py)) / LAND_RES).astype(int), 0, land.shape[0] - 1)
        return land[iy, ix]

    def sea_distance_raster(self, limit=SEA_KEEP + 200.0):
        """海の点の、いちばん近い陸までの距離の絵（SEA_DIST_RES、面取りの 8 近傍で近似。陸は 0）"""
        k = int(SEA_DIST_RES / LAND_RES)
        land = self.land_raster()
        H, W = land.shape[0] // k, land.shape[1] // k
        land = land[:H * k, :W * k].reshape(H, k, W, k).any(axis=(1, 3))
        r = SEA_DIST_RES
        diag = math.sqrt(2.0) * r
        d = np.where(land, 0.0, 1e9)
        for _ in range(int(limit / r) + 2):
            p = np.pad(d, 1, constant_values=1e9)
            cand = [p[1:H + 1, 0:W] + r, p[1:H + 1, 2:W + 2] + r, p[0:H, 1:W + 1] + r, p[2:H + 2, 1:W + 1] + r,
                    p[0:H, 0:W] + diag, p[0:H, 2:W + 2] + diag, p[2:H + 2, 0:W] + diag, p[2:H + 2, 2:W + 2] + diag]
            nd = np.minimum(d, np.minimum.reduce(cand))
            if np.array_equal(nd, d):
                break
            d = nd
        return d

    def sea_distance(self, px, py):
        if self._sea_d is None:
            self._sea_d = self.sea_distance_raster()
        x0, x1, y0, y1 = FAR_BOX
        d = self._sea_d
        ix = np.clip(((np.asarray(px) - x0) / SEA_DIST_RES).astype(int), 0, d.shape[1] - 1)
        iy = np.clip(((y1 - np.asarray(py)) / SEA_DIST_RES).astype(int), 0, d.shape[0] - 1)
        return d[iy, ix]

    def near(self, group, box, pad=50.0):
        """box = (x0, x1, y0, y1) の近くにある物"""
        x0, x1, y0, y1 = box
        return [f for f in group if not (f["_box"][1] < x0 - pad or f["_box"][0] > x1 + pad
                                         or f["_box"][3] < y0 - pad or f["_box"][2] > y1 + pad)]


def seabed_depth(d):
    """陸からの距離 d [m] の海底の高さ（araha_map の海と同じ形）"""
    z = -(1.8 * (1.0 - np.exp(-d / 25.0)) + 0.012 * d) - 3.0 * smoothstep(110.0, 170.0, d)
    return np.maximum(z, AM.SEABED_MAX)


def far_height(town, m, px, py):
    """今の地形の格子の外の地面の高さ（陸は LAWN から標高に沿って上げ、海は陸からの距離で深くする）"""
    px = np.asarray(px, float)
    py = np.asarray(py, float)
    land = town.is_land(px, py)
    z_land = AM.LAWN + AM.town_rise(m, px, py)
    z_sea = seabed_depth(town.sea_distance(px, py))
    return np.where(land, z_land, z_sea)


# ---- 地面のテクスチャ ----

def _noise(h, w, rng, scales):
    """値ノイズ（粗い乱数を拡大して足したもの、平均 0 付近）"""
    out = np.zeros((h, w), np.float32)
    for cell, amp in scales:
        gh = max(2, h // cell + 2)
        gw = max(2, w // cell + 2)
        g = rng.random((gh, gw)).astype(np.float32)
        im = Image.fromarray((g * 255).astype(np.uint8)).resize((gw * cell, gh * cell), Image.BICUBIC)
        out += (np.asarray(im, np.float32)[:h, :w] / 255.0 - 0.5) * amp
    return out


def _dashes(pts, dash, gap):
    """折れ線を dash [m] 描いて gap [m] 空ける破線の線分の一覧"""
    out = []
    pts = np.asarray(pts, float)
    seg = np.hypot(*(pts[1:] - pts[:-1]).T)
    acc = np.concatenate([[0.0], np.cumsum(seg)])
    s = 0.0
    while s < acc[-1]:
        e = min(s + dash, acc[-1])
        ss = np.linspace(s, e, 3)
        out.append(np.stack([np.interp(ss, acc, pts[:, 0]), np.interp(ss, acc, pts[:, 1])], -1))
        s += dash + gap
    return out


def _offset(pts, d):
    """折れ線を左へ d [m] ずらす"""
    pts = np.asarray(pts, float)
    if len(pts) < 2:
        return pts
    t = np.gradient(pts, axis=0)
    n = np.linalg.norm(t, axis=-1, keepdims=True)
    t = t / np.maximum(n, 1e-9)
    return pts + np.stack([-t[:, 1], t[:, 0]], -1) * d


def road_width(r):
    hw = r["highway"]
    w = ROAD_WIDTH.get(hw)
    if w is None:
        return None
    if "width" in r and 2.0 <= r["width"] <= 40.0:
        return float(r["width"])
    if "lanes" in r and hw not in PAVED_PATHS:
        return max(w * 0.6, r["lanes"] * 3.25 + 1.0)
    return w


def paint_ground(town, box, px, seed, building_lots=True):
    """box = (x0, x1, y0, y1) の地面の色の絵（px x px、北が上）を作る"""
    x0, x1, y0, y1 = box
    ss = 2
    W = px * ss
    sx = W / (x1 - x0)
    sy = W / (y1 - y0)

    def P(pts):
        a = np.asarray(pts, float)
        return list(map(tuple, np.stack([(a[:, 0] - x0) * sx, (y1 - a[:, 1]) * sy], -1)))

    im = Image.new("RGB", (W, W), LAND_COLOR)
    dr = ImageDraw.Draw(im)
    # 何も無い陸（255）と木立の濃さ（0..250）の絵。舗装・建物の所は 0
    free = Image.new("L", (W, W), 255)
    dfree = ImageDraw.Draw(free)
    trees = Image.new("L", (W, W), 0)
    dtrees = ImageDraw.Draw(trees)
    # アスファルトの所（255）
    paved = Image.new("L", (W, W), 0)
    dpaved = ImageDraw.Draw(paved)
    rank = {k: i for i, k in enumerate(AREA_ORDER)}
    for a in sorted(town.near(town.areas, box), key=lambda a: rank.get(a["kind"], -1)):
        if a["kind"] not in AREA_COLOR or len(a["pts"]) < 3:
            continue
        pts = P(a["_a"])
        dr.polygon(pts, fill=AREA_COLOR[a["kind"]])
        dfree.polygon(pts, fill=0)
        dtrees.polygon(pts, fill=int(TREE_DENSITY.get(a["kind"], 0.0) * 250))
        dpaved.polygon(pts, fill=255 if a["kind"] == "parking" else 0)
    if building_lots:
        for b in town.near(town.buildings, box):
            if len(b["pts"]) >= 3:
                pts = P(b["_a"])
                dr.polygon(pts, fill=LOT_COLOR)
                dfree.polygon(pts, fill=0)
                dtrees.polygon(pts, fill=0)
    scale = (sx + sy) / 2

    def line(pts, width, color):
        if width * scale < 0.6:
            return
        q = P(pts)
        wpx = max(1, int(round(width * scale)))
        dr.line(q, fill=color, width=wpx, joint="curve")
        dfree.line(q, fill=0, width=wpx, joint="curve")
        dtrees.line(q, fill=0, width=wpx, joint="curve")
        dpaved.line(q, fill=255 if color == ASPHALT else 0, width=wpx, joint="curve")

    if scale >= 2.0:
        for a in town.near(town.areas, box):
            if a["kind"] != "parking" or len(a["pts"]) < 3:
                continue
            poly = a["_a"]
            ccw = 0.5 * np.sum(poly[:, 0] * np.roll(poly[:, 1], -1) - np.roll(poly[:, 0], -1) * poly[:, 1]) > 0
            for p0, p1 in zip(poly, np.roll(poly, -1, 0)):
                e = p1 - p0
                L = float(np.hypot(*e))
                if L < 10.0:
                    continue
                t = e / L
                n = np.array([-t[1], t[0]]) * (1.0 if ccw else -1.0)
                for d in np.arange(1.5, L - 1.0, 2.5):
                    q = p0 + t * d
                    dr.line(P([q + n * 0.3, q + n * 5.0]), fill=LINE_WHITE, width=max(1, int(round(0.12 * scale))))
    roads = [r for r in town.near(town.roads, box) if r.get("tunnel") not in ("yes", "building_passage")
             and len(r["pts"]) >= 2]
    order = {k: i for i, k in enumerate(["path", "footway", "cycleway", "steps", "track", "pedestrian", "service",
                                         "living_street", "residential", "unclassified", "tertiary_link",
                                         "secondary_link", "primary_link", "trunk_link", "motorway_link", "tertiary",
                                         "secondary", "primary", "trunk", "motorway"])}
    roads.sort(key=lambda r: order.get(r["highway"], -1))
    # 歩道（大きい道の両側）→ 舗装 → 線
    for r in roads:
        w = road_width(r)
        if w and r["highway"] in SIDEWALK:
            line(r["_a"], w + SIDEWALK[r["highway"]] * 2, WALK)
    for r in roads:
        w = road_width(r)
        if not w:
            continue
        if r.get("area") == "yes" and len(r["pts"]) >= 4:
            pts = P(r["_a"])
            dr.polygon(pts, fill=PAVING if r["highway"] in PAVED_PATHS else ASPHALT)
            dfree.polygon(pts, fill=0)
            dtrees.polygon(pts, fill=0)
            continue
        line(r["_a"], w, PAVING if r["highway"] in PAVED_PATHS else ASPHALT)
    if scale >= 0.9:
        for r in roads:
            hw = r["highway"]
            w = road_width(r)
            if not w or hw in PAVED_PATHS or hw in ("track",) or r.get("area") == "yes":
                continue
            pts = r["_a"]
            if hw in MAJOR and w >= 8.0:
                for side in (-1, 1):
                    line(_offset(pts, side * (w / 2 - 0.6)), 0.15, LINE_WHITE)
                if r.get("oneway") != "yes":
                    if w >= 14.0:
                        line(_offset(pts, 0.25), 0.15, LINE_YELLOW)
                        line(_offset(pts, -0.25), 0.15, LINE_YELLOW)
                    else:
                        line(pts, 0.15, LINE_YELLOW)
                lanes_side = max(1, int((w - 2.0) / 3.25) // (1 if r.get("oneway") == "yes" else 2))
                for k in range(1, lanes_side):
                    off = k * 3.25 - (0.0 if r.get("oneway") != "yes" else (w / 2 - 1.0))
                    for side in ((-1, 1) if r.get("oneway") != "yes" else (1,)):
                        for d in _dashes(_offset(pts, side * off), 5.0, 7.5):
                            line(d, 0.15, LINE_WHITE)
            elif hw in ("residential", "unclassified", "living_street") and w >= 5.5:
                for d in _dashes(pts, 3.0, 4.0):
                    line(d, 0.12, LINE_WHITE)
    img = np.asarray(im.resize((px, px), Image.LANCZOS), np.float32) / 255.0
    paved_a = np.asarray(paved.resize((px, px), Image.BILINEAR), np.float32) / 255.0
    free_a = np.asarray(free.resize((px, px), Image.BILINEAR), np.float32) / 255.0
    tree_a = np.asarray(trees.resize((px, px), Image.BILINEAR), np.float32) / 250.0
    rng = np.random.default_rng(seed)
    res = (x1 - x0) / px
    gx = x0 + (np.arange(px) + 0.5) * res
    gy = y1 - (np.arange(px) + 0.5) * res
    X, Y = np.meshgrid(gx, gy)
    # 地図の範囲の外の陸は草と林
    bx0, bx1, by0, by1 = town.box
    outside = (X < bx0) | (X > bx1) | (Y < by0) | (Y > by1)
    img[outside] = np.array(WILD, np.float32) / 255.0
    tree_a = np.where(outside, 0.55, tree_a)
    free_a = np.where(outside, 0.0, free_a)

    def cells(m):
        return max(2, int(m / res))

    # 何も無い陸のところどころに草
    patch = smoothstep(0.05, 0.3, _noise(px, px, rng, [(cells(30.0), 1.0), (cells(9.0), 0.5)]))
    grass = np.array(GRASS_PATCH, np.float32) / 255.0
    img = img + (grass - img) * (patch * free_a * 0.7)[..., None]
    # 木立（濃さの分だけ、粗いむらで塊にする）
    clump = _noise(px, px, rng, [(cells(24.0), 1.0), (cells(7.0), 0.8), (cells(2.5), 0.35)])
    cover = smoothstep(0.0, 0.18, clump + (tree_a - 0.5) * 0.9) * np.clip(tree_a * 2.0, 0.0, 1.0)
    tone = 1.0 + _noise(px, px, rng, [(cells(3.0), 0.5), (cells(1.2), 0.3)])
    tree = np.array(TREE, np.float32) / 255.0
    img = img + (tree[None, None, :] * tone[..., None] - img) * cover[..., None]
    fine = _noise(px, px, rng, [(cells(2.0), 0.10), (cells(9.0), 0.10), (cells(40.0), 0.12)])
    img *= (1.0 + fine)[..., None]
    if res <= 0.35:
        # アスファルト: 骨材の粒・補修の継ぎはぎ・ひび・タイヤの跡の黒ずみ
        grain = _noise(px, px, rng, [(2, 0.18)])
        patch = smoothstep(0.1, 0.14, _noise(px, px, rng, [(cells(6.0), 1.0)])) * 0.12             - smoothstep(0.12, 0.16, _noise(px, px, rng, [(cells(9.0), 1.0)])) * 0.1
        crack = (np.abs(_noise(px, px, rng, [(cells(3.0), 1.0), (cells(1.0), 0.4)])) < 0.012).astype(np.float32)
        img *= (1.0 + (grain + patch - crack * 0.35) * paved_a)[..., None]
    # 海（海岸線の外）は海底の砂の色
    sea = ~town.is_land(X, Y)
    if sea.any():
        d = town.sea_distance(X[sea], Y[sea])
        depth = np.clip(d / 150.0, 0.0, 1.0)[:, None]
        base = np.array(SEABED, np.float32) / 255.0
        deep = np.array((120, 140, 128), np.float32) / 255.0
        img[sea] = (base * (1 - depth) + deep * depth) * (1.0 + fine[sea] * 0.5)[:, None]
    return np.clip(img, 0.0, 1.0)


def save_rgb(path, img):
    Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").save(path, optimize=True)


# ---- 外側の地面の格子 ----

def axis_lines(lo, hi, inner_lo, inner_hi):
    """外側の格子の線の位置（細かい範囲は INNER_STEP、その外は OUTER_STEP）"""
    a = np.arange(lo, inner_lo, OUTER_STEP)
    b = np.arange(inner_lo, inner_hi, INNER_STEP)
    c = np.arange(inner_hi, hi + 0.1, OUTER_STEP)
    return np.concatenate([a, b, c])


def grid_ring(P):
    """今の地形の格子の外周の格子の番号 (i, j) の列（反時計回り。最後の点は持たない）"""
    ns, nt = P.shape[:2]
    idx = ([(i, 0) for i in range(ns - 1)] + [(ns - 1, j) for j in range(nt - 1)]
           + [(i, nt - 1) for i in range(ns - 1, 0, -1)] + [(0, j) for j in range(nt - 1, 0, -1)])
    ring = np.array([P[i, j] for i, j in idx])
    area = 0.5 * np.sum(ring[:, 0] * np.roll(ring[:, 1], -1) - np.roll(ring[:, 0], -1) * ring[:, 1])
    return idx if area > 0 else idx[::-1]
