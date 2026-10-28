"""ArahaBeachScene をプレイヤーが歩くためのデータを、シーンの配置と glTF・街の建物の輪郭から作る

    python Tools/OkinawaBeach/bake_araha_walk.py

出力（Projects/Sandbox/Application/Assets の下）:
  - Data/ArahaBeach/ArahaGround.json
      浜と公園の歩ける地面の高さの表（1m 格子・cm の整数）。地形・園路・護岸・突堤・浮き桟橋の床の高い方を取る
  - Data/ArahaBeach/ArahaGroundWide.json
      まわりの街まで含めた高さの表（4m 格子）。浜と公園の表の外ではこちらを引く
  - Data/ArahaBeach/ArahaBuildings.json
      街の建物の輪郭（0.1m の整数）と、32m の区画ごとの建物の番号。BeachPlayer.as が輪郭の中へ入らないようにする
  - Scenes/ArahaBeachScene/PlayerColliders.json
      浜と公園の物に当たるための静的な箱（作り方は bake_player_walk.py と同じ）
配置を変えたら（export_araha_scene.py で書き出し直したら）これも流し直す。numpy を使う。
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bake_player_walk as BP  # noqa: E402
import build_araha_town as BT  # noqa: E402
from okinawa import araha_map as AM  # noqa: E402
from okinawa import araha_town as AT  # noqa: E402

SCENE_DIR = os.path.join(BP.ASSETS_DIR, "Scenes", "ArahaBeachScene")
DATA_DIR = os.path.join(BP.ASSETS_DIR, "Data", "ArahaBeach")
GROUND_OUT = os.path.join(DATA_DIR, "ArahaGround.json")
WIDE_OUT = os.path.join(DATA_DIR, "ArahaGroundWide.json")
BUILDINGS_OUT = os.path.join(DATA_DIR, "ArahaBuildings.json")
COLLIDERS_NAME = "PlayerColliders"

# 浜と公園の表（エンジンの x / z）
AREA_X = (-140.0, 182.0)
AREA_Z = (-390.0, 300.0)
CELL = 1.0
# 街の表（araha_town の細かい格子の範囲）。陸と浅い海のある行と列だけ残す
WIDE_X = (AT.INNER_BOX[0], AT.INNER_BOX[1])
WIDE_Z = (AT.INNER_BOX[2], AT.INNER_BOX[3])
WIDE_CELL = 4.0
WIDE_KEEP_ABOVE = -3.0
# 建物の輪郭
BUILDING_UNIT = 0.1     # 座標の単位 [m]
BUCKET = 32.0           # 区画の幅 [m]
BUCKET_MARGIN = 1.0     # 区画へ入れるときに輪郭を広げる幅（プレイヤーの半径より大きく）
PILLAR = 0.4            # 屋根だけの建物の柱の太さ
PILLAR_INSET = 0.8      # 屋根の縁から柱までの距離（town_mesh.build_building と同じ）
# 物の箱: 0.5m までの隙間はつないで 1 つの箱にする（プレイヤーの幅より狭い隙間は通れない）
PROP_REACH = 5
# 背の高い木の葉は箱にしない（幹・枝・根だけ）
FOLIAGE_MATERIALS = ("Leaf", "Frond", "Flower")
TREE_HEIGHT = 3.0
NO_GROUND = BP.NO_GROUND

# 足元の高さに使う物（モデルの名前の頭）と、そのうち床にしない材質
GROUND_MODELS = ("ArahaTerrain_", "ArahaPath", "Seawall_Straight", "Seawall_Stairs", "Jetty_",
                 "ArahaTown_Ground", "ArahaTown_Seawall")
DECK_MODELS = ("FloatingPier_Walk",)
NOT_GROUND_MATERIALS = ("Rail",)
WIDE_MODELS = ("ArahaTerrain_", "ArahaTown_Ground", "ArahaTown_Seawall")
DECK_SUB = 4            # 床板は 1 マスを DECK_SUB x DECK_SUB に分けて塗り、どれかが床ならそのマスを床にする


def model_name(model):
    return os.path.splitext(os.path.basename(model))[0]


def scene_objects():
    blocks = {f[:-5] for f in os.listdir(SCENE_DIR) if f.startswith("ArahaTown_Block_")}
    return BP.scene_objects(SCENE_DIR, blocks | {"Player", "PlayerModel", COLLIDERS_NAME})


def ground_triangles(model, T):
    """床にする三角形（手すりの材質を除く）"""
    materials = BP.triangle_materials(os.path.join(BP.PROJECT_DIR, model))
    keep = np.ones(len(T), bool)
    for word in NOT_GROUND_MATERIALS:
        keep &= np.array([word not in m for m in materials], bool)
    return T[keep]


def solid_triangles(model, P, T):
    """箱にする三角形（背の高い木なら葉を除く）"""
    if P[:, 1].max() - P[:, 1].min() <= TREE_HEIGHT:
        return T
    materials = BP.triangle_materials(os.path.join(BP.PROJECT_DIR, model))
    keep = np.array([not any(word in m for word in FOLIAGE_MATERIALS) for m in materials], bool)
    return T[keep]


def rasterize_deck(H, xs, zs, P, T):
    """床板の上面を細かい格子で塗り、床に触れるマスを床の高さにする（板の隙間と細い床を埋める）"""
    flat, top, _ = BP.flat_faces(P, T)
    tris = T[flat & (top >= top[flat].max() - 0.03)] if flat.any() else T[:0]
    if len(tris) == 0:
        return
    q = P[tris.ravel()]
    cols = np.nonzero((xs >= q[:, 0].min() - CELL) & (xs <= q[:, 0].max() + CELL))[0]
    rows = np.nonzero((zs >= q[:, 2].min() - CELL) & (zs <= q[:, 2].max() + CELL))[0]
    if len(cols) == 0 or len(rows) == 0:
        return
    sub = CELL / DECK_SUB
    fx = xs[cols[0]] - CELL * 0.5 + sub * (np.arange(len(cols) * DECK_SUB) + 0.5)
    fz = zs[rows[0]] - CELL * 0.5 + sub * (np.arange(len(rows) * DECK_SUB) + 0.5)
    F = np.full((len(fz), len(fx)), -np.inf)
    BP.rasterize_max(F, fx, fz, P, tris)
    F = F.reshape(len(rows), DECK_SUB, len(cols), DECK_SUB).max(axis=(1, 3))
    block = np.ix_(rows, cols)
    H[block] = np.maximum(H[block], F)


def write_table(path, xs, zs, H):
    heights = np.where(np.isfinite(H), np.round(H * 100.0), NO_GROUND).astype(int)
    BP.write_json(path, {
        "originX": BP.f32(xs[0]),
        "originZ": BP.f32(zs[0]),
        "cell": BP.f32(xs[1] - xs[0]),
        "countX": int(len(xs)),
        "countZ": int(len(zs)),
        "noGround": NO_GROUND,
        "heights": heights.ravel().tolist(),
    }, indent=None)
    BP.ensure_meta(path)


def fine_table(objects):
    xs = np.arange(AREA_X[0], AREA_X[1] + 1e-6, CELL)
    zs = np.arange(AREA_Z[0], AREA_Z[1] + 1e-6, CELL)
    H = np.full((len(zs), len(xs)), -np.inf)
    used = set()
    for name, model, P, T, _ in objects:
        base = model_name(model)
        if base.startswith(GROUND_MODELS):
            BP.rasterize_max(H, xs, zs, P, ground_triangles(model, T))
            used.add(name)
        elif base.startswith(DECK_MODELS):
            rasterize_deck(H, xs, zs, P, T)
            used.add(name)
    return xs, zs, H, used


def wide_table(objects):
    xs = np.arange(WIDE_X[0], WIDE_X[1] + 1e-6, WIDE_CELL)
    zs = np.arange(WIDE_Z[0], WIDE_Z[1] + 1e-6, WIDE_CELL)
    H = np.full((len(zs), len(xs)), -np.inf)
    for _, model, P, T, _ in objects:
        if model_name(model).startswith(WIDE_MODELS):
            BP.rasterize_max(H, xs, zs, P, T)
    keep = np.isfinite(H) & (H > WIDE_KEEP_ABOVE)
    rows = np.nonzero(keep.any(axis=1))[0]
    cols = np.nonzero(keep.any(axis=0))[0]
    r0, r1 = max(0, rows[0] - 1), min(len(zs), rows[-1] + 2)
    c0, c1 = max(0, cols[0] - 1), min(len(xs), cols[-1] + 2)
    return xs[c0:c1], zs[r0:r1], H[r0:r1, c0:c1]


def building_shapes(xs, zs):
    """街の表の中の建物の輪郭（エンジンの x / z）。屋根だけの建物は柱ごとの四角にする"""
    town = AT.Town()
    m = AM.Map()
    shapes = []
    for b, poly, info in BT.town_buildings(town, m.park):
        cx, cy = poly.mean(axis=0)
        if not (xs[0] < cx < xs[-1] and zs[0] < cy < zs[-1]):
            continue
        if info["canopy"]:
            inner = BT.TM.inset_polygon(poly, PILLAR_INSET)
            if inner is not None and len(inner) <= 8:
                h = PILLAR * 0.5
                for q in inner:
                    shapes.append(np.array([(q[0] - h, q[1] - h), (q[0] + h, q[1] - h),
                                            (q[0] + h, q[1] + h), (q[0] - h, q[1] + h)]))
            continue
        shapes.append(np.asarray(poly, float))
    return shapes


def write_buildings(path, shapes, xs, zs):
    x0, z0 = float(xs[0]), float(zs[0])
    nx = int(math.ceil((xs[-1] - x0) / BUCKET))
    nz = int(math.ceil((zs[-1] - z0) / BUCKET))
    buckets = [[] for _ in range(nx * nz)]
    starts, points = [0], []
    for index, s in enumerate(shapes):
        q = np.round(s / BUILDING_UNIT).astype(int)
        points.extend(q.ravel().tolist())
        starts.append(len(points) // 2)
        i0 = max(0, int((s[:, 0].min() - BUCKET_MARGIN - x0) // BUCKET))
        i1 = min(nx - 1, int((s[:, 0].max() + BUCKET_MARGIN - x0) // BUCKET))
        j0 = max(0, int((s[:, 1].min() - BUCKET_MARGIN - z0) // BUCKET))
        j1 = min(nz - 1, int((s[:, 1].max() + BUCKET_MARGIN - z0) // BUCKET))
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                buckets[j * nx + i].append(index)
    bucket_start, items = [0], []
    for b in buckets:
        items.extend(b)
        bucket_start.append(len(items))
    BP.write_json(path, {
        "unit": BP.f32(BUILDING_UNIT),
        "originX": BP.f32(x0),
        "originZ": BP.f32(z0),
        "cell": BP.f32(BUCKET),
        "countX": nx,
        "countZ": nz,
        "bucketStart": bucket_start,
        "bucketItems": items,
        "shapeStart": starts,
        "points": points,
    }, indent=None)
    BP.ensure_meta(path)
    return len(items)


def collider_order():
    path = os.path.join(SCENE_DIR, COLLIDERS_NAME + ".json")
    orders = []
    for f in os.listdir(SCENE_DIR):
        if f.endswith(".json") and not f.startswith("_"):
            with open(os.path.join(SCENE_DIR, f), encoding="utf-8") as fp:
                data = json.load(fp)
            if f == COLLIDERS_NAME + ".json":
                return data["order"]
            orders.append(data.get("order", 0))
    return (max(orders) + 1) if orders else 0


def main():
    objects = scene_objects()
    print(f"物 {len(objects)} 個")

    xs, zs, H, used = fine_table(objects)
    write_table(GROUND_OUT, xs, zs, H)
    finite = H[np.isfinite(H)]
    print(f"浜と公園の表: {len(xs)} x {len(zs)}（{CELL}m 格子）、高さ {finite.min():.2f}〜{finite.max():.2f}")

    wxs, wzs, W = wide_table(objects)
    write_table(WIDE_OUT, wxs, wzs, W)
    print(f"街の表: {len(wxs)} x {len(wzs)}（{WIDE_CELL}m 格子）"
          f" x {wxs[0]:.0f}〜{wxs[-1]:.0f} z {wzs[0]:.0f}〜{wzs[-1]:.0f}")

    shapes = building_shapes(wxs, wzs)
    items = write_buildings(BUILDINGS_OUT, shapes, wxs, wzs)
    print(f"建物の輪郭: {len(shapes)} 個（区画への登録 {items}）")

    H[~np.isfinite(H)] = np.nan
    counts = {}
    boxes = []
    for name, model, P, T, animated in objects:
        if animated or name in used or model_name(model).startswith(WIDE_MODELS):
            continue
        found = BP.prop_boxes(P, solid_triangles(model, P, T), H, xs, zs, PROP_REACH)
        if found:
            counts[model_name(model)] = counts.get(model_name(model), 0) + len(found)
            boxes.extend(found)

    collider_object = {
        "active": True,
        "components": [
            {
                "enabled": True,
                "parameters": {"parent": None, "rotate": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0],
                               "translate": [0.0, 0.0, 0.0]},
                "type": "Transform",
            },
            {"enabled": True, "parameters": {"shapes": boxes}, "type": "Collider"},
        ],
        "id": BP.object_id(COLLIDERS_NAME),
        "name": COLLIDERS_NAME,
        "order": collider_order(),
    }
    BP.write_json(os.path.join(SCENE_DIR, COLLIDERS_NAME + ".json"), collider_object)
    print(f"箱: {len(boxes)} 個")
    for name in sorted(counts):
        print(f"  {name}: {counts[name]}")


if __name__ == "__main__":
    main()
