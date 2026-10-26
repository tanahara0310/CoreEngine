"""アラハビーチのシーン（Sandbox の ArahaBeachScene）を書き出す

使い方: python Tools/OkinawaBeach/export_araha_scene.py
data/araha_map.json（OpenStreetMap の浜・海岸線）の線に沿って、地形・護岸・突堤・沖の岩・浮き桟橋・ネット・
ヤシ並木・海賊船の遊具、公園の施設・木・園路などを置く。高さは地形（okinawa/araha_map.py の Field）と同じ式で求める。
太陽・水面・見た目の設定は OkinawaBeachScene のものを使う。シーンのフォルダは書き出すたびに作り直す。

座標: Blender と同じ（x = 東、y = 北、z = 上）で計算し、エンジンへは (x, z, y)、Z 軸まわりの角度は符号を反転して書く。
"""
import json
import math
import os
import random
import shutil
import struct
import sys
import uuid

import numpy as np

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOL_DIR)
from okinawa import araha_map as AM  # noqa: E402

REPO = os.path.dirname(os.path.dirname(TOOL_DIR))
PROJECT_DIR = os.path.join(REPO, "Projects", "Sandbox")
MODELS_DIR = os.path.join(PROJECT_DIR, "Application", "Assets", "Models", "Okinawa")
SCENES_DIR = os.path.join(PROJECT_DIR, "Application", "Assets", "Scenes")
TEMPLATE_DIR = os.path.join(SCENES_DIR, "OkinawaBeachScene")
SCENE = "ArahaBeachScene"
OUT_DIR = os.path.join(SCENES_DIR, SCENE)
META_TYPES = {".gltf": "Model", ".png": "Texture"}
GUID_NAMESPACE = uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/tanahara0310/CoreEngine/Tools/OkinawaBeach")

TERRAIN = ["ArahaTerrain_SandDry", "ArahaTerrain_SandWet", "ArahaTerrain_SandUnder", "ArahaTerrain_Seabed",
           "ArahaTerrain_Lawn"]
MODULE = 8.0               # 護岸のモジュールの長さ
STAIRS_EVERY = 15          # 何モジュールおきに階段にするか
JETTY_HEAD = 10.76         # 突堤の先端のモジュールが進む長さ（直線 4 m ＋半径 7 m の 75° の弧）
# 突堤（付け根と先端の丸い所の中心、曲がる向き）
JETTIES = [((128.0, 289.0), (40.0, 278.0), "Jetty_HeadR"), ((-68.0, -318.0), (-125.0, -284.0), "Jetty_HeadL")]
# 沖の岩（航空写真で見た位置）と大きさ
ROCKS = [((-14.0, 150.0), 0.8), ((-47.0, 3.0), 0.75), ((-100.0, -142.0), 0.85)]
PIER_AT = (36.0, 106.0)    # 浮き桟橋の陸側の端（波打ち際）
NET_OFFSET = 85.0          # ネットを波打ち際から沖へ離す距離
SHIP = ((50.5, -52.0), 250.0)          # 海賊船の遊具の位置と、船首の向き（北から時計回りの方位）
MONUMENT = ((64.0, -68.0), 270.0)      # 石碑の位置と、正面の向き
SIGN = ((74.0, 52.0), 90.0)            # シーサー看板の位置と、正面の向き
LIFEGUARD_NEAR = (10.0, -115.0)        # 監視台（管理棟の前）
CAMERA = ((-70.0, 80.0, 14.0), (40.0, -30.0, 3.0), 55.0)
# 公園の施設（araha_map.PARK_FACILITIES の位置）のまわりの物
BBQ_ORIGIN = (8.0, -128.0)             # バーベキューの屋根の並びの北の端
BBQ_AZ = 18.0                          # 並びの向き（海岸に沿う方位）
GAZEBOS = [(88.7, -187.7), (85.8, -181.7)]  # 傘形の東屋（地図の位置）
PERGOLA_AT = (0.2, 0.36, 0.55, 0.72)   # 格子屋根の東屋を置く、遊歩道から 15 m 陸側の線の位置（南端からの割合）
# 植栽の区域: (x0, x1, y0, y1)、木どうしの間隔 [m]、樹種の重み
TREE_ZONES = [
    ((95.0, 160.0, 120.0, 285.0), 7.0,
     {"Terihaboku": 3, "Fukugi": 2.5, "Kobateishi_A": 1.5, "Kobateishi_B": 1, "Gajumaru": 1.5, "Hououboku": 1}),
    ((50.0, 105.0, -45.0, 42.0), 13.0,
     {"Gajumaru": 1, "Hououboku": 1.5, "Kobateishi_A": 2, "Kobateishi_B": 1, "Terihaboku": 1}),
    ((25.0, 85.0, -100.0, -45.0), 15.0, {"Kobateishi_A": 2, "Terihaboku": 1, "Hououboku": 1}),
    ((30.0, 80.0, -172.0, -120.0), 6.5, {"Gajumaru": 3, "Terihaboku": 2, "Fukugi": 1.5}),
    ((-5.0, 32.0, -180.0, -122.0), 11.0, {"Kobateishi_A": 2, "Kobateishi_B": 1.5, "Terihaboku": 1}),
    ((-40.0, 100.0, -265.0, -175.0), 12.0,
     {"Kobateishi_A": 1.5, "Terihaboku": 1.5, "Hououboku": 1, "Gajumaru": 1, "Fukugi": 1}),
    ((-60.0, -15.0, -335.0, -262.0), 8.0, {"Fukugi": 2, "Terihaboku": 1.5}),
]
COURT_ROW = 9.0                        # コートの東の縁に沿った並木の間隔
FEATURE_TREES = [((78.0, 25.0), "Gajumaru", 1.15)]   # 航空写真で目立つ大木
SOTETSU = [(36.0, -100.0), (53.0, -101.0), (84.0, 15.0), (99.0, 2.0), (70.0, 47.0), (-8.0, -205.0), (-6.0, -222.0)]
CROWN = {"Gajumaru": 6.0, "Kobateishi_A": 4.5, "Kobateishi_B": 3.0, "Fukugi": 1.8, "Terihaboku": 4.0,
         "Hououboku": 5.5}           # 樹冠の半径 [m]
SUN_DIR = (0.663, -0.643, 0.383)        # 光の進む向き（エンジンの座標）。西南西の高さ 40°


# ---- 書式（エンジンが保存するときと同じ形） ----

def f32(x):
    v = struct.unpack("<f", struct.pack("<f", x))[0]
    if v == 0.0:
        return 0.0
    for digits in range(1, 10):
        s = float(f"{v:.{digits}g}")
        if struct.unpack("<f", struct.pack("<f", s))[0] == v:
            return s
    return v


def write_json(path, data, indent=4):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(data, ensure_ascii=False, indent=indent, sort_keys=True) + "\n")


def project_path(path):
    return os.path.relpath(path, PROJECT_DIR).replace(os.sep, "/")


def object_id(key):
    """ObjectId::FromKey と同じ（名前の 64bit FNV-1a）。Feature が先に作る水面などと同じ ID になる"""
    h = 14695981039346656037
    for c in key.encode("utf-8"):
        h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return format(h or 1, "016x")


def ensure_meta(path):
    meta = path + ".meta"
    if os.path.exists(meta):
        with open(meta, encoding="utf-8") as f:
            return json.load(f)["guid"]
    guid = str(uuid.uuid5(GUID_NAMESPACE, project_path(path)))
    write_json(meta, {"guid": guid, "type": META_TYPES[os.path.splitext(path)[1].lower()]}, indent=2)
    return guid


def ensure_folder_meta(model):
    folder = os.path.join(MODELS_DIR, model)
    for name in sorted(os.listdir(folder)):
        if os.path.splitext(name)[1].lower() in META_TYPES:
            ensure_meta(os.path.join(folder, name))


def model_ref(model):
    path = os.path.join(MODELS_DIR, model, model + ".gltf")
    if not os.path.exists(path):
        raise SystemExit(f"モデルがありません: {path}")
    ensure_folder_meta(model)
    return {"guid": ensure_meta(path), "path": project_path(path)}


# ---- シーンのオブジェクト ----

class Scene:
    def __init__(self):
        self.objects = {}
        self.order = 0
        self.counts = {}

    def unique(self, base):
        n = self.counts.get(base, 0)
        self.counts[base] = n + 1
        return base if n == 0 else f"{base} ({n})"

    def add(self, name, components):
        obj = {"active": True, "components": components, "id": object_id(name), "name": name,
               "order": self.order}
        self.order += 1
        self.objects[name] = obj
        return obj

    def group(self, name):
        self.add(name, [transform((0.0, 0.0, 0.0))])
        return name

    def mesh(self, model, group, pos, yaw_deg=0.0, scale=1.0):
        name = self.unique(model)
        comp = {"enabled": True, "type": "MeshRenderer",
                "parameters": {"blendMode": 0, "model": model_ref(model), "texture": None}}
        self.add(name, [transform(pos, yaw_deg, scale, group), comp])
        return name


def transform(pos, yaw_deg=0.0, scale=1.0, parent=None):
    """pos = (x 東, y 北, z 上)、yaw_deg = Blender の Z 軸まわり（反時計回り）の角度"""
    x, y, z = pos
    return {"enabled": True, "type": "Transform", "parameters": {
        "parent": {"comp": "Transform", "ref": object_id(parent)} if parent else None,
        "rotate": [0.0, f32(-math.radians(yaw_deg)), 0.0],
        "scale": [f32(scale)] * 3,
        "translate": [f32(x), f32(z), f32(y)]}}


def load_template(name):
    with open(os.path.join(TEMPLATE_DIR, name), encoding="utf-8") as f:
        return json.load(f)


def angle(v):
    return math.degrees(math.atan2(v[1], v[0]))


def heading(azimuth):
    """北から時計回りの方位 → 平面のベクトル"""
    a = math.radians(azimuth)
    return np.array([math.sin(a), math.cos(a)])


def arc(points):
    seg = np.hypot(*np.diff(points, axis=0).T)
    return np.concatenate([[0.0], np.cumsum(seg)])


def at(points, acc, s):
    return np.array([np.interp(s, acc, points[:, 0]), np.interp(s, acc, points[:, 1])])


# ---- 配置 ----

def place_seawall(sc, m, rnd):
    g = sc.group("Seawall")
    wall = m.wall
    acc = arc(wall)
    count = int(acc[-1] // MODULE)
    start = (acc[-1] - count * MODULE) / 2
    placed = []
    for k in range(count):
        s = start + MODULE * (k + 0.5)
        p = at(wall, acc, s)
        t = at(wall, acc, min(s + 1.0, acc[-1])) - at(wall, acc, max(s - 1.0, 0.0))
        # モジュールの +X は線の南向き（-Y が海側になる）
        yaw = angle(-t)
        model = "Seawall_Stairs" if k % STAIRS_EVERY == STAIRS_EVERY // 2 else "Seawall_Straight"
        sc.mesh(model, g, (p[0], p[1], AM.PROMENADE), yaw)
        placed.append((s, p, yaw))
    # 監視台（管理棟の前）。護岸の同じ向きで張り出す
    d = np.hypot(*(wall - np.array(LIFEGUARD_NEAR)).T)
    s = acc[int(np.argmin(d))]
    p = at(wall, acc, s)
    t = at(wall, acc, s + 1.0) - at(wall, acc, s - 1.0)
    sc.mesh("LifeguardTower", g, (p[0], p[1], AM.PROMENADE), angle(-t))
    return wall, acc


def place_jetties(sc):
    g = sc.group("Jetty")
    for root, head, head_model in JETTIES:
        root = np.array(root)
        dvec = np.array(head) - root
        length = float(np.hypot(*dvec))
        dvec /= length
        n = max(1, round((length - JETTY_HEAD) / 10.0))
        # モジュールの -Y を沖の向きに
        yaw = math.degrees(math.atan2(dvec[0], -dvec[1]))
        for k in range(n):
            p = root + dvec * 10.0 * k
            sc.mesh("Jetty_Straight", g, (p[0], p[1], 0.0), yaw)
        p = root + dvec * 10.0 * n
        sc.mesh(head_model, g, (p[0], p[1], 0.0), yaw)


def shore_frame(m, p):
    """波打ち際で p にいちばん近い点と、その点の海側の向き"""
    water = AM.smooth_curve(AM.resample(m.water, 2.0), passes=60)
    nrm = AM.normals(water)
    i = int(np.argmin(np.hypot(*(water - np.array(p)).T)))
    return water[i], nrm[i]


def place_sea(sc, m, field, rnd):
    g = sc.group("Sea")
    for (x, y), s in ROCKS:
        _, nrm = shore_frame(m, (x, y))
        # 長い向きを浜と平行に
        yaw = angle((nrm[1], -nrm[0])) + rnd.uniform(-12.0, 12.0)
        sc.mesh("ArahaIsland", g, (x, y, 0.0), yaw, s)
    # 浮き桟橋（波打ち際から沖へまっすぐ 3 枚、先に横向きの 1 枚で T 字）
    base, nrm = shore_frame(m, PIER_AT)
    yaw = angle(nrm)
    for k in range(3):
        p = base + nrm * (6.0 + 12.0 * k)
        sc.mesh("FloatingPier_Walk", g, (p[0], p[1], 0.0), yaw)
    p = base + nrm * (36.0 + 1.25)
    sc.mesh("FloatingPier", g, (p[0], p[1], 0.0), yaw + 90.0)
    # ネット（波打ち際から沖へ離した線を、両端の突堤の先までつなぐ）
    line = AM.offset_line(m.water, NET_OFFSET, step=2.0, passes=200)
    heads = []
    for root, head, _ in JETTIES:
        heads.append(np.array(head))
    south, north = sorted(heads, key=lambda h: h[1])
    keep = (line[:, 1] > south[1] + 25.0) & (line[:, 1] < north[1] - 25.0)
    line = line[keep]
    path = np.vstack([south, line, north])
    path = AM.resample(path, 1.0)
    acc = arc(path)
    for k in range(int(acc[-1] // 10.0)):
        s = 10.0 * (k + 0.5)
        p = at(path, acc, s)
        t = at(path, acc, s + 1.0) - at(path, acc, s - 1.0)
        sc.mesh("SwimNet", g, (p[0], p[1], 0.0), angle(t))


def place_reef(sc, m, field, rnd):
    g = sc.group("Reef")
    corals = ["Coral_Table_A", "Coral_Branch_A", "Coral_Massive_A", "Coral_Brain", "Coral_Soft", "Coral_Branch_B",
              "Coral_Table_B", "Coral_Massive_B", "Coral_MicroAtoll", "GiantClam", "SeaUrchin", "Anemone_Clownfish"]
    k = 0
    for (x, y), s in ROCKS:
        _, nrm = shore_frame(m, (x, y))
        along = np.array([nrm[1], -nrm[0]])
        for i in range(9):
            a = i / 9 * math.tau + rnd.uniform(-0.2, 0.2)
            r_along = rnd.uniform(11.0, 15.0) * s
            r_across = rnd.uniform(7.5, 11.0) * s
            p = np.array([x, y]) + along * math.cos(a) * r_along + nrm * math.sin(a) * r_across
            z = float(field.height(p[0], p[1]))
            sc.mesh(corals[k % len(corals)], g, (p[0], p[1], z), rnd.uniform(0, 360), rnd.uniform(0.85, 1.15))
            k += 1
    # 砂地の海草（岩から離す）
    n = 0
    while n < 12:
        d = rnd.uniform(25.0, 75.0)
        line = AM.offset_line(m.water, d, step=4.0, passes=100)
        p = line[rnd.randrange(len(line) // 8, len(line) * 7 // 8)]
        if min(np.hypot(p[0] - x, p[1] - y) for (x, y), _ in ROCKS) < 18.0:
            continue
        z = float(field.height(p[0], p[1]))
        sc.mesh("Seagrass_Patch_A" if n % 2 else "Seagrass_Patch_B", g, (p[0], p[1], z), rnd.uniform(0, 360),
                rnd.uniform(0.9, 1.3))
        n += 1
    # 岩のあいだを泳ぐウミガメ
    turtle = load_template("SeaTurtle.json")
    (x, y), _ = ROCKS[1]
    _, nrm = shore_frame(m, (x, y))
    p = np.array([x, y]) + nrm * 14.0
    z = float(field.height(p[0], p[1])) + 1.2
    comps = [transform((p[0], p[1], z), angle((nrm[1], -nrm[0])), 1.0, g)]
    comps += [c for c in turtle["components"] if c["type"] != "Transform"]
    sc.add(sc.unique("SeaTurtle"), comps)


def place_park(sc, m, field, wall, acc, rnd):
    g = sc.group("Park")
    (x, y), az = SHIP
    v = heading(az)
    sc.mesh("IndianOak_Ship", g, (x, y, AM.LAWN), angle(v))
    (x, y), az = MONUMENT
    v = heading(az)
    # 正面（-Y）を az の向きに
    sc.mesh("IndianOak_Monument", g, (x, y, AM.LAWN), angle(v) + 90.0)
    (x, y), az = SIGN
    v = heading(az)
    sc.mesh("ShisaSign", g, (x, y, AM.LAWN), angle(v) + 90.0)


def place_facilities(sc, m, rnd):
    """公園の施設と東屋・遊具・街灯・ベンチ。戻り値は木を避ける点 [(x, y, 半径)]"""
    g = sc.group("ParkFacilities")
    F = AM.PARK_FACILITIES
    keep = []
    (x, y), az, _ = F["ArahaAmphitheater"]
    sc.mesh("ArahaAmphitheater", g, (x, y, AM.LAWN), angle(heading(az)))
    for name in ("ArahaCourts", "ArahaHouse", "ParkToilet"):
        (x, y), az, _ = F[name]
        sc.mesh(name, g, (x, y, AM.LAWN), angle(heading(az)) - 90.0)
    (x, y), az, _ = F["PlayTower"]
    yaw = angle(heading(az))
    sc.mesh("PlayTower", g, (x, y, AM.LAWN), yaw)
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    for model, (lx, ly) in (("SpringLion", (-3.2, -5.6)), ("SpringHippo", (0.2, -6.0)), ("SpringLion", (3.4, -5.4))):
        px, py = x + lx * c - ly * s, y + lx * s + ly * c
        sc.mesh(model, g, (px, py, AM.LAWN), yaw + 180.0 + rnd.uniform(-15, 15))
        keep.append((px, py, 1.5))
    # バーベキューの屋根の並び（長手を海岸に沿わせる）
    along, across = heading(BBQ_AZ), heading(BBQ_AZ + 90.0)
    for i in range(4):
        for j in range(3):
            p = np.array(BBQ_ORIGIN) - along * 8.5 * i + across * 6.5 * j
            sc.mesh("BBQShelter", g, (p[0], p[1], AM.LAWN), angle(along) - 90.0)
            keep.append((p[0], p[1], 3.5))
    for p in GAZEBOS:
        sc.mesh("UmbrellaGazebo", g, (p[0], p[1], AM.LAWN), angle(heading(15.0)))
        keep.append((p[0], p[1], 3.5))
    # 遊歩道の近くの格子屋根の東屋・街灯・ベンチ
    line = AM.offset_line(m.wall, -15.0, step=1.0, passes=100)
    acc = arc(line)
    for f in PERGOLA_AT:
        s_ = acc[-1] * f
        p = at(line, acc, s_)
        t = at(line, acc, s_ + 1.0) - p
        if AM.in_facility(p[0], p[1], margin=4.0):
            continue
        sc.mesh("Pergola", g, (p[0], p[1], AM.LAWN), angle(t))
        keep.append((p[0], p[1], 3.5))
    for d_off, every, model, s0 in ((-4.2, 32.0, "ParkLamp", 10.0), (-3.4, 32.0, "ParkBench", 26.0)):
        line = AM.offset_line(m.wall, d_off, step=1.0, passes=100)
        acc = arc(line)
        nrm = AM.normals(line)
        s_ = s0
        while s_ < acc[-1] - 5.0:
            i = int(np.searchsorted(acc, s_))
            p, n = line[i], nrm[i]
            if model == "ParkLamp":
                # 灯具の腕（+X）を遊歩道の上へ
                sc.mesh(model, g, (p[0], p[1], AM.PROMENADE), angle(n))
            else:
                # 座る人が海を見る（ベンチの -Y を海へ）
                sc.mesh(model, g, (p[0], p[1], AM.PROMENADE), angle(n) + 90.0)
            s_ += every
    # 園路沿いの街灯
    for k, path in enumerate(AM.park_footways(m)):
        acc = arc(path)
        nrm = AM.normals(path)
        s_ = 8.0
        while s_ < acc[-1] - 4.0:
            i = int(np.searchsorted(acc, s_))
            p = path[i] + nrm[i] * 2.2
            sc.mesh("ParkLamp", g, (p[0], p[1], AM.LAWN), angle(-nrm[i]))
            keep.append((p[0], p[1], 1.2))
            s_ += 26.0
    # コートの東の観客席のベンチ
    (cx, cy), az, (hl, hw) = F["ArahaCourts"]
    al, ac = heading(az), heading(az + 90.0)
    for k in range(-3, 4):
        p = np.array((cx, cy)) + ac * (hw - 3.0) + al * (k * 9.0)
        sc.mesh("ParkBench", g, (p[0], p[1], AM.LAWN + 0.08), angle(ac) + 90.0 + 180.0)
    return keep


def place_trees(sc, m, rnd, keep):
    """植栽の区域ごとに間隔を空けて公園の木を散らす"""
    g = sc.group("ParkTrees")
    data = json.load(open(AM.MAP_PATH, encoding="utf-8"))
    park = np.array(data["park"]["points"], float)
    paths = AM.park_footways(m)
    placed = []
    others = [(np.array(SHIP[0]), 9.0), (np.array(MONUMENT[0]), 4.0), (np.array(SIGN[0]), 5.0),
              (np.array(LIFEGUARD_NEAR), 6.0)] + [(np.array((x, y)), r) for x, y, r in keep]

    def ok(x, y, crown, spacing):
        if not AM.inside(np.array([x]), np.array([y]), park)[0] or not AM.inside(np.array([x]), np.array([y]), m.land)[0]:
            return False
        if AM.dist(np.array([x]), np.array([y]), m.wall)[0] < 9.0:
            return False
        if AM.in_facility(x, y, margin=crown * 0.5):
            return False
        for line in paths:
            if np.min(np.hypot(line[:, 0] - x, line[:, 1] - y)) < 2.6:
                return False
        for c, r in others:
            if math.hypot(c[0] - x, c[1] - y) < r + crown * 0.5:
                return False
        for px, py, sp in placed:
            if math.hypot(px - x, py - y) < max(sp, spacing) * 0.8:
                return False
        return True

    def put(model, x, y, spacing, scale=None):
        sc.mesh(model, g, (x, y, AM.LAWN - 0.05), rnd.uniform(0, 360), scale or rnd.uniform(0.85, 1.12))
        placed.append((x, y, spacing))

    for (x, y), model, scale in FEATURE_TREES:
        put(model, x, y, 12.0, scale)
    for (x, y) in SOTETSU:
        if not AM.in_facility(x, y, margin=0.8):
            sc.mesh("Sotetsu", g, (x, y, AM.LAWN - 0.05), rnd.uniform(0, 360), rnd.uniform(0.9, 1.15))
    (cx, cy), az, (hl, hw) = AM.PARK_FACILITIES["ArahaCourts"]
    al, ac = heading(az), heading(az + 90.0)
    for k in range(-4, 5):
        p = np.array((cx, cy)) + ac * (hw + 1.6) + al * (k * COURT_ROW)
        model = "Kobateishi_A" if k % 2 else "Terihaboku"
        if ok(p[0], p[1], CROWN[model] * 0.6, COURT_ROW):
            put(model, p[0], p[1], COURT_ROW)
    for (x0, x1, y0, y1), spacing, weights in TREE_ZONES:
        models = list(weights)
        w = np.array([weights[k] for k in models], float)
        w /= w.sum()
        tries = int((x1 - x0) * (y1 - y0) / spacing ** 2 * 6)
        for _ in range(tries):
            x, y = rnd.uniform(x0, x1), rnd.uniform(y0, y1)
            model = models[int(np.searchsorted(np.cumsum(w), rnd.random()))]
            if ok(x, y, CROWN[model], spacing):
                put(model, x, y, spacing)
    print(f"  公園の木 {len(placed)} 本")


def place_vegetation(sc, m, field, rnd):
    g = sc.group("Vegetation")
    # 遊歩道の陸側の縁に沿ったヤシ並木
    line = AM.offset_line(m.wall, -5.8, step=0.5, passes=200)
    acc = arc(line)
    s = 4.0
    palms = ["CoconutPalm_A", "CoconutPalm_B", "CoconutPalm_C"]
    sign = np.array(SIGN[0])
    while s < acc[-1] - 4.0:
        p = at(line, acc, s)
        if np.hypot(*(p - sign)) > 6.0:
            sc.mesh(rnd.choice(palms), g, (p[0], p[1], AM.LAWN - 0.05), rnd.uniform(0, 360), rnd.uniform(0.9, 1.15))
        s += rnd.uniform(8.0, 10.5)
    # 突堤の付け根と看板のまわりの低木
    for (x, y), model in (((118.0, 300.0), "Adan_A"), ((112.0, 294.0), "Adan_B"), ((-60.0, -330.0), "Adan_A"),
                          ((-52.0, -326.0), "Adan_B"), ((sign[0] + 3.0, sign[1] - 3.0), "Hibiscus_A"),
                          ((sign[0] + 3.0, sign[1] + 3.5), "Bougainvillea_A")):
        reg = int(field.classify(x, y))
        z = float(field.height(x, y)) if reg != 2 else AM.LAWN
        sc.mesh(model, g, (x, y, z - 0.05), rnd.uniform(0, 360))


def place_beach_props(sc, m, field, rnd):
    g = sc.group("BeachProps")
    line = AM.offset_line(m.water, -16.0, step=2.0, passes=100)
    acc = arc(line)
    for s_frac in (0.3, 0.45, 0.62, 0.78):
        p = at(line, acc, acc[-1] * s_frac)
        z = float(field.height(p[0], p[1]))
        sc.mesh("BeachParasol_A" if s_frac < 0.5 else "BeachParasol_B", g, (p[0], p[1], z), rnd.uniform(0, 360))
        for dx in (-1.3, 1.2):
            q = p + np.array([dx, rnd.uniform(-1.4, -0.9)])
            zq = float(field.height(q[0], q[1]))
            sc.mesh("DeckChair_A", g, (q[0], q[1], zq), rnd.uniform(70, 110))


def camera_object(sc):
    (cx, cy, cz), (tx, ty, tz), fov = CAMERA
    f = np.array([tx - cx, tz - cz, ty - cy])  # エンジンの座標 (x, 上, z=北)
    f /= np.linalg.norm(f)
    tf = transform((cx, cy, cz))
    tf["parameters"]["rotate"] = [f32(math.asin(-f[1])), f32(math.atan2(f[0], f[2])), 0.0]
    cam = load_template("MainCamera.json")
    comps = [tf] + [c for c in cam["components"] if c["type"] == "Camera"]
    for c in comps:
        if c["type"] == "Camera":
            c["parameters"]["fov"] = fov
    sc.add("MainCamera", comps)


def sun_object(sc):
    sun = load_template("Sun.json")
    comps = [transform((0.0, 0.0, 20.0))]
    for c in sun["components"]:
        if c["type"] == "Light":
            c["parameters"]["direction"] = [f32(v) for v in SUN_DIR]
            comps.append(c)
    sc.add("Sun", comps)


def water_object(sc):
    water = load_template("WaterPlane.json")
    sc.add("WaterPlane", water["components"])


def main():
    rnd = random.Random(2026)
    m = AM.Map()
    field = AM.Field(m)
    sc = Scene()
    sun_object(sc)
    camera_object(sc)
    water_object(sc)
    g = sc.group("Terrain")
    for t in TERRAIN:
        sc.mesh(t, g, (0.0, 0.0, 0.0))
    wall, acc = place_seawall(sc, m, rnd)
    place_jetties(sc)
    place_sea(sc, m, field, rnd)
    place_reef(sc, m, field, rnd)
    place_park(sc, m, field, wall, acc, rnd)
    place_vegetation(sc, m, field, rnd)
    place_beach_props(sc, m, field, rnd)
    # 公園の施設・木・園路（シーンの最後に足し、別の乱数で置く）
    sc.mesh("ArahaPath", g, (0.0, 0.0, 0.0))
    park_rnd = random.Random(2027)
    keep = place_facilities(sc, m, park_rnd)
    place_trees(sc, m, park_rnd, keep)

    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR)
    for name in ("_scene.json", "_environment.json"):
        shutil.copy(os.path.join(TEMPLATE_DIR, name), os.path.join(OUT_DIR, name))
    for name, obj in sc.objects.items():
        write_json(os.path.join(OUT_DIR, name + ".json"), obj)
    print(f"{len(sc.objects)} objects -> {OUT_DIR}")


if __name__ == "__main__":
    main()
