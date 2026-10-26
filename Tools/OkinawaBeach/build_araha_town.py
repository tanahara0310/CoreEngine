"""アラハビーチのまわりの街（地面・建物）のモデルを書き出す（Blender を使わない）

使い方: python Tools/OkinawaBeach/build_araha_town.py [blocks] [ground] [seawall]（省くと全部）
data/araha_town.json（OpenStreetMap の建物・道路・面・海岸線）と data/araha_dem.npz（国土地理院の標高タイル）から、
  ArahaTown_Ground        : 公園の外の街の地面と、その外の遠くの丘・海底（道路・駐車場・緑地を描いたテクスチャ）
  ArahaTown_Block_<区画>  : 512 m 四方ごとの建物（外壁・屋上・給水タンク・塔屋）
  ArahaTown_Seawall       : 浜と突堤と砂浜の外の海岸の護岸（壁と天端）
  ArahaTown_Textures      : 建物の外壁と屋上のテクスチャ（区画のモデルが共有する）
を Projects/Sandbox/Application/Assets/Models/Okinawa へ書き出す。どのモデルも原点に回転なしで置く。
"""
import math
import os
import shutil
import sys
import time
import uuid

import numpy as np

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOL_DIR)
from okinawa import araha_map as AM  # noqa: E402
from okinawa import araha_town as AT  # noqa: E402
from okinawa import gltf_write as GW  # noqa: E402
from okinawa import town_mesh as TM  # noqa: E402
from okinawa import town_textures as TT  # noqa: E402

REPO = os.path.dirname(os.path.dirname(TOOL_DIR))
PROJECT_DIR = os.path.join(REPO, "Projects", "Sandbox")
MODELS_DIR = os.path.join(PROJECT_DIR, "Application", "Assets", "Models", "Okinawa")
TEX_NAME = "ArahaTown_Textures"
TEX_DIR = os.path.join(MODELS_DIR, TEX_NAME)
GROUND = "ArahaTown_Ground"
SEAWALL = "ArahaTown_Seawall"
GUID_NAMESPACE = uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/tanahara0310/CoreEngine/Tools/OkinawaBeach")
BLEND_OUT = 60.0     # 今の地形の格子の外で、その高さから外側の高さへ移る幅 [m]
SKIRT = 60.0         # 外側の地面の外周から下へ垂らす深さ [m]
# 護岸: 天端の幅・陸の地面からの高さ・壁の下端 [m]、浜・突堤・砂浜から離す距離 [m]
WALL_TOP_WIDTH = 6.0
WALL_TOP_RISE = 0.3
WALL_BOTTOM = -3.0
WALL_CLEAR_BEACH = 14.0
WALL_CLEAR_JETTY = 6.0
WALL_CLEAR_JETTY_OUTLINE = 22.0
JETTY_ROOT_FREE = 6.0
WALL_CLEAR_SAND = 10.0
WALL_MIN_RUN = 12.0


def write_meta(path, kind):
    meta = path + ".meta"
    if os.path.exists(meta):
        return
    rel = os.path.relpath(path, PROJECT_DIR).replace(os.sep, "/")
    guid = str(uuid.uuid5(GUID_NAMESPACE, rel))
    with open(meta, "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n  "guid": "%s",\n  "type": "%s"\n}\n' % (guid, kind))


def fresh_dir(path):
    if os.path.isdir(path):
        shutil.rmtree(path)
    os.makedirs(path)


# ---------------------------------------------------------------------------
# 材質
# ---------------------------------------------------------------------------

def building_materials():
    fresh_dir(TEX_DIR)
    mats = {}
    seed = 11
    for style, (_, _, palettes, _) in TT.STYLES.items():
        for name, _ in palettes:
            key = TM.facade_key(style, name)
            prefix = f"ArahaTown_{key}"
            TT.save_maps(TEX_DIR, prefix, TT.paint_facade(style, name, seed))
            seed += 1
            rel = f"../{TEX_NAME}/{prefix}"
            mats[key] = GW.Material(f"ArahaTown_{key}", f"{rel}_BaseColor.png", f"{rel}_Normal.png", f"{rel}_ORM.png")
    for kind in ("Concrete", "Coated", "Metal"):
        prefix = f"ArahaTown_Roof_{kind}"
        TT.save_maps(TEX_DIR, prefix, TT.paint_roof(kind, seed))
        seed += 1
        rel = f"../{TEX_NAME}/{prefix}"
        mats[f"Roof_{kind}"] = GW.Material(prefix, f"{rel}_BaseColor.png", f"{rel}_Normal.png", f"{rel}_ORM.png")
    mats["Plain"] = GW.Material("ArahaTown_Plain", base=(0.66, 0.65, 0.62, 1.0), rough=0.85)
    mats["Tank_White"] = GW.Material("ArahaTown_TankWhite", base=(0.78, 0.78, 0.76, 1.0), rough=0.45)
    mats["Tank_Blue"] = GW.Material("ArahaTown_TankBlue", base=(0.10, 0.22, 0.50, 1.0), rough=0.4)
    mats["Tank_Steel"] = GW.Material("ArahaTown_TankSteel", base=(0.62, 0.63, 0.64, 1.0), rough=0.35, metal=0.9)
    for f in sorted(os.listdir(TEX_DIR)):
        write_meta(os.path.join(TEX_DIR, f), "Texture")
    return mats


# ---------------------------------------------------------------------------
# 地面の高さ
# ---------------------------------------------------------------------------

class Ground:
    """外側の格子の上の地面の高さ（今の地形の格子の中は araha_map の Field、その外は街の標高と海の深さ）"""

    def __init__(self, town, m, field, ring_pts):
        x0, x1, y0, y1 = AT.FAR_BOX
        self.xs = AT.axis_lines(x0, x1, AT.INNER_BOX[0], AT.INNER_BOX[1])
        self.ys = AT.axis_lines(y0, y1, AT.INNER_BOX[2], AT.INNER_BOX[3])
        X, Y = np.meshgrid(self.xs, self.ys)
        t = time.time()
        z_far = AT.far_height(town, m, X, Y)
        inside = AM.inside(X, Y, ring_pts)
        d_out = np.where(inside, 0.0, AM.dist(X, Y, np.vstack([ring_pts, ring_pts[:1]])))
        near = d_out < BLEND_OUT
        z = z_far.copy()
        zf = field.height(X[near], Y[near])
        w = AM.smoothstep(0.0, BLEND_OUT, d_out[near])
        z[near] = zf * (1 - w) + z_far[near] * w
        self.Z = z
        self.land = town.is_land(X, Y)
        self.sea_d = town.sea_distance(X, Y)
        print(f"  地面の高さ {z.shape} ({time.time() - t:.0f}s)")

    def __call__(self, px, py):
        xs, ys, Z = self.xs, self.ys, self.Z
        px = np.asarray(px, float)
        py = np.asarray(py, float)
        ix = np.clip(np.searchsorted(xs, px) - 1, 0, len(xs) - 2)
        iy = np.clip(np.searchsorted(ys, py) - 1, 0, len(ys) - 2)
        tx = np.clip((px - xs[ix]) / (xs[ix + 1] - xs[ix]), 0, 1)
        ty = np.clip((py - ys[iy]) / (ys[iy + 1] - ys[iy]), 0, 1)
        return (Z[iy, ix] * (1 - tx) * (1 - ty) + Z[iy, ix + 1] * tx * (1 - ty)
                + Z[iy + 1, ix] * (1 - tx) * ty + Z[iy + 1, ix + 1] * tx * ty)


# ---------------------------------------------------------------------------
# 建物
# ---------------------------------------------------------------------------

def build_blocks(town, ground, mats, park):
    """区画ごとの建物のモデルを書き出す。park（公園の輪郭）の中の建物は作らない"""
    ctx = TM.Context(town)
    blocks = {}
    stats = {}
    skipped = 0
    t = time.time()
    for b in town.buildings:
        poly = TM.clean_polygon(b["_a"])
        if poly is None:
            continue
        cx, cy = poly.mean(axis=0)
        r = math.hypot(cx, cy)
        area = TM.signed_area(poly)
        if area < 8.0 or (r > TM.SMALL_SKIP_RADIUS and area < TM.SMALL_SKIP_AREA):
            skipped += 1
            continue
        if not (AT.FAR_BOX[0] < cx < AT.FAR_BOX[1] and AT.FAR_BOX[2] < cy < AT.FAR_BOX[3]):
            continue
        if AM.inside(np.array([cx]), np.array([cy]), park)[0]:
            skipped += 1
            continue
        info = TM.classify(b, poly, ctx)
        if info is None:
            continue
        name = TM.chunk_name(cx, cy)
        B = blocks.get(name)
        if B is None:
            B = blocks[name] = TM.Builder((0.0, 0.0))
        TM.build_building(B, b, poly, info, ground, r < TM.DETAIL_RADIUS)
        stats[info["style"]] = stats.get(info["style"], 0) + 1
    print(f"  建物 {sum(stats.values())} 棟 {dict(sorted(stats.items()))}（小さくて省いた {skipped}）({time.time() - t:.0f}s)")
    for name in os.listdir(MODELS_DIR):
        if name.startswith("ArahaTown_Block_"):
            shutil.rmtree(os.path.join(MODELS_DIR, name))
    total = 0
    for name in sorted(blocks):
        out_dir = os.path.join(MODELS_DIR, name)
        fresh_dir(out_dir)
        B = blocks[name]
        prims = [(mats[k], B.parts[k]) for k in sorted(B.parts)]
        path = os.path.join(out_dir, name + ".gltf")
        total += GW.write(path, name, prims)
        write_meta(path, "Model")
    print(f"  区画 {len(blocks)} 個・{total} 三角形")
    return sorted(blocks)


# ---------------------------------------------------------------------------
# 地面
# ---------------------------------------------------------------------------

def tile_boxes(x0, x1, y0, y1, size):
    """size 四方のタイル（余白込みの範囲）の一覧 {(i, j): (box, core)}"""
    out = {}
    for i in range(int(math.floor(x0 / size)), int(math.ceil(x1 / size))):
        for j in range(int(math.floor(y0 / size)), int(math.ceil(y1 / size))):
            core = (i * size, (i + 1) * size, j * size, (j + 1) * size)
            m = AT.TILE_MARGIN
            out[(i, j)] = ((core[0] - m, core[1] + m, core[2] - m, core[3] + m), core)
    return out


def ring_with_cuts(P, ring_idx, parts):
    """今の地形の格子の外周の点列に、面の種類の境目で切った点を足したもの [n, 3]（反時計回り）"""
    allv = np.concatenate([v for v, _ in parts.values()])
    out = []
    n = len(ring_idx)
    for q in range(n):
        a = P[ring_idx[q]]
        b = P[ring_idx[(q + 1) % n]]
        e = b - a
        L2 = float(e @ e)
        lo = np.minimum(a, b) - 1e-4
        hi = np.maximum(a, b) + 1e-4
        cand = allv[(allv[:, 0] >= lo[0]) & (allv[:, 0] <= hi[0]) & (allv[:, 1] >= lo[1]) & (allv[:, 1] <= hi[1])]
        t = ((cand[:, 0] - a[0]) * e[0] + (cand[:, 1] - a[1]) * e[1]) / L2
        off = np.abs((cand[:, 0] - a[0]) * e[1] - (cand[:, 1] - a[1]) * e[0]) / math.sqrt(L2)
        on = (off < 1e-5) & (t > -1e-7) & (t < 1.0 - 1e-7)
        pts = cand[on]
        order = np.argsort(t[on])
        seen = set()
        for p in pts[order]:
            k = (round(float(p[0]), 4), round(float(p[1]), 4))
            if k not in seen:
                seen.add(k)
                out.append(p)
    return np.array(out)


def build_ground(town, m, field, P, parts, ring_idx, ground):
    out_dir = os.path.join(MODELS_DIR, GROUND)
    fresh_dir(out_dir)
    xs, ys, Zf = ground.xs, ground.ys, ground.Z
    nx, ny = len(xs), len(ys)
    NF = nx * ny
    t = time.time()
    # ---- 頂点（外側の格子 → 今の地形の格子の中の街 → 今の地形の格子の外周）----
    X, Y = np.meshgrid(xs, ys)
    town_v, town_t = parts.get(5, (np.zeros((0, 3)), np.zeros((0, 3), int)))
    ring = ring_with_cuts(P, ring_idx, parts)
    pos = np.concatenate([np.stack([X.ravel(), Y.ravel(), Zf.ravel()], -1), town_v, ring])
    nrm = np.concatenate([TM.grid_normals(xs, ys, Zf).reshape(-1, 3),
                          TM.field_normals(field, town_v[:, 0], town_v[:, 1]),
                          TM.field_normals(field, ring[:, 0], ring[:, 1])])
    NT = NF + len(town_v)

    def fid(iy, ix):
        return iy * nx + ix

    tris = [(a + NF, b + NF, c + NF, "near") for a, b, c in town_t]
    n_near = len(tris)
    # ---- 外側の格子（今の地形の格子にかかる升目と、陸から遠い海を抜く）----
    ring_pts = np.array([P[i, j] for i, j in ring_idx])
    removed = TM.removed_cells(xs, ys, ring_pts)
    cx = (X[:-1, :-1] + X[:-1, 1:]) / 2
    cy = (Y[:-1, :-1] + Y[1:, :-1]) / 2
    far_sea = town.sea_distance(cx, cy) > AT.SEA_KEEP
    keep = ~removed & ~far_sea
    for iy, ix in np.argwhere(keep):
        a, b, c, d = fid(iy, ix), fid(iy, ix + 1), fid(iy + 1, ix + 1), fid(iy + 1, ix)
        tris += [(a, b, c, "far"), (a, c, d, "far")]
    n_far = len(tris) - n_near
    # ---- 縫い合わせ ----
    loop = TM.boundary_loop(removed)
    Bpts = np.array([(xs[ix], ys[iy]) for iy, ix in loop])
    zt, j0 = TM.zipper(ring[:, :2], Bpts)
    loop = loop[j0:] + loop[:j0]
    ids = [NT + k for k in range(len(ring))] + [fid(iy, ix) for iy, ix in loop]
    folded = 0
    for a, b, c in zt:
        A, Bv, Cv = pos[ids[a]], pos[ids[b]], pos[ids[c]]
        if (Bv[0] - A[0]) * (Cv[1] - A[1]) - (Bv[1] - A[1]) * (Cv[0] - A[0]) <= 0:
            folded += 1
        tris.append((ids[a], ids[b], ids[c], "far"))
    print(f"  地面の三角形: 街 {n_near}・外側 {n_far}・縫い目 {len(zt)}（裏返り {folded}）({time.time() - t:.0f}s)")
    # ---- 外周の垂れ（外側の地面の端を下へ）----
    skirt = []
    for side in (0, 1, 2, 3):
        if side == 0:
            line = [(0, ix) for ix in range(nx)]
        elif side == 1:
            line = [(iy, nx - 1) for iy in range(ny)]
        elif side == 2:
            line = [(ny - 1, ix) for ix in range(nx - 1, -1, -1)]
        else:
            line = [(iy, 0) for iy in range(ny - 1, -1, -1)]
        skirt.append(line)
    # ---- 材質（タイル）へ分ける ----
    nb = (town_v[:, 0].min(), town_v[:, 0].max(), town_v[:, 1].min(), town_v[:, 1].max())
    near_tiles = tile_boxes(*nb, AT.NEAR_TILE)
    ib = AT.INNER_BOX
    mid_tiles = tile_boxes(ib[0], ib[1], ib[2], ib[3], AT.MID_TILE)
    groups = {}
    for a, b, c, kind in tris:
        g = (pos[a] + pos[b] + pos[c]) / 3
        if kind == "near":
            key = ("near", int(math.floor(g[0] / AT.NEAR_TILE)), int(math.floor(g[1] / AT.NEAR_TILE)))
        elif ib[0] <= g[0] < ib[1] and ib[2] <= g[1] < ib[3]:
            key = ("mid", int(math.floor(g[0] / AT.MID_TILE)), int(math.floor(g[1] / AT.MID_TILE)))
        else:
            key = ("far", 0, 0)
        groups.setdefault(key, []).append((a, b, c))
    prims = []
    painted = 0
    t = time.time()
    for key in sorted(groups):
        kind, i, j = key
        if kind == "far":
            box = AT.FAR_BOX
            px = AT.FAR_PX
            name = f"{GROUND}_Far"
        else:
            box = (near_tiles if kind == "near" else mid_tiles)[(i, j)][0]
            px = AT.NEAR_PX if kind == "near" else AT.TILE_PX
            ew = f"E{i}" if i >= 0 else f"W{-i}"
            ns_ = f"N{j}" if j >= 0 else f"S{-j}"
            name = f"{GROUND}_{'Near' if kind == 'near' else 'Mid'}_{ew}_{ns_}"
        img = AT.paint_ground(town, box, px, painted + 101)
        AT.save_rgb(os.path.join(out_dir, name + "_BaseColor.png"), img)
        painted += 1
        mat = GW.Material(name, color=name + "_BaseColor.png", rough=0.9)
        tri = np.array(groups[key])
        used, inv = np.unique(tri.ravel(), return_inverse=True)
        p = pos[used]
        uv = np.stack([(p[:, 0] - box[0]) / (box[1] - box[0]), (p[:, 1] - box[2]) / (box[3] - box[2])], -1)
        tan = np.tile([1.0, 0.0, 0.0], (len(used), 1))
        pr = GW.Primitive()
        pr.add(p, nrm[used], uv, tan, inv.reshape(-1, 3))
        if kind == "far":
            # 外周の垂れ
            for line in skirt:
                for (iy0, ix0), (iy1, ix1) in zip(line[:-1], line[1:]):
                    a = np.array([xs[ix0], ys[iy0], Zf[iy0, ix0]])
                    b = np.array([xs[ix1], ys[iy1], Zf[iy1, ix1]])
                    q = np.array([a, b, [b[0], b[1], -SKIRT], [a[0], a[1], -SKIRT]])
                    e = b - a
                    nn = np.array([e[1], -e[0], 0.0])
                    nn /= max(np.linalg.norm(nn), 1e-9)
                    quv = np.stack([(q[:, 0] - box[0]) / (box[1] - box[0]), (q[:, 1] - box[2]) / (box[3] - box[2])],
                                   -1)
                    pr.add(q, nn, quv, e / max(np.linalg.norm(e), 1e-9), [(0, 2, 1), (0, 3, 2)])
        prims.append((mat, pr))
        write_meta(os.path.join(out_dir, name + "_BaseColor.png"), "Texture")
    path = os.path.join(out_dir, GROUND + ".gltf")
    n = GW.write(path, GROUND, prims)
    write_meta(path, "Model")
    print(f"  地面のテクスチャ {painted} 枚 ({time.time() - t:.0f}s)・{n} 三角形")


def build_seawall(town, m, ground):
    """海岸線（OSM。左が陸）に沿った護岸。浜・突堤・地図の砂浜の近くは作らない"""
    import export_araha_scene as EX
    out_dir = os.path.join(MODELS_DIR, SEAWALL)
    fresh_dir(out_dir)
    prefix = f"{SEAWALL}_Concrete"
    TT.save_maps(out_dir, prefix, TT.paint_seawall(91))
    for f in sorted(os.listdir(out_dir)):
        write_meta(os.path.join(out_dir, f), "Texture")
    wall_mat = GW.Material(prefix, f"{prefix}_BaseColor.png", f"{prefix}_Normal.png", f"{prefix}_ORM.png")
    top_mat = GW.Material(f"{SEAWALL}_Top", f"../{TEX_NAME}/ArahaTown_Roof_Concrete_BaseColor.png",
                          f"../{TEX_NAME}/ArahaTown_Roof_Concrete_Normal.png",
                          f"../{TEX_NAME}/ArahaTown_Roof_Concrete_ORM.png")
    jetties = [(np.array(r), np.array(h)) for r, h, _ in EX.JETTIES]
    sands = [a["_a"] for a in town.areas if a["kind"] == "beach"]
    x0, x1, y0, y1 = AT.FAR_BOX
    wall = GW.Primitive()
    top = GW.Primitive()
    runs = 0
    for c in town.coastline:
        pts = AM.resample(c, 2.0)
        ok = (pts[:, 0] > x0 + 20) & (pts[:, 0] < x1 - 20) & (pts[:, 1] > y0 + 20) & (pts[:, 1] < y1 - 20)
        ok &= AM.dist(pts[:, 0], pts[:, 1], np.vstack([m.beach_area, m.beach_area[:1]])) > WALL_CLEAR_BEACH
        ok &= ~AM.inside(pts[:, 0], pts[:, 1], m.beach_area)
        for r, h in jetties:
            # 突堤の石の中は作らない。付け根から JETTY_ROOT_FREE より先は、突堤の縁をなぞる海岸線も除く
            ax = (h - r) / np.hypot(*(h - r))
            along = (pts[:, 0] - r[0]) * ax[0] + (pts[:, 1] - r[1]) * ax[1]
            d = AM.dist(pts[:, 0], pts[:, 1], np.array([r, h]))
            ok &= d > WALL_CLEAR_JETTY
            ok &= ~((d < WALL_CLEAR_JETTY_OUTLINE) & (along > JETTY_ROOT_FREE))
            ok &= np.hypot(pts[:, 0] - h[0], pts[:, 1] - h[1]) > WALL_CLEAR_JETTY_OUTLINE + 8.0
        for a in sands:
            ok &= ~AM.inside(pts[:, 0], pts[:, 1], a)
            ok &= AM.dist(pts[:, 0], pts[:, 1], np.vstack([a, a[:1]])) > WALL_CLEAR_SAND
        k = 0
        while k < len(pts):
            if not ok[k]:
                k += 1
                continue
            e = k
            while e < len(pts) and ok[e]:
                e += 1
            if e - k >= 3 and (e - k - 1) * 2.0 >= WALL_MIN_RUN:
                add_wall_run(wall, top, pts[k:e], ground)
                runs += 1
            k = e
    path = os.path.join(out_dir, SEAWALL + ".gltf")
    n = GW.write(path, SEAWALL, [(wall_mat, wall), (top_mat, top)])
    write_meta(path, "Model")
    print(f"  護岸 {runs} 本・{n} 三角形")


def add_wall_run(wall, top, pts, ground):
    """海岸線の点列 pts（左が陸）に沿った壁（海側の面）と天端と両端の面"""
    t = np.gradient(pts, axis=0)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    n = np.stack([-t[:, 1], t[:, 0]], -1)                 # 陸の向き
    inland = pts + n * (WALL_TOP_WIDTH + 2.0)
    z_top = ground(inland[:, 0], inland[:, 1]) + WALL_TOP_RISE
    z_top = np.convolve(np.pad(z_top, 3, mode="edge"), np.ones(7) / 7, mode="valid")
    acc = np.concatenate([[0.0], np.cumsum(np.hypot(*np.diff(pts, axis=0).T))])
    W = TT.SEAWALL_W
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        za, zb = z_top[i], z_top[i + 1]
        ua, ub = acc[i] / W, acc[i + 1] / W
        va = lambda z: (z - TT.SEAWALL_Z0) / W  # noqa: E731
        e = b - a
        L = max(float(np.hypot(*e)), 1e-9)
        tan = (e[0] / L, e[1] / L, 0.0)
        nrm = (e[1] / L, -e[0] / L, 0.0)                    # 海の向き
        # 海側の壁（海から見ると a が左、b が右）
        wall.add([(a[0], a[1], WALL_BOTTOM), (b[0], b[1], WALL_BOTTOM), (b[0], b[1], zb), (a[0], a[1], za)], nrm,
                 [(ua, va(WALL_BOTTOM)), (ub, va(WALL_BOTTOM)), (ub, va(zb)), (ua, va(za))], tan,
                 [(0, 1, 2), (0, 2, 3)])
        # 天端
        ia, ib = a + n[i] * WALL_TOP_WIDTH, b + n[i + 1] * WALL_TOP_WIDTH
        q = np.array([(a[0], a[1], za), (b[0], b[1], zb), (ib[0], ib[1], zb), (ia[0], ia[1], za)])
        top.add(q, (0, 0, 1), q[:, :2] / TT.ROOF_TILE, (1, 0, 0), [(0, 1, 2), (0, 2, 3)])
        # 天端の陸側の縁（地面まで。陸から見ると b が左、a が右）
        zga, zgb = za - WALL_TOP_RISE - 0.6, zb - WALL_TOP_RISE - 0.6
        wall.add([(ib[0], ib[1], zgb), (ia[0], ia[1], zga), (ia[0], ia[1], za), (ib[0], ib[1], zb)],
                 (-nrm[0], -nrm[1], 0.0), [(-ub, va(zgb)), (-ua, va(zga)), (-ua, va(za)), (-ub, va(zb))],
                 (-tan[0], -tan[1], 0.0), [(0, 1, 2), (0, 2, 3)])
    # 両端の面
    for i, sgn in ((0, -1.0), (len(pts) - 1, 1.0)):
        a = pts[i]
        ia = a + n[i] * WALL_TOP_WIDTH
        tz = z_top[i]
        q = np.array([(a[0], a[1], WALL_BOTTOM), (ia[0], ia[1], WALL_BOTTOM), (ia[0], ia[1], tz), (a[0], a[1], tz)])
        nn = (t[i, 0] * sgn, t[i, 1] * sgn, 0.0)
        uv = np.array([(0.0, (WALL_BOTTOM - TT.SEAWALL_Z0) / W), (WALL_TOP_WIDTH / W, (WALL_BOTTOM - TT.SEAWALL_Z0) / W),
                       (WALL_TOP_WIDTH / W, (tz - TT.SEAWALL_Z0) / W), (0.0, (tz - TT.SEAWALL_Z0) / W)])
        tris = [(0, 1, 2), (0, 2, 3)] if sgn > 0 else [(0, 2, 1), (0, 3, 2)]
        wall.add(q, nn, uv, (n[i, 0], n[i, 1], 0.0), tris)


def main():
    t0 = time.time()
    town = AT.Town()
    m = AM.Map()
    field = AM.Field(m)
    P = AM.grid_points(m)
    parts = field.split_grid(P)
    ring_idx = AT.grid_ring(P)
    ring_pts = np.array([P[i, j] for i, j in ring_idx])
    ground = Ground(town, m, field, ring_pts)
    only = set(sys.argv[1:]) or {"blocks", "ground", "seawall"}
    if "blocks" in only:
        mats = building_materials()
        print(f"  外壁と屋上のテクスチャ ({time.time() - t0:.0f}s)")
        build_blocks(town, ground, mats, m.park)
    if "ground" in only:
        build_ground(town, m, field, P, parts, ring_idx, ground)
    if "seawall" in only:
        build_seawall(town, m, ground)
    print(f"[done] {time.time() - t0:.0f}s")


if __name__ == "__main__":
    main()
