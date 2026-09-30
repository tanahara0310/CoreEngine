"""エンジン（CoreEngine）の頂点アニメーションと魚の群れの配置が、Blender の確認用デモと同じ動き・配置になるかを調べる

使い方（Linux でも動く。エンジンのビルドは不要）:
    pip install slangpy numpy
    python3 Tools/OkinawaBeach/verify_engine_anim.py

1. Shaders/RayTracing/VertexAnimationDeform.CS.hlsl（描画の頂点シェーダーと同じ VertexAnimation.hlsli の
   ApplyVertexAnimation を呼ぶ）を slang の CPU バックエンドで実行し、書き出した glTF の全頂点について
   okinawa/motion.py のジオメトリノードと同じ式（numpy 版、Blender 座標）の結果と比べる。
2. 群れの配置 JSON の位置・回転を、FishSchoolComponent と同じ MathCore::Matrix::MakeAffine
   （DirectXMath の XMMatrixAffineTransformation = S R T、行ベクトル）で行列にし、
   Blender の確認用シーン（assemble.place_school: T(p) R S、列ベクトル）の配置と一致するかを見る。

座標: エンジン = glTF の (x, y, -z)（Assimp の ConvertToLeftHanded）、Blender = glTF の (x, -z, y)。
つまりエンジン = Blender の (x, z, y)。UV は Assimp の FlipUVs で v → 1 - v。
"""
import json
import math
import os
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SHADERS = os.path.join(ROOT, "Project", "Engine", "Assets", "Shaders")
MODELS = os.path.join(ROOT, "Projects", "Sandbox", "Application", "Assets", "Models", "Okinawa")
TAU = 2.0 * math.pi
T = 1.37             # 調べる時刻 [s]
TOLERANCE = 1e-4     # 許す差 [m]（float の丸め誤差は数 µm）


# ---------------------------------------------------------------------------
# glTF と座標変換
# ---------------------------------------------------------------------------
def load_gltf(name):
    """位置・法線・TEXCOORD_1/2・インデックスと、プリミティブ（= マテリアル）ごとのインデックス範囲"""
    path = os.path.join(MODELS, name, name + ".gltf")
    with open(path, encoding="utf-8") as f:
        g = json.load(f)
    bins = {}

    def accessor(i):
        a = g["accessors"][i]
        view = g["bufferViews"][a["bufferView"]]
        if view["buffer"] not in bins:
            uri = g["buffers"][view["buffer"]]["uri"]
            with open(os.path.join(os.path.dirname(path), uri), "rb") as fb:
                bins[view["buffer"]] = fb.read()
        dtype = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a["componentType"]]
        ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        offset = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        arr = np.frombuffer(bins[view["buffer"]], dtype=dtype, count=a["count"] * ncomp, offset=offset)
        return arr.reshape(a["count"], ncomp) if ncomp > 1 else arr

    parts = {k: [] for k in ("P", "N", "U1", "U2", "I")}
    ranges = []
    base = start = 0
    for prim in g["meshes"][0]["primitives"]:
        at = prim["attributes"]
        p = accessor(at["POSITION"]).astype(np.float64)
        idx = accessor(prim["indices"]).astype(np.int64) + base
        parts["P"].append(p)
        parts["N"].append(accessor(at["NORMAL"]).astype(np.float64))
        parts["U1"].append(accessor(at["TEXCOORD_1"]).astype(np.float64))
        parts["U2"].append(accessor(at["TEXCOORD_2"]).astype(np.float64))
        parts["I"].append(idx)
        ranges.append((start, len(idx)))
        start += len(idx)
        base += len(p)
    P, N, U1, U2, I = (np.concatenate(parts[k]) for k in ("P", "N", "U1", "U2", "I"))
    # FlipUVs で元の値 (R, G), (B, A) に戻る
    anim = np.concatenate([np.stack([U1[:, 0], 1.0 - U1[:, 1]], 1), np.stack([U2[:, 0], 1.0 - U2[:, 1]], 1)], 1)
    return P * [1.0, 1.0, -1.0], N * [1.0, 1.0, -1.0], anim, I, ranges


def swap_yz(v):
    """エンジン ⇔ Blender（y と z の入れ替え。自分自身が逆変換）"""
    v = np.asarray(v, dtype=np.float64)
    return v[..., [0, 2, 1]]


def quantize_hz(hz):
    """VertexAnimationQuantizeHz と同じ（240 秒で途切れない周波数に丸める）"""
    return round(hz * 240.0) / 240.0


# ---------------------------------------------------------------------------
# okinawa/motion.py と同じ式（Blender 座標、Z-up）
# ---------------------------------------------------------------------------
def reference_wind(p, n, anim, wind, strength, phase, loop, branch_up, bias, freq):
    R, G, B, A = anim.T
    wt = TAU / loop * T

    def wave(k, ph):
        return np.sin(wt * k + ph)

    D = wind / np.linalg.norm(wind)
    Dp = np.cross([0.0, 0.0, 1.0], D)
    S = strength
    gust = S * (0.55 + 0.3 * wave(1.0, phase) + 0.15 * wave(3.0, phase * 1.3))
    side = S * 0.25 * wave(2.0, phase * 2.0)
    q = p + (D * gust + Dp * side)[None, :] * A[:, None]
    length, qlen = np.linalg.norm(p, axis=1), np.linalg.norm(q, axis=1)
    p1 = np.where(qlen[:, None] > 1e-12, q / np.maximum(qlen, 1e-30)[:, None] * length[:, None], 0.0)
    phb = G * TAU + phase
    wb = 0.65 * wave(freq[0], phb) + 0.35 * wave(freq[1], phb * 1.7)
    dir_b = np.array([0.0, 0.0, 1.0])[None, :] * (wb * branch_up)[:, None] + D[None, :] * (S * bias + 0.4 * wb)[:, None]
    sgn = np.where(n[:, 2] > -1e-6, 1.0, -1.0)
    phf = G * 10.0 * math.pi + (p @ np.array([1.3, 1.7, 2.1])) * 3.0
    return p1 + dir_b * (B * S)[:, None] + n * (sgn * R * S * wave(freq[2], phf))[:, None]


def reference_swim(p, anim, phase, body_hz, fin_hz, wavelength, k):
    tb, amp, pec, side = anim.T
    arg = TAU * (tb / wavelength - body_hz * T + phase)
    flap = TAU * (fin_hz * T + 1.7 * phase)
    oy = amp * k * np.sin(arg) + side * pec * k * np.sin(flap)
    ox = -0.5 * pec * k * np.cos(flap)
    return p + np.stack([ox, oy, np.zeros_like(ox)], axis=1)


# ---------------------------------------------------------------------------
# エンジンの行列
# ---------------------------------------------------------------------------
def rotation_y(deg, translate):
    """行ベクトル規約（p' = p M）の Y 軸回転 + 平行移動（XMMatrixRotationY と同じ向き）"""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    m = np.eye(4)
    m[0, 0], m[0, 2], m[2, 0], m[2, 2] = c, -s, s, c
    m[3, :3] = translate
    return m


def object_phase(world):
    """ApplyVertexAnimation の個体ごとの位相 0..1"""
    return float(np.dot(world[3, :3], [0.371, 0.137, 0.613])) % 1.0


def xm_rotation_quaternion(q):
    """DirectXMath の XMMatrixRotationQuaternion（行ベクトル規約）"""
    x, y, z, w = q
    return np.array([
        [1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * z * w, 2 * x * z - 2 * y * w],
        [2 * x * y - 2 * z * w, 1 - 2 * x * x - 2 * z * z, 2 * y * z + 2 * x * w],
        [2 * x * z + 2 * y * w, 2 * y * z - 2 * x * w, 1 - 2 * x * x - 2 * y * y]])


def blender_quaternion_matrix(w, x, y, z):
    """mathutils.Quaternion.to_matrix（列ベクトル規約）"""
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
        [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
        [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])


# ---------------------------------------------------------------------------
# 1. 頂点アニメーション
# ---------------------------------------------------------------------------
def check_vertex_animation():
    import slangpy as spy

    dirs = sorted({d for d, _, fs in os.walk(SHADERS) if any(f.endswith((".hlsl", ".hlsli")) for f in fs)})
    device = spy.create_device(type=spy.DeviceType.cpu, include_paths=dirs)
    path = os.path.join(SHADERS, "RayTracing", "VertexAnimationDeform.CS.hlsl")
    with open(path, encoding="utf-8") as f:
        # main は CPU バックエンド（C++）の予約名なので、読み込む文字列の中だけ名前を変える
        source = f.read().replace("void main(", "void csMain(")
    module = device.load_module_from_source("vertex_animation_deform", source, path)
    kernel = device.create_compute_kernel(device.link_program([module], [module.entry_point("csMain")]))

    anim_params = {"time": T, "prevTime": T - 1.0 / 60.0, "windStrength": 1.3, "waterStrength": 0.7,
                   "windDir": spy.float2(0.6, 0.8), "waterDir": spy.float2(-0.8, 0.6)}

    def run(name, mode, strength, speed, world):
        P, N, anim, I, ranges = load_gltf(name)
        verts = np.zeros((len(P), 16), np.float32)   # C++ の VertexData（64 バイト）
        verts[:, 0:3], verts[:, 3], verts[:, 6:9], verts[:, 12:16] = P, 1.0, N, anim
        src = device.create_buffer(element_count=len(P), struct_size=64,
                                   usage=spy.BufferUsage.shader_resource, data=verts)
        idx = device.create_buffer(element_count=len(I), struct_size=4,
                                   usage=spy.BufferUsage.shader_resource, data=I.astype(np.uint32))
        out = device.create_buffer(element_count=len(P), struct_size=12, usage=spy.BufferUsage.unordered_access)
        material = {"color": spy.float4(1, 1, 1, 1), "uvTransform": spy.float4x4(np.eye(4, dtype=np.float32)),
                    "metallic": 0.0, "roughness": 0.5, "occlusionStrength": 1.0, "useNormalMap": 0,
                    "emissiveFactor": spy.float3(0, 0, 0), "enableLighting": 1, "enableDithering": 0,
                    "ditheringScale": 1.0, "alphaCutoff": 0.5, "iblIntensity": 1.0,
                    "vertexAnimation": mode, "vertexAnimStrength": strength, "vertexAnimSpeed": speed,
                    "vertexAnimPadding": 0.0}
        for start, count in ranges:  # エンジンと同じくサブメッシュ（マテリアル）ごとに 1 回
            kernel.dispatch(thread_count=[count, 1, 1], vars={
                "gSourceVertices": src, "gIndices": idx, "gOutputPositions": out,
                "gMaterial": material, "gVertexAnim": anim_params,
                "gDeform": {"world": spy.float4x4(world.astype(np.float32)),
                            "worldInverseTranspose": spy.float4x4(np.linalg.inv(world).T.astype(np.float32)),
                            "indexStart": start, "indexCount": count, "vertexCount": len(P), "padding": 0}})
        return P, N, anim, out.to_numpy().view(np.float32).reshape(len(P), 3).astype(np.float64)

    def wind_in_object_space(world, xz):
        d = np.linalg.inv(world).T[:3, :3] @ np.array([xz[0], 0.0, xz[1]])
        return swap_yz(d / np.linalg.norm(d))

    worst = 0.0

    def report(label, got, want, rest):
        nonlocal worst
        moved = np.linalg.norm(want - rest, axis=1)
        diff = np.linalg.norm(got - want, axis=1).max()
        worst = max(worst, diff)
        print(f"  {label:<34} {len(rest):6d} 頂点  変位 最大 {moved.max():.4f} m / 平均 {moved.mean():.4f} m"
              f"  差 {diff:.2e} m")

    print("1. 頂点アニメーション（HLSL を CPU で実行 vs Blender のジオメトリノードと同じ式）")
    world = rotation_y(30.0, [3.2, 0.4, -1.7])
    for name in ["CoconutPalm_A", "CoconutPalm_C", "Adan_A", "Hibiscus_A", "Bougainvillea_A"]:
        P, N, anim, got = run(name, 1, 0.8, 1.0, world)
        want = reference_wind(swap_yz(P), swap_yz(N), anim, wind_in_object_space(world, (0.6, 0.8)),
                              1.3 * 0.8, object_phase(world) * TAU, 4.0, 0.8, 0.6, (4.0, 9.0, 28.0))
        report(f"植物 {name}", got, swap_yz(want), P)
    world = rotation_y(-50.0, [-2.0, -1.5, 4.0])
    for name in ["Seagrass_Patch_A", "Seagrass_Patch_B"]:
        P, N, anim, got = run(name, 2, 1.0, 1.0, world)
        want = reference_wind(swap_yz(P), swap_yz(N), anim, wind_in_object_space(world, (-0.8, 0.6)),
                              0.7, object_phase(world) * TAU, 6.0, 0.0, 0.25, (1.0, 3.0, 10.0))
        report(f"海草 {name}", got, swap_yz(want), P)
    world = rotation_y(10.0, [0.3, 1.1, 0.2])
    for name, speed in [("Fish_SapphireDevil_F", 2.4 / 2.2), ("Fish_BlueGreenChromis_A", 1.0),
                        ("Fish_ThreadfinButterfly", 1.4 / 2.2)]:
        P, N, anim, got = run(name, 3, 1.2, speed, world)
        want = reference_swim(swap_yz(P), anim, object_phase(world), quantize_hz(2.2 * speed),
                              quantize_hz(4.5 * speed), 0.95, 1.2)
        report(f"魚 {name}", got, swap_yz(want), P)
    return worst


# ---------------------------------------------------------------------------
# 2. 魚の群れの配置
# ---------------------------------------------------------------------------
def check_schools():
    print("2. 魚の群れの配置（エンジンの MakeAffine vs Blender の確認用シーン）")
    P = np.array([[1, 0, 0], [0, 0, 1], [0, 1, 0]], float)
    rng = np.random.default_rng(1)
    worst = 0.0
    folder = os.path.join(MODELS, "FishSchools")
    for file in sorted(os.listdir(folder)):
        if not file.endswith(".json"):
            continue
        with open(os.path.join(folder, file), encoding="utf-8") as f:
            data = json.load(f)
        for it in data["instances"]:
            qx, qy, qz, qw = it["rotation"]
            s = it["scale"]
            engine = np.eye(4)
            engine[:3, :3] = s * xm_rotation_quaternion((qx, qy, qz, qw))
            engine[3, :3] = it["position"]
            blender = np.eye(4)   # fish.from_engine と同じ戻し方
            blender[:3, :3] = s * blender_quaternion_matrix(qw, -qx, -qz, -qy)
            blender[:3, 3] = swap_yz(it["position"])
            for v in [np.array([0.03, 0.0, 0.0]), np.array([0.0, 0.0, 0.01]), rng.normal(size=3) * 0.02]:
                wb = (blender @ np.append(v, 1.0))[:3]
                we = (np.append(P @ v, 1.0) @ engine)[:3]
                worst = max(worst, float(np.abs(we - P @ wb).max()))
        print(f"  {file:<24} {len(data['instances']):3d} 匹")
    print(f"  位置の差の最大 {worst:.2e} m")
    return worst


def main():
    worst = max(check_vertex_animation(), check_schools())
    ok = worst < TOLERANCE
    print("OK" if ok else "MISMATCH", f"（最大の差 {worst:.2e} m、許容 {TOLERANCE:g} m）")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
