"""揺れのための頂点データ（植物の風・海草の水流）

Blender 上では頂点（POINT）ドメインの FLOAT_COLOR 属性 "Anim" に (R, G, B, A) を書き、
common.bake_and_export が書き出し直前に 2 本の UV レイヤーへ移す:

    AnimA = (R, G) → glTF TEXCOORD_1
    AnimB = (B, A) → glTF TEXCOORD_2

    R: 葉先・葉縁の細かい震え（flutter）の振幅 [m]
    G: 位相 0..1。葉・葉柄ごとの乱数で、同じ葉の頂点はすべて同じ値
    B: 葉・枝のしなりの振幅 [m]。付け根 0 → 先端で最大
    A: 株全体の曲げの振幅 [m]。根元 0 → 樹冠で最大（葉は付け根の値をそのまま持つ）

振幅は「風の強さ 1（やや強い海風）」での目安。シェーダー側で強さを掛ける。
幹と葉のようにつながる部品は、つなぎ目の値が一致するように作ってあるので、揺らしても裂けない。

glTF の UV は v を 1 - v で格納する規約なので、ファイル上の値は (R, 1 - G), (B, 1 - A)。
Assimp の aiProcess_FlipUVs を使うと読み込み後は元の (R, G), (B, A) に戻る。
"""
import bpy
import numpy as np

ATTR = "Anim"
UV_NAMES = ("AnimA", "AnimB")


def hash01(*keys):
    """形状用の乱数列を消費しない、決定的な 0..1 の値（FNV-1a）"""
    h = 0x811C9DC5
    for k in keys:
        for b in repr(k).encode():
            h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    # 末尾の 1 文字違い（葉の番号など）でも値が大きく変わるよう攪拌する（murmur3 の fmix32）
    h ^= h >> 16
    h = (h * 0x85EBCA6B) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 0xC2B2AE35) & 0xFFFFFFFF
    h ^= h >> 16
    return (h >> 8) / float(1 << 24)


def main_bend(z, height, amp):
    """株全体の曲げの振幅。高さの 2 乗で増え、根元（z <= 0）で 0、height で amp"""
    t = np.clip(np.asarray(z, dtype=np.float64) / height, 0.0, 1.0)
    return amp * t * t


def world_co(obj):
    me = obj.data
    n = len(me.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    bpy.context.view_layer.update()
    M = np.array(obj.matrix_world)
    return co @ M[:3, :3].T + M[:3, 3]


def write(obj, R, G, B, A):
    """R, G, B, A はスカラーか頂点数の配列"""
    me = obj.data
    n = len(me.vertices)
    data = np.zeros((n, 4), dtype=np.float32)
    for i, v in enumerate((R, G, B, A)):
        data[:, i] = v
    attr = me.color_attributes.get(ATTR)
    if attr is None:
        attr = me.color_attributes.new(ATTR, "FLOAT_COLOR", "POINT")
    attr.data.foreach_set("color", data.ravel())
    return obj


def nearest_param(obj, pts):
    """各頂点に最も近い中心線の点の番号 / (点数 - 1)（チューブの付け根 0 → 先 1）"""
    co = world_co(obj)
    P = np.array([tuple(p) for p in pts], dtype=np.float64)
    d = ((co[:, None, :] - P[None, :, :]) ** 2).sum(-1)
    return d.argmin(1) / max(1, len(P) - 1)


def has(obj):
    return obj.type == "MESH" and obj.data.color_attributes.get(ATTR) is not None


def to_uv_layers(obj):
    """Anim 属性を UV レイヤー AnimA=(R,G), AnimB=(B,A) に移して属性は消す。
    テクスチャ用の UVMap は先頭（TEXCOORD_0）でアクティブのまま"""
    me = obj.data
    attr = me.color_attributes.get(ATTR)
    if attr is None:
        return False
    n = len(me.vertices)
    col = np.empty(n * 4, dtype=np.float32)
    attr.data.foreach_get("color", col)
    col = col.reshape(n, 4)
    vi = np.empty(len(me.loops), dtype=np.int32)
    me.loops.foreach_get("vertex_index", vi)
    for name, (a, b) in zip(UV_NAMES, ((0, 1), (2, 3))):
        layer = me.uv_layers.get(name) or me.uv_layers.new(name=name)
        uv = np.stack([col[vi, a], col[vi, b]], axis=1).astype(np.float32)
        layer.uv.foreach_set("vector", uv.ravel())
    me.color_attributes.remove(attr)
    base = me.uv_layers["UVMap"]
    me.uv_layers.active = base
    base.active_render = True
    rng = col.max(0)
    print(f"  [anim] {obj.name}: max R={rng[0]:.3f} B={rng[2]:.3f} A={rng[3]:.3f} m")
    return True
