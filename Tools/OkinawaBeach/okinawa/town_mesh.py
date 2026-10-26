"""街の地面と建物の形（numpy）

地面: 今の地形の格子（araha_map.grid_points）を境目で切り分けたうちの公園の外（面の種類 5）と、その外を覆う粗い格子。
      粗い格子は今の地形の格子にかかる升目を抜き、抜いた所の縁と今の地形の格子の外周を三角形で縫い合わせる。
建物: OSM の建物の輪郭を立ち上げ、外壁・屋上の手すり壁・屋上・給水タンク・塔屋を作る。
"""
import math
import zlib

import numpy as np

from . import araha_map as AM
from . import araha_town as AT
from . import town_textures as TT
from .gltf_write import Primitive

PARAPET = 0.9
# 屋上の手すり壁の内側・給水タンク・塔屋まで作る距離（浜の中央から）[m]、小さな建物を作らない距離と大きさ
DETAIL_RADIUS = 900.0
SMALL_SKIP_RADIUS = 2600.0
SMALL_SKIP_AREA = 45.0
CHUNK = 1024.0
AV_BOX = (-540.0, -60.0, 1140.0, 1460.0)   # 美浜アメリカンビレッジの店の並ぶ範囲
SHOP_ROAD_DIST = 14.0


def h32(*keys):
    return zlib.crc32("/".join(str(k) for k in keys).encode())


def frand(*keys):
    return (h32(*keys) & 0xFFFFFF) / float(0x1000000)


# ---------------------------------------------------------------------------
# 多角形
# ---------------------------------------------------------------------------

def signed_area(p):
    return 0.5 * float(np.sum(p[:, 0] * np.roll(p[:, 1], -1) - np.roll(p[:, 0], -1) * p[:, 1]))


def clean_polygon(pts):
    """重なった点と一直線の点を除き、反時計回りにする"""
    p = np.asarray(pts, float)
    if len(p) > 1 and np.allclose(p[0], p[-1]):
        p = p[:-1]
    keep = [p[0]]
    for q in p[1:]:
        if np.hypot(*(q - keep[-1])) > 0.25:
            keep.append(q)
    p = np.array(keep)
    if len(p) >= 2 and np.hypot(*(p[0] - p[-1])) <= 0.25:
        p = p[:-1]
    changed = True
    while changed and len(p) > 3:
        changed = False
        for i in range(len(p)):
            a, b, c = p[i - 1], p[i], p[(i + 1) % len(p)]
            ab, bc = b - a, c - b
            cr = ab[0] * bc[1] - ab[1] * bc[0]
            if abs(cr) < 0.05 * max(np.hypot(*ab), np.hypot(*bc)):
                p = np.delete(p, i, 0)
                changed = True
                break
    if len(p) < 3:
        return None
    if signed_area(p) < 0:
        p = p[::-1].copy()
    return p


def triangulate(p):
    """反時計回りの単純多角形の耳切り。失敗したら None"""
    n = len(p)
    idx = list(range(n))
    tris = []
    guard = 0
    while len(idx) > 3 and guard < n * n:
        guard += 1
        m = len(idx)
        found = False
        for k in range(m):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % m]
            a, b, c = p[i0], p[i1], p[i2]
            if (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) <= 1e-9:
                continue
            ok = True
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                q = p[j]
                d1 = (b[0] - a[0]) * (q[1] - a[1]) - (b[1] - a[1]) * (q[0] - a[0])
                d2 = (c[0] - b[0]) * (q[1] - b[1]) - (c[1] - b[1]) * (q[0] - b[0])
                d3 = (a[0] - c[0]) * (q[1] - c[1]) - (a[1] - c[1]) * (q[0] - c[0])
                if d1 >= 0 and d2 >= 0 and d3 >= 0:
                    ok = False
                    break
            if ok:
                tris.append((i0, i1, i2))
                idx.pop(k)
                found = True
                break
        if not found:
            return None
    if len(idx) == 3:
        tris.append(tuple(idx))
    return tris


def inset_polygon(p, d):
    """反時計回りの多角形を内側へ d ずらす（辺ごとにずらして隣の辺と交わる点）。崩れたら None"""
    n = len(p)
    out = []
    for i in range(n):
        a, b, c = p[i - 1], p[i], p[(i + 1) % n]
        t1 = (b - a) / max(np.hypot(*(b - a)), 1e-9)
        t2 = (c - b) / max(np.hypot(*(c - b)), 1e-9)
        n1 = np.array([-t1[1], t1[0]])
        n2 = np.array([-t2[1], t2[0]])
        m = n1 + n2
        ln = np.hypot(*m)
        if ln < 1e-6:
            return None
        m /= ln
        cosv = float(m @ n1)
        if cosv < 0.3:
            return None
        out.append(b + m * d / cosv)
    q = np.array(out)
    if signed_area(q) <= 0.3 * signed_area(p) or abs(signed_area(q)) < 4.0:
        return None
    for i in range(n):
        e_in = q[(i + 1) % n] - q[i]
        e_out = p[(i + 1) % n] - p[i]
        if e_in @ e_out <= 0:
            return None
    return q


def point_in(p, x, y):
    return bool(AM.inside(np.array([x]), np.array([y]), p)[0])


# ---------------------------------------------------------------------------
# 建物の種類と高さ
# ---------------------------------------------------------------------------

HOUSE_TYPES = ("house", "detached", "semidetached_house", "terrace", "bungalow")
APT_TYPES = ("apartments", "residential", "dormitory")
SHOP_TYPES = ("retail", "commercial", "supermarket", "mall")
PUBLIC_TYPES = ("school", "university", "college", "kindergarten", "hospital", "public", "civic", "government",
                "office", "church", "chapel", "temple", "fire_station", "bank")
INDUSTRIAL_TYPES = ("industrial", "warehouse", "hangar", "manufacture", "storage", "stadium", "greenhouse")
SMALL_TYPES = ("garage", "garages", "shed", "hut", "kiosk", "toilets", "shelter", "service", "pavilion")
CANOPY_TYPES = ("roof", "carport", "canopy", "parking")


class Context:
    """建物の種類を決めるための周りの情報（基地の範囲・大きな道）"""

    def __init__(self, town):
        self.military = [a["_a"] for a in town.areas if a["kind"] == "military"]
        segs = []
        for r in town.roads:
            if r["highway"] in AT.MAJOR:
                a = r["_a"]
                w = AT.road_width(r) or 10.0
                for p, q in zip(a[:-1], a[1:]):
                    segs.append((p[0], p[1], q[0], q[1], w))
        self.segs = np.array(segs, float)

    def in_military(self, x, y):
        return any(point_in(m, x, y) for m in self.military)

    def road_dist(self, x, y):
        """いちばん近い大きな道の縁までの距離"""
        s = self.segs
        near = (np.minimum(s[:, 0], s[:, 2]) < x + 80) & (np.maximum(s[:, 0], s[:, 2]) > x - 80) \
            & (np.minimum(s[:, 1], s[:, 3]) < y + 80) & (np.maximum(s[:, 1], s[:, 3]) > y - 80)
        s = s[near]
        if len(s) == 0:
            return 1e9
        vx, vy = s[:, 2] - s[:, 0], s[:, 3] - s[:, 1]
        L2 = np.maximum(vx * vx + vy * vy, 1e-9)
        t = np.clip(((x - s[:, 0]) * vx + (y - s[:, 1]) * vy) / L2, 0, 1)
        d = np.hypot(x - (s[:, 0] + t * vx), y - (s[:, 1] + t * vy)) - s[:, 4] / 2
        return float(d.min())


def classify(b, poly, ctx):
    """建物の外壁の種類・色・階数などを決める。作らないなら None"""
    t = b["type"]
    bid = b["id"]
    area = signed_area(poly)
    cx, cy = poly.mean(axis=0)
    levels = b.get("levels")
    r = frand(bid, "r")
    pal = lambda style: TT.STYLES[style][2][int(frand(bid, "pal") * len(TT.STYLES[style][2]))][0]  # noqa: E731
    out = {"shop": False, "roof": "Concrete" if r < 0.65 else "Coated", "canopy": False}
    military = ctx.in_military(cx, cy)
    in_av = AV_BOX[0] < cx < AV_BOX[1] and AV_BOX[2] < cy < AV_BOX[3]
    if t in CANOPY_TYPES:
        out.update(style="BigBox", palette="Gray", levels=1, canopy=True, roof="Metal")
        return out
    if t in SMALL_TYPES or area < 40:
        style = "Base" if military else "House"
        out.update(style=style, palette=pal(style), levels=levels or 1)
        return out
    if t == "hotel" or (b.get("tourism") == "hotel"):
        lv = levels or 7
        style = "Tower" if lv >= 7 else "AptGlass"
        out.update(style=style, palette=pal(style), levels=lv)
        return out
    if levels and levels >= 9:
        style = "Tower" if r < 0.6 else "AptGlass"
        out.update(style=style, palette=pal(style), levels=levels)
        return out
    if in_av and t in SHOP_TYPES + ("yes",) and area < 4000:
        out.update(style="Village", palette=pal("Village"), levels=levels or 2)
        return out
    if t in INDUSTRIAL_TYPES:
        out.update(style="BigBox", palette=pal("BigBox"), levels=levels or 1, roof="Metal")
        return out
    if t in PUBLIC_TYPES:
        style = "Base" if military else "Office"
        out.update(style=style, palette=pal(style), levels=levels or (2 if t == "kindergarten" else 3))
        return out
    if t in SHOP_TYPES:
        if area > 1500:
            out.update(style="BigBox", palette=pal("BigBox"), levels=levels or (1 if area > 4000 else 2),
                       roof="Metal")
        else:
            out.update(style="Apt" if r < 0.5 else "Office", palette=pal("Apt" if r < 0.5 else "Office"),
                       levels=levels or 2, shop=True)
        return out
    if military:
        out.update(style="Base", palette=pal("Base"), levels=levels or (1 if area > 2500 else 2))
        return out
    if t in HOUSE_TYPES:
        out.update(style="House", palette=pal("House"), levels=levels or 2)
        return out
    if t in APT_TYPES:
        if area < 120:
            out.update(style="House", palette=pal("House"), levels=levels or 2)
        else:
            lv = levels or (3 + int(r * 3))
            style = "AptGlass" if lv >= 6 and r < 0.5 else "Apt"
            out.update(style=style, palette=pal(style), levels=lv)
        return out
    # 種類の分からない建物は広さと場所で決める
    road = ctx.road_dist(cx, cy) if area < 900 else 1e9
    if area > 2500:
        out.update(style="BigBox", palette=pal("BigBox"), levels=levels or (1 if r < 0.6 else 2), roof="Metal")
    elif area > 700:
        style = "Office" if r < 0.45 else "Apt"
        out.update(style=style, palette=pal(style), levels=levels or (2 + int(r * 3)))
    elif area > 180:
        if road < SHOP_ROAD_DIST and r < 0.8:
            out.update(style="Apt" if r < 0.5 else "House", palette=pal("Apt" if r < 0.5 else "House"),
                       levels=levels or (2 + int(frand(bid, "lv") * 3)), shop=True)
        else:
            style = "Apt" if r < 0.55 else "House"
            out.update(style=style, palette=pal(style), levels=levels or (2 + int(frand(bid, "lv") * 3)))
    else:
        if road < SHOP_ROAD_DIST and r < 0.6:
            out.update(style="House", palette=pal("House"), levels=levels or 2, shop=True)
        else:
            out.update(style="House", palette=pal("House"), levels=levels or (2 if r < 0.85 else 3))
    return out


# ---------------------------------------------------------------------------
# 建物の形
# ---------------------------------------------------------------------------

class Builder:
    """材質ごとの三角形を集める。key = 材質の名前"""

    def __init__(self, origin):
        self.origin = np.array([origin[0], origin[1], 0.0])
        self.parts = {}

    def prim(self, key):
        if key not in self.parts:
            self.parts[key] = Primitive()
        return self.parts[key]

    def quad(self, key, a, b, c, d, uva, uvb, uvc, uvd, nrm, tan):
        P = np.array([a, b, c, d], float) - self.origin
        self.prim(key).add(P, nrm, [uva, uvb, uvc, uvd], tan, [(0, 1, 2), (0, 2, 3)])

    def poly_cap(self, key, poly2d, z, tris, uv_scale, uv_off=(0.0, 0.0), up=True):
        P = np.column_stack([poly2d, np.full(len(poly2d), z)]) - self.origin
        uv = (poly2d / uv_scale) + np.array(uv_off)
        t = np.array(tris)
        if not up:
            t = t[:, ::-1]
        self.prim(key).add(P, (0, 0, 1 if up else -1), uv, (1, 0, 0), t)


def wall_band(B, key, poly, z0, z1, u0, v0, v1, tile_w=TT.TILE_W, outward=True):
    """多角形の各辺に z0..z1 の壁（u は周長 / tile_w + u0、v は v0..v1）"""
    n = len(poly)
    s = u0 * tile_w
    for i in range(n):
        a = poly[i]
        b = poly[(i + 1) % n]
        e = b - a
        L = float(np.hypot(*e))
        if L < 1e-6:
            continue
        t = np.array([e[0] / L, e[1] / L, 0.0])
        nrm = np.array([t[1], -t[0], 0.0])
        ua, ub = s / tile_w, (s + L) / tile_w
        if outward:
            B.quad(key, (a[0], a[1], z0), (b[0], b[1], z0), (b[0], b[1], z1), (a[0], a[1], z1),
                   (ua, v0), (ub, v0), (ub, v1), (ua, v1), nrm, t)
        else:
            B.quad(key, (b[0], b[1], z0), (a[0], a[1], z0), (a[0], a[1], z1), (b[0], b[1], z1),
                   (ub, v0), (ua, v0), (ua, v1), (ub, v1), -nrm, -t)
        s += L


def box(B, key, x0, y0, x1, y1, z0, z1, uv_scale=4.0):
    p = np.array([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
    wall_band(B, key, p, z0, z1, 0.0, z0 / uv_scale, z1 / uv_scale, tile_w=uv_scale)
    B.poly_cap(key, p, z1, [(0, 1, 2), (0, 2, 3)], uv_scale)


def cylinder(B, key, cx, cy, r, z0, z1, seg=10):
    ang = np.linspace(0, 2 * math.pi, seg + 1)
    ring = np.stack([cx + r * np.cos(ang), cy + r * np.sin(ang)], -1)
    for i in range(seg):
        a, b = ring[i], ring[i + 1]
        mid = (ang[i] + ang[i + 1]) / 2
        nrm = (math.cos(mid), math.sin(mid), 0.0)
        tan = (-math.sin(mid), math.cos(mid), 0.0)
        B.quad(key, (a[0], a[1], z0), (b[0], b[1], z0), (b[0], b[1], z1), (a[0], a[1], z1),
               (i / seg, 0), ((i + 1) / seg, 0), ((i + 1) / seg, 1), (i / seg, 1), nrm, tan)
    cap = ring[:-1]
    B.poly_cap(key, cap, z1, [(0, k, k + 1) for k in range(1, seg - 1)], 2.0)


def random_point_inside(poly, inner, key, tries=24, margin=1.2):
    lo, hi = inner.min(axis=0) + margin, inner.max(axis=0) - margin
    if np.any(hi <= lo):
        return None
    for k in range(tries):
        x = lo[0] + (hi[0] - lo[0]) * frand(key, k, "x")
        y = lo[1] + (hi[1] - lo[1]) * frand(key, k, "y")
        if point_in(inner, x, y) and np.min(AM.dist(np.array([x]), np.array([y]),
                                                   np.vstack([inner, inner[:1]]))) > margin:
            return x, y
    return None


def facade_key(style, palette):
    return f"Facade_{style}_{palette}"


def build_building(B, b, poly, info, ground, detail):
    """1 棟の三角形を B へ足す。ground(x, y) は地面の高さ"""
    bid = b["id"]
    style, palette = info["style"], info["palette"]
    floor_h, _, _, (pv0, pv1) = TT.STYLES[style]
    zg = ground(poly[:, 0], poly[:, 1])
    base = float(zg.min()) - 0.4
    lvl0 = float(zg.max())
    u0 = math.floor(frand(bid, "u") * 4) * 0.25
    key = facade_key(style, palette)
    if info["canopy"]:
        z_bot = lvl0 + 4.6
        z_top = z_bot + 0.5
        wall_band(B, "Plain", poly, z_bot, z_top, 0.0, 0.0, 0.1, tile_w=4.0)
        tris = triangulate(poly)
        if tris:
            B.poly_cap(f"Roof_{info['roof']}", poly, z_top, tris, TT.ROOF_TILE)
            B.poly_cap("Plain", poly, z_bot, tris, 4.0, up=False)
        inner = inset_polygon(poly, 0.8)
        if inner is not None and len(inner) <= 8:
            for q in inner:
                box(B, "Plain", q[0] - 0.2, q[1] - 0.2, q[0] + 0.2, q[1] + 0.2, base, z_bot)
        return
    levels = max(1, int(round(info["levels"])))
    if "height" in b and b["height"] > 2.5:
        body = max(2.5, b["height"] - PARAPET)
        levels = max(1, int(round(body / floor_h)))
    shop_h = 4.0 if info["shop"] else 0.0
    if info["shop"]:
        wall_band(B, facade_key("Shop", "A" if frand(bid, "shop") < 0.5 else "B"), poly, base, lvl0 + shop_h,
                  u0, (base - lvl0) / shop_h, 1.0)
        start = lvl0 + shop_h
        upper = max(0, levels - 1)
    else:
        start = lvl0
        upper = levels
    top = start + upper * floor_h
    if upper > 0:
        wall_band(B, key, poly, base if not info["shop"] else start, top, u0,
                  ((base if not info["shop"] else start) - start) / floor_h, upper)
    roof_key = f"Roof_{info['roof']}"
    off = (frand(bid, "rx"), frand(bid, "ry"))
    tris = triangulate(poly)
    if tris is None:
        return
    inner = inset_polygon(poly, 0.25) if detail else None
    pkey = key if upper > 0 else facade_key("Shop", "A")
    pv = (pv0, pv1) if upper > 0 else TT.STYLES["Shop"][3]
    wall_band(B, pkey, poly, top, top + PARAPET, u0, pv[0], pv[1])
    if inner is None:
        B.poly_cap(roof_key, poly, top + PARAPET, tris, TT.ROOF_TILE, off)
        return
    itris = triangulate(inner)
    if itris is None:
        B.poly_cap(roof_key, poly, top + PARAPET, tris, TT.ROOF_TILE, off)
        return
    wall_band(B, pkey, inner, top, top + PARAPET, u0, pv[0], pv[1], outward=False)
    B.poly_cap(roof_key, inner, top + 0.02, itris, TT.ROOF_TILE, off)
    # 手すり壁の天端（外周と内周のあいだ）
    n = len(poly)
    for i in range(n):
        j = (i + 1) % n
        a, b2, c, d = poly[i], poly[j], inner[j], inner[i]
        B.quad("Plain", (a[0], a[1], top + PARAPET), (b2[0], b2[1], top + PARAPET), (c[0], c[1], top + PARAPET),
               (d[0], d[1], top + PARAPET), (0, 0), (1, 0), (1, 0.1), (0, 0.1), (0, 0, 1), (1, 0, 0))
    roof_z = top + 0.02
    # 屋上の物（給水タンク・塔屋・室外機）
    if style == "House":
        for k in range(1 + int(frand(bid, "nt") * 2)):
            p = random_point_inside(poly, inner, (bid, "tank", k))
            if p:
                tank = "Tank_" + ("White", "Blue", "Steel")[int(frand(bid, "tk", k) * 3)]
                r = 0.5 + 0.2 * frand(bid, "tr", k)
                box(B, "Plain", p[0] - r * 0.7, p[1] - r * 0.7, p[0] + r * 0.7, p[1] + r * 0.7, roof_z, roof_z + 0.45)
                cylinder(B, tank, p[0], p[1], r, roof_z + 0.45, roof_z + 0.45 + 1.1 + 0.5 * frand(bid, "th", k))
    elif style in ("Apt", "AptGlass", "Tower", "Office") and signed_area(poly) > 120:
        p = random_point_inside(poly, inner, (bid, "ph"), margin=3.0)
        if p:
            w, d = 2.0 + 1.5 * frand(bid, "pw"), 2.0 + 1.5 * frand(bid, "pd")
            box(B, "Plain", p[0] - w, p[1] - d, p[0] + w, p[1] + d, roof_z, roof_z + 2.8)
            for k in range(2 + int(frand(bid, "nt") * 2)):
                q = random_point_inside(poly, inner, (bid, "tank", k), margin=1.5)
                if q and abs(q[0] - p[0]) > w + 1.2 or (q and abs(q[1] - p[1]) > d + 1.2):
                    tank = "Tank_" + ("White", "Steel", "Blue")[int(frand(bid, "tk", k) * 3)]
                    r = 0.8 + 0.3 * frand(bid, "tr", k)
                    cylinder(B, tank, q[0], q[1], r, roof_z, roof_z + 1.8 + 0.6 * frand(bid, "th", k))
    elif style == "BigBox" and signed_area(poly) > 400:
        for k in range(3 + int(frand(bid, "ac") * 5)):
            q = random_point_inside(poly, inner, (bid, "ac", k), margin=2.0)
            if q:
                box(B, "Tank_Steel", q[0] - 0.9, q[1] - 0.6, q[0] + 0.9, q[1] + 0.6, roof_z, roof_z + 1.1)


def chunk_name(cx, cy):
    """建物の中心が入る区画のモデルの名前（区画の南西の角の座標から）"""
    i = int(math.floor(cx / CHUNK))
    j = int(math.floor(cy / CHUNK))
    ew = f"E{i * int(CHUNK):05d}" if i >= 0 else f"W{-i * int(CHUNK):05d}"
    ns = f"N{j * int(CHUNK):05d}" if j >= 0 else f"S{-j * int(CHUNK):05d}"
    return f"ArahaTown_Block_{ew}_{ns}"


# ---------------------------------------------------------------------------
# 地面
# ---------------------------------------------------------------------------

def removed_cells(xs, ys, ring):
    """外側の格子の升目のうち、今の地形の格子にかかるもの [ny-1, nx-1]"""
    X, Y = np.meshgrid(xs, ys)
    inside = AM.inside(X, Y, ring)
    any_in = inside[:-1, :-1] | inside[1:, :-1] | inside[1:, 1:] | inside[:-1, 1:]
    cx = (X[:-1, :-1] + X[:-1, 1:]) / 2
    cy = (Y[:-1, :-1] + Y[1:, :-1]) / 2
    hd = 0.5 * np.hypot(X[:-1, 1:] - X[:-1, :-1], Y[1:, :-1] - Y[:-1, :-1])
    near = AM.dist(cx, cy, np.vstack([ring, ring[:1]])) < hd + 4.0
    return any_in | near


def boundary_loop(removed):
    """抜いた升目の集まりの縁（格子の頂点の番号 (iy, ix) の列、反時計回り）。いちばん長い輪を返す"""
    ny, nx = removed.shape
    nxt = {}
    R = np.pad(removed, 1)
    for iy, ix in np.argwhere(removed):
        r, c = iy + 1, ix + 1
        if not R[r - 1, c]:
            nxt.setdefault((iy, ix), []).append((iy, ix + 1))
        if not R[r, c + 1]:
            nxt.setdefault((iy, ix + 1), []).append((iy + 1, ix + 1))
        if not R[r + 1, c]:
            nxt.setdefault((iy + 1, ix + 1), []).append((iy + 1, ix))
        if not R[r, c - 1]:
            nxt.setdefault((iy + 1, ix), []).append((iy, ix))
    loops = []
    while nxt:
        start = next(iter(nxt))
        loop = [start]
        cur = start
        while True:
            outs = nxt.get(cur)
            if not outs:
                break
            n = outs.pop()
            if not outs:
                del nxt[cur]
            if n == start:
                break
            loop.append(n)
            cur = n
        loops.append(loop)
    return max(loops, key=len)


def zipper(A, B):
    """反時計回りの内側の輪 A [na, 2] と外側の輪 B [nb, 2] のあいだの三角形（番号は A が 0..na-1、B が na..）

    B の各点は、A のいちばん近い点の周長の位置で並べる（戻る所は前の位置に合わせる）。
    """
    na, nb = len(A), len(B)
    seg = np.hypot(*(np.roll(A, -1, 0) - A).T)
    acc = np.concatenate([[0.0], np.cumsum(seg)])
    ta = acc / acc[-1]
    j0 = int(np.argmin(np.hypot(*(B - A[0]).T)))
    B = np.roll(B, -j0, 0)
    near = np.array([int(np.argmin(np.hypot(*(A - q).T))) for q in B])
    tb = ta[near]
    tb = np.where(np.arange(nb) < nb // 2, np.where(tb > 0.75, tb - 1.0, tb), np.where(tb < 0.25, tb + 1.0, tb))
    tb = np.clip(np.maximum.accumulate(tb), 0.0, 1.0)
    tb = np.append(tb, 1.0)
    tris = []
    i = j = 0
    while i < na or j < nb:
        ai, aj = i % na, (i + 1) % na
        bi, bj = j % nb, (j + 1) % nb
        adv_a = j >= nb or (i < na and ta[i + 1] <= tb[j + 1])
        if adv_a:
            tris.append((ai, na + bi, aj))
            i += 1
        else:
            tris.append((ai, na + bi, na + bj))
            j += 1
    return tris, j0


def grid_normals(xs, ys, Z):
    """格子の頂点の法線（隣の頂点の高さの差から）"""
    dzdx = np.gradient(Z, xs, axis=1)
    dzdy = np.gradient(Z, ys, axis=0)
    n = np.stack([-dzdx, -dzdy, np.ones_like(Z)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def field_normals(field, px, py, e=0.25):
    hx = (field.height(px + e, py) - field.height(px - e, py)) / (2 * e)
    hy = (field.height(px, py + e) - field.height(px, py - e)) / (2 * e)
    n = np.stack([-hx, -hy, np.ones_like(hx)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)
