"""確認用シーン（assemble.py の配置）を、エンジン（CoreEngine）のシーンとして書き出す

    python3 Tools/OkinawaBeach/export_engine_scene.py

出力: Projects/Sandbox/Application/Assets/Scenes/OkinawaBeachScene/
  - 置いた物 1 つにつき 1 つの JSON（Transform + MeshRenderer）。種類ごとのグループ（親）の下にまとめる
  - 魚の群れは FishSchool（Models/Okinawa/FishSchools の配置 JSON を読む）、ウミガメは Animator（泳ぎ）
  - 太陽の向きとカメラの構図（全景 main）は確認用シーンと同じ。水面と見た目の設定は WaterTestScene の値を使う
あわせて Models/Okinawa のアセットに .meta（GUID）が無ければ作る（シーンは GUID とパスでアセットを指す）。
書き出し先に前からある JSON は、今回書かないものを消す（エディタで直した内容は上書きされる）。

置く物は保存済みの Blend/OkinawaBeachScene.blend のワールド行列をそのまま使う。assemble.py の LAYOUT / SCHOOLS から
組み立てと同じ方法（地形・桟橋・海底だけに真上からレイを当てた高さ）で求めた位置・大きさと 1 つずつ突き合わせ、
食い違えば何も書かない（魚は回転も含めて突き合わせる）。
.blend に無い物（骨を入れてから組み直していないウミガメ）は LAYOUT の位置に置く。LAYOUT の回転は、glTF から読んだ
物が回転をクォータニオンで持つため assemble.place の rotation_euler が効かず、.blend では 0 のままなので、
.blend の物に回転が無ければ同じく回さない（Blender のシーンと同じ見た目にする）。
書いた後は JSON を読み直し、エンジンと同じ式（行ベクトル規約の S * Rx * Ry * Rz * T、クォータニオン）で組んだ
ワールド行列を、Blender のワールド行列と比べる。

座標: Blender（右手系・Z 上）の (x, y, z) → エンジン（glTF を Assimp の ConvertToLeftHanded で読んだ左手系・Y 上）の
(x, z, y)。回転は Transform の rotate（ラジアン、X → Y → Z の順に固定軸まわり）で書く。
"""
import json
import math
import os
import re
import struct
import sys
import uuid
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
from mathutils import Matrix, Quaternion, Vector  # noqa: E402

import assemble as A  # noqa: E402
from okinawa import common as C  # noqa: E402
from okinawa.assets import fish as F  # noqa: E402

SCENE_NAME = "OkinawaBeachScene"
PROJECT_DIR = os.path.join(C.REPO_ROOT, "Projects", "Sandbox")
SCENES_DIR = os.path.join(PROJECT_DIR, "Application", "Assets", "Scenes")
OUT_DIR = os.path.join(SCENES_DIR, SCENE_NAME)
TEMPLATE_DIR = os.path.join(SCENES_DIR, "WaterTestScene")  # 水面・見た目の設定の元
BLEND_PATH = os.path.join(C.BLEND_DIR, "OkinawaBeachScene.blend")

# Blender → エンジンの基底の入れ替え（Y と Z）
P = Matrix(((1, 0, 0), (0, 0, 1), (0, 1, 0)))

# 置く物のグループ（エディタの階層でまとめる親）。モデル名の先頭で振り分ける
GROUPS = [
    ("Terrain", ("BeachTerrain_", "ReefTerrain_")),
    ("Rocks", ("NotchRock_", "ReefRock_", "Tetrapod_", "BeachRock_")),
    ("Vegetation", ("CoconutPalm_", "Adan_", "Hibiscus_", "Bougainvillea_")),
    ("Village", ("Azumaya_", "Ishigaki_", "Shisa_")),
    ("BeachProps", ("Sabani_", "BeachParasol_", "DeckChair_", "Driftwood_", "Coconut_Husk", "GlassFloat",
                    "Shell_", "CoralPiece_")),
    ("Pier", ("Pier_",)),
    ("Reef", ("Coral_", "GiantClam", "SeaUrchin", "Anemone_Clownfish", "Seagrass_Patch_", "SeaCucumber",
              "BlueStarfish")),
    ("SeaLife", ("SeaTurtle",)),  # 魚の群れもここへ入れる
]
ANIMATED = {"SeaTurtle": "Swim"}  # 骨のアニメーションで動かすモデル → 最初に流すクリップ
CAMERA = "main"                   # エンジンの MainCamera にする確認用カメラ
SENSOR_WIDTH = 36.0               # add_camera のカメラのセンサー幅 [mm]（Blender の既定）
ASPECT = 16.0 / 9.0               # 確認用のレンダーの縦横比（エンジンの fov は縦の視野角）

# .meta を作るアセットの種類（AssetDatabase::GetAssetType と同じ。.bin はアセットとして登録されない）
META_TYPES = {".gltf": "Model", ".glb": "Model", ".png": "Texture", ".jpg": "Texture", ".json": "Json"}
# GUID はプロジェクトからのパスで決める（.meta を消して作り直しても同じ値になる）
GUID_NAMESPACE = uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/tanahara0310/CoreEngine/Tools/OkinawaBeach")


# ---- 書式（エンジンが保存するときと同じ形にして、エディタで保存し直しても差分が出ないようにする）----

def f32(x):
    """float に丸め、float として最も短い 10 進の値にする（JsonManager の ShortenFloats と同じ）"""
    v = struct.unpack("<f", struct.pack("<f", x))[0]
    if v == 0.0:
        return 0.0  # -0.0 も 0.0 にそろえる
    for digits in range(1, 10):
        s = float(f"{v:.{digits}g}")
        if struct.unpack("<f", struct.pack("<f", s))[0] == v:
            return s
    return v


def write_json(path, data, indent=4):
    """キーの名前順・4 字下げ・LF・末尾に改行（nlohmann::json の dump(4) と同じ並び）"""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(data, ensure_ascii=False, indent=indent, sort_keys=True) + "\n")


def project_path(path):
    """プロジェクト（Projects/Sandbox）からの相対パス（区切りは /）"""
    return os.path.relpath(path, PROJECT_DIR).replace(os.sep, "/")


def object_id(key):
    """ObjectId::FromKey と同じ（保存キーの 64bit FNV-1a）"""
    h = 14695981039346656037
    for c in key.encode("utf-8"):
        h = ((h ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return format(h or 1, "016x")


def ensure_meta(path):
    """アセットの .meta の GUID を返す（無ければ作る。形はエンジンの .meta と同じ）"""
    meta = path + ".meta"
    if os.path.exists(meta):
        with open(meta, encoding="utf-8") as f:
            return json.load(f)["guid"]
    guid = str(uuid.uuid5(GUID_NAMESPACE, project_path(path)))
    kind = META_TYPES[os.path.splitext(path)[1].lower()]
    write_json(meta, {"guid": guid, "type": kind}, indent=2)
    return guid


def ensure_all_meta():
    """Models/Okinawa の下のアセット全部に .meta を付ける（作った数を返す）"""
    made = 0
    for root, _, files in os.walk(C.MODELS_DIR):
        for name in sorted(files):
            if os.path.splitext(name)[1].lower() not in META_TYPES:
                continue
            path = os.path.join(root, name)
            if not os.path.exists(path + ".meta"):
                made += 1
            ensure_meta(path)
    return made


def asset_ref(path):
    """AssetRef の保存形 {guid, path}"""
    return {"guid": ensure_meta(path), "path": project_path(path)}


def model_file(name):
    return os.path.join(C.MODELS_DIR, name, f"{name}.gltf")


def school_file(name):
    return os.path.join(C.MODELS_DIR, "FishSchools", f"{name}.json")


# ---- 配置（assemble.py と同じ求め方）----

def ray_targets():
    """組み立てで高さを調べたときにあった物（地形・桟橋・確認用の海底）"""
    heads = ("BeachTerrain_", "ReefTerrain_", "Pier_", "PreviewSeabed")
    return [o for o in bpy.context.scene.objects if o.type == "MESH" and o.name.startswith(heads)]


def terrain_height(targets, x, y):
    """assemble.terrain_height と同じ（真上から下へレイを当て、いちばん上の当たり）"""
    best = None
    for o in targets:
        inv = o.matrix_world.inverted()
        origin = inv @ Vector((x, y, 100.0))
        direction = (inv.to_3x3() @ Vector((0.0, 0.0, -1.0))).normalized()
        hit, loc, *_ = o.ray_cast(origin, direction)
        if hit:
            z = (o.matrix_world @ loc).z
            best = z if best is None else max(best, z)
    return best if best is not None else 0.0


def layout_items(targets):
    """LAYOUT → [(モデル名, ワールド行列)]（地形に合わせる物は、地形の高さ + z）"""
    out = []
    for name, (x, y, z), rot, scale in A.LAYOUT:
        if name not in A.NO_SNAP:
            z = terrain_height(targets, x, y) + z
        M = Matrix.LocRotScale(Vector((x, y, z)), Matrix.Rotation(math.radians(rot), 3, "Z"),
                               Vector((scale, scale, scale)))
        out.append((name, M))
    return out


def school_items(targets):
    """SCHOOLS → [(配置 JSON の名前, 群れのワールド行列, [(モデル名, 1 匹のワールド行列)])]"""
    out = []
    for name, (x, y, h), rot in A.SCHOOLS:
        M = Matrix.Translation(Vector((x, y, terrain_height(targets, x, y) + h))) @ \
            Matrix.Rotation(math.radians(rot), 4, "Z")
        _, fishes = F.load_school(school_file(name))
        members = [(model, M @ Matrix.Translation(p) @ R.to_4x4() @ Matrix.Scale(s, 4))
                   for model, p, R, s, _ in fishes]
        out.append((name, M, members))
    return out


def blend_objects():
    """保存済みの確認用シーンの物 → (置いた物, 魚)。どちらもモデル名 → [ワールド行列]"""
    statics, fish = defaultdict(list), defaultdict(list)
    for o in bpy.context.scene.objects:
        if o.type != "MESH":
            continue
        base = re.sub(r"\.\d{3}$", "", o.name)
        if not os.path.exists(model_file(base)):
            continue  # 確認用の水・海底など
        (fish if "phase" in o.keys() else statics)[base].append(o.matrix_world.copy())
    return statics, fish


def matrix_diff(a, b):
    return max(abs(a[i][j] - b[i][j]) for i in range(4) for j in range(4))


def has_rotation(M):
    return M.to_3x3().normalized().to_quaternion().angle > 1e-6


def placed_items(items, statics):
    """LAYOUT の各物に .blend の物を対応づける → ([(モデル名, ワールド行列)], 位置と大きさの最大の差)

    .blend の物は、同じモデルで位置のいちばん近いものを 1 対 1 で選び、行列は .blend の値を使う。
    .blend に無い物は、.blend の物に回転があるかどうかに合わせて LAYOUT の行列から作る"""
    pool = {name: list(ms) for name, ms in statics.items()}
    matched, absent, worst = [], [], 0.0
    for model, M in items:
        cands = pool.get(model, [])
        if not cands:
            absent.append((model, M))
            continue
        i = min(range(len(cands)), key=lambda k: (cands[k].translation - M.translation).length)
        B = cands.pop(i)
        worst = max(worst, (B.translation - M.translation).length, (B.to_scale() - M.to_scale()).length)
        matched.append((model, B))
    extra = [name for name, ms in pool.items() for _ in ms]
    if extra or any(model not in ANIMATED for model, _ in absent):
        raise SystemExit(f"確認用シーンと LAYOUT の物が合いません（.blend に無い {[m for m, _ in absent]}、"
                         f"LAYOUT に無い {extra}）")
    rotated = any(has_rotation(B) for _, B in matched)
    for model, M in absent:
        loc, rot, scale = M.decompose()
        matched.append((model, M if rotated else Matrix.LocRotScale(loc, None, scale)))
    return matched, worst, rotated, [m for m, _ in absent]


def match(expected, actual):
    """モデルごとに、位置のいちばん近いもの同士を 1 対 1 で対応づける → (最大の差, 対応の無い期待値, 余った実物)"""
    worst, missing, extra = 0.0, [], 0
    for name in sorted(set(expected) | set(actual)):
        pool = list(actual.get(name, []))
        for M in expected.get(name, []):
            if not pool:
                missing.append(name)
                continue
            i = min(range(len(pool)), key=lambda k: (pool[k].translation - M.translation).length)
            worst = max(worst, matrix_diff(pool.pop(i), M))
        extra += len(pool)
    return worst, missing, extra


# ---- エンジンの値へ ----

def engine_trs(M):
    """Blender のワールド行列 → エンジンの translate / rotate / scale"""
    loc, rot, scale = M.decompose()
    R = P @ rot.to_matrix() @ P  # エンジン座標での回転（列ベクトル。rotate の意味は Rz Ry Rx）
    if abs(R[1][1] - 1.0) < 1e-6:
        # 縦軸（エンジンの Y）まわりだけの回転は Y の角度だけで書く（エディタで読みやすい値にする）
        e = (0.0, math.atan2(R[0][2], R[2][2]), 0.0)
    else:
        e = tuple(R.to_euler("XYZ"))
    return {
        "translate": [f32(loc.x), f32(loc.z), f32(loc.y)],
        "rotate": [f32(a) for a in e],
        "scale": [f32(scale.x), f32(scale.z), f32(scale.y)],
    }


def transform(M=None, parent=None):
    params = engine_trs(M if M is not None else Matrix.Identity(4))
    params["parent"] = {"comp": "Transform", "ref": object_id(parent)} if parent else None
    return {"enabled": True, "parameters": params, "type": "Transform"}


def game_object(name, order, components):
    return {"active": True, "components": components, "id": object_id(name), "name": name, "order": order}


def group_of(model):
    for group, heads in GROUPS:
        if model.startswith(heads):
            return group
    raise SystemExit(f"{model} の入るグループが GROUPS にありません")


def load_template(name):
    with open(os.path.join(TEMPLATE_DIR, name), encoding="utf-8") as f:
        return json.load(f)


def sun_object(order):
    """太陽: WaterTestScene の値に、確認用シーンの太陽の向きを入れる"""
    sun = bpy.data.objects["Sun"]
    d = (sun.matrix_world.to_3x3() @ Vector((0.0, 0.0, -1.0))).normalized()  # 光の進む向き（Blender）
    data = load_template("Sun.json")
    comps = []
    for c in data["components"]:
        if c["type"] == "Light":
            c["parameters"]["direction"] = [f32(d.x), f32(d.z), f32(d.y)]
            comps.append(c)
    return game_object("Sun", order, [transform(Matrix.Translation((0.0, 0.0, 20.0)))] + comps)


def camera_object(order):
    """ゲームの視点: 確認用カメラと同じ位置・向き・縦の視野角"""
    loc, target, lens = A.CAMERAS[CAMERA]
    pos = Vector(loc)
    f = P @ (Vector(target) - pos).normalized()  # エンジンの前方向（+Z が前）
    hfov = 2.0 * math.atan(SENSOR_WIDTH * 0.5 / lens)
    vfov = 2.0 * math.atan(math.tan(hfov * 0.5) / ASPECT)
    tf = transform(Matrix.Translation(pos))
    # Camera::LookAt と同じ（ロールは 0）
    tf["parameters"]["rotate"] = [f32(math.asin(-f.y)), f32(math.atan2(f.x, f.z)), 0.0]
    cam = {"enabled": True, "type": "Camera", "parameters": {
        "farClip": 20000.0, "fov": f32(math.degrees(vfov)), "isMainCamera": True, "nearClip": 0.1, "projection": 0}}
    return game_object("MainCamera", order, [tf, cam])


def water_object(order):
    """水面: WaterTestScene と同じ FFT の海を、平均水面（Blender の z = 0）に置く"""
    data = load_template("WaterPlane.json")
    comps = []
    for c in data["components"]:
        if c["type"] == "Transform":
            c = transform(Matrix.Diagonal((40.0, 40.0, 1.0, 1.0)))  # 100 m の格子を 40 倍（4 km 四方）
        comps.append(c)
    return game_object("WaterPlane", order, comps)


def mesh_renderer(model):
    return {"enabled": True, "type": "MeshRenderer",
            "parameters": {"blendMode": 0, "model": asset_ref(model_file(model)), "texture": None}}


def animator(model):
    return {"enabled": True, "type": "Animator",
            "parameters": {"clips": [{"name": ANIMATED[model]}], "model": project_path(model_file(model))}}


def fish_school(name):
    return {"enabled": True, "type": "FishSchool",
            "parameters": {"castShadow": True, "drift": 1.0, "school": asset_ref(school_file(name)),
                           "swimSpeed": 1.0}}


def build_objects(items, schools):
    """書き出すオブジェクト（ファイル名 → 中身）。名前が重なるものは「名前 (1)」「名前 (2)」…にする"""
    counts = defaultdict(int)

    def unique(base):
        n = counts[base]
        counts[base] += 1
        return base if n == 0 else f"{base} ({n})"

    members = defaultdict(list)  # グループ → [(名前, 部品)]
    for model, M in items:
        name = unique(model)
        comp = animator(model) if model in ANIMATED else mesh_renderer(model)
        members[group_of(model)].append((name, M, comp))
    for school, M, _ in schools:
        members["SeaLife"].append((unique(school), M, fish_school(school)))

    objects = {}
    order = 0
    for build in (sun_object, camera_object, water_object):
        obj = build(order)
        objects[obj["name"]] = obj
        order += 1
    for group, _ in GROUPS:
        if not members[group]:
            continue
        objects[group] = game_object(group, order, [transform()])
        order += 1
        for name, M, comp in members[group]:
            objects[name] = game_object(name, order, [transform(M, parent=group), comp])
            order += 1
    return objects


# ---- 書いた JSON を、エンジンと同じ式で読み直して確かめる ----

def row_affine(scale, rotate, translate):
    """MathCore::Matrix::MakeAffine（オイラー角）と同じ: S * Rx * Ry * Rz * T（行ベクトル規約）→ 列ベクトルの行列"""
    rx, ry, rz = rotate
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    # DirectXMath の XMMatrixRotationX/Y/Z（行ベクトル規約）
    Rx = Matrix(((1, 0, 0, 0), (0, cx, sx, 0), (0, -sx, cx, 0), (0, 0, 0, 1)))
    Ry = Matrix(((cy, 0, -sy, 0), (0, 1, 0, 0), (sy, 0, cy, 0), (0, 0, 0, 1)))
    Rz = Matrix(((cz, sz, 0, 0), (-sz, cz, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))
    S = Matrix.Diagonal((*scale, 1.0))
    T = Matrix.Identity(4)
    T[3][0], T[3][1], T[3][2] = translate
    return (S @ Rx @ Ry @ Rz @ T).transposed()


def to_engine_matrix(M):
    """Blender のワールド行列 → エンジン座標での同じ変換（列ベクトル）"""
    P4 = P.to_4x4()
    return P4 @ M @ P4


def component(obj, kind):
    return next((c for c in obj["components"] if c["type"] == kind), None)


def model_name(path):
    return os.path.splitext(os.path.basename(path))[0]


def check_written(objects, items, schools):
    """書いた Transform から組んだ行列（親も掛ける）と、Blender の行列（エンジン座標へ移したもの）の最大の差"""
    by_id = {obj["id"]: name for name, obj in objects.items()}

    def world(name):
        params = component(objects[name], "Transform")["parameters"]
        M = row_affine(params["scale"], params["rotate"], params["translate"])
        if params["parent"]:
            M = world(by_id[params["parent"]["ref"]]) @ M  # 行ベクトル規約の local * parent と同じ
        return M

    placed, fish_placed = defaultdict(list), defaultdict(list)
    for name, obj in objects.items():
        renderer = component(obj, "MeshRenderer")
        if renderer and renderer["parameters"]["model"]:
            placed[model_name(renderer["parameters"]["model"]["path"])].append(world(name))
        anim = component(obj, "Animator")
        if anim:
            placed[model_name(anim["parameters"]["model"])].append(world(name))
        school = component(obj, "FishSchool")
        if school:
            # FishSchoolComponent と同じく、配置 JSON の 1 匹の行列（クォータニオン）に群れの行列を掛ける
            with open(os.path.join(PROJECT_DIR, school["parameters"]["school"]["path"]), encoding="utf-8") as f:
                data = json.load(f)
            for it in data["instances"]:
                qx, qy, qz, qw = it["rotation"]
                local = Matrix.LocRotScale(Vector(it["position"]), Quaternion((qw, qx, qy, qz)),
                                           Vector((it["scale"],) * 3))
                fish_placed[it["model"]].append(world(name) @ local)

    expected, fish_expected = defaultdict(list), defaultdict(list)
    for model, M in items:
        expected[model].append(to_engine_matrix(M))
    for _, _, members in schools:
        for model, M in members:
            fish_expected[model].append(to_engine_matrix(M))
    worst, missing, extra = match(expected, placed)
    fish_worst, fish_missing, fish_extra = match(fish_expected, fish_placed)
    if missing or extra or fish_missing or fish_extra:
        raise SystemExit(f"書いたシーンの数が合いません（足りない {missing + fish_missing}、"
                         f"余り {extra + fish_extra}）")
    return max(worst, fish_worst)


def main():
    bpy.ops.wm.open_mainfile(filepath=BLEND_PATH)
    targets = ray_targets()
    schools = school_items(targets)

    # 保存済みの .blend と突き合わせる
    statics, fish = blend_objects()
    items, worst, rotated, absent = placed_items(layout_items(targets), statics)
    fish_expected = defaultdict(list)
    for _, _, members in schools:
        for model, M in members:
            fish_expected[model].append(M)
    fish_worst, fish_missing, fish_extra = match(fish_expected, fish)
    print(f"確認用シーンとの照合: 置いた物 {len(items) - len(absent)} 個（位置と大きさの最大の差 {worst:.2e}）、"
          f"魚 {sum(map(len, fish_expected.values()))} 匹（行列の最大の差 {fish_worst:.2e}）")
    print(f".blend に無いので LAYOUT の位置に置く物: {absent}（配置表の回転は"
          f"{'使う' if rotated else ' .blend と同じく使わない'}）")
    if fish_missing or fish_extra or max(worst, fish_worst) > 1e-4:
        raise SystemExit(f"確認用シーンと食い違います（魚の足りない {fish_missing}、余り {fish_extra}）")

    made = ensure_all_meta()
    objects = build_objects(items, schools)

    os.makedirs(OUT_DIR, exist_ok=True)
    stale = [f for f in os.listdir(OUT_DIR)
             if f.endswith(".json") and not f.startswith("_") and f[:-5] not in objects]
    for f in stale:
        os.remove(os.path.join(OUT_DIR, f))
    for name, obj in objects.items():
        write_json(os.path.join(OUT_DIR, f"{name}.json"), obj)
    write_json(os.path.join(OUT_DIR, "_scene.json"), load_template("_scene.json"))
    write_json(os.path.join(OUT_DIR, "_environment.json"), load_template("_environment.json"))

    diff = check_written(objects, items, schools)
    print(f"書き出し: {OUT_DIR}（オブジェクト {len(objects)} 個、消した古いファイル {len(stale)} 個、"
          f"作った .meta {made} 個）")
    print(f"書いた値をエンジンの式で組み直した行列と、Blender の行列の最大の差: {diff:.2e}")
    if diff > 1e-4:
        raise SystemExit("書いた値が Blender の配置と合いません")


if __name__ == "__main__":
    main()
