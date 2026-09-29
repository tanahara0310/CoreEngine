"""リーフ地形タイル（40 m × 40 m）: 礁池 → 礁原・礁縁・ドロップオフ → 外洋側の砂地

座標: z=0 = 平均水位, +Y = 陸側, -Y = 海側（terrain.py と同じ）。各タイルの原点はタイル中心
  ReefTerrain_Lagoon: (x, -40) に置く。世界 Y -20..-60。礁池（イノー）の白砂、水深 2.2〜3.2 m
  ReefTerrain_Edge  : (x, -80) に置く。世界 Y -60..-100。礁原 → 礁縁（-0.35 m 前後）→ 縁脚縁溝のある急斜面
  ReefTerrain_Deep  : (x, -120) に置く。世界 Y -100..-140。水深 22 → 34 m の砂地
  （x = …, -40, 0, 40, … に並べる。BeachTerrain_Shore は (x, 0)）

タイル接続:
  - 高さは世界座標 (x, Y) の 1 つの関数 height()。各タイルはそれを自分の範囲で切り出す
  - X 方向は周期 40 m（PNoise と整数波数の縁溝）
  - Lagoon の +Y 縁は shore() と値・勾配とも一致（Y -20..-26 で shore() から切り替え）
  - Edge は X 256 分割。128 分割の隣と接する縁の奇数頂点は隣の辺の中点に置き、T 字の隙間を作らない
  - 法線は解析的な勾配（カスタム法線）、AO は高さ関数から水平線法で求めて頂点属性で渡す
    （ベイクの AO はタイル単体しか見えず、縁で隣の斜面の陰りが切れるため）
  - テクスチャは世界座標のプロシージャル模様。浜寄りは Shore の水中の砂と同じノード
"""
import math
import os

import bpy
import numpy as np
from scipy import ndimage
from scipy.interpolate import PchipInterpolator, RegularGridInterpolator

from .. import common as C
from ..common import pbr_material, srgb
from .terrain import PNoise, TILE, Torus, _dry_sand, _smoothstep, shore

R_T = TILE / math.tau
Y_SHORE_EDGE = -20.0                  # Shore の -Y 縁（世界 Y）
TILES = {                             # 名前: (中心の世界 Y, X 分割数, Y 分割数)
    "ReefTerrain_Edge": (-80.0, 256, 150),
    "ReefTerrain_Lagoon": (-40.0, 128, 128),
    "ReefTerrain_Deep": (-120.0, 128, 96),
}
NX_BASE = 128                         # Shore / Lagoon / Deep の X 分割

# ---------------------------------------------------------------------------
# 岸沖方向の平均断面（世界 Y → z）
# ---------------------------------------------------------------------------
_PY = [-20, -26, -32, -40, -48, -54, -58, -61, -64, -67, -70, -74, -78, -81, -82.5,
       -83.7, -85, -86.5, -89.5, -92.5, -95.5, -97.5, -100, -104, -110, -120, -130, -140, -150]
_PZ = [-3.0, -3.0, -2.9, -2.72, -2.78, -2.62, -2.45, -2.05, -1.45, -0.95, -0.70, -0.60, -0.52, -0.46, -0.42,
       -0.78, -1.8, -3.5, -7.55, -11.6, -15.6, -18.3, -21.2, -24.2, -27.0, -30.0, -32.4, -34.2, -35.6]
_PROFILE = PchipInterpolator(_PY[::-1], _PZ[::-1], extrapolate=True)

K_SG = 4                              # 縁脚縁溝の本数 / 40 m（波長 10 m）

# 礁池
N_LAG = PNoise(201, 2, 6, n=40, beta=1.0)              # 緩いうねり（7〜20 m）
N_SW = PNoise(202, 6, 14, n=80, beta=1.0, aniso=1.4)   # 砂の小さな起伏
N_MIC = PNoise(203, 12, 40, n=220, beta=1.0)           # 細かな起伏
# 礁原
N_FLAT = PNoise(211, 8, 28, n=180, beta=0.9)           # 石灰岩の舗床の凹凸（1.4〜5 m）
N_FLAT2 = PNoise(212, 28, 64, n=260, beta=0.8)         # さらに細かい凹凸
N_POOL = PNoise(213, 5, 14, n=80, beta=1.0)            # 潮だまり（くぼみ）
N_EDGEW = PNoise(214, 1, 5, n=24, beta=0.8, xonly=True)  # 礁縁線の出入り
# 縁脚縁溝
N_SGC = PNoise(222, 2, 7, n=40, beta=1.0)              # 溝の幅のゆらぎ
N_SGA = PNoise(223, 1, 4, n=16, beta=0.7, xonly=True)  # 縁脚の張り出しのゆらぎ
N_CORAL = PNoise(224, 8, 34, n=240, beta=0.7)          # 縁脚上のサンゴ塊
# 深場
N_DUNE = PNoise(231, 3, 9, n=60, beta=1.0, aniso=2.2)  # 礁縁に平行な砂のうねり
N_DBIG = PNoise(232, 1, 3, n=20, beta=1.0)


def _pnoise_k(seed, ks, beta=1.0):
    """波数を直接指定した PNoise"""
    n = PNoise.__new__(PNoise)
    rng = np.random.default_rng(seed)
    k = np.array(ks, np.float64)
    n.kx, n.ky = k[:, 0], k[:, 1]
    n.amp = np.hypot(n.kx, n.ky) ** -beta
    n.amp /= math.sqrt((n.amp ** 2).sum() / 2)
    n.ph = rng.uniform(0, math.tau, len(k))
    return n


# 縁脚の間隔のゆらぎ（主に x 方向。Y 方向はゆっくり曲がる程度）
N_SGW = _pnoise_k(221, [(1, 0), (2, 0), (3, 0), (0, 1), (1, 1), (-1, 1), (2, 1), (-2, 1)], beta=0.6)


def _softmin(h, cap, k):
    """h を cap 以下に滑らかに抑える"""
    t = (cap - h) / k
    return cap - k * np.logaddexp(0.0, t)


def spur_groove(x, Y):
    """縁脚縁溝の (縁脚 0..1, 溝 0..1)。縁脚はほぼ正弦の丸い断面、溝はその谷の狭い砂底"""
    th = math.tau * K_SG * x / TILE + 1.1 * N_SGW(x, Y)
    c = np.cos(th) + 0.12 * N_SGC(x, Y)
    spur = _smoothstep(-1.15, 1.15, c)
    groove = 1.0 - _smoothstep(-0.9, -0.3, c)
    return spur, groove


def _zones(Y):
    """帯ごとの重み（世界 Y の滑らかな関数）"""
    lag = 1.0 - _smoothstep(-52.0, -62.0, Y)
    flat = _smoothstep(-58.0, -68.0, Y) * (1.0 - _smoothstep(-82.0, -85.0, Y))
    fore = _smoothstep(-79.0, -84.0, Y) * (1.0 - _smoothstep(-100.0, -110.0, Y))
    deep = _smoothstep(-97.0, -108.0, Y)
    return lag, flat, fore, deep


def _pool(x, Y):
    """礁原の潮だまり（くぼみ）0..1。縁は細かいノイズで不規則に"""
    p = N_POOL(x, Y) + 0.35 * N_FLAT(x, Y)
    return _smoothstep(1.15, 1.8, p) * _smoothstep(-68.0, -71.0, Y) * (1 - _smoothstep(-79.0, -81.5, Y))


def reef_h(x, Y):
    """リーフ側の高さ（shore との切り替え前）"""
    x = np.asarray(x, np.float64)
    Y = np.asarray(Y, np.float64)
    lag, flat, fore, deep = _zones(Y)
    spur, groove = spur_groove(x, Y)
    # 縁脚は沖へ張り出し、溝は礁縁に切れ込む（断面を Y 方向にずらす）
    env_c = _smoothstep(-77.0, -82.5, Y) * (1.0 - _smoothstep(-91.0, -104.0, Y))
    a_s = (0.9 + 0.3 * N_SGA(x, Y)) * env_c
    a_g = (0.9 + 1.2 * (1.0 - _smoothstep(-84.0, -92.0, Y))) * env_c
    wav = 0.7 * N_EDGEW(x, Y) * _smoothstep(-74.0, -80.0, Y)   # 礁縁線そのもののうねり
    yp = Y + a_s * spur - a_g * (1.0 - spur) + wav
    h = _PROFILE(yp)
    # 溝の底はさらに一段低く、砂がたまって平ら
    env_g = _smoothstep(-82.0, -86.0, Y) * (1.0 - _smoothstep(-98.0, -108.0, Y))
    h = h - 0.4 * groove * env_g
    # 礁池: 緩いうねりと砂の起伏
    h = h + lag * (0.13 * N_LAG(x, Y) + 0.04 * N_SW(x, Y) + 0.015 * N_MIC(x, Y))
    # 礁原: 舗床の凹凸と潮だまり
    h = h + flat * (0.05 * N_FLAT(x, Y) + 0.018 * N_FLAT2(x, Y)) - 0.24 * _pool(x, Y)
    # 縁脚の上のサンゴ塊（溝の砂地は滑らか。礁縁の肩では控えめ）
    lump = fore * (1.0 - groove) * (0.35 + 0.65 * _smoothstep(-83.0, -87.0, Y))
    h = h + lump * (0.2 * N_CORAL(x, Y) + 0.03 * N_FLAT2(x, Y))
    # 深場: 礁縁に平行な砂のうねり
    h = h + deep * (0.28 * N_DUNE(x, Y) + 0.45 * N_DBIG(x, Y) + 0.015 * N_MIC(x, Y))
    # 礁原は水面下 0.3 m を超えない
    return _softmin(h, -0.3, 0.04)


def height(x, Y):
    """世界座標の高さ。Y -20..-26 で BeachTerrain_Shore の shore() から切り替える（縁で値・勾配が一致）"""
    x = np.asarray(x, np.float64)
    Y = np.asarray(Y, np.float64)
    w = _smoothstep(Y_SHORE_EDGE, Y_SHORE_EDGE - 6.0, Y)
    r = reef_h(x, Y)
    if np.all(w >= 1.0):
        return r
    s = shore(x, np.maximum(Y, Y_SHORE_EDGE - 7.0))
    return s * (1.0 - w) + r * w


def attrs(x, Y):
    """頂点属性 Terr: R = 溝（砂の筋）, G = 潮だまり, B = 縁脚"""
    spur, groove = spur_groove(x, Y)
    env = _smoothstep(-79.5, -83.0, Y) * (1.0 - _smoothstep(-104.0, -112.0, Y))
    return np.stack([groove * env, _pool(x, Y), spur * env], -1)


def _eval(fn, X, Y, chunk=16384):
    """大きな格子を分割して評価（PNoise は 点数 × 波数 の配列を作るため）"""
    X = np.asarray(X, np.float64)
    Y = np.asarray(Y, np.float64)
    xf, yf = X.ravel(), Y.ravel()
    parts = [fn(xf[i:i + chunk], yf[i:i + chunk]) for i in range(0, len(xf), chunk)]
    out = np.concatenate(parts, 0)
    return out.reshape(X.shape + out.shape[1:])


# ---------------------------------------------------------------------------
# メッシュ
# ---------------------------------------------------------------------------
def _arc_rows(y_off, ny):
    """平均断面の弧長で等間隔な行位置（ローカル y）と、y → 弧長 0..1 の対応表"""
    yl = np.linspace(-TILE / 2, TILE / 2, 8001)
    ds = np.sqrt(1.0 + _PROFILE(yl + y_off, 1) ** 2)
    s = np.concatenate([[0.0], np.cumsum((ds[1:] + ds[:-1]) * 0.5 * np.diff(yl))])
    s /= s[-1]
    rows = np.interp(np.linspace(0.0, 1.0, ny + 1), s, yl)
    rows[0], rows[-1] = -TILE / 2, TILE / 2
    return rows, (yl, s)


def horizon_ao(y_off, res=0.1, radius=2.0, ndir=16):
    """高さ関数から AO を求める（水平線法）。隣のタイルの高さも含めた格子で計算 → 継ぎ目で一致

    戻り値: (xs, ys, ao, hollow) ローカル座標の格子。hollow = 周囲より低い量(m)（砂だまり用）
    """
    m = radius + 2 * res
    n = int(round((TILE + 2 * m) / res)) + 1
    lin = np.linspace(-TILE / 2 - m, TILE / 2 + m, n)
    X, Y = np.meshgrid(lin, lin)
    H = _eval(height, X, Y + y_off)
    gy, gx = np.gradient(H, res)
    steps = int(round(radius / res))
    pad = steps + 1
    Hp = np.pad(H, pad, mode="edge")
    occ = np.zeros_like(H)
    for k in range(ndir):
        a = (k + 0.5) / ndir * math.tau
        dx, dy = math.cos(a), math.sin(a)
        t = gx * dx + gy * dy
        sin_t = t / np.sqrt(1.0 + t * t)
        best = np.zeros_like(H)
        seen = set()
        for st in range(1, steps + 1):
            ox, oy = int(round(dx * st)), int(round(dy * st))
            if (ox, oy) in seen:
                continue
            seen.add((ox, oy))
            dist = math.hypot(ox, oy) * res
            if dist > radius:
                break
            sh = Hp[pad + oy:pad + oy + n, pad + ox:pad + ox + n]
            dh = sh - H
            sin_h = dh / np.sqrt(dh * dh + dist * dist)
            fall = 1.0 - (dist / radius) ** 2
            np.maximum(best, (sin_h - sin_t) * fall, out=best)
        occ += np.clip(best, 0.0, 1.0)
    ao = 1.0 - occ / ndir
    hollow = ndimage.gaussian_filter(H, 0.7 / res, mode="nearest") - H
    return lin, ao, hollow


def tile_mesh(name, y_off, nx, ny, arc=True):
    """タイル 1 枚。高さは世界座標の height(x, y + y_off)"""
    xs = np.linspace(-TILE / 2, TILE / 2, nx + 1)
    if arc:
        ys, (tab_y, tab_s) = _arc_rows(y_off, ny)
    else:
        ys = np.linspace(-TILE / 2, TILE / 2, ny + 1)
        tab_y, tab_s = np.array([-TILE / 2, TILE / 2]), np.array([0.0, 1.0])
    X, Y = np.meshgrid(xs, ys)                   # [iy, ix]
    Z = _eval(height, X, Y + y_off)
    # 解析的な法線（中心差分）
    e = 0.05
    dx = (_eval(height, X + e, Y + y_off) - _eval(height, X - e, Y + y_off)) / (2 * e)
    dy = (_eval(height, X, Y + y_off + e) - _eval(height, X, Y + y_off - e)) / (2 * e)
    N = np.stack([-dx, -dy, np.ones_like(dx)], -1)
    N /= np.linalg.norm(N, axis=-1, keepdims=True)
    # 頂点属性: Terr = (溝, 潮だまり, 縁脚), Terr2 = (AO, 砂だまり, 急斜面)
    A1 = _eval(attrs, X, Y + y_off)
    lin, ao, hollow = horizon_ao(y_off)
    pts = np.stack([Y.ravel(), X.ravel()], -1)
    ao_v = RegularGridInterpolator((lin, lin), ao)(pts).reshape(X.shape)
    ho_v = RegularGridInterpolator((lin, lin), hollow)(pts).reshape(X.shape)
    A2 = np.stack([ao_v, ho_v, 1.0 - N[..., 2]], -1)
    # 128 分割の隣と接する縁: 奇数頂点を隣の辺の中点へ（T 字の隙間をなくす）
    if nx == 2 * NX_BASE:
        for r in (0, -1):
            Z[r, 1::2] = 0.5 * (Z[r, 0:-1:2] + Z[r, 2::2])
            nn = N[r, 0:-1:2] + N[r, 2::2]
            N[r, 1::2] = nn / np.linalg.norm(nn, axis=-1, keepdims=True)
            for A in (A1, A2):
                A[r, 1::2] = 0.5 * (A[r, 0:-1:2] + A[r, 2::2])

    verts = np.stack([X, Y, Z], -1).reshape(-1, 3)
    w = nx + 1
    iy, ix = np.meshgrid(np.arange(ny), np.arange(nx), indexing="ij")
    a = (iy * w + ix).ravel()
    faces = np.stack([a, a + 1, a + w + 1, a + w], -1)
    obj = C.mesh_object(name, verts, faces)
    me = obj.data
    # ベイク用 UV: U = x の平面投影（周期）、V = 平均断面の弧長（急斜面でもテクセルが伸びない）
    layer = me.uv_layers.new(name="Bake")
    loops_v = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get("vertex_index", loops_v)
    u = (verts[:, 0] + TILE / 2) / TILE
    v = np.interp(verts[:, 1], tab_y, tab_s)
    uv = np.stack([u, v], -1)[loops_v]
    layer.data.foreach_set("uv", uv.astype(np.float32).ravel())
    me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), bool))
    me.normals_split_custom_set_from_vertices([tuple(q) for q in N.reshape(-1, 3)])
    for aname, A in (("Terr", A1), ("Terr2", A2)):
        at = me.color_attributes.new(aname, "FLOAT_COLOR", "POINT")
        rgba = np.concatenate([A.reshape(-1, 3), np.ones((len(verts), 1))], -1).astype(np.float32)
        at.data.foreach_set("color", rgba.ravel())
    me.update()
    return obj


# ---------------------------------------------------------------------------
# マテリアル
# ---------------------------------------------------------------------------
class Vol:
    """x を円周（周長 40 m）に埋め込み、世界 Y と z はそのまま使う 4D 座標

    X 方向は周期 40 m。体積の模様を地表で切り出すので急斜面でも模様が伸びない
    """

    def __init__(self, nb, T, y_off):
        self.nb = nb
        self.yw = nb.add(T.y, y_off)          # 世界 Y
        k = math.tau / TILE
        ax = nb.mul(T.x, k)
        self.vec = nb.comb(nb.mul(nb.math("COSINE", ax), R_T), nb.mul(nb.math("SINE", ax), R_T), self.yw)
        self.w = T.z

    def at(self, off):
        return self.vec if not off else self.nb.vmath("ADD", self.vec, (off, off * 1.7, off * 0.6))

    def noise(self, scale, detail=3.0, rough=0.55, off=0.0, distortion=0.0, out="Fac"):
        return self.nb.noise(self.at(off), scale, detail, rough, distortion=distortion, dims="4D", w=self.w, out=out)

    def voronoi(self, scale, off=0.0, feature="F1", vec=None):
        return _vnode(self.nb, self, scale, off, feature, vec)


def _sepc(nb, col):
    """色 → (R, G, B)。Separate XYZ に色をつなぐと Cycles が変換ノードを足し、その先のノードの
    コンパイルが後回しになって SVM スタックを使い切るため、色は Separate Color で分ける"""
    n = nb.node("ShaderNodeSeparateColor")
    nb.link(col, n.inputs[0])
    return n.outputs


def _cvec(nb, col, sub=0.5, mul=1.0):
    """色 → ベクトル (col - sub) * mul（変換ノードを作らない）"""
    c = _sepc(nb, col)
    return nb.comb(*(nb.mul(nb.sub(c[i], sub), mul) for i in range(3)))


def _vnode(nb, src, scale, off=0.0, feature="F1", vec=None):
    """4D ボロノイのノード（Torus / Vol どちらの座標でも）。Distance と Color を両方使うためノードを返す"""
    if vec is None:
        vec = src.vec if not off else nb.vmath("ADD", src.vec, (off, off * 1.3, off * 0.4))
    n = nb.node("ShaderNodeTexVoronoi", feature=feature, distance="EUCLIDEAN", voronoi_dimensions="4D")
    nb.link(vec, n.inputs["Vector"])
    nb.link(src.w, n.inputs["W"])
    n.inputs["Scale"].default_value = scale
    n.inputs["Randomness"].default_value = 1.0
    return n


# 帯の前線: (e0, e1, 大きな揺らぎ w, 小さなまだらの揺らぎ w2, ノイズ scale, off)
# 世界 Y が e0 + w + w2 より陸側で 0、e1 - w - w2 より沖側で 1（揺らぎはクランプしてあるので厳密）
FRONTS = {
    "rubble": (-49.0, -54.5, 2.0, 1.0, 0.1, 301.0),     # 礁池 → 礁原の手前のサンゴれき帯（Y -58 で全面）
    "flat": (-62.0, -68.0, 2.5, 1.0, 0.12, 311.0),      # → 礁原（石灰岩の舗床）
    "fore": (-84.5, -88.0, 1.0, 0.0, 0.2, 321.0),       # → 礁斜面のサンゴ（肩は深さで判定）
    "deep": (-99.0, -108.0, 2.0, 3.0, 0.1, 331.0),      # → 深場の砂（サンゴがまばらになって砂へ）
}
FORE_SHOULDER = (-77.0, -80.0)                     # この Y より沖では深さ -0.8 m 以深を礁斜面とみなす


def _front(nb, V, key):
    e0, e1, w, w2, sc, off = FRONTS[key]
    warp = nb.maprange(V.noise(sc, 2.0, 0.5, off=off), 0.2, 0.8, -w, w)
    if w2:
        warp = nb.add(warp, nb.maprange(V.noise(sc * 6.0, 3.0, 0.6, off=off + 2.0), 0.3, 0.7, -w2, w2))
    return nb.smooth(nb.add(V.yw, warp), e0, e1)


def _front_range(key):
    """(これより陸側では 0, これより沖側では 1)"""
    e0, e1, w, w2, _, _ = FRONTS[key]
    return e0 + w + w2, e1 - w - w2


def _over(nb, base, top, m):
    """層を重ねる（色・高さ・粗さ・陰影をまとめて）"""
    if base is None:
        return top
    return dict(col=nb.mix(base["col"], top["col"], m), h=nb.mixf(base["h"], top["h"], m),
                r=nb.mixf(base["r"], top["r"], m), cav=nb.mixf(base["cav"], top["cav"], m))


def _pieces(nb, V, scale, off, cover=0.42):
    """サンゴれき・貝片（ボロノイの粒）: (粒 0..1, セル色)。V は Torus / Vol"""
    v = _vnode(nb, V, scale, off)
    piece = nb.smooth(v.outputs["Distance"], cover, cover - 0.2)
    return piece, _sepc(nb, v.outputs["Color"])


def _lagoon_layer(nb, T, V):
    """礁池の白砂。浜寄りは BeachTerrain_Shore の水中の砂と同じ式（縁で色・凹凸が一致）"""
    d = _dry_sand(nb, T)
    uw = nb.ramp(d["big"], [(0.3, srgb("#e2d7bf")), (0.5, srgb("#eadfc6")), (0.7, srgb("#efe6cf"))])
    uw = nb.mix(uw, srgb("#f7f2e4"), nb.mul(d["white"], 0.6))
    uw = nb.mix(uw, srgb("#efe4d0"), nb.mul(d["frag"], 0.8))
    # 砂漣（Shore と同じ式。位相は世界 Y で続ける）
    warp = nb.add(nb.mul(T.noise(0.22, 3, 0.5, off=121.0), 10.0), nb.mul(T.noise(1.4, 2, 0.5, off=123.0), 1.6))
    ph = nb.add(nb.mul(V.yw, math.tau / 0.34), warp)
    rip = nb.sub(1.0, nb.math("ABSOLUTE", nb.math("SINE", nb.mul(ph, 0.5))))
    rip_mask = nb.maprange(T.noise(0.15, 2, 0.5, off=127.0), 0.3, 0.6, 0.45, 1.0)

    # ここから礁池の追加要素（浜の縁 Y=-20 では 0）
    b = nb.smooth(V.yw, -21.0, -27.0)
    # 大きな明暗のむら（10〜20 m）
    uw = nb.mix(uw, nb.hsv(uw, 0.5, 1.0, nb.maprange(T.noise(0.07, 2, 0.5, off=221.0), 0.3, 0.7, 0.93, 1.03)), b)
    # 白いサンゴれきの散る所（数 m のパッチ、縁はまばら）
    pn = T.noise(0.09, 3, 0.55, off=201.0)
    patch = nb.mul(nb.smooth(pn, 0.56, 0.63), nb.smooth(T.noise(1.0, 3, 0.6, off=203.0), 0.28, 0.5))
    patch = nb.mul(patch, b)
    piece, pc = _pieces(nb, T, 7.0, 205.0, cover=0.46)
    # 砂（暖かいベージュ）より白く冷たい色で、遠目にも淡いパッチに見えるように
    rub = nb.ramp(pc[0], [(0.0, srgb("#f0efe9")), (0.35, srgb("#f7f6f1")), (0.6, srgb("#e1ded5")),
                          (0.8, srgb("#cdc9bd")), (0.93, srgb("#b9b3a5")), (1.0, srgb("#e7d6cf"))])
    rub = nb.mix(srgb("#d8d3c6"), rub, piece)
    small, _ = _pieces(nb, T, 19.0, 207.0, cover=0.34)
    rub = nb.mix(rub, srgb("#f4f0e6"), nb.mul(small, 0.55))
    # 藻でくすんだ所（まばら。オリーブ褐色）
    alg = nb.mul(nb.smooth(T.noise(0.06, 2, 0.5, off=211.0), 0.6, 0.67), b)
    alg = nb.mul(alg, nb.maprange(T.noise(2.0, 4, 0.6, off=213.0), 0.3, 0.7, 0.35, 1.0))
    alg_col = nb.mix(srgb("#a49a70"), srgb("#7f7a52"), T.noise(6.0, 3, 0.6, off=215.0))

    rip_mask = nb.mul(rip_mask, nb.sub(1.0, nb.mul(patch, 0.8)))
    col = nb.mix(uw, srgb("#f3ecdc"), nb.mul(nb.mul(nb.sub(1.0, rip), rip_mask), 0.12))
    # 砂漣の峰はわずかに暗い（藻の付く面）
    col = nb.mix(col, srgb("#d8cdb2"), nb.mul(nb.mul(nb.smooth(rip, 0.75, 1.0), rip_mask), nb.mul(b, 0.18)))
    col = nb.mix(col, rub, nb.mul(patch, 0.92))
    col = nb.mix(col, alg_col, nb.mul(alg, 0.6))
    h = nb.add(nb.mul(nb.mul(rip, rip_mask), 0.014), nb.mul(d["fine"], 0.002))
    h = nb.add(h, nb.mul(patch, nb.add(nb.mul(piece, 0.035), nb.mul(small, 0.008))))
    r = nb.mixf(0.5, 0.72, patch)
    r = nb.mixf(r, 0.62, alg)
    cav = nb.maprange(nb.mul(nb.sub(1.0, rip), rip_mask), 0.0, 1.0, 1.0, 0.9)
    cav = nb.mul(cav, nb.mixf(1.0, nb.maprange(piece, 0.0, 1.0, 0.7, 1.0), patch))
    return dict(col=col, h=h, r=r, cav=cav)


def _heads(nb, V, scale, off, frac, rad=(0.22, 0.34)):
    """まばらな丸い塊（サンゴの頭・根）: (塊 0..1, 縁 0..1, セル色)。輪郭はノイズでゆがめる"""
    wv = _cvec(nb, V.noise(scale * 4.0, 3, 0.6, off=off + 1.0, out="Color"), 0.5, 0.35 / scale)
    v = V.voronoi(scale, vec=nb.vmath("ADD", V.at(off), wv))
    c = _sepc(nb, v.outputs["Color"])
    r = nb.maprange(c[1], 0.0, 1.0, rad[0], rad[1])
    on = nb.math("LESS_THAN", c[0], frac)
    dd = nb.sub(v.outputs["Distance"], r)
    body = nb.mul(nb.smooth(dd, 0.03, -0.04), on)
    ring = nb.mul(nb.mul(nb.smooth(dd, 0.14, 0.0), on), nb.sub(1.0, body))
    return body, ring, c


def _rubble_layer(nb, V, tb):
    """礁原の手前: サンゴれきと粗い砂、点在するサンゴの頭、芝状の藻"""
    piece, pc = _pieces(nb, V, 6.0, 401.0, cover=0.48)
    col = nb.ramp(pc[0], [(0.0, srgb("#ebe6da")), (0.25, srgb("#d6cfbf")), (0.5, srgb("#c0b6a3")),
                          (0.72, srgb("#aaa089")), (0.86, srgb("#cfb3ab")), (1.0, srgb("#a3a07f"))])
    col = nb.mix(srgb("#c8bca1"), col, piece)
    small, _ = _pieces(nb, V, 16.0, 403.0, cover=0.34)
    col = nb.mix(col, srgb("#ece6d8"), nb.mul(small, 0.5))
    # 芝状の藻（沖側ほど多い、まだら）
    tmask = nb.smooth(V.noise(0.3, 3, 0.6, off=405.0), 0.5, 0.64)
    turf = nb.mul(tmask, nb.maprange(V.yw, -54.0, -66.0, 0.25, 1.0))
    tcol = nb.mix(srgb("#857c52"), srgb("#96845a"), V.noise(5.0, 2, 0.5, off=407.0))
    col = nb.mix(col, tcol, nb.mul(nb.mul(turf, nb.maprange(piece, 0.0, 1.0, 0.5, 1.0)), 0.75))
    # 点在するサンゴの頭（0.5〜1.2 m）。明るい縁と褐色〜オリーブの本体
    head, ring, hc = _heads(nb, V, 0.45, 409.0, 0.24, rad=(0.22, 0.38))
    hcol = nb.ramp(hc[1], [(0.0, srgb("#8a6c46")), (0.35, srgb("#9a8352")), (0.65, srgb("#7a7543")),
                           (0.9, srgb("#b39b72"))], interp="CONSTANT")
    hcol = nb.hsv(hcol, 0.5, 1.0, nb.maprange(V.noise(9.0, 3, 0.6, off=411.0), 0.3, 0.7, 0.75, 1.15))
    col = nb.mix(col, srgb("#ddd6c6"), nb.mul(ring, 0.35))
    col = nb.mix(col, hcol, head)
    # 窪みには砂がたまる
    sand = nb.smooth(tb[1], 0.02, 0.07)
    col = nb.mix(col, srgb("#dbd1ba"), nb.mul(sand, 0.7))
    h = nb.add(nb.mul(piece, 0.035), nb.mul(small, 0.008))
    h = nb.add(h, nb.mul(head, nb.add(0.1, nb.mul(V.noise(9.0, 3, 0.6, off=413.0), 0.04))))
    h = nb.mixf(h, 0.0, nb.mul(sand, 0.7))
    r = nb.mixf(nb.mixf(0.72, 0.85, turf), 0.55, nb.mul(sand, 0.7))
    cav = nb.mul(nb.maprange(piece, 0.0, 1.0, 0.7, 1.0), nb.maprange(ring, 0.0, 1.0, 1.0, 0.85))
    cav = nb.mixf(cav, 1.0, nb.mul(sand, 0.7))
    return dict(col=col, h=h, r=r, cav=cav)


def _flat_layer(nb, V, ta, tb):
    """礁原: 死サンゴの石灰岩の舗床。芝状の藻、ピンク紫の石灰藻、割れ目と穴、小さなサンゴ、潮だまり"""
    big = V.noise(0.4, 4, 0.6, off=501.0)
    col = nb.ramp(big, [(0.3, srgb("#8f8570")), (0.55, srgb("#a3987f")), (0.75, srgb("#b5ab91"))])
    col = nb.mix(col, srgb("#8c8573"), nb.mul(nb.smooth(V.noise(2.2, 4, 0.6, off=503.0), 0.52, 0.7), 0.5))
    # 芝状の藻（オリーブ〜茶。ムラと細かい毛羽）
    tn = V.noise(0.28, 4, 0.6, off=505.0)
    turf = nb.smooth(tn, 0.4, 0.54)
    tcol = nb.ramp(V.noise(2.6, 3, 0.6, off=507.0), [(0.3, srgb("#5e5936")), (0.5, srgb("#6e6641")),
                                                     (0.66, srgb("#806c46")), (0.8, srgb("#8d7f53"))])
    fuzz = V.noise(14.0, 2, 0.6, off=509.0)
    tcol = nb.hsv(tcol, 0.5, 1.0, nb.maprange(fuzz, 0.3, 0.7, 0.78, 1.18))
    col = nb.mix(col, tcol, nb.mul(turf, 0.85))
    # ピンク紫の石灰藻（礁縁と、高まりの上に多い）
    crest = nb.mul(nb.smooth(V.yw, -75.0, -80.5), nb.sub(1.0, nb.smooth(V.yw, -84.0, -87.0)))
    cn = nb.add(nb.add(V.noise(0.45, 3, 0.6, off=511.0), nb.mul(crest, 0.09)), nb.mul(tb[1], -1.5))
    cor = nb.mul(nb.smooth(cn, 0.58, 0.66), nb.smooth(V.noise(2.0, 3, 0.6, off=541.0), 0.3, 0.5))
    ccol = nb.ramp(V.noise(3.5, 3, 0.6, off=513.0), [(0.3, srgb("#94768a")), (0.5, srgb("#a88c98")),
                                                     (0.7, srgb("#bca2aa"))])
    knob, _ = _pieces(nb, V, 26.0, 515.0, cover=0.36)
    ccol = nb.mix(ccol, srgb("#d9bcc1"), nb.mul(knob, 0.35))
    col = nb.mix(col, ccol, nb.mul(cor, 0.9))
    # 小さな生きたサンゴ（褐色〜緑褐色）と白く枯れた塊（どちらもまばら）
    live, lring, lc = _heads(nb, V, 1.1, 517.0, 0.09, rad=(0.14, 0.26))
    lcol = nb.ramp(lc[1], [(0.0, srgb("#7c6340")), (0.4, srgb("#6f6b3b")), (0.75, srgb("#8e7a4a")),
                           (0.92, srgb("#7a6a86"))], interp="CONSTANT")
    lcol = nb.hsv(lcol, 0.5, 1.0, nb.maprange(fuzz, 0.3, 0.7, 0.8, 1.15))
    dead, _, _ = _heads(nb, V, 0.9, 519.0, 0.05, rad=(0.12, 0.22))
    col = nb.mix(col, srgb("#4f4a3a"), nb.mul(lring, 0.25))
    col = nb.mix(col, lcol, live)
    col = nb.mix(col, srgb("#cdc6b5"), nb.mul(dead, 0.7))
    # 割れ目: ゆがめたノイズの等値線（曲がりくねった細い線）。所々だけに出す
    c1 = nb.math("ABSOLUTE", nb.sub(V.noise(0.32, 2, 0.5, off=521.0, distortion=0.7), 0.5))
    crack = nb.mul(nb.smooth(c1, 0.012, 0.0), nb.smooth(V.noise(0.45, 2, 0.5, off=523.0), 0.44, 0.56))
    c2 = nb.math("ABSOLUTE", nb.sub(V.noise(1.1, 2, 0.5, off=525.0, distortion=0.5), 0.5))
    crack2 = nb.mul(nb.smooth(c2, 0.009, 0.0), nb.smooth(V.noise(0.9, 2, 0.5, off=527.0), 0.52, 0.64))
    pit, _ = _pieces(nb, V, 12.0, 529.0, cover=0.15)
    pit2, _ = _pieces(nb, V, 4.0, 531.0, cover=0.1)
    col = nb.mix(col, srgb("#4a4436"), nb.mul(crack, 0.75))
    col = nb.mix(col, srgb("#5a5343"), nb.mul(crack2, 0.45))
    col = nb.mix(col, srgb("#4a4538"), nb.mul(nb.math("MAXIMUM", pit, pit2), 0.6))
    # 潮だまり: 砂とれきの底、縁は濡れて暗く石灰藻が付く
    pool = ta[1]
    pin = nb.smooth(pool, 0.3, 0.7)
    rim = nb.mul(nb.smooth(pool, 0.04, 0.22), nb.sub(1.0, pin))
    col = nb.mix(col, nb.mix(ccol, srgb("#6d6548"), 0.4), nb.mul(rim, 0.55))
    psand = nb.mix(srgb("#c6bca3"), srgb("#d6cdb7"), V.noise(3.0, 3, 0.6, off=533.0))
    ppiece, ppc = _pieces(nb, V, 8.0, 535.0, cover=0.36)
    psand = nb.mix(psand, nb.mix(srgb("#b2a893"), srgb("#ddd6c7"), ppc[0]), nb.mul(ppiece, 0.7))
    # 暗い潮だまり（底にサンゴと藻）
    dark_pool = nb.smooth(V.noise(0.25, 2, 0.5, off=543.0), 0.36, 0.48)
    psand = nb.mix(psand, nb.mix(srgb("#5f5a3c"), srgb("#7e6a48"), V.noise(4.0, 3, 0.6, off=545.0)), dark_pool)
    # 窪みの砂
    hol = nb.smooth(tb[1], 0.025, 0.07)
    sand = nb.math("MAXIMUM", pin, nb.mul(hol, 0.8))
    col = nb.mix(col, psand, sand)

    rough_h = V.noise(1.5, 6, 0.62, off=537.0)
    h = nb.add(nb.mul(rough_h, 0.03), nb.mul(V.noise(7.0, 3, 0.6, off=539.0), 0.008))
    h = nb.add(h, nb.mul(nb.mul(turf, fuzz), 0.004))
    h = nb.add(h, nb.mul(nb.mul(cor, knob), 0.008))
    h = nb.add(h, nb.add(nb.mul(live, 0.05), nb.mul(dead, 0.03)))
    h = nb.sub(h, nb.add(nb.add(nb.mul(crack, 0.025), nb.mul(crack2, 0.01)),
                         nb.mul(nb.math("MAXIMUM", pit, pit2), 0.014)))
    h = nb.mixf(h, nb.mul(ppiece, 0.012), sand)
    r = nb.mixf(nb.mixf(0.8, 0.86, turf), 0.84, cor)
    r = nb.mixf(r, 0.55, sand)
    cav = nb.mul(nb.maprange(crack, 0.0, 1.0, 1.0, 0.5),
                 nb.maprange(nb.add(crack2, nb.math("MAXIMUM", pit, pit2)), 0.0, 1.0, 1.0, 0.65))
    cav = nb.mul(cav, nb.maprange(lring, 0.0, 1.0, 1.0, 0.8))
    cav = nb.mixf(cav, nb.maprange(ppiece, 0.0, 1.0, 0.8, 1.0), sand)
    return dict(col=col, h=h, r=r, cav=cav)


def _sand2(nb, V, tb, deep):
    """溝と深場の砂（礁池より少し灰色）。deep: 深場の大きな砂漣の重み"""
    col = nb.ramp(V.noise(0.3, 3, 0.5, off=601.0), [(0.3, srgb("#c8bea5")), (0.5, srgb("#d4cbb4")),
                                                    (0.7, srgb("#ddd5c1"))])
    col = nb.hsv(col, 0.5, 1.0, nb.maprange(V.noise(0.07, 2, 0.5, off=619.0), 0.3, 0.7, 0.9, 1.05))
    # 斜面を流れ下る砂の筋（Y 方向に伸びた模様）
    streak = nb.noise(nb.mapping(V.at(615.0), scale=(1.0, 1.0, 0.2)), 1.2, 3, 0.55, dims="4D", w=V.w)
    col = nb.hsv(col, 0.5, 1.0, nb.maprange(streak, 0.3, 0.7, 0.86, 1.06))
    # 粗いれき（溝の底や斜面の下）
    rp, rpc = _pieces(nb, V, 5.5, 621.0, cover=0.36)
    rmix = nb.mul(rp, nb.maprange(V.noise(0.4, 3, 0.6, off=623.0), 0.35, 0.65, 0.1, 0.8))
    col = nb.mix(col, nb.mix(srgb("#a79e89"), srgb("#d8d1c2"), rpc[0]), rmix)
    gv = V.voronoi(16.0, off=603.0)
    gc = _sepc(nb, gv.outputs["Color"])
    grain = nb.smooth(gv.outputs["Distance"], 0.22, 0.08)
    col = nb.mix(col, srgb("#f1ece0"), nb.mul(grain, nb.math("GREATER_THAN", gc[0], 0.6)))
    col = nb.mix(col, srgb("#8f8676"), nb.mul(grain, nb.math("GREATER_THAN", gc[1], 0.93)))
    # 窪み（砂のうねりの谷）には細かい有機物がたまり少し灰色
    col = nb.mix(col, srgb("#bcb39d"), nb.mul(nb.smooth(tb[1], 0.0, 0.1), 0.5))
    # 藻・藍藻のうすい膜（まだら）
    film = nb.smooth(V.noise(0.14, 3, 0.55, off=605.0), 0.54, 0.66)
    film = nb.mul(film, nb.maprange(V.noise(2.0, 3, 0.6, off=625.0), 0.3, 0.7, 0.4, 1.0))
    col = nb.mix(col, nb.mix(srgb("#a39975"), srgb("#8e7d5f"), V.noise(1.5, 2, 0.5, off=617.0)), nb.mul(film, 0.5))
    # 深場の砂漣（礁縁に平行）
    warp = nb.add(nb.mul(V.noise(0.12, 3, 0.5, off=607.0), 9.0), nb.mul(V.noise(0.9, 2, 0.5, off=609.0), 1.2))
    ph = nb.add(nb.mul(V.yw, math.tau / 0.62), warp)
    rip = nb.sub(1.0, nb.math("ABSOLUTE", nb.math("SINE", nb.mul(ph, 0.5))))
    rmask = nb.mul(nb.maprange(V.noise(0.2, 2, 0.5, off=611.0), 0.3, 0.65, 0.2, 1.0), deep)
    col = nb.mix(col, srgb("#e6dfcd"), nb.mul(nb.mul(nb.sub(1.0, rip), rmask), 0.12))
    h = nb.add(nb.mul(nb.mul(rip, rmask), 0.022), nb.mul(grain, 0.002))
    h = nb.add(h, nb.add(nb.mul(V.noise(2.0, 3, 0.6, off=613.0), 0.01), nb.mul(rmix, 0.03)))
    cav = nb.maprange(nb.mul(nb.sub(1.0, rip), rmask), 0.0, 1.0, 1.0, 0.88)
    return dict(col=col, h=h, r=0.55, cav=cav)


def _colonies(nb, V, off, scale, zone):
    """サンゴ群体（輪郭をゆがめたボロノイのセル = 1 群体）: (色, ドーム 0..1)

    色は場所ごとの基調色 zone からのゆらぎ。まれに白化・灰色の死サンゴ・青・紫の群体
    """
    wv = _cvec(nb, V.noise(scale * 3.0, 2, 0.5, off=off + 1.0, out="Color"), 0.5, 0.45 / scale)
    cv = V.voronoi(scale, vec=nb.vmath("ADD", V.at(off), wv))
    cc = _sepc(nb, cv.outputs["Color"])
    col = nb.hsv(zone, nb.add(0.48, nb.mul(cc[1], 0.04)), nb.maprange(cc[2], 0.0, 1.0, 0.8, 1.15),
                 nb.maprange(cc[0], 0.0, 1.0, 0.72, 1.25))
    special = nb.ramp(cc[2], [(0.0, srgb("#8d887c")), (0.4, srgb("#cdc4ad")), (0.65, srgb("#5f6c8e")),
                              (0.85, srgb("#7a648c"))], interp="CONSTANT")
    col = nb.mix(col, special, nb.math("GREATER_THAN", cc[1], 0.93))
    dome = nb.smooth(cv.outputs["Distance"], 0.75, 0.1)
    return col, dome


def _fore_layer(nb, V, ta, tb, sand):
    """礁斜面: 縁脚はサンゴの群体に覆われ（大小が混じる）、間は芝状の藻と暗い隙間。溝は砂とれき"""
    # 場所ごとの基調色（褐色・オリーブ・黄土のまだら）
    zone = nb.ramp(V.noise(0.22, 3, 0.6, off=723.0), [(0.3, srgb("#6f5538")), (0.44, srgb("#686236")),
                                                      (0.56, srgb("#7d6a42")), (0.7, srgb("#9a8258"))])
    c1, d1 = _colonies(nb, V, 701.0, 0.9, zone)
    c2, d2 = _colonies(nb, V, 703.0, 2.2, zone)
    c3, d3 = _colonies(nb, V, 705.0, 0.45, zone)
    sel = nb.smooth(V.noise(0.5, 3, 0.6, off=707.0), 0.46, 0.6)     # 小さな群体の多い所
    sel3 = nb.smooth(V.noise(0.35, 3, 0.6, off=709.0), 0.56, 0.64)  # 大きな群体の所
    col = nb.mix(nb.mix(c1, c2, sel), c3, sel3)
    dome = nb.mixf(nb.mixf(d1, d2, sel), d3, sel3)
    # 群体の表面（ポリプの粒）と下地（芝状の藻・死サンゴ）
    pol = V.noise(10.0, 3, 0.6, off=711.0)
    col = nb.hsv(col, 0.5, 1.0, nb.maprange(pol, 0.3, 0.7, 0.86, 1.1))
    sub = nb.mix(srgb("#5c5738"), srgb("#6f6346"), V.noise(1.2, 3, 0.6, off=713.0))
    sub = nb.hsv(sub, 0.5, 1.0, nb.maprange(V.noise(12.0, 2, 0.6, off=715.0), 0.3, 0.7, 0.8, 1.15))
    cover = nb.smooth(dome, 0.06, 0.28)
    col = nb.mix(sub, col, cover)
    col = nb.mix(col, srgb("#352f26"), nb.mul(nb.smooth(dome, 0.05, 0.0), 0.5))
    # 浅い所（礁縁の肩）は石灰藻がまだ多い
    shallow = nb.smooth(V.w, -4.0, -1.0)
    col = nb.mix(col, nb.ramp(V.noise(4.0, 3, 0.6, off=717.0), [(0.3, srgb("#94768a")), (0.7, srgb("#b89ea8"))]),
                 nb.mul(nb.mul(shallow, nb.smooth(V.noise(0.7, 3, 0.6, off=719.0), 0.52, 0.62)), 0.6))
    h = nb.add(nb.mul(dome, 0.08), nb.mul(pol, nb.mul(cover, 0.008)))
    cav = nb.mul(nb.maprange(dome, 0.0, 0.3, 0.72, 1.0), nb.maprange(pol, 0.3, 0.7, 0.9, 1.0))
    coral = dict(col=col, h=h, r=0.76, cav=cav)
    # 溝: 砂の帯、縁には粗いれき
    groove = ta[0]
    fringe = nb.mul(nb.smooth(groove, 0.08, 0.4), nb.sub(1.0, nb.smooth(groove, 0.55, 0.9)))
    piece, pc = _pieces(nb, V, 5.0, 721.0, cover=0.48)
    rcol = nb.mix(srgb("#b4aa94"), nb.ramp(pc[0], [(0.0, srgb("#dcd6c8")), (0.5, srgb("#b3aa96")),
                                                   (0.8, srgb("#8f8775")), (1.0, srgb("#c9ada4"))]), piece)
    rub = dict(col=rcol, h=nb.mul(piece, 0.045), r=0.72, cav=nb.maprange(piece, 0.0, 1.0, 0.65, 1.0))
    out = _over(nb, coral, rub, nb.mul(fringe, 0.85))
    out = _over(nb, out, sand, nb.smooth(groove, 0.45, 0.8))
    out["zone"] = zone
    return out


def _deep_layer(nb, V, fore, sand):
    """深場: 砂地に黒っぽい根（ボミー、径 2〜4 m）が点在し、まわりにはれきが散る。斜面の下にはれきの裾"""
    body, ring, bc = _heads(nb, V, 0.16, 801.0, 0.55, rad=(0.2, 0.36))
    body2, ring2, _ = _heads(nb, V, 0.45, 803.0, 0.12, rad=(0.2, 0.32))     # 小さな根
    body = nb.math("MAXIMUM", body, body2)
    ring = nb.math("MAXIMUM", ring, ring2)
    bc1, _ = _colonies(nb, V, 809.0, 1.3, fore["zone"])
    bcol = nb.mix(bc1, srgb("#3b382e"), 0.35)
    # 斜面の下の崩れたれきの裾
    talus = nb.mul(nb.smooth(V.yw, -99.0, -103.0), nb.sub(1.0, nb.smooth(V.yw, -106.0, -112.0)))
    talus = nb.mul(talus, nb.smooth(V.noise(0.3, 3, 0.6, off=811.0), 0.45, 0.6))
    ring = nb.math("MAXIMUM", ring, nb.mul(talus, 0.8))
    piece, _ = _pieces(nb, V, 6.0, 805.0, cover=0.46)
    col = nb.mix(sand["col"], nb.mix(srgb("#a79e87"), srgb("#cfc7b4"), piece), nb.mul(ring, 0.65))
    col = nb.mix(col, bcol, body)
    h = nb.add(sand["h"], nb.mul(body, nb.add(0.12, nb.mul(V.noise(4.0, 3, 0.6, off=807.0), 0.05))))
    h = nb.add(h, nb.mul(nb.mul(ring, piece), 0.03))
    r = nb.mixf(nb.mixf(0.55, 0.7, ring), 0.76, body)
    cav = nb.mul(sand["cav"], nb.maprange(nb.mul(ring, nb.sub(1.0, piece)), 0.0, 1.0, 1.0, 0.65))
    return dict(col=col, h=h, r=r, cav=cav)


def reef_material(name, y_off):
    """タイル用のマテリアル。y_off = タイル中心の世界 Y（模様は世界座標で続く）

    帯（礁池 → れき → 礁原 → 礁斜面 → 深場）を前線マスクで重ねる。タイルの範囲で 0 の層は作らず、
    1 で覆われる層より下は省く（どちらも結果は同じなので、継ぎ目の両側で色が一致する）
    """
    y_lo, y_hi = y_off - TILE / 2, y_off + TILE / 2

    def active(key):
        return y_lo < _front_range(key)[0]

    def full(key):
        return y_hi <= _front_range(key)[1]

    def fn(nb):
        T = Torus(nb)
        V = Vol(nb, T, y_off)
        ta = nb.sep(nb.attr("Terr", out="Vector"))       # 溝, 潮だまり, 縁脚
        tb = nb.sep(nb.attr("Terr2", out="Vector"))      # AO, 窪み, 急斜面
        out = None
        if not full("rubble") and not full("flat") and not full("fore"):
            out = _lagoon_layer(nb, T, V)
        if active("rubble") and not full("flat") and not full("fore"):
            out = _over(nb, out, _rubble_layer(nb, V, tb), _front(nb, V, "rubble"))
        if active("flat") and not full("fore"):
            out = _over(nb, out, _flat_layer(nb, V, ta, tb), _front(nb, V, "flat"))
        if active("fore") or y_lo < FORE_SHOULDER[0]:
            dz = nb.smooth(T.z, -0.8, -1.6)
            sh = nb.smooth(V.yw, FORE_SHOULDER[0], FORE_SHOULDER[1])
            m = nb.math("MAXIMUM", _front(nb, V, "fore"), nb.mul(dz, sh))
            md = _front(nb, V, "deep") if active("deep") else 0.0
            sand = _sand2(nb, V, tb, md)
            fore = _fore_layer(nb, V, ta, tb, sand)
            out = _over(nb, out, fore, m) if not full("fore") else fore
            if active("deep"):
                out = _over(nb, out, _deep_layer(nb, V, fore, sand), md)
        # 地形の AO（頂点属性）と細部の陰影
        ao = nb.maprange(tb[0], 0.0, 1.0, 0.3, 1.0)
        cav = nb.mul(out["cav"], ao)
        return dict(color=out["col"], rough=out["r"], height=out["h"], height_scale=1.0, cavity=cav)

    return pbr_material(name, fn, res=2048, uv="keep", ao_samples=1, ao_strength=0.0, ao_distance=1.0)


# ---------------------------------------------------------------------------
# プレビュー（浜のタイルからつなげ、水を張って確認）
# ---------------------------------------------------------------------------
def water_material():
    """プレビュー用の海水: 赤が速く青が遅く吸収される（浅瀬はターコイズ、深場は紺碧）

    影のレイは素通し（Cycles は屈折面越しの太陽光を拾えないため。コースティクスはエンジン側）
    """
    m = bpy.data.materials.new("PreviewSea")
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


def preview_scene(objs, xs=(-120, -80, -40, 0, 40, 80, 120), water=True):
    """読み込んだタイルを並べ、浜のタイルと海を足す。戻り値: 水の箱"""
    scene = bpy.context.scene
    g = bpy.data.objects.get("PreviewGround")
    if g:
        bpy.data.objects.remove(g)
    meshes = [o for o in objs if o.type == "MESH"]
    placed = []
    for name, (yc, _, _) in TILES.items():
        o = next(m for m in meshes if m.name.startswith(name))
        o.parent = None
        o.location = (0, yc, 0)
        placed.append(o)
    for name, yc in (("BeachTerrain_Shore", 0.0), ("BeachTerrain_Flat", TILE)):
        path = os.path.join(C.MODELS_DIR, name, name + ".gltf")
        if os.path.exists(path):
            o = next(m for m in C.import_gltf(path) if m.type == "MESH")
            o.parent = None
            o.location = (0, yc, 0)
            placed.append(o)
    for dx in xs:
        if dx == 0:
            continue
        for src in placed:
            o = bpy.data.objects.new(src.name + "_dup", src.data)
            C.link_object(o)
            o.location = (dx, src.location.y, 0)

    def plane(name, size, loc, color):
        bpy.ops.mesh.primitive_plane_add(size=1.0)
        o = bpy.context.object
        o.name = name
        o.scale = (*size, 1)
        o.location = loc
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*srgb(color), 1)
        C.assign(o, mat)
        return o

    # タイルの外（沖の海底と陸側の奥）
    plane("PreviewDeepFloor", (2000, 1200), (0, -140 - 600, float(_PROFILE(-140.0)) - 0.3), "#cfc5ad")
    plane("PreviewInland", (2000, 600), (0, TILE * 1.5 + 300, 1.5), "#e3d9c2")
    sea = None
    if water:
        bpy.ops.mesh.primitive_cube_add(size=1.0)
        sea = bpy.context.object
        sea.name = "PreviewSea"
        sea.scale = (2000, 1600, 80.0)
        sea.location = (0, 4.0 - 800, -40.0)
        C.assign(sea, water_material())
    scene.cycles.volume_bounces = 1
    scene.cycles.transmission_bounces = 8
    scene.cycles.volume_step_rate = 4.0
    try:
        scene.view_settings.look = "AgX - Punchy"
    except TypeError:
        pass
    # 太陽を高めに（水中がよく見える）
    sun = bpy.data.objects.get("Sun")
    if sun:
        el, az = math.radians(58), math.radians(15)
        from mathutils import Vector
        d = Vector((math.sin(az) * math.cos(el), math.cos(az) * math.cos(el), math.sin(el)))
        sun.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        world = scene.world
        for n in world.node_tree.nodes:
            if n.type == "TEX_SKY":
                n.sun_elevation, n.sun_rotation = el, az
    return sea


def _contact_sheet(out_path, thumb=256):
    """3 タイル分のテクスチャ一覧（行 = 岸から沖の順、列 = BaseColor / Normal / ORM）"""
    from PIL import Image, ImageDraw
    rows = ("ReefTerrain_Lagoon", "ReefTerrain_Edge", "ReefTerrain_Deep")
    sheet = Image.new("RGB", (3 * thumb, len(rows) * (thumb + 18)), (24, 28, 36))
    d = ImageDraw.Draw(sheet)
    for r, name in enumerate(rows):
        mat = name.replace("ReefTerrain_", "Reef")
        for c, kind in enumerate(("BaseColor", "Normal", "ORM")):
            path = os.path.join(C.MODELS_DIR, name, f"{name}_{mat}_{kind}.png")
            if not os.path.exists(path):
                continue
            im = Image.open(path).convert("RGB").resize((thumb, thumb), Image.LANCZOS)
            sheet.paste(im, (c * thumb, r * (thumb + 18) + 18))
            d.text((c * thumb + 4, r * (thumb + 18) + 3), f"{name} {kind}", fill=(230, 230, 230))
    sheet.save(out_path, quality=90)


def _preview_extra(objs):
    scene = bpy.context.scene
    _contact_sheet(os.path.join(C.PREVIEW_DIR, "reef_terrain_textures.jpg"))
    sea = preview_scene(objs)
    cam = scene.camera
    dbg = os.environ.get("OKI_DEBUG_DIR")
    if dbg:
        # 継ぎ目の確認（真上、水なし）
        sea.hide_render = True
        cam.location = (0, -60, 150)
        C.look_at(cam, (0, -60, -10))
        cam.data.lens = 30
        C.render(os.path.join(dbg, "reef_top.jpg"), samples=16)
        sea.hide_render = False
        # 礁縁と縁脚縁溝（水面の上から斜めに）
        cam.location = (-8, -64, 7.5)
        C.look_at(cam, (6, -90, -6))
        cam.data.lens = 28
        C.render(os.path.join(dbg, "reef_crest.jpg"), samples=32)
        # ドロップオフの壁（水中から見上げる。水中は暗いので露出を上げる）
        cam.location = (4, -114, -19)
        C.look_at(cam, (0, -92, -9))
        cam.data.lens = 22
        scene.view_settings.exposure = 2.5
        C.render(os.path.join(dbg, "reef_wall.jpg"), samples=32)
        scene.view_settings.exposure = 0.0
    # 浜の上から沖を見る（ターコイズ → 紺碧）
    cam.location = (6, 45, 40)
    C.look_at(cam, (4, -58, -6))
    cam.data.lens = 26


PREVIEW = dict(cam_dir=(0.0, -1.0, 0.5), lens=35, extra=_preview_extra)


def prebake_height(obj, mat):
    """高さを先に画像へ焼き、バンプはその画像から取る

    Cycles のバンプは高さのノード群を 3 回分複製して評価するため、この大きな材質では
    SVM のスタックを超えてシェーダーが壊れる（黒くなる）。画像 1 枚の参照なら小さく収まる。
    BSDF の色・粗さの入力も外す（ベイクは _REGISTRY の出力ソケットから直接行われる）
    """
    info = C._REGISTRY[mat.name]
    res = max(64, int(info["res"] * float(os.environ.get("OKI_RES_SCALE", "1"))))
    nt = mat.node_tree
    obj.data.uv_layers.active = obj.data.uv_layers["Bake"]
    img = bpy.data.images.new(mat.name + "_Height", res, res, alpha=False, float_buffer=True)
    img.colorspace_settings.name = "Non-Color"
    outn = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL" and n.is_active_output)
    old = outn.inputs["Surface"].links[0].from_socket
    em = nt.nodes.new("ShaderNodeEmission")
    # 放射は負の値が切られるので +0.5 して焼く（バンプは差分だけを見る）
    add = nt.nodes.new("ShaderNodeMath")
    add.operation = "ADD"
    add.inputs[1].default_value = 0.5
    nt.links.new(info["out"]["height"], add.inputs[0])
    nt.links.new(add.outputs[0], em.inputs["Color"])
    nt.links.new(em.outputs[0], outn.inputs["Surface"])
    C._bake(obj, img, "EMIT", 4, mat)
    nt.nodes.remove(em)
    nt.nodes.remove(add)
    nt.links.new(old, outn.inputs["Surface"])
    uvn = nt.nodes.new("ShaderNodeUVMap")
    uvn.uv_map = "Bake"
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.interpolation = "Linear"
    tex.extension = "EXTEND"
    nt.links.new(uvn.outputs["UV"], tex.inputs["Vector"])
    bump = next(n for n in nt.nodes if n.type == "BUMP")
    nt.links.new(tex.outputs["Color"], bump.inputs["Height"])
    bsdf = info["bsdf"]
    for key in ("Base Color", "Roughness"):
        for link in list(bsdf.inputs[key].links):
            nt.links.remove(link)
    info["out"]["height"] = tex.outputs["Color"]
    return img


def build():
    out = {}
    for name, (yc, nx, ny) in TILES.items():
        o = tile_mesh(name, yc, nx, ny, arc=(name != "ReefTerrain_Lagoon"))
        mat = reef_material(name.replace("ReefTerrain_", "Reef"), yc)
        C.assign(o, mat)
        prebake_height(o, mat)
        out[name] = [o]
    return out
