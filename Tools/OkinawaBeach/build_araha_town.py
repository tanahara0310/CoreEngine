"""アラハビーチのまわりの街（地面・建物）のモデルを書き出す（Blender を使わない）

使い方: python Tools/OkinawaBeach/build_araha_town.py
data/araha_town.json（OpenStreetMap の建物・道路・面・海岸線）と data/araha_dem.npz（国土地理院の標高タイル）から、
  ArahaTown_Ground        : 公園の外の街の地面と、その外の遠くの丘・海底（道路・駐車場・緑地を描いたテクスチャ）
  ArahaTown_Block_<区画>  : 512 m 四方ごとの建物（外壁・屋上・給水タンク・塔屋）
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
GUID_NAMESPACE = uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/tanahara0310/CoreEngine/Tools/OkinawaBeach")
BLEND_OUT = 60.0     # 今の地形の格子の外で、その高さから外側の高さへ移る幅 [m]
SKIRT = 60.0         # 外側の地面の外周から下へ垂らす深さ [m]


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

def build_blocks(town, ground, mats):
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


def build_ground(town, m, field, P, Zn, ring_idx, ground):
    out_dir = os.path.join(MODELS_DIR, GROUND)
    fresh_dir(out_dir)
    xs, ys, Zf = ground.xs, ground.ys, ground.Z
    nx, ny = len(xs), len(ys)
    ns, nt = P.shape[:2]
    NF = nx * ny
    t = time.time()
    # ---- 頂点（外側の格子 → 今の地形の格子）----
    X, Y = np.meshgrid(xs, ys)
    pos = np.concatenate([np.stack([X.ravel(), Y.ravel(), Zf.ravel()], -1),
                          np.column_stack([P.reshape(-1, 2), Zn.ravel()])])
    nrm_far = TM.grid_normals(xs, ys, Zf).reshape(-1, 3)
    nrm_near = TM.field_normals(field, P[..., 0], P[..., 1]).reshape(-1, 3)
    nrm = np.concatenate([nrm_far, nrm_near])

    def fid(iy, ix):
        return iy * nx + ix

    def nid(i, j):
        return NF + i * nt + j

    tris = []
    # ---- 今の地形の格子の中の街 ----
    cells = TM.near_town_cells(field, P)
    for i, j in cells:
        a, b, c, d = nid(i, j), nid(i + 1, j), nid(i + 1, j + 1), nid(i, j + 1)
        e1 = pos[b] - pos[a]
        e2 = pos[d] - pos[a]
        if e1[0] * e2[1] - e1[1] * e2[0] > 0:
            tris += [(a, b, c, "near"), (a, c, d, "near")]
        else:
            tris += [(a, c, b, "near"), (a, d, c, "near")]
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
    zt, j0 = TM.zipper(ring_pts, Bpts)
    loop = loop[j0:] + loop[:j0]
    ids = [nid(i, j) for i, j in ring_idx] + [fid(iy, ix) for iy, ix in loop]
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
    near_cells_pos = pos[[nid(i, j) for i, j in cells]] if len(cells) else np.zeros((0, 3))
    nb = (near_cells_pos[:, 0].min(), near_cells_pos[:, 0].max(), near_cells_pos[:, 1].min(),
          near_cells_pos[:, 1].max())
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
            px = AT.TILE_PX
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


def main():
    t0 = time.time()
    town = AT.Town()
    m = AM.Map()
    field = AM.Field(m)
    P = AM.grid_points(m)
    Zn = field.height(P[..., 0], P[..., 1])
    ring_idx = AT.grid_ring(P)
    ring_pts = np.array([P[i, j] for i, j in ring_idx])
    ground = Ground(town, m, field, ring_pts)
    mats = building_materials()
    print(f"  外壁と屋上のテクスチャ ({time.time() - t0:.0f}s)")
    build_blocks(town, ground, mats)
    build_ground(town, m, field, P, Zn, ring_idx, ground)
    print(f"[done] {time.time() - t0:.0f}s")


if __name__ == "__main__":
    main()
