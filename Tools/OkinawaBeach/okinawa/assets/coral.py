"""リーフの生きたサンゴ 9 バリエーション（礁池の水深 1〜3m を想定）

  Coral_Table_A / B  : テーブルサンゴ（ミドリイシ属）。短く太い幹の上に、上向きの小枝が密生した
                       ほぼ水平の板。A は約 1.6m、B は約 1.0m で 2 段に重なる
  Coral_Branch_A     : 枝サンゴ（スギノキミドリイシ型）。鹿の角のように二又に分かれる枝の茂み
  Coral_Branch_B     : 散房状のミドリイシ。根元近くで何度も分かれた短い枝が上を向き、丸い茂みになる
  Coral_Massive_A / B: ハマサンゴ（塊状）。こぶ・丸いローブのある岩のような群体
  Coral_MicroAtoll   : マイクロアトール。平らに死んだ頂面（灰色、藻の膜、小さな潮だまり）と生きた縁・側面
  Coral_Brain        : ノウサンゴ。反応拡散（Gray-Scott）で迷路状の谷と稜を作る
  Coral_Soft         : ソフトコーラル（ウミキノコ / Sarcophyton）。太い柄と波打つ傘、ポリプの毛羽立ち

作り方
  塊状のものは SDF / 変位で高密度の形を作り、デシメートした低ポリへ
  法線と属性（くぼみ・死んだ部分など）をテクスチャ空間で転写する（ハイ → ロー転写）。
  細かなポリプ・莢の凹凸はプロシージャルのバンプとしてノーマルに焼く。
  枝ものはチューブを BarkPacker で 1 枚のテクスチャに詰める。
  水は赤を吸収するので、ベースカラーは実物よりやや明るめ・彩度は控えめ。

原点: 接地点の中心（z=0）。根元は 5〜10cm 埋まる。
"""
import math
import os
import random

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from .adan import BarkPacker
from .shisa import SDFGrid, mesh_from_sdf, smin

UP = Vector((0, 0, 1))


def _res(res):
    """ベイク時と同じ実解像度（OKI_RES_SCALE を反映）"""
    return max(64, int(res * float(os.environ.get("OKI_RES_SCALE", "1"))))


# ---------------------------------------------------------------------------
# numpy のノイズ（格子・頂点の配列にまとめて掛ける）
# ---------------------------------------------------------------------------
class Noise3:
    """ベクトル化した 3D グラディエントノイズ（Perlin 型）。出力は標準偏差 ≈0.3、ほぼ -1..1"""

    def __init__(self, seed=0):
        rs = np.random.RandomState(seed)
        self.perm = np.concatenate([rs.permutation(256)] * 2).astype(np.int64)
        g = rs.normal(size=(256, 3))
        self.g = g / np.linalg.norm(g, axis=1, keepdims=True)
        self.off = rs.uniform(-100, 100, size=(16, 3))

    def __call__(self, P):
        P = np.asarray(P, dtype=np.float64)
        shape = P.shape[:-1]
        P = P.reshape(-1, 3)
        i0 = np.floor(P).astype(np.int64)
        f = P - i0
        i0 &= 255
        u = f * f * f * (f * (f * 6 - 15) + 10)
        perm, g = self.perm, self.g
        out = np.zeros(len(P))
        for dx in (0, 1):
            wx = u[:, 0] if dx else 1 - u[:, 0]
            px = perm[i0[:, 0] + dx]
            for dy in (0, 1):
                wy = u[:, 1] if dy else 1 - u[:, 1]
                pxy = perm[px + i0[:, 1] + dy]
                for dz in (0, 1):
                    wz = u[:, 2] if dz else 1 - u[:, 2]
                    gg = g[perm[pxy + i0[:, 2] + dz]]
                    out += wx * wy * wz * (gg[:, 0] * (f[:, 0] - dx) + gg[:, 1] * (f[:, 1] - dy)
                                           + gg[:, 2] * (f[:, 2] - dz))
        return (out * 1.6).reshape(shape)

    def fbm(self, P, octaves=4, lac=2.0, gain=0.5):
        P = np.asarray(P, dtype=np.float64)
        s, a, tot = 1.0, 1.0, 0.0
        out = 0.0
        for o in range(octaves):
            out = out + a * self(P * s + self.off[o])
            tot += a
            s *= lac
            a *= gain
        return out / math.sqrt(tot)


def _hash(ix, iy, iz, seed):
    h = (ix.astype(np.uint64) * np.uint64(0x8DA6B343) ^ iy.astype(np.uint64) * np.uint64(0xD8163841)
         ^ iz.astype(np.uint64) * np.uint64(0xCB1AB31F) ^ np.uint64(seed * 0x165667B1 + 0x27D4EB2F))
    h &= np.uint64(0xFFFFFFFF)
    h ^= h >> np.uint64(15)
    h = (h * np.uint64(0x2C1B3C6D)) & np.uint64(0xFFFFFFFF)
    h ^= h >> np.uint64(12)
    h = (h * np.uint64(0x297A2D39)) & np.uint64(0xFFFFFFFF)
    h ^= h >> np.uint64(15)
    return h


def _hrand(h, k):
    """ハッシュから 0..1 の乱数（k で系列を変える）"""
    x = (h * np.uint64(0x9E3779B1 + 0x7F4A7C15 * k)) & np.uint64(0xFFFFFFFF)
    x ^= x >> np.uint64(16)
    return (x & np.uint64(0xFFFFFF)).astype(np.float64) / float(1 << 24)


def worley(P, seed=0, jitter=0.9):
    """3D セルノイズ: (F1, F2, F1 セルの乱数 0..1)"""
    P = np.asarray(P, dtype=np.float64)
    shape = P.shape[:-1]
    P = P.reshape(-1, 3)
    c = np.floor(P).astype(np.int64)
    f1 = np.full(len(P), 1e9)
    f2 = np.full(len(P), 1e9)
    rid = np.zeros(len(P))
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                cx, cy, cz = c[:, 0] + dx, c[:, 1] + dy, c[:, 2] + dz
                h = _hash(cx, cy, cz, seed)
                fx = cx + 0.5 + jitter * (_hrand(h, 1) - 0.5)
                fy = cy + 0.5 + jitter * (_hrand(h, 2) - 0.5)
                fz = cz + 0.5 + jitter * (_hrand(h, 3) - 0.5)
                d = np.sqrt((P[:, 0] - fx) ** 2 + (P[:, 1] - fy) ** 2 + (P[:, 2] - fz) ** 2)
                closer = d < f1
                f2 = np.where(closer, f1, np.minimum(f2, d))
                rid = np.where(closer, _hrand(h, 4), rid)
                f1 = np.where(closer, d, f1)
    return f1.reshape(shape), f2.reshape(shape), rid.reshape(shape)


def _sstep(x, a, b):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------------------
# メッシュの補助（numpy）
# ---------------------------------------------------------------------------
def _co(me):
    a = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)


def _set_co(me, P):
    me.vertices.foreach_set("co", np.asarray(P, dtype=np.float64).ravel())
    me.update()


def _vnormals(me):
    a = np.empty(len(me.vertices) * 3, np.float32)
    me.vertex_normals.foreach_get("vector", a)
    return a.reshape(-1, 3).astype(np.float64)


def _edges(me):
    a = np.empty(len(me.edges) * 2, np.int64)
    me.edges.foreach_get("vertices", a)
    return a.reshape(-1, 2)


def _tris(me):
    me.calc_loop_triangles()
    a = np.empty(len(me.loop_triangles) * 3, np.int64)
    me.loop_triangles.foreach_get("vertices", a)
    return a.reshape(-1, 3)


def _neighbour_avg(me):
    """隣接頂点の平均を取る疎行列（ランダムウォーク型ラプラシアン L = W - I の W）"""
    from scipy import sparse
    E = _edges(me)
    n = len(me.vertices)
    A = sparse.coo_matrix((np.ones(2 * len(E)), (np.r_[E[:, 0], E[:, 1]], np.r_[E[:, 1], E[:, 0]])),
                          shape=(n, n)).tocsr()
    deg = np.asarray(A.sum(1)).ravel()
    return sparse.diags(1.0 / np.maximum(deg, 1)) @ A


def _smooth_vals(W, x, iters, lam=0.5):
    for _ in range(iters):
        x = (1 - lam) * x + lam * (W @ x)
    return x


def _curvature(P, N, W, iters):
    """ならした形との差を法線方向に測る（正 = 出っ張り、負 = くぼみ）。iters で見るスケールが変わる"""
    Ps = _smooth_vals(W, P, iters)
    return np.einsum("ij,ij->i", P - Ps, N)


def _clean_sdf_mesh(obj, h):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=h * 0.05)
    bm.to_mesh(obj.data)
    bm.free()
    geo.smooth_mod(obj, factor=0.5, iterations=2)
    return obj


def _decimate(obj, target_tris):
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    m = obj.modifiers.new("Decimate", "DECIMATE")
    m.ratio = min(1.0, target_tris / max(tris, 1))
    m.use_collapse_triangulate = True
    C.apply_modifiers(obj)
    return obj


def _triangulate(obj):
    """全面を三角形に（n 角形が残ると glTF 出力でタンジェントが書かれず、エンジン側の Assimp が
    別の方法でタンジェントを作るため、焼いたノーマルとずれる）"""
    m = obj.modifiers.new("Tri", "TRIANGULATE")
    m.quad_method = "BEAUTY"
    m.ngon_method = "BEAUTY"
    C.apply_modifiers(obj)
    return obj


def _copy(obj, name):
    o = obj.copy()
    o.data = obj.data.copy()
    o.name = name
    o.data.name = name
    return C.link_object(o)


def _remove(obj):
    me = obj.data
    bpy.data.objects.remove(obj)
    if me.users == 0:
        bpy.data.meshes.remove(me)


# ---------------------------------------------------------------------------
# UV 展開
# ---------------------------------------------------------------------------
def unwrap(obj, angle=62.0, margin=0.004, shrink=None, shrink_scale=0.25, proxy_iters=40):
    """有機形状の展開（shisa.unwrap_smooth_proxy と同じ考え方）:
    強くならした複製で Smart UV の島を決め、実形状に沿って等角展開し直す。

    shrink(face_centers, face_normals) -> bool 配列: 埋まる底面など見えない面。
    境界に継ぎ目を入れて島を分け、テクセル密度を shrink_scale 倍に落としてから詰める。
    """
    proxy = _copy(obj, obj.name + "_proxy")
    m = proxy.modifiers.new("Smooth", "SMOOTH")
    m.factor = 1.0
    m.iterations = proxy_iters
    C.apply_modifiers(proxy)
    C.smart_uv(proxy, angle=angle, margin=margin, uv_name="Bake")
    src = proxy.data.uv_layers["Bake"].data
    arr = np.empty(len(src) * 2, np.float32)
    src.foreach_get("uv", arr)
    _remove(proxy)
    me = obj.data
    layer = me.uv_layers.get("Bake") or me.uv_layers.new(name="Bake")
    layer.data.foreach_set("uv", arr)
    me.uv_layers.active = layer
    hidden = None
    if shrink is not None:
        fc = np.empty(len(me.polygons) * 3)
        me.polygons.foreach_get("center", fc)
        fn = np.empty(len(me.polygons) * 3)
        me.polygons.foreach_get("normal", fn)
        hidden = np.asarray(shrink(fc.reshape(-1, 3), fn.reshape(-1, 3)), dtype=bool)
    C._select_only([obj], obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.seams_from_islands()
    if hidden is not None and hidden.any():
        bm = bmesh.from_edit_mesh(me)
        bm.faces.ensure_lookup_table()
        for e in bm.edges:
            lf = e.link_faces
            if len(lf) == 2 and hidden[lf[0].index] != hidden[lf[1].index]:
                e.seam = True
        bmesh.update_edit_mesh(me)
    bpy.ops.uv.unwrap(method="CONFORMAL", margin=margin)
    if hidden is not None and hidden.any():
        bm = bmesh.from_edit_mesh(me)
        uvl = bm.loops.layers.uv.active
        bm.faces.ensure_lookup_table()
        pts = [(f, l) for f in bm.faces if hidden[f.index] for l in f.loops]
        cen = sum((l[uvl].uv for _, l in pts), Vector((0, 0))) / max(1, len(pts))
        for _, l in pts:
            l[uvl].uv = cen + (l[uvl].uv - cen) * shrink_scale
        bmesh.update_edit_mesh(me)
    try:
        bpy.ops.uv.pack_islands(udim_source="CLOSEST_UDIM", rotate=True, margin=margin, shape_method="CONCAVE")
    except TypeError:
        bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")
    return obj


def _buried(zcut=-0.02):
    """面の中心が地面より下にある面（砂に埋まって見えない底）"""
    return lambda c, n: c[:, 2] < zcut


# ---------------------------------------------------------------------------
# ハイ → ロー転写（テクスチャ空間）
# ---------------------------------------------------------------------------
def _raster(me, res, uv_name="Bake"):
    """UV 三角形をテクセル中心でラスタライズ。戻り値: (テクセル番号, 三角形番号, 重心座標)"""
    me.calc_loop_triangles()
    nt = len(me.loop_triangles)
    lt = np.empty(nt * 3, np.int64)
    me.loop_triangles.foreach_get("loops", lt)
    lt = lt.reshape(-1, 3)
    uv = np.empty(len(me.loops) * 2, np.float32)
    me.uv_layers[uv_name].data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2).astype(np.float64) * res - 0.5  # テクセル中心が整数座標
    T = uv[lt]
    texs, tris, bars = [], [], []
    lo = np.floor(T.min(1)).astype(np.int64)
    hi = np.ceil(T.max(1)).astype(np.int64)
    for t in range(nt):
        x0, y0 = max(lo[t, 0], 0), max(lo[t, 1], 0)
        x1, y1 = min(hi[t, 0], res - 1), min(hi[t, 1], res - 1)
        if x1 < x0 or y1 < y0:
            continue
        a, b, c = T[t]
        v0x, v0y = b[0] - a[0], b[1] - a[1]
        v1x, v1y = c[0] - a[0], c[1] - a[1]
        den = v0x * v1y - v1x * v0y
        if abs(den) < 1e-14:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        px, py = xs.ravel() - a[0], ys.ravel() - a[1]
        l1 = (px * v1y - v1x * py) / den
        l2 = (v0x * py - px * v0y) / den
        l0 = 1.0 - l1 - l2
        m = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not m.any():
            continue
        texs.append(ys.ravel()[m] * res + xs.ravel()[m])
        tris.append(np.full(m.sum(), t))
        bars.append(np.stack([l0[m], l1[m], l2[m]], -1))
    return np.concatenate(texs), np.concatenate(tris), np.concatenate(bars)


def _bary(H, A, B, Cc):
    v0, v1, v2 = B - A, Cc - A, H - A
    d00 = np.einsum("ij,ij->i", v0, v0)
    d01 = np.einsum("ij,ij->i", v0, v1)
    d11 = np.einsum("ij,ij->i", v1, v1)
    d20 = np.einsum("ij,ij->i", v2, v0)
    d21 = np.einsum("ij,ij->i", v2, v1)
    den = np.maximum(d00 * d11 - d01 * d01, 1e-20)
    v = (d11 * d20 - d01 * d21) / den
    w = (d00 * d21 - d01 * d20) / den
    return np.stack([1 - v - w, v, w], -1)


def _image(name, arr):
    """(res, res, 3|4) の 0..1 配列（下の行から）を Non-Color の画像にする"""
    h, w = arr.shape[:2]
    if arr.shape[2] == 3:
        arr = np.concatenate([arr, np.ones((h, w, 1))], -1)
    img = bpy.data.images.new(name, w, h, alpha=True, float_buffer=False)
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(np.clip(arr, 0, 1).astype(np.float32).ravel())
    img.update()
    return img


def transfer(low, high, res, cage=0.03, fields=(), name="xfer"):
    """high の形状（法線）と頂点属性を low の Bake UV のテクスチャへ写す

    low の各テクセルから法線方向の外側（cage）へ出て内向きにレイを飛ばし、最初に当たった
    high の面で滑らかな法線と属性を補間する（外れたら最近点）。
    戻り値: 法線画像（オブジェクト空間を 0..1 に詰めたもの）, 属性画像のリスト（3 つずつ RGB）
    """
    lme, hme = low.data, high.data
    tex, tri, bar = _raster(lme, res)
    lv = _co(lme)
    ltri = _tris(lme)
    cn = np.empty(len(lme.loops) * 3, np.float32)
    lme.corner_normals.foreach_get("vector", cn)
    cn = cn.reshape(-1, 3).astype(np.float64)
    lt = np.empty(len(lme.loop_triangles) * 3, np.int64)
    lme.loop_triangles.foreach_get("loops", lt)
    lt = lt.reshape(-1, 3)
    P = np.einsum("ki,kij->kj", bar, lv[ltri[tri]])
    N = np.einsum("ki,kij->kj", bar, cn[lt[tri]])
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)

    hv = _co(hme)
    htri = _tris(hme)
    hn = _vnormals(hme)
    bvh = BVHTree.FromPolygons(hv.tolist(), htri.tolist(), all_triangles=True)
    hit_i = np.empty(len(P), np.int64)
    hit_p = np.empty((len(P), 3))
    far = 2.0 * cage
    ch = 1 << 16           # Python のリストに直す量を抑える（フル解像度で数百万テクセル）
    for c0 in range(0, len(P), ch):
        O = (P[c0:c0 + ch] + N[c0:c0 + ch] * cage).tolist()
        D = (-N[c0:c0 + ch]).tolist()
        Pl = P[c0:c0 + ch].tolist()
        ii, pp = [], []
        for k in range(len(O)):
            loc, nrm, idx, _ = bvh.ray_cast(O[k], D[k], far)
            if idx is None or (nrm[0] * D[k][0] + nrm[1] * D[k][1] + nrm[2] * D[k][2]) > 0.0:
                loc, nrm, idx, _ = bvh.find_nearest(Pl[k])
            ii.append(idx)
            pp.append((loc[0], loc[1], loc[2]))
        hit_i[c0:c0 + len(O)] = ii
        hit_p[c0:c0 + len(O)] = pp
    tv = htri[hit_i]
    w = _bary(hit_p, hv[tv[:, 0]], hv[tv[:, 1]], hv[tv[:, 2]])
    w = np.clip(w, 0.0, 1.0)
    w /= np.maximum(w.sum(1, keepdims=True), 1e-9)
    Nh = np.einsum("ki,kij->kj", w, hn[tv])
    Nh /= np.maximum(np.linalg.norm(Nh, axis=1, keepdims=True), 1e-9)

    mask = np.zeros(res * res, bool)
    mask[tex] = True
    mask2 = mask.reshape(res, res)
    out = np.zeros((res * res, 3))
    out[tex] = Nh * 0.5 + 0.5
    nimg = _image(f"{name}_N", C.dilate(out.reshape(res, res, 3), mask2))
    imgs = []
    fields = list(fields)
    for i in range(0, len(fields), 3):
        arr = np.zeros((res * res, 3))
        for j, fld in enumerate(fields[i:i + 3]):
            arr[tex, j] = np.einsum("ki,ki->k", w, np.asarray(fld, dtype=np.float64)[tv])
        imgs.append(_image(f"{name}_F{i // 3}", C.dilate(arr.reshape(res, res, 3), mask2)))
    print(f"  [transfer] {name}: {len(P)} texels @ {res}")
    return nimg, imgs


def xfer_normal(nb, img):
    """転写した法線画像 → ワールド（= オブジェクト）空間の法線ソケット"""
    c = nb.image(img, nb.uv("Bake"), ext="EXTEND")["Color"]
    n = nb.node("ShaderNodeVectorMath", operation="MULTIPLY_ADD")
    nb.link(c, n.inputs[0])
    n.inputs[1].default_value = (2.0, 2.0, 2.0)
    n.inputs[2].default_value = (-1.0, -1.0, -1.0)
    return nb.vmath("NORMALIZE", n.outputs[0])


def xfer_fields(nb, img):
    """転写した属性画像 → (R, G, B) の float ソケット"""
    return nb.sep(nb.image(img, nb.uv("Bake"), ext="EXTEND")["Color"])


# ---------------------------------------------------------------------------
# SDF → 高密度メッシュ → 低ポリ
# ---------------------------------------------------------------------------
def _grid_axes(g):
    return [g.o[d] + np.arange(g.n[d]) * g.h for d in range(3)]


def _cut_ground(g, zcut):
    """z < zcut を切り落とす（埋まる平らな底）"""
    z = _grid_axes(g)[2]
    g.F = np.maximum(g.F, (zcut - z)[None, None, :])


def _dir(az, el):
    return np.array([math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)])


def _check_grid(g, name):
    """形が格子の外周に触れると Surface Nets の面が開くので、境界の内側（負）を検出して知らせる"""
    F = g.F
    faces = [F[0], F[-1], F[:, 0], F[:, -1], F[:, :, 0], F[:, :, -1]]
    if any((f < 0).any() for f in faces):
        raise RuntimeError(f"{name}: SDF が格子の端に届いている（面が閉じない）")


def sdf_high(name, g):
    obj = mesh_from_sdf(name, g)
    _clean_sdf_mesh(obj, g.h)
    return obj


def lowpoly(high, name, target_tris, angle=62.0, zbury=-0.03, margin=0.004):
    """高密度メッシュを複製してデシメートし、埋まる底の密度を落として展開する"""
    low = _copy(high, name)
    _decimate(low, target_tris)
    _triangulate(low)
    C.set_smooth(low, True)
    unwrap(low, angle=angle, shrink=_buried(zbury), margin=margin)
    return low


# ---------------------------------------------------------------------------
# ハマサンゴ（塊状）
# ---------------------------------------------------------------------------
def porites_material(name, nimg, fimg, pal, res=2048, seed=0.0, pit_scale=300.0, dead_amt=1.0, worms=1.0):
    """ハマサンゴの生きた組織: 細かな莢の穴、2〜4cm の小さなこぶ、穿孔の穴、死んで藻に覆われた斑

    fimg: R = 曲率（0.5 が平ら、1 が出っ張り）, G = 死んだ斑, B = ローブごとの色むら
    """

    def fn(nb):
        co = nb.mapping(nb.coord("Object"), loc=(seed, seed * 0.7, -seed * 0.3))
        z = nb.sep(nb.coord("Object"))[2]
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        curv, dead, tint = xfer_fields(nb, fimg)
        dead = nb.mul(dead, dead_amt)
        # 莢（ポリプの入る小孔, 約 3mm）と、その間の共骨
        pv = nb.voronoi(co, pit_scale)
        pit = nb.smooth(pv, 0.45, 0.1)
        # 表面の小さなこぶ（2〜4cm）
        kv = nb.voronoi(nb.mapping(co, loc=(3.1, 1.2, 0.4)), 34.0, rand=0.9)
        knob = nb.smooth(kv, 0.85, 0.15)
        big = nb.noise(co, 1.3, 4, 0.6)
        mid = nb.noise(co, 6.0, 5, 0.6)
        fine = nb.noise(co, 45.0, 4, 0.6)
        # 穿孔貝・ゴカイの穴（まばら, 径 1cm 前後）
        bco = nb.mapping(co, loc=(7.3, 2.9, 5.1))
        bd = nb.voronoi(bco, 7.0)
        brnd = nb.sep(nb.voronoi(bco, 7.0, out="Color"))[0]
        hole = nb.mul(nb.smooth(bd, 0.075, 0.045), nb.math("GREATER_THAN", brnd, 0.9))
        hole_rim = nb.mul(nb.mul(nb.smooth(bd, 0.12, 0.08), nb.smooth(bd, 0.045, 0.08)),
                          nb.math("GREATER_THAN", brnd, 0.9))

        # --- 生きた組織の色 ---
        t = nb.add(nb.mul(tint, 0.55), nb.mul(big, 0.45))
        col = nb.ramp(t, [(0.25, srgb(pal["lo"])), (0.5, srgb(pal["mid"])), (0.78, srgb(pal["hi"]))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(mid, 0.3, 0.7, 0.9, 1.08))
        # 色相のむら: 緑がかった所（褐虫藻の多い所）と黄褐色の所
        hue = nb.noise(nb.mapping(co, loc=(5.5, 1.1, 3.3)), 0.9, 3, 0.55)
        col = nb.mix(col, nb.mix(col, srgb(pal["green"]), 0.7), nb.mul(nb.smooth(hue, 0.52, 0.7), 0.6))
        # 出っ張り（ローブ・こぶの頂）は明るく、くぼみは暗く緑がかる
        col = nb.mix(col, srgb(pal["crest"]), nb.mul(nb.smooth(curv, 0.56, 0.85), 0.55))
        col = nb.mix(col, srgb(pal["crest"]), nb.mul(knob, 0.18))
        col = nb.mix(col, srgb(pal["groove"]), nb.mul(nb.smooth(curv, 0.46, 0.2), 0.65))
        col = nb.mix(col, srgb(pal["pit"]), nb.mul(pit, 0.3))
        # 死んだ斑: 灰色の骨格に糸状藻の膜。生きた組織との境は白っぽく（露出した骨格）
        turf = nb.mix(srgb(pal["dead"]), srgb(pal["turf"]), nb.smooth(fine, 0.35, 0.65))
        edge = nb.mul(nb.smooth(dead, 0.2, 0.42), nb.smooth(dead, 0.75, 0.5))
        dmask = nb.smooth(dead, 0.45, 0.62)
        col = nb.mix(col, srgb(pal["bare"]), nb.mul(edge, 0.7))
        col = nb.mix(col, turf, dmask)
        # 根元: 死んで石灰藻（ピンク灰）と砂をかぶる
        base = nb.smooth(z, 0.22, 0.02)
        cca = nb.smooth(nb.noise(co, 9.0, 4, 0.6), 0.5, 0.62)
        basec = nb.mix(srgb(pal["dead"]), srgb("#b08c90"), nb.mul(cca, 0.7))
        col = nb.mix(col, basec, nb.mul(base, 0.85))
        sand = nb.mul(nb.smooth(z, 0.05, -0.04), nb.smooth(nz, -0.2, 0.5))
        col = nb.mix(col, srgb("#d6ccb0"), nb.mul(sand, 0.8))
        col = nb.mix(col, srgb("#2c2418"), hole)
        # イバラカンザシ（ケヤリムシ）: 生きた組織にまばらに咲く色とりどりの小さな渦
        wco = nb.mapping(co, loc=(2.2, 8.1, 4.4))
        wd = nb.voronoi(wco, 9.0)
        wr = nb.sep(nb.voronoi(wco, 9.0, out="Color"))
        won = nb.mul(nb.math("GREATER_THAN", wr[0], 1.0 - 0.28 * worms), nb.sub(1.0, dmask))
        worm = nb.mul(nb.smooth(wd, 0.12, 0.06), won)
        wring = nb.mul(nb.smooth(wd, 0.07, 0.03), won)
        wcol = nb.ramp(wr[1], [(0.0, srgb("#e0892f")), (0.25, srgb("#3f6fd1")), (0.45, srgb("#e8e2d0")),
                               (0.65, srgb("#e3c23a")), (0.85, srgb("#c9533b"))], interp="CONSTANT")
        col = nb.mix(col, wcol, worm)
        col = nb.mix(col, nb.hsv(wcol, 0.5, 1.0, 0.55), nb.mul(wring, 0.6))
        # ブダイのかじり跡: 出っ張りに白く平行な 2 本の擦り傷
        bco2 = nb.mapping(co, loc=(1.3, 6.6, 2.2))
        bpos = nb.voronoi(bco2, 6.0, out="Position")
        brc = nb.sep(nb.voronoi(bco2, 6.0, out="Color"))
        dv = nb.vmath("SUBTRACT", bco2, bpos)
        ang = nb.mul(brc[0], math.tau)
        ca, sa = nb.math("COSINE", ang), nb.math("SINE", ang)
        dsep = nb.sep(dv)
        along = nb.add(nb.mul(dsep[0], ca), nb.mul(dsep[1], sa))
        across = nb.add(nb.add(nb.mul(dsep[0], nb.mul(sa, -1.0)), nb.mul(dsep[1], ca)), nb.mul(dsep[2], 0.3))
        stroke = nb.math("MAXIMUM", nb.smooth(nb.math("ABSOLUTE", nb.sub(across, 0.0045)), 0.0028, 0.0012),
                         nb.smooth(nb.math("ABSOLUTE", nb.add(across, 0.0045)), 0.0028, 0.0012))
        bite = nb.mul(nb.mul(stroke, nb.smooth(nb.math("ABSOLUTE", along), 0.016, 0.011)),
                      nb.mul(nb.math("GREATER_THAN", brc[1], 0.55), nb.smooth(curv, 0.5, 0.62)))
        bite = nb.mul(bite, nb.sub(1.0, dmask))
        col = nb.mix(col, srgb("#e9e4d6"), nb.mul(bite, 0.85))

        live = nb.mul(nb.sub(1.0, dmask), nb.sub(1.0, base))
        rough = nb.add(nb.mixf(0.86, 0.66, live), nb.mul(pit, 0.08))
        height = nb.add(nb.mul(knob, 0.55), nb.mul(fine, 0.25))
        height = nb.add(height, nb.mul(nb.mul(pit, -0.35), live))
        height = nb.add(height, nb.mul(nb.mul(nb.noise(co, 120.0, 3, 0.6), 0.25), nb.sub(1.0, live)))
        height = nb.add(height, nb.add(nb.mul(hole, -1.6), nb.mul(hole_rim, 0.3)))
        height = nb.add(height, nb.add(nb.mul(worm, 0.6), nb.mul(bite, -0.5)))
        cavity = nb.mul(nb.maprange(curv, 0.15, 0.5, 0.55, 1.0), nb.maprange(hole, 0, 1, 1.0, 0.25))
        cavity = nb.mul(cavity, nb.maprange(pit, 0, 1, 1.0, 0.86))
        return dict(color=col, rough=rough, height=height, height_scale=0.004, normal=xfer_normal(nb, nimg),
                    cavity=cavity)

    return pbr_material(name, fn, res=res, uv="keep", ao_distance=0.5)


def porites(name, seed, radii, zc, pal, bodies=(), lobes=7, hummocks=24, knobs=40, h=0.012,
            target_tris=12000, res=2048, lobe_r=(0.28, 0.45), lobe_out=0.5, lobe_k=0.12, lobe_up=1.0,
            dead=0.6, lump=0.04, hum_r=(0.12, 0.26), knob_r=(0.045, 0.09), lobe_list=(), worms=1.0):
    """塊状ハマサンゴ: 楕円体（+ 副塊）にローブ → 中くらいのこぶ → 小さなこぶを表面に積み、
    なめらかな和でつなぐ（間にくびれた溝ができる）。最後に大きなうねりを変位で足す

    bodies: 追加の楕円体 [(中心, 半径), ...]。lobe_up: ローブの縦長さ（柱状のローブ）
    lobe_list: 位置を決めたローブ [(中心, 半径), ...]（なめらかな和 lobe_k）
    """
    rnd = random.Random(seed)
    rx, ry, rz = radii
    ext = max(rx, ry) + 0.5
    ztop = max([zc + rz] + [c[2] + rr[2] for c, rr in lobe_list] + [c[2] + rr[2] for c, rr in bodies])
    # 上に積み重なるこぶが格子からはみ出すと穴があくので、上は広めに取る
    g = SDFGrid((-ext, -ext, -0.14), (ext, ext, ztop + 0.5), h)
    g.ellipsoid((0, 0, zc), radii)
    for c, rr in bodies:
        g.ellipsoid(c, rr, k=0.12)
    c0 = np.array([0.0, 0.0, zc])
    for c, rr in lobe_list:
        g.ellipsoid(c, rr, k=lobe_k)
    for i in range(lobes):
        az = (i + rnd.uniform(-0.3, 0.3)) / lobes * math.tau
        el = math.radians(rnd.uniform(15, 72))
        hit = g.surf(c0, _dir(az, el), max_d=3.0)
        if hit is None:
            continue
        p, n = hit
        r = rnd.uniform(*lobe_r)
        c = p - n * r * (1.0 - lobe_out)
        g.ellipsoid(c, (r, r, r * lobe_up), k=lobe_k)
    # 中くらいのこぶ → 小さなこぶ（いまの表面を探って載せる）
    for count, (r0, r1), sink, kk in ((hummocks, hum_r, 0.55, 0.6), (knobs, knob_r, 0.6, 0.8)):
        for i in range(count):
            # 面積あたり一様（仰角を一様に取ると頂に集まって煙突のように積み上がる）
            el = math.asin(rnd.uniform(math.sin(math.radians(6)), math.sin(math.radians(78))))
            d = _dir(rnd.uniform(0, math.tau), el)
            hit = g.surf(c0, d, max_d=3.0)
            if hit is None:
                continue
            p, n = hit
            r = rnd.uniform(r0, r1)
            g.sphere(p - n * r * sink, r, k=r * kk)
    _cut_ground(g, -0.1)
    _check_grid(g, name)
    high = sdf_high(name + "_hi", g)
    me = high.data
    P = _co(me)
    N = _vnormals(me)
    nz = Noise3(seed)
    fade = _sstep(P[:, 2], -0.06, 0.3)
    disp = (nz.fbm(P * 1.2, 3) * lump + nz.fbm(P * 4.0 + 7.7, 3) * 0.012) * fade
    P = P + N * disp[:, None]
    _set_co(me, P)
    N = _vnormals(me)
    W = _neighbour_avg(me)
    curv = np.clip(0.5 + _curvature(P, N, W, 10) / 0.03, 0, 1)
    warp = nz.fbm(P * 6.0 + 1.1, 2)[:, None] * 0.08
    dn = nz.fbm(P * 2.2 + 3.3 + warp, 4)
    dead_m = _sstep(dn + 0.2 * N[:, 2] - (1.0 - dead) * 0.6, 0.5, 0.62)
    tint = np.clip(0.5 + nz.fbm(P * 0.8 + 11.0, 2) * 0.9, 0, 1)
    low = lowpoly(high, name, target_tris)
    nimg, (fimg,) = transfer(low, high, _res(res), cage=0.05, fields=(curv, dead_m, tint), name=name)
    _remove(high)
    C.assign(low, porites_material(name, nimg, fimg, pal, res=res, seed=seed * 1.37, worms=worms))
    return [low]


PAL_PORITES_A = dict(lo="#a18752", mid="#b99f62", hi="#ceb87e", crest="#ddd0a8", groove="#716b44",
                     pit="#62553a", dead="#918e80", turf="#727151", bare="#d0c8b2", green="#96995f")
PAL_PORITES_B = dict(lo="#a39869", mid="#bbb07b", hi="#d0c794", crest="#e0dab8", groove="#6a6e47",
                     pit="#5a563d", dead="#8e8b7e", turf="#6c6d50", bare="#d3cdba", green="#93a16c")


def massive_a():
    """約 2m のハマサンゴの岩: 2 つの塊が合わさったヘルメット形。なだらかなこぶが重なり、裾はややえぐれる"""
    return porites("Coral_Massive_A", 21, (0.92, 0.82, 0.74), 0.34, PAL_PORITES_A,
                   bodies=[((0.42, 0.22, 0.2), (0.6, 0.55, 0.62)), ((-0.35, -0.3, 0.12), (0.55, 0.5, 0.5))],
                   lobes=7, hummocks=26, knobs=34, h=0.012, target_tris=12500, res=2048,
                   lobe_r=(0.32, 0.48), lobe_out=0.3, lobe_k=0.24, hum_r=(0.14, 0.28), knob_r=(0.06, 0.1),
                   lump=0.045)


def massive_b():
    """約 1.2m のローブ状の群体: 丸い頭の縦長のローブが寄り集まり、間に深い谷ができる"""
    rnd = random.Random(34)
    lobes = [((0.02, 0.0, 0.34), (0.24, 0.23, 0.32))]
    for i in range(7):
        a = (i + rnd.uniform(-0.2, 0.2)) / 7 * math.tau
        rr = rnd.uniform(0.3, 0.38)
        r = rnd.uniform(0.17, 0.23)
        top = rnd.uniform(0.35, 0.52)
        hz = top * 0.62
        lobes.append(((math.cos(a) * rr, math.sin(a) * rr * 0.9, top - hz), (r, r * 0.95, hz)))
    return porites("Coral_Massive_B", 34, (0.52, 0.47, 0.2), 0.05, PAL_PORITES_B, lobe_list=lobes,
                   lobes=0, hummocks=34, knobs=40, h=0.009, target_tris=9500, res=2048, lobe_k=0.05,
                   hum_r=(0.06, 0.12), knob_r=(0.03, 0.055), dead=0.72, lump=0.02, worms=0.6)


# ---------------------------------------------------------------------------
# ノウサンゴ（反応拡散の迷路）
# ---------------------------------------------------------------------------
def gray_scott(W, n, steps, F=0.029, k=0.057, Du=0.5, Dv=0.25, seeds=None, rs=None):
    """グラフ上の Gray-Scott 反応拡散。v の縞が迷路状の稜になる"""
    u = np.ones(n)
    v = np.zeros(n)
    if seeds is not None:
        u[seeds] = 0.5
        v[seeds] = 0.5
    v += rs.rand(n) * 0.01
    for _ in range(steps):
        Lu = W @ u - u
        Lv = W @ v - v
        uvv = u * v * v
        u += Du * Lu - uvv + F * (1 - u)
        v += Dv * Lv + uvv - (F + k) * v
    return v


def brain_material(name, nimg, fimg, res=2048):
    """稜（壁）は褐色で頂が明るく、谷は緑がかった灰褐色。谷底の溝は暗い

    fimg: R = 稜 (1) / 谷 (0), G = 細かな曲率（0.5 が平ら）, B = 色むら
    """

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        ridge, curv, tint = xfer_fields(nb, fimg)
        big = nb.noise(co, 2.5, 4, 0.6)
        fine = nb.noise(co, 90.0, 4, 0.6)
        t = nb.add(nb.mul(tint, 0.6), nb.mul(big, 0.4))
        wall = nb.ramp(t, [(0.25, srgb("#8d7a51")), (0.5, srgb("#9f895b")), (0.8, srgb("#b09969"))])
        valley = nb.ramp(t, [(0.25, srgb("#71844f")), (0.5, srgb("#7f915c")), (0.8, srgb("#8d9c64"))])
        w = nb.smooth(ridge, 0.3, 0.72)
        col = nb.mix(valley, wall, w)
        col = nb.mix(col, srgb("#c3b385"), nb.mul(nb.smooth(ridge, 0.82, 0.98), 0.35))
        col = nb.mix(col, srgb("#4f5a38"), nb.mul(nb.smooth(ridge, 0.14, 0.02), 0.6))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(fine, 0.3, 0.7, 0.92, 1.06))
        # 隔壁（壁の側面の細かい縦筋）はノイズで粗さとして
        sept = nb.noise(nb.mapping(co, scale=(1, 1, 1)), 420.0, 2, 0.5)
        side = nb.mul(nb.smooth(ridge, 0.15, 0.4), nb.smooth(ridge, 0.9, 0.65))
        # 裾は死んで灰色、石灰藻のピンク
        base = nb.smooth(z, 0.05, -0.02)
        cca = nb.smooth(nb.noise(co, 14.0, 3, 0.6), 0.5, 0.62)
        col = nb.mix(col, nb.mix(srgb("#8c8878"), srgb("#b08a90"), nb.mul(cca, 0.7)), nb.mul(base, 0.85))
        height = nb.add(nb.mul(fine, 0.35), nb.mul(nb.mul(sept, side), 0.6))
        rough = nb.maprange(ridge, 0.0, 1.0, 0.74, 0.62)
        cavity = nb.mul(nb.maprange(curv, 0.2, 0.5, 0.5, 1.0), nb.maprange(ridge, 0.0, 0.35, 0.8, 1.0))
        return dict(color=col, rough=rough, height=height, height_scale=0.0012, normal=xfer_normal(nb, nimg),
                    cavity=cavity)

    return pbr_material(name, fn, res=res, uv="keep", ao_distance=0.25)


def brain(name="Coral_Brain", seed=5, res=2048, target_tris=12500):
    """約 0.8m の半球。icosphere（辺 ≈3.5mm）上で反応拡散 → 稜と谷の変位"""
    nz = Noise3(seed)
    rs = np.random.RandomState(seed)
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=8, radius=1.0)
    me = bpy.data.meshes.new(name + "_hi")
    bm.to_mesh(me)
    bm.free()
    high = C.link_object(bpy.data.objects.new(name + "_hi", me))
    D = _co(me)
    D /= np.linalg.norm(D, axis=1, keepdims=True)
    radii = np.array([0.405, 0.385, 0.365])
    r = 1.0 / np.linalg.norm(D / radii, axis=1)
    r *= 1.0 + 0.045 * nz.fbm(D * 1.5, 3)
    P = D * r[:, None]
    P[:, 0] += 0.03 * np.clip(P[:, 2], 0, None)  # わずかに傾く
    _set_co(me, P)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, 0, -0.07),
                           plane_no=(0, 0, 1), clear_inner=True)
    bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary], sides=0)
    bm.to_mesh(me)
    bm.free()
    P = _co(me)
    n = len(P)
    W = _neighbour_avg(me)
    from scipy.spatial import cKDTree
    sc = rs.normal(size=(500, 3))
    sc = sc / np.linalg.norm(sc, axis=1, keepdims=True) * 0.4
    dd, _ = cKDTree(sc).query(P)
    v = gray_scott(W, n, 8000, seeds=dd < 0.012, rs=rs)
    lo, hi = np.percentile(v, 3), np.percentile(v, 97)
    vn = _smooth_vals(W, np.clip((v - lo) / (hi - lo), 0, 1), 2)
    ridge = _sstep(vn, 0.36, 0.72)
    groove = _sstep(vn, 0.2, 0.03)
    N = _vnormals(me)
    fade = _sstep(P[:, 2], -0.03, 0.06)
    disp = ((ridge - 0.5) * 0.016 - groove * 0.003) * fade + nz.fbm(P * 3.0 + 5.0, 3) * 0.008
    P = P + N * disp[:, None]
    _set_co(me, P)
    N = _vnormals(me)
    curv = np.clip(0.5 + _curvature(P, N, W, 3) / 0.006, 0, 1)
    tint = np.clip(0.5 + nz.fbm(P * 2.0 + 3.0, 2) * 0.9, 0, 1)
    ridge_f = ridge * fade + 0.5 * (1 - fade)
    high.name = name + "_hi"
    low = lowpoly(high, name, target_tris)
    nimg, (fimg,) = transfer(low, high, _res(res), cage=0.025, fields=(ridge_f, curv, tint), name=name)
    _remove(high)
    C.assign(low, brain_material(name, nimg, fimg, res=res))
    return [low]


# ---------------------------------------------------------------------------
# マイクロアトール
# ---------------------------------------------------------------------------
def microatoll_material(name, nimg, fimg, res=2048):
    """頂面: 死んだ灰色の骨格に藻の膜・黒ずんだ斑・潮だまりの砂。縁と側面: 生きた褐色〜黄土色

    fimg: R = 死んだ頂面 (1), G = 曲率（0.5 が平ら）, B = 潮だまり (1)
    """

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        dead, curv, pool = xfer_fields(nb, fimg)
        big = nb.noise(co, 1.0, 4, 0.6)
        mid = nb.noise(co, 5.0, 5, 0.6)
        fine = nb.noise(co, 40.0, 4, 0.6)
        # 生きた縁・側面（ハマサンゴ）
        pv = nb.voronoi(co, 300.0)
        pit = nb.smooth(pv, 0.45, 0.1)
        kv = nb.voronoi(nb.mapping(co, loc=(1.7, 0.3, 2.2)), 30.0, rand=0.9)
        knob = nb.smooth(kv, 0.85, 0.15)
        live = nb.ramp(nb.add(nb.mul(big, 0.6), nb.mul(mid, 0.4)),
                       [(0.25, srgb("#8b7446")), (0.5, srgb("#a38b56")), (0.78, srgb("#b8a068"))])
        hue = nb.noise(nb.mapping(co, loc=(3.3, 7.1, 1.9)), 0.8, 3, 0.55)
        live = nb.mix(live, srgb("#858a55"), nb.mul(nb.smooth(hue, 0.5, 0.7), 0.45))
        live = nb.mix(live, srgb("#c4b387"), nb.mul(nb.smooth(curv, 0.56, 0.85), 0.5))
        live = nb.mix(live, srgb("#57512f"), nb.mul(nb.smooth(curv, 0.46, 0.2), 0.7))
        live = nb.mix(live, srgb("#5a4f33"), nb.mul(pit, 0.15))
        live = nb.mix(live, srgb("#c4b387"), nb.mul(knob, 0.15))
        # 縁の成長帯はやや明るい
        rimz = nb.smooth(z, 0.62, 0.86)
        live = nb.mix(live, srgb("#bfae80"), nb.mul(rimz, 0.15))
        # 死んだ頂面
        turf = nb.ramp(mid, [(0.3, srgb("#727254")), (0.5, srgb("#858464")), (0.7, srgb("#989680"))])
        blot = nb.smooth(nb.noise(co, 3.0, 4, 0.65, distortion=0.5), 0.55, 0.7)
        turf = nb.mix(turf, srgb("#4d4c3a"), nb.mul(blot, 0.65))
        bare = nb.smooth(nb.noise(nb.mapping(co, loc=(4, 4, 4)), 2.5, 4, 0.6), 0.6, 0.72)
        turf = nb.mix(turf, srgb("#9a9582"), nb.mul(bare, 0.45))
        cca = nb.smooth(nb.noise(nb.mapping(co, loc=(9, 1, 3)), 6.0, 4, 0.6), 0.58, 0.68)
        turf = nb.mix(turf, srgb("#a88790"), nb.mul(cca, 0.5))
        turf = nb.hsv(turf, 0.5, 1.0, nb.maprange(fine, 0.3, 0.7, 0.88, 1.1))
        sand = nb.mix(srgb("#a8a185"), srgb("#8c8a6c"), nb.mul(fine, 0.8))
        turf = nb.mix(turf, sand, nb.smooth(pool, 0.3, 0.7))
        dm = nb.smooth(dead, 0.4, 0.6)
        edge = nb.mul(nb.smooth(dead, 0.15, 0.4), nb.smooth(dead, 0.75, 0.45))
        col = nb.mix(live, srgb("#c9c0a6"), nb.mul(edge, 0.45))
        col = nb.mix(col, turf, dm)
        # 根元は死んで砂をかぶる
        col = nb.mix(col, nb.mix(srgb("#8c8878"), srgb("#cdc3a8"), nb.smooth(z, 0.08, -0.04)),
                     nb.mul(nb.smooth(z, 0.18, 0.0), 0.85))
        livem = nb.mul(nb.sub(1.0, dm), nb.smooth(z, 0.1, 0.2))
        rough = nb.add(nb.mixf(0.88, 0.68, livem), nb.mul(fine, 0.05))
        rough = nb.mixf(rough, 0.6, nb.mul(nb.smooth(pool, 0.4, 0.8), 0.6))
        height = nb.add(nb.mul(knob, nb.mul(livem, 0.5)), nb.mul(fine, 0.3))
        height = nb.add(height, nb.mul(nb.mul(pit, -0.3), livem))
        height = nb.add(height, nb.mul(nb.mul(nb.noise(co, 150.0, 3, 0.6), 0.35), dm))
        cavity = nb.maprange(curv, 0.15, 0.5, 0.55, 1.0)
        return dict(color=col, rough=rough, height=height, height_scale=0.004, normal=xfer_normal(nb, nimg),
                    cavity=cavity)

    return pbr_material(name, fn, res=res, uv="keep", ao_distance=0.6)


def microatoll(name="Coral_MicroAtoll", seed=9, res=2048, target_tris=13500):
    """直径約 3m・高さ約 0.9m。側面は生きたハマサンゴのこぶが重なって少し張り出し、
    縁は途切れながら少し盛り上がり、頂面は平らに死んで藻の膜・小さな潮だまり・古い縁の同心円の段が残る"""
    nz = Noise3(seed)
    rnd = random.Random(seed)
    R0, zt, zb, rc = 1.36, 0.8, -0.1, 0.1
    h = 0.014
    ext = R0 + 0.5
    g = SDFGrid((-ext, -ext, zb - 0.04), (ext, ext, zt + 0.25), h)
    X, Y, Z = _grid_axes(g)
    XX, YY = np.meshgrid(X, Y, indexing="ij")
    TH = np.arctan2(YY, XX)
    RR = np.hypot(XX, YY)
    nt = 720
    ths = np.linspace(-math.pi, math.pi, nt, endpoint=False)
    ring = np.stack([np.cos(ths), np.sin(ths), np.zeros(nt)], -1)
    Rth = R0 * (1 + 0.09 * nz.fbm(ring * 0.9, 3) + 0.03 * nz.fbm(ring * 3.5 + 3.0, 2))
    rim_r = 0.07 * np.clip(0.6 + 1.1 * nz.fbm(ring * 2.2 + 6.0, 2), 0.0, 1.3)
    ti = np.round((TH + math.pi) / math.tau * nt).astype(int) % nt
    Rxy = Rth[ti]
    rim_xy = rim_r[ti]
    for k, z in enumerate(Z):
        q = np.stack([XX * 2.0, YY * 2.0, np.full_like(XX, z * 0.8)], -1)
        lump = 0.05 * nz.fbm(q + 7.0, 3) * _sstep(z, -0.05, 0.3)
        s_ = 0.9 + 0.1 * _sstep(z, 0.0, zt)          # 上ほど張り出す
        rho = RR - Rxy * (s_ + lump)
        qx = rho + rc
        qz = z - zt + rc
        d = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qz, 0) ** 2) + np.minimum(np.maximum(qx, qz), 0) - rc
        # 生きた縁のわずかな盛り上がり（ところどころ途切れる）
        rim = np.sqrt((rho + 0.08) ** 2 + (z - (zt - 0.03)) ** 2) - rim_xy
        d = smin(d, rim, 0.04)
        g.F[:, :, k] = np.maximum(d, zb - z)
    # 側面と縁のこぶ（生きたハマサンゴ）: 表面を探って球をなめらかに足す
    for i in range(78):
        az = rnd.uniform(0, math.tau)
        zz = rnd.uniform(0.04, zt - 0.1)
        d = np.array([math.cos(az), math.sin(az), rnd.uniform(-0.1, 0.25)])
        hit = g.surf((0.0, 0.0, zz), d, max_d=2.6)
        if hit is None:
            continue
        p, n = hit
        r = rnd.uniform(0.07, 0.17)
        g.sphere(p - n * r * 0.58, r, k=r * 0.6)
    for i in range(36):
        # 縁の肩: 半径の半ばから外へ（中心から撃つと平らな頂面から抜けてしまう）
        az = rnd.uniform(0, math.tau)
        o = np.array([math.cos(az) * R0 * 0.5, math.sin(az) * R0 * 0.5, zt - 0.1])
        hit = g.surf(o, np.array([math.cos(az), math.sin(az), 0.12]), max_d=2.0)
        if hit is None:
            continue
        p, n = hit
        r = rnd.uniform(0.05, 0.09)
        g.sphere(p - n * r * 0.6, r, k=r * 0.7)
    _cut_ground(g, zb)
    _check_grid(g, name)
    high = sdf_high(name + "_hi", g)
    me = high.data
    P = _co(me)
    N = _vnormals(me)
    th = np.arctan2(P[:, 1], P[:, 0])
    rr = np.hypot(P[:, 0], P[:, 1])
    Rv = Rth[np.round((th + math.pi) / math.tau * nt).astype(int) % nt]
    rel = rr / Rv
    rim_in = 0.84 + 0.04 * nz.fbm(np.stack([np.cos(th), np.sin(th), np.zeros_like(th)], -1) * 3.0 + 9.0, 2)
    top = _sstep(P[:, 2], zt - 0.05, zt - 0.01) * _sstep(N[:, 2], 0.6, 0.85) * _sstep(rel, rim_in + 0.02,
                                                                                         rim_in - 0.03)
    # 頂面: 同心円状の古い縁（低い段）、潮だまり、細かな凹凸
    terr = sum(np.exp(-(((rel - c) * R0) / 0.05) ** 2) for c in (0.3, 0.52, 0.7)) * 0.014
    terr *= 0.6 + 0.4 * _sstep(nz.fbm(P * 2.0, 2), -0.3, 0.3)
    pn = nz.fbm(P * 1.6 + 13.0, 3)
    pool = _sstep(pn, 0.32, 0.5) * _sstep(rel, rim_in - 0.05, rim_in - 0.15)
    dz = (terr - pool * 0.06 + nz.fbm(P * 9.0, 3) * 0.006) * top
    P[:, 2] += dz
    # 側面・縁: 細かなこぶの凹凸
    side = 1.0 - top
    P += N * ((nz.fbm(P * 6.0 + 2.0, 3) * 0.014) * side * _sstep(P[:, 2], -0.02, 0.2))[:, None]
    _set_co(me, P)
    N = _vnormals(me)
    W = _neighbour_avg(me)
    curv = np.clip(0.5 + _curvature(P, N, W, 8) / 0.03, 0, 1)
    dead = _smooth_vals(W, top, 3)
    low = lowpoly(high, name, target_tris)
    nimg, (fimg,) = transfer(low, high, _res(res), cage=0.05, fields=(dead, curv, pool * top), name=name)
    _remove(high)
    C.assign(low, microatoll_material(name, nimg, fimg, res=res))
    return [low]


# ---------------------------------------------------------------------------
# ソフトコーラル（ウミキノコ）
# ---------------------------------------------------------------------------
def soft_material(name, nimg, fimg, res=1024):
    """傘の上面はポリプが開いた細かな毛羽立ち（クリーム〜オリーブ）、裏と柄は滑らかで白っぽい

    fimg: R = 傘の上面 (1), G = 曲率（0.5 が平ら）, B = 縁 (1)
    """

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        cap, curv, rim = xfer_fields(nb, fimg)
        big = nb.noise(co, 3.0, 4, 0.6)
        fine = nb.noise(co, 60.0, 4, 0.6)
        # ポリプ: 2〜3mm 間隔の粒。先が明るく、根元（隙間）は暗い
        pd = nb.voronoi(co, 380.0, rand=0.9)
        polyp = nb.smooth(pd, 0.62, 0.08)
        fuzz = nb.noise(co, 700.0, 2, 0.5)
        top = nb.ramp(big, [(0.3, srgb("#a3955f")), (0.55, srgb("#b3a56f")), (0.8, srgb("#c1b37f"))])
        top = nb.mix(top, srgb("#7d7550"), nb.mul(nb.sub(1.0, polyp), 0.14))
        top = nb.mix(top, srgb("#d9cfa6"), nb.mul(nb.mul(polyp, fuzz), 0.2))
        top = nb.mix(top, srgb("#d0c59b"), nb.mul(rim, 0.25))
        stalk = nb.ramp(big, [(0.3, srgb("#c2b78f")), (0.7, srgb("#d0c6a2"))])
        stalk = nb.mix(stalk, srgb("#aea57f"), nb.mul(nb.smooth(curv, 0.45, 0.2), 0.6))
        c = nb.smooth(cap, 0.3, 0.7)
        col = nb.mix(stalk, top, c)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(fine, 0.3, 0.7, 0.94, 1.05))
        col = nb.mix(col, srgb("#cdc3a8"), nb.mul(nb.smooth(z, 0.05, -0.03), 0.7))
        height = nb.add(nb.mul(nb.mul(polyp, 0.7), c), nb.mul(nb.mul(fuzz, 0.3), c))
        height = nb.add(height, nb.mul(fine, 0.2))
        rough = nb.mixf(0.62, 0.86, c)
        cavity = nb.mul(nb.maprange(curv, 0.2, 0.5, 0.6, 1.0), nb.mixf(1.0, nb.maprange(polyp, 0, 1, 0.8, 1.0), c))
        return dict(color=col, rough=rough, height=height, height_scale=0.0022, normal=xfer_normal(nb, nimg),
                    cavity=cavity)

    return pbr_material(name, fn, res=res, uv="keep", ao_distance=0.25)


def soft(name="Coral_Soft", seed=13, res=2048, target_tris=7500):
    """ウミキノコ: 太い柄と、縁が不規則に波打って垂れる傘（直径約 0.7m）"""
    nz = Noise3(seed)
    R0, zc = 0.34, 0.27
    h = 0.0045

    def cap_shape(th, rr):
        """(θ, r) → 傘の外周半径, 中面の高さ, 正規化半径 s"""
        ring = np.stack([np.cos(th), np.sin(th), np.zeros_like(th)], -1)
        Rth = R0 * (1 + 0.08 * nz.fbm(ring * 1.3, 3))
        s_ = np.clip(rr / Rth, 0, 1.2)
        ph = th * 5 + 1.7 * nz.fbm(ring * 1.1 + 5.0, 2)
        ph2 = th * 9 + 2.2 * nz.fbm(ring * 1.7 + 9.0, 2)
        fold = _sstep(s_, 0.35, 1.0) * (0.048 * np.sin(ph) + 0.02 * np.sin(ph2))
        pleat = 0.008 * _sstep(s_, 0.75, 1.0) * np.sin(th * 23 + 2.5 * nz.fbm(ring * 2.0 + 1.0, 2))
        zm = zc + 0.016 * s_ * s_ - 0.04 * _sstep(s_, 0.55, 1.05) + fold + pleat
        return Rth, zm, s_

    g = SDFGrid((-R0 - 0.1, -R0 - 0.1, -0.1), (R0 + 0.1, R0 + 0.1, zc + 0.16), h)
    X, Y, Z = _grid_axes(g)
    XX, YY = np.meshgrid(X, Y, indexing="ij")
    TH = np.arctan2(YY, XX)
    RR = np.hypot(XX, YY)
    Rth, zmid, s2 = cap_shape(TH, RR)
    gx, gy = np.gradient(zmid, h, h)
    slope = np.sqrt(1 + gx * gx + gy * gy)
    thick = 0.07 * (1 - 0.72 * np.clip(s2, 0, 1) ** 2)
    drad = RR - Rth
    rr = 0.008
    for k, z in enumerate(Z):
        dsh = (np.abs(z - zmid) - thick * 0.5) / slope
        a_, b_ = dsh + rr, drad + rr
        dcap = np.sqrt(np.maximum(a_, 0) ** 2 + np.maximum(b_, 0) ** 2) + np.minimum(np.maximum(a_, b_), 0) - rr
        # 柄: 縦のしわのある太い円柱、根元は広がる
        rs_ = 0.1 + 0.035 * _sstep(z, 0.08, -0.08) + 0.03 * _sstep(z, zc - 0.13, zc - 0.02)
        rs_ = rs_ * (1 + 0.045 * np.sin(TH * 11 + 3 * z) + 0.02 * np.sin(TH * 4 + 1.0))
        dst = np.maximum(RR - rs_, z - zc)
        g.F[:, :, k] = np.maximum(smin(dcap, dst, 0.05), -0.08 - z)
    _check_grid(g, name)
    high = sdf_high(name + "_hi", g)
    me = high.data
    P = _co(me)
    N = _vnormals(me)
    th = np.arctan2(P[:, 1], P[:, 0])
    _, zm, sv = cap_shape(th, np.hypot(P[:, 0], P[:, 1]))
    P += N * (nz.fbm(P * 10.0, 3) * 0.004)[:, None]
    _set_co(me, P)
    N = _vnormals(me)
    W = _neighbour_avg(me)
    cap = ((P[:, 2] > zm - 0.006) & (N[:, 2] > -0.35)).astype(float)
    cap = _smooth_vals(W, cap, 3)
    rim = _sstep(sv, 0.85, 1.0)
    curv = np.clip(0.5 + _curvature(P, N, W, 6) / 0.01, 0, 1)
    low = lowpoly(high, name, target_tris)
    nimg, (fimg,) = transfer(low, high, _res(res), cage=0.02, fields=(cap, curv, rim), name=name)
    _remove(high)
    C.assign(low, soft_material(name, nimg, fimg, res=res))
    return [low]


# ---------------------------------------------------------------------------
# テーブルサンゴ
# ---------------------------------------------------------------------------
def _angdiff(a, b):
    """a - b を -π..π に"""
    return (a - b + math.pi) % math.tau - math.pi


def _radial_branches(rnd, r0, r1, n0=12, split=0.085, dr=0.01):
    """中心から放射状に伸び、隣との間隔が開くと二又に分かれる横枝の中心線（極座標 (r, θ) の列）"""
    th = np.sort(np.array([(k + rnd.uniform(-0.3, 0.3)) / n0 * math.tau for k in range(n0)]))
    lines = [[(r0, t)] for t in th]
    active = list(range(n0))
    r = r0
    while r < r1:
        r += dr
        cur = np.array([lines[i][-1][1] for i in active])
        prv, nxt = np.roll(cur, 1), np.roll(cur, -1)
        gp = _angdiff(cur, prv) % math.tau
        gn = _angdiff(nxt, cur) % math.tau
        # 両隣の中間へゆっくり寄せて間隔をならす + ゆらぎ
        new = cur + 0.18 * (gn - gp) * 0.5 + np.array([rnd.gauss(0, 0.012) for _ in cur]) * (0.1 / r)
        nact = []
        for k, i in enumerate(active):
            lines[i].append((r, new[k]))
            nact.append(i)
            if gn[k] * r > split and len(lines[i]) > 3:
                # 分岐: 1 つ前の点から、隙間の中ほどへ向かう新しい枝
                lines.append([lines[i][-2], (r, new[k] + gn[k] * 0.3)])
                nact.append(len(lines) - 1)
        active = nact
    return lines


def _tip_mesh(acc_v, acc_f, acc_uv, acc_t, base, d, L, r0, r1, sides=5):
    """小枝の先（短い円錐 + 丸い先端）。底は板に埋まるので開いたまま"""
    d = Vector(d).normalized()
    a = d.cross(UP)
    if a.length < 1e-3:
        a = d.cross(Vector((1, 0, 0)))
    a.normalize()
    b = d.cross(a)
    o = len(acc_v)
    rings = [(0.0, r0), (0.6, (r0 + r1) * 0.5 * 1.05)]
    for t, r in rings:
        c = Vector(base) + d * (L * t)
        for j in range(sides):
            ang = j / sides * math.tau
            acc_v.append(c + (a * math.cos(ang) + b * math.sin(ang)) * r)
            acc_t.append(t)
    acc_v.append(Vector(base) + d * L)
    acc_t.append(1.0)
    apex = len(acc_v) - 1
    circ = math.tau * r0
    for j in range(sides):
        j2 = (j + 1) % sides
        acc_f.append((o + j, o + j2, o + sides + j2, o + sides + j))
        u0, u1 = j / sides * circ, (j + 1) / sides * circ
        acc_uv.append([(u0, 0.0), (u1, 0.0), (u1, L * 0.6), (u0, L * 0.6)])
        acc_f.append((o + sides + j, o + sides + j2, apex))
        acc_uv.append([(u0, L * 0.6), (u1, L * 0.6), ((u0 + u1) * 0.5, L)])


def _set_fields(obj, F):
    """頂点ごとの属性列を float 属性 cf0.. に書く（結合しても順序に依らず残る）"""
    me = obj.data
    for i in range(F.shape[1]):
        a = me.attributes.get(f"cf{i}") or me.attributes.new(f"cf{i}", "FLOAT", "POINT")
        a.data.foreach_set("value", np.ascontiguousarray(F[:, i], dtype=np.float32))


def _get_fields(obj, n):
    me = obj.data
    out = []
    for i in range(n):
        a = np.empty(len(me.vertices), np.float32)
        me.attributes[f"cf{i}"].data.foreach_get("value", a)
        out.append(a.astype(np.float64))
        me.attributes.remove(me.attributes[f"cf{i}"])
    return out


def _uv_area_scale(me, uv_name="Bake"):
    """UV 面積 / 実面積 の平方根（1m あたりの UV 長さ）"""
    me.calc_loop_triangles()
    uv = np.empty(len(me.loops) * 2)
    me.uv_layers[uv_name].data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    lt = np.empty(len(me.loop_triangles) * 3, np.int64)
    me.loop_triangles.foreach_get("loops", lt)
    lt = lt.reshape(-1, 3)
    P = _co(me)
    tv = _tris(me)
    a3 = np.linalg.norm(np.cross(P[tv[:, 1]] - P[tv[:, 0]], P[tv[:, 2]] - P[tv[:, 0]]), axis=1).sum()
    u = uv[lt]
    a2 = np.abs(np.cross(u[:, 1] - u[:, 0], u[:, 2] - u[:, 0])).sum()
    return math.sqrt(a2 / max(a3, 1e-12))


def _pack(obj, margin=0.004):
    C._select_only([obj], obj)
    obj.data.uv_layers.active = obj.data.uv_layers["Bake"]
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    try:
        bpy.ops.uv.pack_islands(udim_source="CLOSEST_UDIM", rotate=True, margin=margin, shape_method="CONCAVE")
    except TypeError:
        bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")


class Plate:
    """テーブルサンゴの板 1 枚（極座標の格子）

    上面: 小枝の房の小さなドーム、枝の走る向きに沿ったかすかな筋
    裏面: 中心の幹から放射状に伸び二又に分かれる横枝が肋のように出っ張る
    縁: 上下をつなぐ丸い縁（半楕円の断面）
    """

    def __init__(self, nz, rnd, c, R0, H, dish=0.03, thick=(0.1, 0.022), tilt=(0.0, 0.0), notches=(),
                 n_ang=480, n_top=110, n_bot=64, n_rim=9, dome_amp=0.007, rib_depth=0.02, rib_w=(0.022, 0.012),
                 split=0.085):
        self.c = np.array(c, dtype=np.float64)
        self.R0, self.H = R0, H
        ang = np.linspace(0, math.tau, n_ang, endpoint=False)
        ring = np.stack([np.cos(ang), np.sin(ang), np.zeros(n_ang)], -1)
        R = R0 * (1 + 0.065 * nz.fbm(ring * 1.3 + 4.0, 3) + 0.025 * nz.fbm(ring * 4.0 + 2.0, 2))
        for a0, w, dep in notches:
            R *= 1 - dep * np.exp(-(_angdiff(ang, a0) / w) ** 2)
        self.ang, self.R = ang, R
        self.nz, self.tilt, self.dish, self.thick = nz, tilt, dish, thick
        self.dome_amp, self.rib_depth = dome_amp, rib_depth
        rr = thick[1] * 0.6                       # 縁の丸みの半径（水平）
        # 裏の横枝
        lines = _radial_branches(rnd, 0.1 * R0 / 0.8, R.max() * 1.05, n0=12, split=split)
        pts, wid = [], []
        for ln in lines:
            for (ra, ta), (rb, tb) in zip(ln[:-1], ln[1:]):
                for t in np.linspace(0, 1, max(2, int(abs(rb - ra) / 0.004) + 1), endpoint=False):
                    r_ = ra + (rb - ra) * t
                    th_ = ta + _angdiff(tb, ta) * t
                    pts.append((self.c[0] + r_ * math.cos(th_), self.c[1] + r_ * math.sin(th_)))
                    wid.append(rib_w[0] + (rib_w[1] - rib_w[0]) * min(1.0, r_ / R0))
        from scipy.spatial import cKDTree
        self.rib_tree = cKDTree(np.array(pts))
        self.rib_wid = np.array(wid)
        # 格子（上面: 中心 → 縁、縁: 半楕円、裏面: 縁 → 中心）
        st = 1 - (1 - np.linspace(0.004, 1, n_top)) ** 1.5
        sb = (1 - (1 - np.linspace(0.004, 1, n_bot)) ** 1.5)[::-1]
        phis = np.linspace(math.pi / 2, -math.pi / 2, n_rim)[1:-1]
        rows = []    # (種類, 値)
        rows += [("t", v) for v in st]
        rows += [("r", v) for v in phis]
        rows += [("b", v) for v in sb]
        V, kind, sval, phiv = [], [], [], []
        re = R - rr                                # 上下の面が終わる半径
        cosA, sinA = np.cos(ang), np.sin(ang)
        xe, ye = self.c[0] + re * cosA, self.c[1] + re * sinA
        zte = self.z_top(xe, ye, np.ones_like(re), smooth=True)
        zbe = self.z_bot(xe, ye, np.ones_like(re))
        zc_e, hz_e = (zte + zbe) * 0.5, (zte - zbe) * 0.5
        for kd, v in rows:
            if kd == "r":
                rad = re + rr * math.cos(v)
                x, y = self.c[0] + rad * cosA, self.c[1] + rad * sinA
                z = zc_e + hz_e * math.sin(v)
                s_ = np.ones_like(rad)
            else:
                rad = re * v
                x, y = self.c[0] + rad * cosA, self.c[1] + rad * sinA
                s_ = np.full_like(rad, v)
                z = self.z_top(x, y, s_) if kd == "t" else self.z_bot(x, y, s_)
            V.append(np.stack([x, y, z], -1))
            kind.append(np.full(n_ang, {"t": 0, "r": 1, "b": 2}[kd]))
            sval.append(s_)
            phiv.append(np.full(n_ang, v if kd == "r" else (math.pi / 2 if kd == "t" else -math.pi / 2)))
        nrow = len(rows)
        V = np.concatenate(V)
        top_c = np.array([[self.c[0], self.c[1], float(self.z_top(self.c[0:1], self.c[1:2], np.zeros(1))[0])]])
        bot_c = np.array([[self.c[0], self.c[1], float(self.z_bot(self.c[0:1], self.c[1:2], np.zeros(1))[0])]])
        V = np.concatenate([V, top_c, bot_c])
        it, ib = nrow * n_ang, nrow * n_ang + 1
        faces = []
        for i in range(nrow - 1):
            for j in range(n_ang):
                j2 = (j + 1) % n_ang
                faces.append((i * n_ang + j, (i + 1) * n_ang + j, (i + 1) * n_ang + j2, i * n_ang + j2))
        for j in range(n_ang):
            j2 = (j + 1) % n_ang
            faces.append((it, j, j2))
            faces.append((ib, (nrow - 1) * n_ang + j2, (nrow - 1) * n_ang + j))
        self.V = V
        self.faces = faces
        self.kind = np.concatenate(kind + [np.array([0, 2])])
        self.s = np.concatenate(sval + [np.zeros(2)])
        self.phi = np.concatenate(phiv + [np.array([math.pi / 2, -math.pi / 2])])
        self.re, self.rr, self.zte, self.zbe = re, rr, zte, zbe

    def z_top(self, x, y, s, smooth=False):
        nz = self.nz
        dx, dy = x - self.c[0], y - self.c[1]
        r = np.hypot(dx, dy)
        P2 = np.stack([x, y, np.zeros_like(x)], -1)
        z = self.H + self.dish * (r / self.R0) ** 2 + self.tilt[0] * dx + self.tilt[1] * dy
        z = z + 0.016 * nz.fbm(P2 * 1.4 + 1.0, 3)
        if not smooth:
            f1, _, _ = worley(P2 * 26.0 + np.array([0, 0, 0.5]), seed=7)
            dome = 1 - _sstep(f1, 0.05, 0.75)
            rib = self.rib(x, y)
            z = z + self.dome_amp * (0.75 * dome + 0.35 * rib) * _sstep(s, 1.0, 0.9)
        return z

    def rib(self, x, y):
        d, i = self.rib_tree.query(np.stack([x, y], -1))
        w = self.rib_wid[i]
        return np.sqrt(np.clip(1 - (d / w) ** 2, 0, 1))

    def z_bot(self, x, y, s):
        t0, t1 = self.thick
        t = t1 + (t0 - t1) * np.clip(1 - s, 0, 1) ** 1.3
        return self.z_top(x, y, s, smooth=True) - t - self.rib(x, y) * self.rib_depth * (1 - 0.4 * s)

    def mesh(self, name):
        return C.mesh_object(name, self.V.tolist(), self.faces)


def _stalk(name, base, top, r_foot, r_neck, r_top, nz, sides=18):
    """太く短い幹。根元は広がって岩に付き、上は裏の横枝へ開く。縦に融合した枝の筋"""
    base, top = Vector(base), Vector(top)
    n = 14
    pts, radii = [], []
    for i in range(n):
        t = i / (n - 1)
        p = base.lerp(top, t) + Vector((0.02 * math.sin(t * 3.0), 0.015 * math.sin(t * 2.2 + 1), 0))
        pts.append(p)
        r = r_neck + (r_foot - r_neck) * (1 - _sstep(t, 0.0, 0.35)) + (r_top - r_neck) * _sstep(t, 0.6, 1.0)
        radii.append(r)
    off = np.random.RandomState(3).rand(3) * 10

    def ring_fn(i, j, a):
        q = np.array([[math.cos(a) * 1.5 + off[0], math.sin(a) * 1.5 + off[1], i * 0.25 + off[2]]])
        return 1.0 + 0.07 * math.sin(a * 5 + i * 0.25) + 0.08 * float(nz.fbm(q, 2)[0])

    # 上端は板の中に隠れるので開いたまま、埋まる下端は閉じる
    o = geo.tube_along(pts, radii, sides=sides, name=name, cap_start=True, cap_end=False, ring_fn=ring_fn)
    me = o.data
    proc = me.uv_layers["Proc"].data
    bake = me.uv_layers["Bake"].data
    circ = math.tau * sum(radii) / len(radii)
    for poly in me.polygons:
        for li in poly.loop_indices:
            u, v = proc[li].uv
            if poly.loop_total > 4:
                bake[li].uv = ((u - 0.5) * 2 * radii[0], (v - 0.5) * 2 * radii[0])
            else:
                bake[li].uv = (u * circ, v)
    return o


def table_material(name, nimg, fimgs, pal, res=2048):
    """テーブルサンゴ

    上面: 上向きの小枝が密生した房（先は淡色の軸ポリプ、間は暗い隙間）
    縁: 成長帯は淡く（青紫・桃）、小枝の先はさらに淡い
    裏: 暗めの灰褐色、放射状の横枝（肋）は少し明るい
    幹: 死んだ灰褐色の骨格に石灰藻（ピンク）と藻

    fimgs[0]: R = 上面 (1) / 裏 (0), G = 裏の肋, B = 小枝の先 (0 付け根 → 1 先端)
    fimgs[1]: R = 中心からの距離 (0..1, 縁 1), G = 曲率 (0.5 平ら), B = 幹 (1)
    """

    def fn(nb):
        co = nb.coord("Object")
        top, rib, tip = xfer_fields(nb, fimgs[0])
        edge, curv, stalk = xfer_fields(nb, fimgs[1])
        big = nb.noise(co, 1.6, 4, 0.6)
        mid = nb.noise(co, 7.0, 4, 0.6)
        fine = nb.noise(co, 55.0, 3, 0.6)
        # --- 上面の小枝: 約 7mm おきに立つ丸い小枝の先（先端に淡い軸ポリプ）、間は深く暗い隙間 ---
        bv = nb.voronoi(co, 140.0, rand=0.8)
        brnd = nb.sep(nb.voronoi(co, 140.0, out="Color", rand=0.8))[0]
        rad = nb.maprange(brnd, 0.0, 1.0, 0.36, 0.48)             # 小枝ごとの太さ
        dome = nb.pow(nb.smooth(bv, rad, 0.0), 0.6)
        cap = nb.smooth(bv, 0.16, 0.05)
        crev = nb.smooth(bv, nb.mul(rad, 0.8), nb.add(rad, 0.06))
        tcol = nb.ramp(nb.add(nb.mul(big, 0.6), nb.mul(mid, 0.4)),
                       [(0.25, srgb(pal["top_lo"])), (0.5, srgb(pal["top_mid"])), (0.8, srgb(pal["top_hi"]))])
        tcol = nb.hsv(tcol, 0.5, 1.0, nb.maprange(brnd, 0, 1, 0.9, 1.08))
        # 放射状の横枝の上は小枝が密で少し明るく、間はわずかに暗い筋
        tcol = nb.hsv(tcol, 0.5, 1.0, nb.maprange(rib, 0.0, 1.0, 0.93, 1.07))
        hue = nb.noise(nb.mapping(co, loc=(4.1, 2.3, 0.0)), 1.1, 3, 0.55)
        tcol = nb.mix(tcol, srgb(pal["top_alt"]), nb.mul(nb.smooth(hue, 0.5, 0.7), 0.45))
        tcol = nb.mix(tcol, srgb(pal["polyp"]), nb.mul(cap, 0.6))
        tcol = nb.mix(tcol, srgb(pal["crev"]), nb.mul(crev, 0.75))
        # 縁の成長帯（外周 5% ほど）と小枝の先
        grow = nb.smooth(edge, 0.93, 0.995)
        tcol = nb.mix(tcol, srgb(pal["rim"]), nb.mul(grow, 0.4))
        tcol = nb.mix(tcol, nb.mix(srgb(pal["rim"]), srgb(pal["tip"]), nb.smooth(tip, 0.5, 0.95)),
                      nb.smooth(tip, 0.02, 0.35))
        # --- 裏 ---
        streak = nb.noise(nb.mapping(co, scale=(1, 1, 1)), 30.0, 4, 0.6, distortion=0.4)
        bcol = nb.ramp(nb.add(nb.mul(big, 0.5), nb.mul(streak, 0.5)),
                       [(0.3, srgb(pal["under_lo"])), (0.6, srgb(pal["under_mid"])), (0.85, srgb(pal["under_hi"]))])
        bcol = nb.mix(bcol, srgb(pal["under_hi"]), nb.mul(rib, 0.45))
        bcol = nb.mix(bcol, srgb(pal["rim"]), nb.mul(grow, 0.35))
        # --- 幹（死んだ骨格） ---
        cca = nb.smooth(nb.noise(co, 8.0, 4, 0.6), 0.52, 0.64)
        scol = nb.ramp(mid, [(0.3, srgb("#6f6b58")), (0.55, srgb("#858170")), (0.8, srgb("#9a9584"))])
        scol = nb.mix(scol, srgb("#b3878f"), nb.mul(cca, 0.6))
        scol = nb.mix(scol, srgb("#5f6044"), nb.mul(nb.smooth(fine, 0.55, 0.75), 0.5))
        tw = nb.smooth(top, 0.35, 0.65)
        col = nb.mix(bcol, tcol, tw)
        col = nb.mix(col, scol, nb.smooth(stalk, 0.3, 0.7))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(fine, 0.3, 0.7, 0.94, 1.05))
        z = nb.sep(co)[2]
        col = nb.mix(col, srgb("#cfc5a9"), nb.mul(nb.smooth(z, 0.06, -0.04), 0.7))
        # 凹凸: 上面は房のドームと隙間、裏は細かな莢
        pore = nb.smooth(nb.voronoi(co, 400.0), 0.4, 0.05)
        h_top = nb.add(nb.mul(dome, 0.9), nb.mul(crev, -0.6))
        h_bot = nb.add(nb.mul(pore, -0.3), nb.mul(streak, 0.4))
        height = nb.add(nb.mixf(h_bot, h_top, tw), nb.mul(fine, 0.15))
        rough = nb.mixf(0.8, 0.68, tw)
        rough = nb.mixf(rough, 0.86, stalk)
        cavity = nb.mul(nb.maprange(curv, 0.2, 0.5, 0.6, 1.0), nb.mixf(1.0, nb.maprange(crev, 0, 1, 1.0, 0.62), tw))
        return dict(color=col, rough=rough, height=height, height_scale=0.004, normal=xfer_normal(nb, nimg),
                    cavity=cavity)

    return pbr_material(name, fn, res=res, uv="keep", ao_distance=0.45)


def table(name, seed, tiers, pal, res=2048, plate_tris=9000, tip_spacing=0.03):
    """tiers: [dict(c=(x, y), R0, H, stalk=(x, y) or None, ...Plate の引数), ...]（下の段から）"""
    rnd = random.Random(seed)
    nz = Noise3(seed)
    highs, lows = [], []   # 高密度側は属性 cf0..5 = (上面, 裏の肋, 小枝の先, 中心からの距離, 曲率, 幹)
    tv, tf, tuv, tt = [], [], [], []
    plates = []
    for ti, spec in enumerate(tiers):
        spec = dict(spec)
        stalk_from = spec.pop("stalk")
        foot = spec.pop("foot", (0.2, 0.11, 0.2))
        pl = Plate(nz, rnd, **spec)
        plates.append(pl)
        hi = pl.mesh(f"{name}_plate{ti}_hi")
        me = hi.data
        P = _co(me)
        W = _neighbour_avg(me)
        C.set_smooth(hi, True)
        N = _vnormals(me)
        curv = np.clip(0.5 + _curvature(P, N, W, 4) / 0.008, 0, 1)
        top = _sstep(np.sin(pl.phi), -0.35, 0.35)
        rib = pl.rib(P[:, 0], P[:, 1]) * np.where(pl.kind == 1, 0.0, 1.0)
        _set_fields(hi, np.stack([top, rib, np.zeros(len(P)), pl.s, curv, np.zeros(len(P))], -1))
        highs.append(hi)
        lo = _copy(hi, f"{name}_plate{ti}")
        _decimate(lo, plate_tris * (pl.R0 / 0.8) ** 2)
        C.set_smooth(lo, True)
        # 上から見える上面にテクセルを回し、裏は密度を 0.75 倍に
        unwrap(lo, angle=60.0, margin=0.004, shrink=lambda c, n: n[:, 2] < -0.25, shrink_scale=0.75)
        lows.append(lo)
        # 縁の小枝の先: 外周に沿って等間隔（外上向き）+ 縁の少し内側に上向きの短い先
        L_out = np.sum(np.hypot(np.diff(np.r_[pl.re, pl.re[0]]), pl.re * (math.tau / len(pl.ang))))
        n_tip = int(L_out / tip_spacing)
        for k in range(n_tip):
            a = (k + rnd.uniform(-0.3, 0.3)) / n_tip * math.tau
            j = int(round(a / math.tau * len(pl.ang))) % len(pl.ang)
            rad = pl.re[j] + pl.rr * 0.35
            radial = Vector((math.cos(a), math.sin(a), 0))
            base = Vector((pl.c[0], pl.c[1], 0)) + radial * rad
            base.z = pl.zte[j] - 0.003
            el = math.radians(rnd.uniform(35, 72))
            d = radial * math.cos(el) + UP * math.sin(el) + Vector((rnd.gauss(0, 0.15), rnd.gauss(0, 0.15), 0))
            _tip_mesh(tv, tf, tuv, tt, base, d, rnd.uniform(0.013, 0.024), rnd.uniform(0.0045, 0.0058), 0.0028)
        for k in range(int(n_tip * 0.45)):
            a = rnd.uniform(0, math.tau)
            j = int(round(a / math.tau * len(pl.ang))) % len(pl.ang)
            rad = pl.re[j] - rnd.uniform(0.012, 0.05)
            radial = Vector((math.cos(a), math.sin(a), 0))
            x, y = pl.c[0] + radial.x * rad, pl.c[1] + radial.y * rad
            base = Vector((x, y, float(pl.z_top(np.array([x]), np.array([y]), np.array([0.97]))[0]) - 0.003))
            d = radial * 0.35 + UP + Vector((rnd.gauss(0, 0.15), rnd.gauss(0, 0.15), 0))
            _tip_mesh(tv, tf, tuv, tt, base, d, rnd.uniform(0.009, 0.016), rnd.uniform(0.004, 0.005), 0.0026)
        # 幹
        if stalk_from is not None:
            sx, sy, sz = stalk_from
            zb = float(pl.z_bot(np.array([pl.c[0]]), np.array([pl.c[1]]), np.zeros(1))[0])
            st = _stalk(f"{name}_stalk{ti}", (sx, sy, sz), (pl.c[0], pl.c[1], zb + 0.035), *foot, nz)
            C.set_smooth(st, True)
            n_st = len(st.data.vertices)
            sh = _copy(st, f"{name}_stalk{ti}_hi")
            _set_fields(sh, np.stack([np.zeros(n_st), np.zeros(n_st), np.zeros(n_st), np.zeros(n_st),
                                      np.full(n_st, 0.5), np.ones(n_st)], -1))
            highs.append(sh)
            lows.append(st)
    # 小枝の先のメッシュ（高密度側・低ポリ側で同じ形）
    tips = C.mesh_object(f"{name}_tips", tv, tf, tuv)
    tips.data.uv_layers["Proc"].name = "Bake"
    C.set_smooth(tips, True)
    nt = len(tv)
    tt = np.array(tt)
    th_ = _copy(tips, f"{name}_tips_hi")
    _set_fields(th_, np.stack([np.ones(nt), np.zeros(nt), tt, np.ones(nt), np.full(nt, 0.5), np.zeros(nt)], -1))
    highs.append(th_)
    lows.append(tips)
    # 低ポリ: 板は展開済み（0..1）、幹と先は実寸 → 板の密度に合わせて縮尺をそろえてから一緒に詰める
    k = _uv_area_scale(lows[0].data)
    for o in lows[1:]:
        if o.name.startswith(f"{name}_plate"):
            sc = k / _uv_area_scale(o.data)
        elif o.name.startswith(f"{name}_stalk"):
            sc = k * 0.65          # 幹は板の陰でほとんど見えない
        else:
            sc = k
        uvl = o.data.uv_layers["Bake"].data
        arr = np.empty(len(uvl) * 2)
        uvl.foreach_get("uv", arr)
        uvl.foreach_set("uv", arr * sc)
    for o in lows:
        for ln in [l.name for l in o.data.uv_layers if l.name != "Bake"]:
            o.data.uv_layers.remove(o.data.uv_layers[ln])
    low = C.join(lows, name)
    _pack(low)
    _triangulate(low)
    high = C.join(highs, name + "_hi")
    nimg, fimgs = transfer(low, high, _res(res), cage=0.02, fields=_get_fields(high, 6), name=name)
    _remove(high)
    C.assign(low, table_material(name, nimg, fimgs, pal, res=res))
    return [low]


PAL_TABLE_A = dict(top_lo="#786d4b", top_mid="#8e8156", top_hi="#a39565", top_alt="#7f8a61", polyp="#cdc7a9",
                   crev="#3f3928", rim="#a39fb5", tip="#cfcbdf", under_lo="#5f5947", under_mid="#716a53",
                   under_hi="#8c8366")
PAL_TABLE_B = dict(top_lo="#8b774f", top_mid="#a08a5e", top_hi="#b39d6d", top_alt="#978568", polyp="#d8cbad",
                   crev="#453a2c", rim="#c3adb0", tip="#e0cfd1", under_lo="#665c4a", under_mid="#776c56",
                   under_hi="#918468")


def table_a():
    """約 1.6m の 1 段のテーブル。幹はやや中心から外れる"""
    return table("Coral_Table_A", 41, [
        dict(c=(0.05, -0.03), R0=0.8, H=0.46, dish=0.035, thick=(0.1, 0.022), tilt=(0.02, -0.015),
             notches=[(1.1, 0.16, 0.12), (3.9, 0.22, 0.09)], stalk=(-0.02, 0.02, -0.1)),
    ], PAL_TABLE_A, res=2048, plate_tris=9000)


def table_b():
    """約 1.0m、2 段に少しずれて重なるテーブル"""
    return table("Coral_Table_B", 57, [
        dict(c=(-0.04, 0.02), R0=0.5, H=0.3, dish=0.02, thick=(0.08, 0.02), tilt=(-0.02, 0.01),
             notches=[(2.3, 0.2, 0.1)], n_ang=384, n_top=80, n_bot=50, split=0.075, stalk=(0.0, 0.0, -0.1)),
        dict(c=(0.12, 0.09), R0=0.33, H=0.47, dish=0.015, thick=(0.07, 0.02), tilt=(0.03, 0.02),
             notches=[(5.0, 0.25, 0.1)], n_ang=320, n_top=64, n_bot=40, split=0.07, stalk=(0.06, 0.05, 0.25),
             foot=(0.09, 0.07, 0.13)),
    ], PAL_TABLE_B, res=2048, plate_tris=14000)


# ---------------------------------------------------------------------------
# 枝サンゴ（チューブ）
# ---------------------------------------------------------------------------
class _Occupancy:
    """置いた枝の点の空間ハッシュ（枝どうしがぶつからないように）"""

    def __init__(self, cell=0.05):
        self.cell = cell
        self.grid = {}

    def _key(self, p):
        return (int(math.floor(p[0] / self.cell)), int(math.floor(p[1] / self.cell)),
                int(math.floor(p[2] / self.cell)))

    def add(self, p, r, owner):
        self.grid.setdefault(self._key(p), []).append((Vector(p), r, owner))

    def hits(self, p, r, owner, gap):
        kx, ky, kz = self._key(p)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for q, rq, ow in self.grid.get((kx + dx, ky + dy, kz + dz), ()):
                        if ow == owner:
                            continue
                        if (q - p).length < r + rq + gap:
                            return True
        return False


def _rot(v, axis, ang):
    return (Matrix.Rotation(ang, 3, axis) @ v).normalized()


def _tip_dome(pts, radii, d, n=3, end=0.14):
    """チューブの先を丸める（半球の輪を足す。最後はごく小さな輪で閉じる）"""
    p, r = pts[-1], radii[-1]
    for k in range(1, n + 1):
        a = k / n * math.pi / 2 * 0.94
        pts.append(p + d * (r * math.sin(a)))
        radii.append(max(r * math.cos(a), r * end))


def branch_material(name, pal, res=2048):
    """枝サンゴ: 放射状の莢のざらつき、先端 3〜5cm は淡い成長部、根元の古い枝は死んで藻をかぶる

    Proc UV: U = 周, V = 先端からの距離(m)（BarkPacker の v_from_end）
    """

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        tipd = nb.sep(nb.uv("Proc"))[1]
        big = nb.noise(co, 2.2, 4, 0.6)
        mid = nb.noise(co, 9.0, 4, 0.6)
        fine = nb.noise(co, 80.0, 3, 0.6)
        # 放射状の莢: 縁が盛り上がった小さな杯（約 3mm）
        cv = nb.voronoi(co, 300.0, rand=0.9)
        cup = nb.mul(nb.smooth(cv, 0.62, 0.3), nb.smooth(cv, 0.08, 0.28))
        hole = nb.smooth(cv, 0.2, 0.06)
        col = nb.ramp(nb.add(nb.mul(big, 0.6), nb.mul(mid, 0.4)),
                      [(0.25, srgb(pal["lo"])), (0.5, srgb(pal["mid"])), (0.8, srgb(pal["hi"]))])
        col = nb.mix(col, srgb(pal["cup"]), nb.mul(cup, 0.3))
        col = nb.mix(col, srgb(pal["hole"]), nb.mul(hole, 0.35))
        # 先端の淡い成長部
        tip = nb.smooth(tipd, pal.get("tip_len", 0.05), 0.004)
        col = nb.mix(col, srgb(pal["tip"]), nb.mul(tip, 0.85))
        col = nb.mix(col, srgb(pal["tip2"]), nb.smooth(tipd, 0.014, 0.0))
        # 根元の古い枝は死んで灰褐色の藻・石灰藻
        dead = nb.mul(nb.smooth(z, pal.get("dead_z", 0.16), 0.02), nb.sub(1.0, tip))
        dcol = nb.mix(srgb("#77735f"), srgb("#5f5f45"), nb.smooth(fine, 0.4, 0.7))
        dcol = nb.mix(dcol, srgb("#a8868c"), nb.mul(nb.smooth(nb.noise(co, 12.0, 3, 0.6), 0.55, 0.66), 0.6))
        col = nb.mix(col, dcol, nb.mul(dead, 0.9))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(fine, 0.3, 0.7, 0.93, 1.06))
        col = nb.mix(col, srgb("#cdc3a8"), nb.mul(nb.smooth(z, 0.05, -0.04), 0.7))
        live = nb.sub(1.0, dead)
        height = nb.add(nb.mul(nb.sub(nb.mul(cup, 0.7), nb.mul(hole, 0.8)), live), nb.mul(fine, 0.25))
        rough = nb.mixf(0.84, nb.mixf(0.7, 0.6, tip), live)
        cavity = nb.maprange(hole, 0, 1, 1.0, 0.75)
        return dict(color=col, rough=rough, height=height, height_scale=0.0018, cavity=cavity)

    return pbr_material(name, fn, res=res, uv="keep", ao_distance=0.3)


class Thicket:
    """鹿の角のように分かれる枝の茂み（包絡の半楕円体の中で、ぶつからないように伸ばす）"""

    def __init__(self, rnd, env, step=0.035, gap=0.012):
        self.rnd = rnd
        self.env = env          # (rx, ry, rz)
        self.step = step
        self.gap = gap
        self.occ = _Occupancy()
        self.limbs = []         # (pts, radii)
        self.queue = []

    def inside(self, p):
        rx, ry, rz = self.env
        return (p.x / rx) ** 2 + (p.y / ry) ** 2 + (max(p.z, 0.0) / rz) ** 2 < 1.0

    def grow(self, p0, d0, r0, gen, spec):
        rnd = self.rnd
        owner = len(self.limbs)
        pts, radii = [Vector(p0)], [r0]
        d = Vector(d0).normalized()
        p, r = Vector(p0), r0
        seg = rnd.uniform(*spec["seg"]) * (0.6 if gen == 0 else 1.0)
        step = spec.get("step", self.step)
        total = 0.0
        forks = []
        wob = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1)))
        while total < spec["max_len"][min(gen, len(spec["max_len"]) - 1)]:
            h = Vector((p.x, p.y, 0.0))
            out = h.normalized() if h.length > 1e-3 else Vector((1, 0, 0))
            wob = (wob + Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1))) * 0.5).normalized()
            d = (d + UP * spec["photo"] * step + out * spec["spread"] * step + wob * spec["wander"] * step).normalized()
            q = p + d * step
            rq = max(spec["r_tip"], r - spec["taper"] * step)
            if not self.inside(q) or (q.z < -0.02 and d.z < 0.0) or (len(pts) > 2 and self.occ.hits(q, rq, owner,
                                                                                                    self.gap)):
                break
            pts.append(q)
            radii.append(rq)
            p, r = q, rq
            total += step
            seg -= step
            if seg <= 0 and gen < spec["gens"]:
                ax = d.cross(Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1)))).normalized()
                ang = math.radians(rnd.uniform(*spec["fork"]))
                forks.append((p.copy(), _rot(d, ax, ang), r * spec["child_r"], gen + 1))
                d = _rot(d, ax, -ang * spec["bend_back"])
                seg = rnd.uniform(*spec["seg"])
        if len(pts) < 3:
            return
        for q, rq in zip(pts, radii):
            self.occ.add(q, rq, owner)
        _tip_dome(pts, radii, (pts[-1] - pts[-2]).normalized(), n=spec.get("dome", 3))
        self.limbs.append((pts, radii))
        for f in forks:
            self.queue.append((f, spec))

    def run(self):
        while self.queue:
            (p, d, r, gen), spec = self.queue.pop(0)
            self.grow(p - d * r, d, r, gen, spec)


def _branch_tubes(name, limbs, sides_fn, mat, seed=0):
    bp = BarkPacker(chunk_len=0.5)
    rnd = random.Random(seed)
    for i, (pts, radii) in enumerate(limbs):
        n = len(pts)
        ph = rnd.uniform(0, math.tau)

        def ring(ii, jj, a, n=n, ph=ph):
            # 枝の太さのわずかなむら（先端の丸みの輪では弱める）
            w = min(1.0, max(0.0, (n - 3 - ii) / 3.0))
            return 1.0 + w * (0.055 * math.sin(3 * a + ii * 1.7 + ph) + 0.035 * math.sin(5 * a - ii * 0.9 + 2 * ph))

        # 先端からの距離を V に（色の成長部）。付け根は親に埋まるので開いたまま
        bp.tube(pts, radii, sides_fn(radii[0]), f"{name}_b{i}", kind=0, v_from_end=True, cap_start=False,
                cap_end=True, ring_fn=ring)
    objs = bp.pack(margin=0.004)
    for o in objs:
        _triangulate(o)
        C.set_smooth(o, True)
        C.assign(o, mat)
    return objs


def branch_a(name="Coral_Branch_A", seed=77, res=2048):
    """スギノキミドリイシ型の枝の茂み（幅約 1.4m・高さ約 0.8m）。太さ 2〜3cm の枝が上外向きに反り、
    二又・側枝を繰り返して鹿の角のように込み合う。根元の古い枝は死んでいる。先は淡い青紫"""
    rnd = random.Random(seed)
    th = Thicket(rnd, env=(0.72, 0.68, 0.82), step=0.063, gap=0.016)
    spec = dict(seg=(0.1, 0.18), max_len=(0.42, 0.38, 0.32, 0.27, 0.22, 0.18), gens=5, photo=1.6, spread=0.3,
                wander=0.7, taper=0.012, r_tip=0.0105, child_r=0.92, fork=(24, 44), bend_back=0.4, dome=2)
    n0 = 14
    for k in range(n0):
        az = (k + rnd.uniform(-0.4, 0.4)) / n0 * math.tau
        el = math.radians(rnd.uniform(16, 58))
        rb = rnd.uniform(0.02, 0.26)
        base = Vector((math.cos(az) * rb, math.sin(az) * rb * 0.9, -0.07))
        d = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
        th.queue.append(((base + d * 0.015, d, rnd.uniform(0.017, 0.0195), 0), spec))
    th.run()
    print(f"  [branch] {name}: {len(th.limbs)} limbs")
    return _branch_tubes(name, th.limbs, lambda r: 6, branch_material(name, PAL_BRANCH_A, res))


def branch_b(name="Coral_Branch_B", seed=91, res=2048):
    """散房状のミドリイシ（約 0.8m）: 根元近くで何度も分かれた短い枝が上向きにそろい、
    先がこんもり丸い天蓋をつくる。枝は太さ 1.2〜1.8cm、先は桃紫"""
    rnd = random.Random(seed)
    th = Thicket(rnd, env=(0.41, 0.39, 0.36), step=0.032, gap=0.009)
    spec = dict(seg=(0.045, 0.085), max_len=(0.16, 0.15, 0.13, 0.11, 0.09, 0.08), gens=5, photo=3.0, spread=0.15,
                wander=0.6, taper=0.012, r_tip=0.0068, child_r=0.93, fork=(20, 36), bend_back=0.45, dome=2)
    n0 = 18
    for k in range(n0):
        az = (k + rnd.uniform(-0.4, 0.4)) / n0 * math.tau
        el = math.radians((rnd.uniform(62, 85), rnd.uniform(10, 32), rnd.uniform(30, 55))[k % 3])
        rb = rnd.uniform(0.01, 0.05) if k % 3 == 0 else rnd.uniform(0.03, 0.09)
        base = Vector((math.cos(az) * rb, math.sin(az) * rb, -0.05))
        d = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
        th.queue.append(((base + d * 0.01, d, rnd.uniform(0.0093, 0.0105), 0), spec))
    th.run()
    print(f"  [branch] {name}: {len(th.limbs)} limbs")
    return _branch_tubes(name, th.limbs, lambda r: 6 if r > 0.0075 else 5, branch_material(name, PAL_BRANCH_B, res))


PAL_BRANCH_A = dict(lo="#8a6f45", mid="#a0845a", hi="#b59b6e", cup="#c9b58c", hole="#5a4a30", tip="#a9b6e6",
                    tip2="#dde6f7", tip_len=0.05, dead_z=0.16)
PAL_BRANCH_B = dict(lo="#6f6f3f", mid="#83844f", hi="#979960", cup="#aeae82", hole="#4a4a2c", tip="#c28cb4",
                    tip2="#ebd3e3", tip_len=0.03, dead_z=0.1)


# ---------------------------------------------------------------------------
# プレビュー: 小さなパッチリーフのように寄せて並べる（OKI_CORAL_WATER=1 で水越しの確認画像も出す）
# ---------------------------------------------------------------------------
LAYOUT = {
    "Coral_MicroAtoll": ((0.3, 2.6), 10), "Coral_Massive_A": ((-3.0, 2.1), 30),
    "Coral_Branch_A": ((3.1, 2.5), 0), "Coral_Table_A": ((-1.4, -0.1), 20),
    "Coral_Table_B": ((1.7, 0.5), 100), "Coral_Massive_B": ((3.6, -0.6), 0),
    "Coral_Branch_B": ((1.6, -1.3), 40), "Coral_Brain": ((-0.1, -1.5), 0), "Coral_Soft": ((-3.0, -1.1), 0),
}


def water_material():
    """確認用の水（reef ブロックアウトと同じ調整）: 深さで赤から吸収、影のレイは素通し"""
    m = bpy.data.materials.new("PreviewWater")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = 0.02
    bsdf.inputs["IOR"].default_value = 1.333
    bsdf.inputs["Transmission Weight"].default_value = 1.0
    co = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0, 2.2, 1.0)
    nt.links.new(co.outputs["Object"], mp.inputs[0])
    wv = nt.nodes.new("ShaderNodeTexNoise")
    wv.inputs["Scale"].default_value = 0.9
    wv.inputs["Detail"].default_value = 8
    nt.links.new(mp.outputs[0], wv.inputs["Vector"])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.2
    bump.inputs["Distance"].default_value = 0.05
    nt.links.new(wv.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs[0], bsdf.inputs["Normal"])
    lp = nt.nodes.new("ShaderNodeLightPath")
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(lp.outputs["Is Shadow Ray"], mix.inputs[0])
    nt.links.new(bsdf.outputs[0], mix.inputs[1])
    nt.links.new(tr.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    ab = nt.nodes.new("ShaderNodeVolumeAbsorption")
    ab.inputs["Color"].default_value = (0.64, 0.93, 0.975, 1)
    ab.inputs["Density"].default_value = 1.2
    sc = nt.nodes.new("ShaderNodeVolumeScatter")
    sc.inputs["Color"].default_value = (0.04, 0.30, 0.90, 1)
    sc.inputs["Density"].default_value = 0.003
    add = nt.nodes.new("ShaderNodeAddShader")
    nt.links.new(ab.outputs[0], add.inputs[0])
    nt.links.new(sc.outputs[0], add.inputs[1])
    nt.links.new(add.outputs[0], out.inputs["Volume"])
    return m


def _preview_extra(objs):
    scene = bpy.context.scene
    for o in objs:
        if o.type != "MESH":
            continue
        key = next((k for k in LAYOUT if o.name.startswith(k)), None)
        if key is None:
            continue
        (x, y), rz = LAYOUT[key]
        o.parent = None
        o.location = (x, y, 0.0)
        o.rotation_euler = (0, 0, math.radians(rz))
    cam = scene.camera
    cam.location = (0.9, -7.6, 4.3)
    C.look_at(cam, (0.3, 0.7, 0.15))
    cam.data.lens = 30
    if os.environ.get("OKI_CORAL_WATER"):
        # 水面を 2m 上に張り、水面の上と水中から見る（Cycles は屈折越しの太陽を拾えないので影は素通し）
        bpy.ops.mesh.primitive_cube_add(size=1)
        w = bpy.context.object
        w.name = "PreviewWater"
        w.scale = (300, 300, 40)
        w.location = (0, 0, 2.0 - 20)
        C.apply_transform(w)
        C.assign(w, water_material())
        s0 = (scene.cycles.volume_bounces, scene.view_settings.look, scene.cycles.transmission_bounces)
        scene.cycles.volume_bounces = 1
        scene.cycles.transmission_bounces = 8
        scene.cycles.volume_step_rate = 4.0
        try:
            scene.view_settings.look = "AgX - Punchy"
        except TypeError:
            pass
        loc0, lens0 = cam.location.copy(), cam.data.lens
        for tag, loc, tgt, lens in (("water", (1.2, -8.5, 5.6), (0.3, 0.9, 0.0), 30),
                                    ("under", (0.8, -6.2, 0.9), (0.3, 1.2, 0.35), 24)):
            cam.location = loc
            C.look_at(cam, tgt)
            cam.data.lens = lens
            C.render(os.path.join(C.PREVIEW_DIR, f"coral_{tag}.jpg"), samples=int(os.environ.get("OKI_CORAL_WATER")))
        bpy.data.objects.remove(w)
        scene.cycles.volume_bounces, scene.view_settings.look, scene.cycles.transmission_bounces = s0
        cam.location, cam.data.lens = loc0, lens0
        C.look_at(cam, (0.3, 0.7, 0.15))


PREVIEW = dict(cam_dir=(0.1, -1.0, 0.55), lens=30, extra=_preview_extra)


# ---------------------------------------------------------------------------
# ビルド
# ---------------------------------------------------------------------------
VARIANTS = [
    ("Coral_Table_A", table_a),
    ("Coral_Table_B", table_b),
    ("Coral_Branch_A", branch_a),
    ("Coral_Branch_B", branch_b),
    ("Coral_Massive_A", massive_a),
    ("Coral_Massive_B", massive_b),
    ("Coral_MicroAtoll", microatoll),
    ("Coral_Brain", brain),
    ("Coral_Soft", soft),
]


def build():
    only = os.environ.get("OKI_CORAL_ONLY")  # 開発用: カンマ区切りでバリエーションを絞る
    only = set(only.split(",")) if only else None
    out = {}
    for vname, fn in VARIANTS:
        if only and vname not in only:
            continue
        out[vname] = fn()
        print(f"  [coral] built {vname}")
    return out
