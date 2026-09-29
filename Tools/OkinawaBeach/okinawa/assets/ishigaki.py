"""7 琉球石灰岩の石垣（あいかた積み風の多角形の石 + かまぼこ形の笠石）

面の石: 壁面を (u=壁沿いの距離, v=高さ) の平面とみなしてボロノイ分割し、
        各セルを目地幅だけ縮めて角を丸めた「まくら形」の石にする（奥へ短いスカート付き）。
        石の裏は心材の箱で塞ぐので、目地の奥は暗く見える。
笠石  : 最上段に丸い笠石を並べる。
つなぎ: 直線は u 方向に周期的なパターンなので、端をまたぐ石は切って反対側へ回す。
        同じピースを端と端でつなぐと石の形・凹凸・テクスチャ座標が連続する。
        コーナーの両端も直線の端と同じ石を共有する。

原点: 壁の始端（壁厚の中心、地面 z=0）。壁は +X 方向に延びる。
  Ishigaki_Straight : x = 0..4, 厚さ 0.7（y = ±0.35）, 高さ 1.4
  Ishigaki_Low      : x = 0..2, 厚さ 0.6, 高さ 0.7
  Ishigaki_Corner   : 始端から +X に 2m 進んで左（+Y）へ折れ、(2, 2) で終わる。
                      外側の面 = 直線の -Y 面。終端には直線を +90° 回して原点を (2, 2) に置く。
"""
import math
import random
import zlib

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from scipy.spatial import Delaunay, Voronoi

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb

PREVIEW = dict(cam_dir=(0.45, -1.0, 0.42), lens=45)

GAP = 0.014        # 目地幅（平均）
SKIRT = 0.08       # 石の奥行き（壁の中へ）
CORE_IN = 0.06     # 心材の面の後退量
DELTA = 0.9        # 端で共有する石の範囲(m)


# ---------------------------------------------------------------------------
# 2D 多角形
# ---------------------------------------------------------------------------
def _clip(poly, n, c):
    """凸多角形を半平面 dot(n, p) <= c で切る"""
    out = []
    m = len(poly)
    for i in range(m):
        a, b = poly[i], poly[(i + 1) % m]
        da, db = n @ a - c, n @ b - c
        if da <= 0:
            out.append(a)
        if (da < 0) != (db < 0) and abs(da - db) > 1e-12:
            t = da / (da - db)
            out.append(a + (b - a) * t)
    return out


def _area(poly):
    x, y = np.asarray(poly).T
    return 0.5 * (x @ np.roll(y, -1) - y @ np.roll(x, -1))


def _centroid(poly):
    p = np.asarray(poly)
    x, y = p.T
    x1, y1 = np.roll(x, -1), np.roll(y, -1)
    cr = x * y1 - x1 * y
    a = cr.sum() / 2
    if abs(a) < 1e-12:
        return p.mean(0)
    return np.array([((x + x1) * cr).sum(), ((y + y1) * cr).sum()]) / (6 * a)


def _shrink(poly, d):
    """凸多角形（CCW）を d だけ内側へオフセット"""
    p = [np.asarray(v, dtype=np.float64) for v in poly]
    out = list(p)
    m = len(p)
    for i in range(m):
        a, b = p[i], p[(i + 1) % m]
        e = b - a
        L = np.linalg.norm(e)
        if L < 1e-9:
            continue
        n = np.array([e[1], -e[0]]) / L          # CCW の外向き
        out = _clip(out, n, n @ a - d)
        if len(out) < 3:
            return None
    return out


def _round_outline(poly, r, spacing):
    """凸多角形を半径 r で膨らませた（角が円弧の）輪郭を等間隔でサンプル"""
    p = [np.asarray(v) for v in poly]
    m = len(p)
    pts = []
    for i in range(m):
        prev, cur, nxt = p[i - 1], p[i], p[(i + 1) % m]
        e0 = cur - prev
        e1 = nxt - cur
        n0 = np.array([e0[1], -e0[0]]) / max(np.linalg.norm(e0), 1e-9)
        n1 = np.array([e1[1], -e1[0]]) / max(np.linalg.norm(e1), 1e-9)
        a0 = math.atan2(n0[1], n0[0])
        a1 = math.atan2(n1[1], n1[0])
        while a1 < a0:
            a1 += math.tau
        k = max(1, int((a1 - a0) / 0.35))
        for j in range(k + 1):
            a = a0 + (a1 - a0) * j / k
            pts.append(cur + r * np.array([math.cos(a), math.sin(a)]))
    return _resample(pts, spacing)


def _resample(pts, spacing):
    pts = np.asarray(pts)
    seg = np.linalg.norm(np.roll(pts, -1, 0) - pts, axis=1)
    total = seg.sum()
    n = max(8, int(round(total / spacing)))
    cum = np.concatenate([[0], np.cumsum(seg)])
    out = []
    for i in range(n):
        s = total * i / n
        j = min(np.searchsorted(cum, s, side="right") - 1, len(pts) - 1)
        t = (s - cum[j]) / max(seg[j], 1e-12)
        out.append(pts[j] + (pts[(j + 1) % len(pts)] - pts[j]) * t)
    return np.asarray(out)


def _inside_dist(q, outline):
    """凸な輪郭の内側への距離（外は負）"""
    p = outline
    e = np.roll(p, -1, 0) - p
    L = np.linalg.norm(e, axis=1)
    n = np.stack([e[:, 1], -e[:, 0]], 1) / L[:, None]
    d = -((q[:, None, :] - p[None]) * n[None]).sum(-1)
    return d.min(1)


# ---------------------------------------------------------------------------
# まくら形の石（パターン空間 (u, v, h)。h = 面から外への張り出し）
# ---------------------------------------------------------------------------
def _pillow(outline, spacing, hfn, skirt, fine_u=None):
    """輪郭（凸, CCW）内を三角形分割し、高さ関数 hfn(uv, d) で盛り上げる"""
    lo = outline.min(0)
    hi = outline.max(0)
    xs = np.arange(lo[0] + spacing * 0.5, hi[0], spacing)
    ys = np.arange(lo[1] + spacing * 0.5, hi[1], spacing * 0.866)
    grid = []
    for j, y in enumerate(ys):
        for x in xs:
            grid.append((x + (spacing * 0.5 if j % 2 else 0.0), y))
    if fine_u is not None:
        # 折れ目（コーナーの角）付近は細かく
        for u0 in fine_u:
            for du in np.linspace(-0.09, 0.09, 7):
                for y in np.arange(lo[1] + 0.02, hi[1], spacing * 0.5):
                    grid.append((u0 + du, y))
    grid = np.asarray(grid) if grid else np.zeros((0, 2))
    if len(grid):
        grid = grid[_inside_dist(grid, outline) > spacing * 0.45]
    pts = np.concatenate([outline, grid]) if len(grid) else outline
    tri = Delaunay(pts)
    faces = []
    for t in tri.simplices:
        a, b, c = pts[t]
        cr = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(cr) < 1e-9:
            continue  # 輪郭上の一直線の 3 点（面積ゼロ）はベイクの法線を壊すので捨てる
        if cr < 0:
            t = t[::-1]
        faces.append(tuple(int(i) for i in t))
    d = np.concatenate([np.zeros(len(outline)), _inside_dist(grid, outline)]) if len(grid) else np.zeros(len(outline))
    h = hfn(pts, d)
    n = len(pts)
    verts = np.concatenate([np.column_stack([pts, h]),
                            np.column_stack([outline, np.full(len(outline), -skirt)])])
    m = len(outline)
    for i in range(m):
        j = (i + 1) % m
        faces.append((i, n + i, n + j, j))
    return verts, faces


def _cut(verts, faces, u, keep_below):
    """u = 一定の面で切って片側だけ残す"""
    bm = bmesh.new()
    vs = [bm.verts.new(v) for v in verts]
    for f in faces:
        try:
            bm.faces.new([vs[i] for i in f])
        except ValueError:
            pass
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-7, plane_co=(u, 0, 0), plane_no=(1, 0, 0),
                           clear_outer=keep_below, clear_inner=not keep_below)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges[:])
    bm.verts.ensure_lookup_table()
    bm.verts.index_update()
    v = np.array([x.co[:] for x in bm.verts]) if bm.verts else np.zeros((0, 3))
    f = [tuple(x.index for x in face.verts) for face in bm.faces]
    bm.free()
    return v, f


def _smin(a, b, k):
    hh = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1 - hh) + a * hh - k * hh * (1 - hh)


def _rng(tag, sid):
    return random.Random(zlib.crc32(repr((tag, int(sid))).encode()))


def face_stone(poly, site, tag, sid, fine_u=None):
    """ボロノイセル poly（CCW）から石 1 個。戻り値 (verts(u,v,h), faces, edge(0..1)) または None"""
    rnd = _rng(tag, sid)
    gap = GAP * rnd.uniform(0.7, 1.4)
    rc = rnd.uniform(0.02, 0.045)                    # 角の丸み
    core = _shrink(poly, gap * 0.5 + rc)
    if core is None or abs(_area(core)) < 1e-4:
        return None
    # 輪郭の始点を一意に（端をまたいで共有する石が両側で同じになるように）
    ang = [math.atan2(p[1] - site[1], p[0] - site[0]) for p in core]
    k = int(np.argmin(ang))
    core = core[k:] + core[:k]
    outline = _round_outline(core, rc, 0.042)
    r_edge = rnd.uniform(0.025, 0.05)
    bulge = rnd.uniform(0.025, 0.05)
    dome = rnd.uniform(0.0, 0.018)
    tilt = (rnd.uniform(-0.05, 0.05), rnd.uniform(-0.06, 0.03))
    off = Vector((rnd.uniform(-90, 90), rnd.uniform(-90, 90), rnd.uniform(-90, 90)))
    dmax = max(1e-3, _inside_dist(np.asarray([site]), outline)[0])
    # 割れ面（ランダムな平面で頭を削いで角張らせる）
    facets = []
    for _ in range(rnd.randint(2, 4)):
        a = rnd.uniform(0, math.tau)
        facets.append((bulge * rnd.uniform(0.55, 1.0), math.cos(a) * rnd.uniform(0.1, 0.3),
                       math.sin(a) * rnd.uniform(0.1, 0.3)))

    def hfn(p, d):
        e = np.sqrt(1.0 - (1.0 - np.clip(d / r_edge, 0, 1)) ** 2)
        loc = p - np.asarray(site)
        n1 = np.array([geo.fbm(Vector((x * 3.2, y * 3.2, 0)) + off, 3) for x, y in loc])
        n2 = np.array([geo.ridged(Vector((x * 9.0, y * 9.0, 0.5)) + off, 3) for x, y in loc])
        h0 = bulge * e + dome * np.clip(d / dmax, 0, 1) ** 0.7
        h = h0
        for c0, gx, gy in facets:
            h = _smin(h, c0 - loc[:, 0] * gx - loc[:, 1] * gy, 0.006)
        h = np.maximum(h, h0 * 0.35)
        h += (loc[:, 0] * tilt[0] + loc[:, 1] * tilt[1]) * e
        h += (n1 * 0.02 + (n2 - 1.0) * 0.012) * e
        return h

    v, f = _pillow(outline, 0.07, hfn, SKIRT, fine_u=fine_u)
    return v, f, outline


def _cells(sites, mirrors_v, mirror_u=(), periodic=None):
    """sites: (N,2)。v 方向は mirrors_v=(lo, hi) で鏡像、u 方向は周期 or 鏡像。セル多角形 (CCW) のリスト"""
    P = np.asarray(sites)
    allp = [P]
    if periodic:
        allp += [P + [periodic, 0], P - [periodic, 0]]
    base = np.concatenate(allp)
    ext = [base]
    for m in mirror_u:
        q = base.copy()
        q[:, 0] = 2 * m - q[:, 0]
        ext.append(q)
    base = np.concatenate(ext)
    lo, hi = mirrors_v
    a = base.copy()
    a[:, 1] = 2 * lo - a[:, 1]
    b = base.copy()
    b[:, 1] = 2 * hi - b[:, 1]
    allpts = np.concatenate([base, a, b])
    vor = Voronoi(allpts)
    cells = []
    for i in range(len(P)):
        reg = vor.regions[vor.point_region[i]]
        if not reg or -1 in reg:
            cells.append(None)
            continue
        poly = [vor.vertices[j].copy() for j in reg]
        c = P[i]
        poly.sort(key=lambda q: math.atan2(q[1] - c[1], q[0] - c[0]))
        cells.append(poly)
    return cells


def _spacing(v, Hb):
    return 0.43 - 0.1 * np.clip(v / Hb, 0, 1)


def _dart(rng, u0, u1, v0, v1, Hb, existing, n_try=4000, periodic=None):
    pts = [tuple(p) for p in existing]
    new = []
    for _ in range(n_try):
        u = rng.uniform(u0, u1)
        v = rng.uniform(v0, v1)
        r = _spacing(v, Hb) * 0.82 * rng.uniform(0.7, 1.35)
        ok = True
        for (a, b) in pts:
            du = abs(a - u)
            if periodic:
                du = min(du, periodic - du)
            if du * du + (b - v) ** 2 < r * r:
                ok = False
                break
        if ok:
            pts.append((u, v))
            new.append((u, v))
    return new


def straight_pattern(L, Hb, vb, seed, iters=1):
    """周期 L のパターン。戻り値 sites (N,2)"""
    rng = random.Random(seed)
    s = np.asarray(_dart(rng, 0, L, vb + 0.05, Hb - 0.05, Hb, [], periodic=L))
    for _ in range(iters):
        cells = _cells(s, (vb, Hb), periodic=L)
        s = np.asarray([_centroid(c) if c is not None else p for p, c in zip(s, cells)])
        s[:, 0] %= L
    return s


def domain_sites(U, Hb, vb, seed, base=None, L=None, start=False, end=False, iters=1):
    """コーナー用の領域 [0, U]。start/end = 直線パターン base の端の石を共有する。
    戻り値: list of (u, v, sid, tex_off, fixed)"""
    rng = random.Random(seed)
    fixed = []
    if start:
        fixed += [(u, v, i, 0.0) for i, (u, v) in enumerate(base) if u < DELTA]
        fixed += [(u - L, v, i, L) for i, (u, v) in enumerate(base) if u > L - 1.4]
    if end:
        fixed += [(u + U - L, v, i, L - U) for i, (u, v) in enumerate(base) if u > L - DELTA]
        fixed += [(u + U, v, i, -U) for i, (u, v) in enumerate(base) if u < 1.4]
    fu0 = DELTA if start else 0.0
    fu1 = U - DELTA if end else U
    fresh = _dart(rng, fu0, fu1, vb + 0.05, Hb - 0.05, Hb, [(a, b) for a, b, _, _ in fixed])
    fresh = np.asarray(fresh) if fresh else np.zeros((0, 2))
    mir = []
    if not start:
        mir.append(0.0)
    if not end:
        mir.append(U)
    for _ in range(iters):
        if not len(fresh):
            break
        allp = np.concatenate([np.asarray([(a, b) for a, b, _, _ in fixed]).reshape(-1, 2), fresh])
        cells = _cells(allp, (vb, Hb), mirror_u=mir)
        nf = len(fixed)
        for i in range(len(fresh)):
            c = cells[nf + i]
            if c is not None:
                cc = _centroid(c)
                cc[0] = min(max(cc[0], fu0), fu1)
                fresh[i] = cc
    out = [(a, b, sid, off, True) for a, b, sid, off in fixed]
    out += [(a, b, 100000 + i, 0.0, False) for i, (a, b) in enumerate(fresh)]
    return out, mir


# ---------------------------------------------------------------------------
# 組み立て（全部の石を 1 メッシュにまとめる）
# ---------------------------------------------------------------------------
class Acc:
    def __init__(self):
        self.v, self.f, self.sc, self.sid = [], [], [], []
        self.n = 0

    def add(self, verts3, faces, sc, sid, flip=False):
        if len(verts3) == 0:
            return
        self.v.append(np.asarray(verts3))
        self.sc.append(np.asarray(sc))
        self.sid.append(np.asarray(sid))
        for f in faces:
            f = tuple(i + self.n for i in f)
            self.f.append(f[::-1] if flip else f)
        self.n += len(verts3)

    def build(self, name):
        V = np.concatenate(self.v)
        me = bpy.data.meshes.new(name)
        me.from_pydata(V.tolist(), [], self.f)
        me.update()
        for aname, data in (("SC", np.concatenate(self.sc)), ("SID", np.concatenate(self.sid))):
            a = me.color_attributes.new(aname, "FLOAT_COLOR", "POINT")
            rgba = np.concatenate([data, np.ones((len(data), 1))], 1).astype(np.float32)
            a.data.foreach_set("color", rgba.ravel())
        obj = bpy.data.objects.new(name, me)
        C.link_object(obj)
        return obj


def _stone_pieces(poly, site, tag, sid, lo, hi, fine_u=None, wrap=None):
    """石を作り [lo, hi] の外側を切る。wrap=L なら外側を反対側へ回す（周期パターン）
    戻り値: list of (verts(u,v,h) 配置後, faces, u_tex(頂点ごと), edge)"""
    st = face_stone(poly, site, tag, sid, fine_u)
    if st is None:
        return []
    v, f, outline = st
    pieces = []
    umin, umax = v[:, 0].min(), v[:, 0].max()
    parts = [(v, f, 0.0)]
    if umin < lo or umax > hi:
        parts = []
        mid_v, mid_f = v, f
        if umin < lo:
            a = _cut(v, f, lo, keep_below=True)
            if wrap and len(a[0]):
                parts.append((a[0], a[1], wrap))
            mid_v, mid_f = _cut(mid_v, mid_f, lo, keep_below=False)
        if umax > hi:
            b = _cut(mid_v, mid_f, hi, keep_below=False)
            if wrap and len(b[0]):
                parts.append((b[0], b[1], -wrap))
            mid_v, mid_f = _cut(mid_v, mid_f, hi, keep_below=True)
        if len(mid_v):
            parts.append((mid_v, mid_f, 0.0))
    for pv, pf, shift in parts:
        if not len(pf):
            continue
        uv = pv[:, :2]
        d = _inside_dist(uv, outline)
        edge = np.clip(d / 0.05, 0, 1)
        edge[pv[:, 2] < -0.01] = 0.0
        pv2 = pv.copy()
        u_tex = pv[:, 0].copy()
        pv2[:, 0] += shift
        pieces.append((pv2, pf, u_tex, edge))
    return pieces


def _add_face(acc, pieces, mapper, flip, tex_off, rnd_val, layer):
    for pv, pf, u_tex, edge in pieces:
        P = mapper(pv)
        sc = np.column_stack([u_tex + tex_off, pv[:, 1], pv[:, 2]])
        sid = np.column_stack([np.full(len(pv), rnd_val), edge, np.full(len(pv), layer)])
        acc.add(P, pf, sc, sid, flip)


def cap_stone(acc, x0, x1, y0, y1, zb, H, tag, sid, tex, axis_x=True):
    """かまぼこ形の笠石。足元 [x0,x1]x[y0,y1] は世界座標の平面"""
    rnd = _rng(tag, sid)
    w = min(x1 - x0, y1 - y0)
    rw = w * 0.5
    side_h = 0.05
    dome = H - zb - side_h + rnd.uniform(-0.03, 0.01)
    off = Vector((rnd.uniform(-90, 90), rnd.uniform(-90, 90), rnd.uniform(-90, 90)))
    rc = 0.03
    poly = [np.array(p) for p in ((x0 + rc, y0 + rc), (x1 - rc, y0 + rc), (x1 - rc, y1 - rc), (x0 + rc, y1 - rc))]
    outline = _round_outline(poly, rc, 0.045)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    tilt = (rnd.uniform(-0.03, 0.03), rnd.uniform(-0.03, 0.03))
    facets = []
    for _ in range(rnd.randint(1, 3)):
        a = rnd.uniform(0, math.tau)
        facets.append((side_h + dome * rnd.uniform(0.75, 1.0), math.cos(a) * rnd.uniform(0.1, 0.35),
                       math.sin(a) * rnd.uniform(0.1, 0.35)))

    def hfn(p, d):
        dx = np.minimum(p[:, 0] - x0, x1 - p[:, 0])
        dy = np.minimum(p[:, 1] - y0, y1 - p[:, 1])
        dd = (np.maximum(dx, 1e-4) ** -6 + np.maximum(dy, 1e-4) ** -6) ** (-1 / 6)
        e = np.sqrt(1.0 - (1.0 - np.clip(dd / rw, 0, 1)) ** 2)
        loc = p - np.array([cx, cy])
        n1 = np.array([geo.fbm(Vector((x * 3.0, y * 3.0, 0)) + off, 3) for x, y in loc])
        n2 = np.array([geo.ridged(Vector((x * 8.0, y * 8.0, 0.5)) + off, 3) for x, y in loc])
        e0 = np.clip(d / 0.04, 0, 1)
        h0 = side_h + dome * e
        h = h0
        for c0, gx, gy in facets:
            h = _smin(h, c0 - loc[:, 0] * gx - loc[:, 1] * gy, 0.01)
        h = np.maximum(h, side_h + dome * e * 0.5)
        return h + (loc[:, 0] * tilt[0] + loc[:, 1] * tilt[1]) * e0 + \
            (n1 * 0.022 + (n2 - 1.0) * 0.012) * e0

    v, f = _pillow(outline, 0.075, hfn, 0.0, None)
    v[:, 2] = np.where(np.arange(len(v)) >= len(v) - len(outline), -0.03, v[:, 2])
    P = np.column_stack([v[:, 0], v[:, 1], zb + v[:, 2]])
    d = _inside_dist(v[:, :2], outline)
    edge = np.clip(d / 0.05, 0, 1)
    edge[len(v) - len(outline):] = 0
    if axis_x:
        sc = np.column_stack([v[:, 0] + tex, P[:, 2], v[:, 1]])
    else:
        sc = np.column_stack([v[:, 1] + tex, P[:, 2], v[:, 0]])
    sid = np.column_stack([np.full(len(v), rnd.random()), edge, np.full(len(v), 2.0)])
    acc.add(P, f, sc, sid)


def _cap_row(acc, length, zb, H, T, tag, seed, to_world, tex=0.0, start_gap=True, end_gap=True):
    """長さ length の笠石の列（局所 x 方向）。to_world(x0,x1,y0,y1) で世界座標の矩形を得る"""
    rnd = random.Random(seed)
    n = max(1, round(length / 0.5))
    cuts = [0.0]
    for i in range(1, n):
        cuts.append(length * i / n + rnd.uniform(-0.08, 0.08))
    cuts.append(length)
    ov = 0.035
    for i in range(n):
        g0 = GAP * 0.5 if (i > 0 or start_gap) else 0.0
        g1 = GAP * 0.5 if (i < n - 1 or end_gap) else 0.0
        x0, x1 = cuts[i] + g0, cuts[i + 1] - g1
        rect, axis_x = to_world(x0, x1, -T / 2 - ov, T / 2 + ov)
        cap_stone(acc, *rect, zb, H, tag, seed * 100 + i, tex, axis_x)


def _core_box(acc, x0, x1, y0, y1, z0, z1):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((x0 if v.co.x < 0 else x1, y0 if v.co.y < 0 else y1, z0 if v.co.z < 0 else z1))
    bm.verts.ensure_lookup_table()
    bm.verts.index_update()
    V = np.array([v.co[:] for v in bm.verts])
    F = [tuple(v.index for v in f.verts) for f in bm.faces]
    bm.free()
    sc = np.column_stack([V[:, 0], V[:, 2], V[:, 1]])
    sid = np.column_stack([np.full(len(V), 0.5), np.zeros(len(V)), np.full(len(V), 3.0)])
    acc.add(V, F, sc, sid)


def _finish(name, acc, cores):
    """石のメッシュを先に UV 展開し（uv="keep"）、目地の奥にしか見えない心材は UV の隅の小さな区画に押し込む
    （心材に普通にテクスチャを割り当てると面積の 1/3 近くを無駄にするため）"""
    obj = acc.build(name)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges[:])
    bm.to_mesh(obj.data)
    bm.free()
    C.set_smooth(obj, True, angle=55)
    C.smart_uv(obj, angle=60, margin=0.003, uv_name="Bake")
    layer = obj.data.uv_layers["Bake"]
    uv = np.empty(len(layer.data) * 2, np.float32)
    layer.data.foreach_get("uv", uv)
    layer.data.foreach_set("uv", uv * 0.97)
    cacc = Acc()
    for box in cores:
        _core_box(cacc, *box)
    core = cacc.build(name + "_core")
    cl = core.data.uv_layers.new(name="Bake")
    for p in core.data.polygons:
        for k, li in enumerate(p.loop_indices):
            cl.data[li].uv = (0.975 + 0.02 * (k in (1, 2)), 0.975 + 0.02 * (k in (2, 3)))
    return [obj, core]


class Wall:
    def __init__(self, L, H, T, seed, cap_h=0.24):
        self.L, self.H, self.T, self.seed = L, H, T, seed
        self.Hb = H - cap_h
        self.vb = -0.1
        self.front = straight_pattern(L, self.Hb, self.vb, seed * 10 + 1)
        self.back = straight_pattern(L, self.Hb, self.vb, seed * 10 + 2)


def build_straight(name, w):
    acc = Acc()
    L, T = w.L, w.T
    for tag, sites, sgn in (("F", w.front, -1), ("B", w.back, 1)):
        cells = _cells(sites, (w.vb, w.Hb), periodic=L)
        tagk = (w.seed, tag)
        for i, (site, poly) in enumerate(zip(sites, cells)):
            if poly is None:
                continue
            pieces = _stone_pieces(poly, site, tagk, i, 0.0, L, wrap=L)
            rv = _rng(tagk, i + 7777).random()

            def mapper(pv, sgn=sgn):
                return np.column_stack([pv[:, 0], sgn * (T / 2 + pv[:, 2]), pv[:, 1]])

            _add_face(acc, pieces, mapper, flip=(sgn > 0), tex_off=0.0, rnd_val=rv, layer=0 if sgn < 0 else 1)
    _cap_row(acc, L, w.Hb, w.H, T, (w.seed, "C"), w.seed * 31,
             lambda x0, x1, y0, y1: ((x0, x1, y0, y1), True))
    return _finish(name, acc, [(0.0, L, -T / 2 + CORE_IN, T / 2 - CORE_IN, -0.08, w.Hb + 0.01)])


def _path_mapper(segs, rho=0.0):
    """折れ線（2 本の直線 + 角の円弧）に沿った写像。segs = (p0, corner, p1)。右側を外向き法線にする"""
    p0, pc, p1 = (np.asarray(p, dtype=np.float64) for p in segs)
    t0 = (pc - p0) / np.linalg.norm(pc - p0)
    t1 = (p1 - pc) / np.linalg.norm(p1 - pc)
    # 円弧の始点・終点
    a = pc - t0 * rho
    b = pc + t1 * rho
    L0 = np.linalg.norm(a - p0)
    Larc = rho * math.pi / 2
    L1 = np.linalg.norm(p1 - b)
    right0 = np.array([t0[1], -t0[0]])
    right1 = np.array([t1[1], -t1[0]])
    center = a - right0 * rho

    def pos_nrm(u):
        if u <= L0:
            return p0 + t0 * u, right0
        if u <= L0 + Larc:
            s = (u - L0) / max(Larc, 1e-9)
            ang = s * math.pi / 2
            n = right0 * math.cos(ang) + t0 * math.sin(ang)
            return center + n * rho, n
        return b + t1 * (u - L0 - Larc), right1

    def mapper(pv):
        out = np.empty_like(pv)
        for i, (u, v, h) in enumerate(pv):
            p, n = pos_nrm(u)
            q = p + n * h
            out[i] = (q[0], q[1], v)
        return out

    return mapper, L0 + Larc + L1, L0 + Larc * 0.5


def _line_mapper(p0, p1, left=False):
    p0, p1 = np.asarray(p0, dtype=np.float64), np.asarray(p1, dtype=np.float64)
    t = (p1 - p0) / np.linalg.norm(p1 - p0)
    n = np.array([-t[1], t[0]]) if left else np.array([t[1], -t[0]])

    def mapper(pv):
        q = p0[None] + t[None] * pv[:, :1] + n[None] * pv[:, 2:3]
        return np.column_stack([q, pv[:, 1]])

    return mapper, float(np.linalg.norm(p1 - p0))


def _build_domain(acc, w, sites_info, mirrors, U, tagk, mapper, flip, layer, fine_u=None):
    pts = np.asarray([(a, b) for a, b, _, _, _ in sites_info])
    cells = _cells(pts, (w.vb, w.Hb), mirror_u=mirrors)
    for (a, b, sid, toff, fixed), poly in zip(sites_info, cells):
        if poly is None:
            continue
        # 領域に掛からない石は飛ばす
        us = [p[0] for p in poly]
        if max(us) < 0 or min(us) > U:
            continue
        # 共有する石は直線と同じ seed（tagk, 直線での番号）、新しい石は別の seed
        key = tagk if fixed else (tagk, "corner")
        fu = [f for f in (fine_u or []) if min(us) - 0.1 < f < max(us) + 0.1] or None
        pieces = _stone_pieces(poly, (a, b), key, sid, 0.0, U, fine_u=fu, wrap=None)
        rv = _rng(key, sid + 7777).random()
        _add_face(acc, pieces, mapper, flip=flip, tex_off=toff, rnd_val=rv, layer=layer)


def build_corner(name, w, La=2.0, Lb=2.0):
    acc = Acc()
    T, L = w.T, w.L
    h = T / 2
    # 外側（直線の前面 F と共有、凸の角は石が回り込む）
    mapper, U, u_corner = _path_mapper(((0, -h), (La + h, -h), (La + h, Lb)), rho=0.07)
    info, mir = domain_sites(U, w.Hb, w.vb, w.seed * 10 + 5, base=w.front, L=L, start=True, end=True)
    _build_domain(acc, w, info, mir, U, (w.seed, "F"), mapper, flip=False, layer=0, fine_u=[u_corner])
    # 内側（直線の背面 B と共有、入隅で突き当てる）
    ia = La - h - 0.03
    mapA, UA = _line_mapper((0, h), (ia, h), left=True)
    info, mir = domain_sites(UA, w.Hb, w.vb, w.seed * 10 + 6, base=w.back, L=L, start=True)
    _build_domain(acc, w, info, mir, UA, (w.seed, "B"), mapA, flip=True, layer=1)
    mapB, UB = _line_mapper((La - h, h + 0.03), (La - h, Lb), left=True)
    info, mir = domain_sites(UB, w.Hb, w.vb, w.seed * 10 + 7, base=w.back, L=L, end=True)
    _build_domain(acc, w, info, mir, UB, (w.seed, "B"), mapB, flip=True, layer=1)
    # 笠石: 脚 A、角の大石、脚 B
    ov = 0.035
    _cap_row(acc, La - h, w.Hb, w.H, T, (w.seed, "CA"), w.seed * 37,
             lambda x0, x1, y0, y1: ((x0, x1, y0, y1), True), end_gap=True)
    cap_stone(acc, La - h + GAP * 0.5, La + h + ov, -h - ov, h, w.Hb, w.H, (w.seed, "CC"), 0, 50.0)
    _cap_row(acc, Lb - h, w.Hb, w.H, T, (w.seed, "CB"), w.seed * 41,
             lambda x0, x1, y0, y1: ((La - y1, La - y0, h + x0, h + x1), False), start_gap=True)
    return _finish(name, acc, [(0.0, La + h - CORE_IN, -h + CORE_IN, h - CORE_IN, -0.08, w.Hb + 0.01),
                               (La - h + CORE_IN, La + h - CORE_IN, -h + CORE_IN, Lb, -0.08, w.Hb + 0.01)])


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------
def ishigaki_material(name, res=2048):
    """風化した琉球石灰岩。座標は頂点属性 SC（壁沿い u, 高さ, 張り出し）なので継ぎ目で連続する"""

    def fn(nb):
        sc = nb.attr("SC", "Vector")
        sid = nb.sep(nb.attr("SID", "Vector"))
        rnd, edge = sid[0], sid[1]
        z = nb.sep(sc)[1]
        big = nb.noise(sc, 0.9, 5, 0.6)
        mid = nb.noise(sc, 4.0, 6, 0.62)
        fine = nb.noise(sc, 55.0, 4, 0.6)
        # 多孔質の穴
        warp = nb.mapping(sc, loc=(0.3, 0.1, 0.2))
        pa = nb.smooth(nb.voronoi(nb.add(sc, nb.mul(nb.noise(warp, 6.0, 3, 0.5), 0.05)), 14.0), 0.1, 0.0)
        pb = nb.smooth(nb.voronoi(nb.mapping(sc, loc=(3.1, 1.7, 0.4)), 37.0), 0.14, 0.0)
        pmask = nb.smooth(nb.noise(sc, 2.5, 3, 0.5), 0.4, 0.6)
        pits = nb.mul(nb.math("MAXIMUM", nb.mul(pa, 0.8), nb.mul(pb, 0.55)), nb.maprange(pmask, 0, 1, 0.3, 1.0))
        # 石ごとの色味（白っぽい / 黄味 / 灰色）
        base = nb.ramp(rnd, [(0.0, srgb("#968f7f")), (0.35, srgb("#aca38f")), (0.65, srgb("#bab19c")),
                             (1.0, srgb("#8e8a81"))])
        base = nb.mix(base, srgb("#c9c0a8"), nb.mul(nb.smooth(big, 0.5, 0.75), 0.5))
        col = nb.mix(base, srgb("#8a857a"), nb.mul(nb.smooth(mid, 0.45, 0.7), 0.6))
        # 黒い地衣類（大きな斑 + 細かい点）
        lich = nb.smooth(nb.noise(sc, 3.2, 5, 0.65, distortion=0.4), 0.52, 0.64)
        dots = nb.smooth(nb.noise(sc, 16.0, 4, 0.6), 0.64, 0.72)
        dark = nb.math("MAXIMUM", nb.mul(lich, 0.85), nb.mul(dots, 0.4))
        lcol = nb.mix(srgb("#2f2e2a"), srgb("#4a4840"), nb.smooth(fine, 0.4, 0.7))
        col = nb.mix(col, lcol, dark)
        # 灰白色の地衣類
        wl = nb.smooth(nb.noise(nb.mapping(sc, loc=(7, 3, 1)), 6.0, 4, 0.6), 0.64, 0.72)
        col = nb.mix(col, srgb("#d9d6cc"), nb.mul(wl, 0.6))
        # 上から垂れる黒い筋
        streak = nb.noise(nb.mapping(sc, scale=(9, 0.6, 9)), 3.0, 4, 0.6)
        col = nb.mix(col, srgb("#4b4840"), nb.mul(nb.smooth(streak, 0.55, 0.75), 0.45))
        # 根元の苔（緑）と湿り
        moss = nb.mul(nb.smooth(z, 0.45, 0.02), nb.smooth(nb.noise(sc, 5.0, 5, 0.6), 0.38, 0.6))
        col = nb.mix(col, nb.mix(srgb("#4c5a2a"), srgb("#6d7437"), fine), nb.mul(moss, 0.85))
        col = nb.mix(col, srgb("#6b6453"), nb.mul(nb.smooth(z, 0.3, -0.05), 0.35))
        # 目地の際は汚れて暗い
        col = nb.mix(col, srgb("#4d483e"), nb.mul(nb.sub(1.0, edge), 0.55))
        # 穴の奥
        col = nb.mix(col, srgb("#3f3b33"), nb.mul(pits, 0.7))
        rough = nb.maprange(nb.add(nb.mul(moss, -0.2), nb.mul(fine, 0.12)), -0.2, 0.12, 0.72, 0.97)
        ridge = nb.noise(nb.mapping(sc, scale=(1, 2.0, 1)), 7.0, 8, 0.7, distortion=0.4)
        height = nb.add(nb.add(nb.mul(pits, -0.7), nb.mul(ridge, 0.45)), nb.mul(fine, 0.2))
        cavity = nb.mul(nb.maprange(pits, 0, 1, 1.0, 0.55), nb.maprange(edge, 0, 1, 0.75, 1.0))
        return dict(color=col, rough=rough, height=height, height_scale=0.03, cavity=cavity)

    return pbr_material(name, fn, res=res, ao_distance=0.35, uv="keep")


def build():
    std = Wall(4.0, 1.4, 0.7, seed=3)
    low = Wall(2.0, 0.7, 0.6, seed=5, cap_h=0.2)
    out = {}
    for vname, objs, mat in (
            ("Ishigaki_Straight", build_straight("Ishigaki_Straight", std), ishigaki_material("IshigakiStraight")),
            ("Ishigaki_Corner", build_corner("Ishigaki_Corner", std), ishigaki_material("IshigakiCorner")),
            ("Ishigaki_Low", build_straight("Ishigaki_Low", low), ishigaki_material("IshigakiLow", res=1024))):
        for o in objs:
            C.assign(o, mat)
        out[vname] = objs
    return out
