"""OkinawaBeachScene をプレイヤーが歩くためのデータを、シーンの配置と glTF から作る

    python Tools/OkinawaBeach/bake_player_walk.py

出力（Projects/Sandbox/Application/Assets の下）:
  - Data/OkinawaBeach/BeachGround.json
      歩ける地面の高さの表（0.5m 格子・cm の整数）。砂浜の地形と桟橋の床の高い方を取る。
      地形の無い所は NO_GROUND。BeachPlayer.as が DataFile で読み、足元をこの高さに合わせる
  - Scenes/OkinawaBeachScene/PlayerColliders.json
      物に当たるための静的な箱を、1 つのオブジェクトの Collider に並べる。
      物のうち真下の地面から 1.6m までにある部分を 0.1m の格子で塗り、つながった塊ごとに箱にする。
      地面からの高さが 0.35m に満たない塊はまたいで歩けるので箱にしない。桟橋の両脇には落ちないための壁を置く
読むのはエンジンのシーン（JSON）と、そこから参照される glTF。座標はエンジンの左手系（glTF の z を反転した値）。
配置を変えたら（export_engine_scene.py で書き出し直したら）これも流し直す。numpy を使う。
"""
import glob
import json
import math
import os
import struct
import uuid
from collections import deque

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PROJECT_DIR = os.path.join(REPO, "Projects", "Sandbox")
ASSETS_DIR = os.path.join(PROJECT_DIR, "Application", "Assets")
SCENE_DIR = os.path.join(ASSETS_DIR, "Scenes", "OkinawaBeachScene")
GROUND_OUT = os.path.join(ASSETS_DIR, "Data", "OkinawaBeach", "BeachGround.json")
COLLIDERS_NAME = "PlayerColliders"
COLLIDERS_ORDER = 153

# 表を作る範囲（エンジンの x / z）と格子の幅
AREA_X = (-40.0, 48.0)
AREA_Z = (-14.0, 20.5)
CELL = 0.5
NO_GROUND = -9999

# 物の箱
SLICE_HEIGHT = 1.6      # いちばん低い所からこの高さまでの形を見る
PROP_CELL = 0.1         # 塗る格子の幅
MIN_RISE = 0.35         # 地面からこれより低い塊は箱にしない
MIN_SIZE = 0.2          # 箱の幅の下限
DEEPEST_WALK = -0.45    # BeachPlayer が入れる水の深さ（これより深い所にしかない塊は箱にしない）
RAIL_EXTRA = 0.9        # 桟橋の壁の上面（床からの高さ）

# プレイヤー自身と、動く物・海の中の物は当たりに含めない
SKIP_OBJECTS = {"Player", "PlayerModel", COLLIDERS_NAME}

META_TYPES = {".json": "Json"}
GUID_NAMESPACE = uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/tanahara0310/CoreEngine/Tools/OkinawaBeach")


# ---- 書式（export_engine_scene.py と同じ）----

def f32(x):
    """float に丸め、float として最も短い 10 進の値にする（JsonManager の ShortenFloats と同じ）"""
    v = struct.unpack("<f", struct.pack("<f", float(x)))[0]
    if v == 0.0:
        return 0.0
    for digits in range(1, 10):
        s = float(f"{v:.{digits}g}")
        if struct.unpack("<f", struct.pack("<f", s))[0] == v:
            return s
    return v


def write_json(path, data, indent=4):
    """キーの名前順・4 字下げ・LF・末尾に改行（nlohmann::json の dump(4) と同じ並び）"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(data, ensure_ascii=False, indent=indent, sort_keys=True) + "\n")


def object_id(key):
    """ObjectId::FromKey と同じ（保存キーの 64bit FNV-1a）"""
    h = 14695981039346656037
    for c in key.encode("utf-8"):
        h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return format(h or 1, "016x")


def ensure_meta(path):
    """アセットの .meta が無ければ作る（GUID はプロジェクトからのパスで決める）"""
    meta = path + ".meta"
    if os.path.exists(meta):
        return
    rel = os.path.relpath(path, PROJECT_DIR).replace(os.sep, "/")
    guid = str(uuid.uuid5(GUID_NAMESPACE, rel))
    write_json(meta, {"guid": guid, "type": META_TYPES[os.path.splitext(path)[1].lower()]}, indent=2)


# ---- glTF ----

COMPONENT_TYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
COMPONENT_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def node_matrix(node):
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    m[:3, :3] = quat_matrix(node.get("rotation", [0, 0, 0, 1])) @ np.diag(node.get("scale", [1, 1, 1]))
    m[:3, 3] = node.get("translation", [0, 0, 0])
    return m


_gltf_cache = {}


def load_gltf(path):
    """glTF の全三角形をシーン空間で返す（頂点 P と添字 T。z は反転済み）"""
    if path in _gltf_cache:
        return _gltf_cache[path][:2]
    with open(path, encoding="utf-8") as f:
        g = json.load(f)
    base = os.path.dirname(path)
    buffers = []
    for b in g["buffers"]:
        with open(os.path.join(base, b["uri"]), "rb") as f:
            buffers.append(f.read())

    def accessor(index):
        a = g["accessors"][index]
        view = g["bufferViews"][a["bufferView"]]
        dtype = COMPONENT_TYPES[a["componentType"]]
        count = COMPONENT_COUNTS[a["type"]]
        offset = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = view.get("byteStride", 0)
        item = np.dtype(dtype).itemsize * count
        raw = buffers[view["buffer"]]
        if stride and stride != item:
            rows = [np.frombuffer(raw, dtype=dtype, count=count, offset=offset + k * stride) for k in range(a["count"])]
            return np.array(rows)
        return np.frombuffer(raw, dtype=dtype, count=a["count"] * count, offset=offset).reshape(a["count"], count)

    points, triangles, materials = [], [], []
    total = 0
    names = [m.get("name", "") for m in g.get("materials", [])]

    def walk(index, parent):
        nonlocal total
        node = g["nodes"][index]
        m = parent @ node_matrix(node)
        if "mesh" in node:
            for prim in g["meshes"][node["mesh"]]["primitives"]:
                p = accessor(prim["attributes"]["POSITION"]).astype(np.float64)
                p = (m[:3, :3] @ p.T).T + m[:3, 3]
                if "indices" in prim:
                    t = accessor(prim["indices"]).astype(np.int64).reshape(-1, 3)
                else:
                    t = np.arange(len(p)).reshape(-1, 3)
                points.append(p)
                triangles.append(t + total)
                material = names[prim["material"]] if "material" in prim else ""
                materials.extend([material] * len(t))
                total += len(p)
        for child in node.get("children", []):
            walk(child, m)

    for index in g["scenes"][g.get("scene", 0)]["nodes"]:
        walk(index, np.eye(4))
    P = np.concatenate(points) if points else np.zeros((0, 3))
    T = np.concatenate(triangles) if triangles else np.zeros((0, 3), dtype=np.int64)
    P[:, 2] *= -1.0
    _gltf_cache[path] = (P, T, np.array(materials, dtype=object))
    return P, T


def triangle_materials(path):
    """load_gltf の三角形ごとの材質の名前"""
    load_gltf(path)
    return _gltf_cache[path][2]


# ---- シーン ----

def euler_matrix(r):
    """Transform の rotate（X → Y → Z の順に回す）"""
    a, b, c = r
    rx = np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])
    ry = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]])
    rz = np.array([[math.cos(c), -math.sin(c), 0], [math.sin(c), math.cos(c), 0], [0, 0, 1]])
    return rz @ ry @ rx


def scene_objects(scene_dir=SCENE_DIR, skip=SKIP_OBJECTS):
    """シーンの物を (名前, モデルのパス, ワールドの三角形 P, T, 動くか) で返す"""
    objects = {}
    for path in glob.glob(os.path.join(scene_dir, "*.json")):
        if os.path.basename(path).startswith("_"):
            continue
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        transform, model, animated = None, None, False
        for comp in data["components"]:
            if comp["type"] == "Transform":
                transform = comp["parameters"]
            elif comp["type"] in ("MeshRenderer", "Animator"):
                m = comp["parameters"].get("model")
                model = (m.get("path") if isinstance(m, dict) else m) or model
                animated = animated or comp["type"] == "Animator"
        objects[data["id"]] = dict(name=data["name"], transform=transform, model=model, animated=animated)

    def world(oid):
        o = objects[oid]
        if "world" not in o:
            t = o["transform"]
            m = np.eye(4)
            m[:3, :3] = euler_matrix(t["rotate"]) @ np.diag(t["scale"])
            m[:3, 3] = t["translate"]
            parent = t.get("parent")
            if parent and parent.get("ref") in objects:
                m = world(parent["ref"]) @ m
            o["world"] = m
        return o["world"]

    result = []
    for oid, o in objects.items():
        if not o["model"] or o["name"] in skip:
            continue
        P, T = load_gltf(os.path.join(PROJECT_DIR, o["model"]))
        W = world(oid)
        result.append((o["name"], o["model"], (W[:3, :3] @ P.T).T + W[:3, 3], T, o["animated"]))
    return sorted(result, key=lambda r: r[0])


# ---- 高さの表 ----

def rasterize_max(H, xs, zs, P, T):
    """三角形を真上から見て、格子点の高さの最大を H へ書く（格子の幅は xs の間隔）"""
    if len(T) == 0:
        return
    cell = xs[1] - xs[0]
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    lo = np.minimum(np.minimum(a, b), c)
    hi = np.maximum(np.maximum(a, b), c)
    x0, z0 = xs[0], zs[0]
    for i in np.nonzero((hi[:, 0] >= xs[0]) & (lo[:, 0] <= xs[-1]) & (hi[:, 2] >= zs[0]) & (lo[:, 2] <= zs[-1]))[0]:
        i0 = max(0, int(math.ceil((lo[i, 0] - x0) / cell - 1e-9)))
        i1 = min(len(xs) - 1, int(math.floor((hi[i, 0] - x0) / cell + 1e-9)))
        j0 = max(0, int(math.ceil((lo[i, 2] - z0) / cell - 1e-9)))
        j1 = min(len(zs) - 1, int(math.floor((hi[i, 2] - z0) / cell + 1e-9)))
        if i1 < i0 or j1 < j0:
            continue
        A, B, C = a[i], b[i], c[i]
        det = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2])
        if abs(det) < 1e-12:
            continue
        gx, gz = np.meshgrid(xs[i0:i1 + 1], zs[j0:j1 + 1])
        l1 = ((B[2] - C[2]) * (gx - C[0]) + (C[0] - B[0]) * (gz - C[2])) / det
        l2 = ((C[2] - A[2]) * (gx - C[0]) + (A[0] - C[0]) * (gz - C[2])) / det
        l3 = 1.0 - l1 - l2
        inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
        y = l1 * A[1] + l2 * B[1] + l3 * C[1]
        sub = H[j0:j1 + 1, i0:i1 + 1]
        update = inside & (y > sub)
        sub[update] = y[update]


def flat_faces(P, T):
    """水平な面かどうか・上端の高さ・面積"""
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    n = np.cross(b - a, c - a)
    length = np.linalg.norm(n, axis=1)
    flat = np.abs(n[:, 1]) > 0.95 * np.maximum(length, 1e-12)
    top = np.maximum(np.maximum(a[:, 1], b[:, 1]), c[:, 1])
    return flat, top, length * 0.5


def deck_height(parts):
    """水平な面の面積が最大の半分以上ある高さのうち、いちばん上を床板の上面とする"""
    area = {}
    for P, T in parts:
        flat, top, size = flat_faces(P, T)
        keep = flat & (top > 0.0)
        for y, s in zip(np.round(top[keep], 2), size[keep]):
            area[y] = area.get(y, 0.0) + s
    if not area:
        return None
    largest = max(area.values())
    return max(y for y, s in area.items() if s >= largest * 0.5)


def deck_triangles(P, T, deck):
    """床板の上面（deck との差が 3cm 以内の水平な面）"""
    flat, top, _ = flat_faces(P, T)
    return T[flat & (np.abs(top - deck) < 0.03)]


def ground_many(H, xs, zs, x, z):
    """表を双一次で引く（配列版。範囲外・地面の無い所は nan）"""
    cell = xs[1] - xs[0]
    fx = (np.asarray(x) - xs[0]) / cell
    fz = (np.asarray(z) - zs[0]) / cell
    i = np.floor(fx).astype(np.int64)
    j = np.floor(fz).astype(np.int64)
    ok = (i >= 0) & (j >= 0) & (i + 1 < len(xs)) & (j + 1 < len(zs))
    i, j = np.where(ok, i, 0), np.where(ok, j, 0)
    tx, tz = fx - i, fz - j
    g = ((H[j, i] * (1 - tx) + H[j, i + 1] * tx) * (1 - tz) +
         (H[j + 1, i] * (1 - tx) + H[j + 1, i + 1] * tx) * tz)
    return np.where(ok, g, np.nan)


def ground_top(H, xs, zs, x, z):
    """点を囲む 4 つの格子点の高い方（配列版。範囲外・地面の無い所は nan）"""
    cell = xs[1] - xs[0]
    i = np.floor((np.asarray(x) - xs[0]) / cell).astype(np.int64)
    j = np.floor((np.asarray(z) - zs[0]) / cell).astype(np.int64)
    ok = (i >= 0) & (j >= 0) & (i + 1 < len(xs)) & (j + 1 < len(zs))
    i, j = np.where(ok, i, 0), np.where(ok, j, 0)
    g = np.maximum(np.maximum(H[j, i], H[j, i + 1]), np.maximum(H[j + 1, i], H[j + 1, i + 1]))
    return np.where(ok, g, np.nan)


# ---- 物の箱 ----

def slice_points(P, T, bottom, top, spacing):
    """bottom〜top の高さに掛かる三角形の上に、spacing 間隔で点を打つ"""
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    lo = np.minimum(np.minimum(a[:, 1], b[:, 1]), c[:, 1])
    hi = np.maximum(np.maximum(a[:, 1], b[:, 1]), c[:, 1])
    out = []
    for i in np.nonzero((hi >= bottom) & (lo <= top))[0]:
        edge = max(np.linalg.norm(b[i] - a[i]), np.linalg.norm(c[i] - b[i]), np.linalg.norm(a[i] - c[i]))
        n = max(1, min(200, int(math.ceil(edge / spacing))))
        u, v = np.meshgrid(np.arange(n + 1), np.arange(n + 1))
        keep = (u + v) <= n
        u, v = u[keep] / n, v[keep] / n
        out.append(a[i] + np.outer(u, b[i] - a[i]) + np.outer(v, c[i] - a[i]))
    return np.concatenate(out) if out else np.zeros((0, 3))


def clusters(points, reach=2):
    """点を PROP_CELL の格子で塗り、reach マスまでの隙間はつないで、塊ごとに点の添字を返す"""
    if len(points) == 0:
        return []
    keys = np.floor(points[:, [0, 2]] / PROP_CELL).astype(np.int64)
    cells = {}
    for index, key in enumerate(map(tuple, keys)):
        cells.setdefault(key, []).append(index)
    seen, groups = set(), []
    for start in cells:
        if start in seen:
            continue
        seen.add(start)
        queue, members = deque([start]), []
        while queue:
            cx, cz = queue.popleft()
            members.extend(cells[(cx, cz)])
            for dx in range(-reach, reach + 1):
                for dz in range(-reach, reach + 1):
                    nb = (cx + dx, cz + dz)
                    if nb in cells and nb not in seen:
                        seen.add(nb)
                        queue.append(nb)
        groups.append(np.array(members))
    return groups


def box_shape(x0, x1, y0, y1, z0, z1):
    if x1 - x0 < MIN_SIZE:
        cx = (x0 + x1) * 0.5
        x0, x1 = cx - MIN_SIZE * 0.5, cx + MIN_SIZE * 0.5
    if z1 - z0 < MIN_SIZE:
        cz = (z0 + z1) * 0.5
        z0, z1 = cz - MIN_SIZE * 0.5, cz + MIN_SIZE * 0.5
    return {
        "enabled": True,
        "layer": "Default",
        "offset": [f32((x0 + x1) * 0.5), f32((y0 + y1) * 0.5), f32((z0 + z1) * 0.5)],
        "size": [f32(x1 - x0), f32(y1 - y0), f32(z1 - z0)],
        "static": True,
        "trigger": False,
        "type": "Box",
    }


def prop_boxes(P, T, H, xs, zs, reach=2):
    """物のうち、真下の地面から SLICE_HEIGHT までにある部分を塊に分けて箱にする（reach は clusters と同じ）"""
    lo, hi = P.min(0), P.max(0)
    if hi[0] < xs[0] or lo[0] > xs[-1] or hi[2] < zs[0] or lo[2] > zs[-1]:
        return []
    gx, gz = np.meshgrid(np.linspace(lo[0], hi[0], 8), np.linspace(lo[2], hi[2], 8))
    under = ground_many(H, xs, zs, gx.ravel(), gz.ravel())
    if np.all(np.isnan(under)) or np.nanmax(under) < DEEPEST_WALK or hi[1] < np.nanmin(under) + 0.05:
        return []
    pts = slice_points(P, T, np.nanmin(under), np.nanmax(under) + SLICE_HEIGHT, PROP_CELL * 0.5)
    if len(pts) == 0:
        return []
    ground = ground_top(H, xs, zs, pts[:, 0], pts[:, 2])
    rise = pts[:, 1] - ground
    keep = (rise > 0.05) & (rise < SLICE_HEIGHT) & (ground >= DEEPEST_WALK - SLICE_HEIGHT)
    pts, ground, rise = pts[keep], ground[keep], rise[keep]
    shapes = []
    for members in clusters(pts, reach):
        if rise[members].max() < MIN_RISE:
            continue
        q = pts[members]
        shapes.append(box_shape(q[:, 0].min(), q[:, 0].max(), ground[members].min() - 1.0, q[:, 1].max(),
                                q[:, 2].min(), q[:, 2].max()))
    return shapes


def pier_rails(deck_parts, terrain, xs, zs, deck):
    """床板の両脇（床が砂より高い間）と海側の先端の外側に壁を置く"""
    P = np.concatenate([p[t.ravel()] for p, t in deck_parts])
    x0, x1 = P[:, 0].min(), P[:, 0].max()
    z_sea, z_land = P[:, 2].min(), P[:, 2].max()
    zs_line = np.arange(z_sea, z_land, CELL * 0.5)
    sand = ground_many(terrain, xs, zs, np.full(len(zs_line), (x0 + x1) * 0.5), zs_line)
    high = np.nonzero(np.nan_to_num(sand, nan=-1e9) > deck - 0.3)[0]
    z_end = zs_line[high[0]] if len(high) else z_land
    width = 0.2
    bottom = deck - 4.0
    return [box_shape(x0 - width, x0, bottom, deck + RAIL_EXTRA, z_sea, z_end),
            box_shape(x1, x1 + width, bottom, deck + RAIL_EXTRA, z_sea, z_end),
            box_shape(x0 - width, x1 + width, bottom, deck + RAIL_EXTRA, z_sea - width, z_sea)]


def main():
    objects = scene_objects()

    xs = np.arange(AREA_X[0], AREA_X[1] + 1e-6, CELL)
    zs = np.arange(AREA_Z[0], AREA_Z[1] + 1e-6, CELL)
    H = np.full((len(zs), len(xs)), -np.inf)

    terrain = [o for o in objects if os.path.basename(o[1]).startswith(("BeachTerrain_", "ReefTerrain_"))]
    piers = [o for o in objects if os.path.basename(o[1]).startswith("Pier_")]
    for _, _, P, T, _ in terrain:
        rasterize_max(H, xs, zs, P, T)
    terrain_only = np.where(np.isfinite(H), H, np.nan)

    deck = deck_height([(P, T) for _, _, P, T, _ in piers])
    deck_parts = []
    for _, _, P, T, _ in piers:
        tris = deck_triangles(P, T, deck)
        if len(tris):
            deck_parts.append((P, tris))
            # 板の隙間と部品の継ぎ目に落ちた格子点も床の高さにする（部品ごとに床板の範囲を半マス広げて埋める）
            q = P[tris.ravel()]
            margin = CELL * 0.5
            cols = (xs >= q[:, 0].min() - margin) & (xs <= q[:, 0].max() + margin)
            rows = (zs >= q[:, 2].min() - margin) & (zs <= q[:, 2].max() + margin)
            block = np.ix_(rows, cols)
            H[block] = np.maximum(H[block], deck)

    heights = np.where(np.isfinite(H), np.round(H * 100.0), NO_GROUND).astype(int)
    write_json(GROUND_OUT, {
        "originX": f32(xs[0]),
        "originZ": f32(zs[0]),
        "cell": f32(CELL),
        "countX": int(len(xs)),
        "countZ": int(len(zs)),
        "noGround": NO_GROUND,
        "heights": heights.ravel().tolist(),
    }, indent=None)
    ensure_meta(GROUND_OUT)
    H[~np.isfinite(H)] = np.nan

    shapes = pier_rails(deck_parts, terrain_only, xs, zs, deck) if deck_parts else []
    terrain_names = {o[0] for o in terrain}
    counts = {}
    for name, model, P, T, animated in objects:
        if animated or name in terrain_names:
            continue
        boxes = prop_boxes(P, T, H, xs, zs)
        if boxes:
            counts[name] = len(boxes)
            shapes.extend(boxes)

    collider_object = {
        "active": True,
        "components": [
            {
                "enabled": True,
                "parameters": {"parent": None, "rotate": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0],
                               "translate": [0.0, 0.0, 0.0]},
                "type": "Transform",
            },
            {"enabled": True, "parameters": {"shapes": shapes}, "type": "Collider"},
        ],
        "id": object_id(COLLIDERS_NAME),
        "name": COLLIDERS_NAME,
        "order": COLLIDERS_ORDER,
    }
    write_json(os.path.join(SCENE_DIR, COLLIDERS_NAME + ".json"), collider_object)

    finite = H[np.isfinite(H)]
    print(f"高さの表: {len(xs)} x {len(zs)}（{CELL}m 格子）、地面のある点 {finite.size}、"
          f"高さ {finite.min():.2f}〜{finite.max():.2f}、桟橋の床 {deck:.3f}")
    print(f"箱: {len(shapes)} 個（桟橋の壁 {len(shapes) - sum(counts.values())} 個）")
    for name in sorted(counts):
        print(f"  {name}: {counts[name]}")


if __name__ == "__main__":
    main()
