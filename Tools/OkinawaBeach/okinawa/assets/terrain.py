"""白砂の浜の地形タイル（40 m × 40 m）

座標: z=0 = 平均水位, +Y = 陸側, -Y = 海側
  BeachTerrain_Shore: 陸側の砂丘・バーム → 前浜 → 水中（沖側の端で約 -3 m）
  BeachTerrain_Flat : 後浜（約 +1.6 m）。小さな砂丘と風紋

タイル接続:
  - 高さは X 方向に周期 40 m（x=-20 と x=+20 の縁が一致）
  - Flat は X, Y とも周期 40 m。Flat の -Y 縁 = Shore の +Y 縁（どちらも後浜関数 B と一致）
  - 法線はカスタム法線（解析的な勾配）で縁の陰影も一致させる
  - テクスチャは x, y をトーラス（4D ノイズ）に埋め込んで周期化 → 縁で模様も連続
"""
import math

import bpy
import numpy as np
from scipy.interpolate import PchipInterpolator

from .. import common as C
from ..common import pbr_material, srgb

TILE = 40.0          # タイルの一辺(m)
GRID = 128           # 分割数
R_T = TILE / math.tau  # トーラス埋め込みの半径（周長 = 40 m）

PREVIEW = dict(cam_dir=(0.0, -1.0, 0.5), lens=35)


# ---------------------------------------------------------------------------
# 周期ノイズ（整数波数のフーリエ級数 → 40 m で厳密に周期）
# ---------------------------------------------------------------------------
class PNoise:
    def __init__(self, seed, kmin, kmax, n=160, beta=1.0, xonly=False, aniso=1.0):
        rng = np.random.default_rng(seed)
        ks = set()
        tries = 0
        while len(ks) < n and tries < n * 50:
            tries += 1
            kx = int(rng.integers(-kmax, kmax + 1))
            ky = 0 if xonly else int(rng.integers(0, kmax + 1))
            if xonly:
                kx = abs(kx)
            if ky == 0 and kx <= 0:
                continue
            if kmin <= math.hypot(kx * aniso, ky) <= kmax:
                ks.add((kx, ky))
        k = np.array(sorted(ks), dtype=np.float64)
        self.kx, self.ky = k[:, 0], k[:, 1]
        self.amp = np.hypot(self.kx, self.ky) ** -beta
        self.amp /= math.sqrt((self.amp ** 2).sum() / 2)  # 標準偏差 ≈ 1
        self.ph = rng.uniform(0, math.tau, len(k))

    def __call__(self, x, y):
        x = np.asarray(x, np.float64)[..., None]
        y = np.asarray(y, np.float64)[..., None]
        a = math.tau / TILE * (x * self.kx + y * self.ky) + self.ph
        return (np.cos(a) * self.amp).sum(-1)


def _smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


# 岸沖方向の断面（y → z）
_PROFILE = PchipInterpolator(
    [-30, -20, -17, -14, -11, -8.5, -7, -6, -4, -2, 0, 2, 3.5, 4.6, 5.6, 7, 10, 12, 20],
    [-4.4, -3.0, -2.6, -2.15, -1.75, -1.35, -1.0, -0.62, -0.22, 0.2, 0.62, 1.08, 1.45, 1.68, 1.70, 1.6, 1.62,
     1.66, 1.7], extrapolate=True)
BERM_Y = 5.1  # バーム頂部（断面座標）

N_SHORE = PNoise(101, 1, 3, n=6, beta=1.0, xonly=True)   # 汀線のうねり
N_BAR = PNoise(102, 3, 8, n=60, beta=1.2)                  # 水中の砂州
N_MICRO = PNoise(103, 12, 40, n=260, beta=1.0)             # 細かな起伏
N_B1 = PNoise(104, 1, 4, n=30, beta=1.0)                   # 後浜の大きなうねり
N_B2 = PNoise(105, 3, 7, n=40, beta=0.5, aniso=1.6)        # 小さな砂丘（汀線方向に長い）


def backshore(x, y):
    """後浜（Flat 全体 / Shore の陸側縁）。X, Y とも周期 40 m"""
    # なだらかに連続する起伏（孤立したこぶにならないよう tanh で頭を抑える）
    dunes = np.tanh(0.5 * N_B2(x, y) + 0.15)
    return 1.62 + 0.13 * N_B1(x, y) + 0.4 * dunes + 0.03 * N_MICRO(x, y)


def _shift(x, y):
    """断面を y 方向にずらす量（ビーチカスプ + 汀線のうねり）"""
    cusp = np.exp(-((y + 1.0) / 6.0) ** 2) * 1.0 * np.cos(math.tau * 3 * x / TILE + 0.7)
    return cusp + 0.9 * N_SHORE(x, y)


def _blend(y):
    return _smoothstep(7.0, 15.0, y)


def shore(x, y):
    s = _PROFILE(y + _shift(x, y))
    s = s + 0.13 * N_BAR(x, y) * _smoothstep(-6.0, -11.0, y) + 0.03 * N_MICRO(x, y)
    w = _blend(y)
    return s * (1 - w) + backshore(x, y) * w


def _grid_mesh(name, hfn, attr_fn=None):
    n = GRID + 1
    lin = np.linspace(-TILE / 2, TILE / 2, n)
    X, Y = np.meshgrid(lin, lin)  # [iy, ix]
    Z = hfn(X, Y)
    verts = np.stack([X, Y, Z], -1).reshape(-1, 3)
    faces = []
    for iy in range(GRID):
        for ix in range(GRID):
            a = iy * n + ix
            faces.append((a, a + 1, a + n + 1, a + n))
    obj = C.mesh_object(name, verts, faces)
    me = obj.data
    # ベイク用 UV = 平面投影
    layer = me.uv_layers.new(name="Bake")
    loops_v = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get("vertex_index", loops_v)
    uv = (verts[loops_v, :2] + TILE / 2) / TILE
    layer.data.foreach_set("uv", uv.astype(np.float32).ravel())
    # 解析的な法線（中心差分）→ カスタム法線（タイル境界の陰影を一致させる）
    e = 0.05
    dx = (hfn(X + e, Y) - hfn(X - e, Y)) / (2 * e)
    dy = (hfn(X, Y + e) - hfn(X, Y - e)) / (2 * e)
    nrm = np.stack([-dx, -dy, np.ones_like(dx)], -1)
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    for p in me.polygons:
        p.use_smooth = True
    me.normals_split_custom_set_from_vertices([tuple(v) for v in nrm.reshape(-1, 3)])
    if attr_fn is not None:
        col = attr_fn(X, Y).reshape(-1, 3)
        attr = me.color_attributes.new("Terr", "FLOAT_COLOR", "POINT")
        rgba = np.concatenate([col, np.ones((len(col), 1))], -1).astype(np.float32)
        attr.data.foreach_set("color", rgba.ravel())
    me.update()
    return obj


def _shore_attr(x, y):
    """R = バーム頂部（漂着物の帯）, G = 断面座標（-20..20 → 0..1）"""
    py = y + _shift(x, y)
    wrack = np.exp(-((py - BERM_Y) / 1.1) ** 2) * (1 - _blend(y))
    return np.stack([wrack, np.clip((py + 20) / 40, 0, 1), np.zeros_like(x)], -1)


# ---------------------------------------------------------------------------
# 砂のマテリアル
# ---------------------------------------------------------------------------
class Torus:
    """(x, y) をトーラスに埋め込んだ 4D 座標（周期 40 m のノイズ用）"""

    def __init__(self, nb):
        co = nb.coord("Object")
        s = nb.sep(co)
        self.x, self.y, self.z = s[0], s[1], s[2]
        k = math.tau / TILE
        ax = nb.mul(self.x, k)
        ay = nb.mul(self.y, k)
        self.vec = nb.comb(nb.mul(nb.math("COSINE", ax), R_T), nb.mul(nb.math("SINE", ax), R_T),
                           nb.mul(nb.math("COSINE", ay), R_T))
        self.w = nb.mul(nb.math("SINE", ay), R_T)
        self.nb = nb

    def noise(self, scale, detail=3.0, rough=0.55, off=0.0, distortion=0.0, out="Fac"):
        nb = self.nb
        vec = self.vec if not off else nb.vmath("ADD", self.vec, (off, off * 1.7, off * 0.6))
        return nb.noise(vec, scale, detail, rough, distortion=distortion, dims="4D", w=self.w, out=out)

    def voronoi(self, scale, out="Distance", off=0.0):
        nb = self.nb
        n = nb.node("ShaderNodeTexVoronoi", feature="F1", distance="EUCLIDEAN", voronoi_dimensions="4D")
        vec = self.vec if not off else nb.vmath("ADD", self.vec, (off, off * 1.3, off * 0.4))
        nb.link(vec, n.inputs["Vector"])
        nb.link(self.w, n.inputs["W"])
        n.inputs["Scale"].default_value = scale
        n.inputs["Randomness"].default_value = 1.0
        return n.outputs[out]


def _dry_sand(nb, T):
    """乾いた白砂（Shore の陸側と Flat で共通 → 境界で模様が連続する）"""
    big = T.noise(0.12, 3, 0.5)
    mid = T.noise(0.9, 4, 0.55, off=11.0)
    fine = T.noise(9.0, 3, 0.6, off=23.0)
    col = nb.ramp(big, [(0.3, srgb("#dbd1ba")), (0.5, srgb("#e8e0cc")), (0.7, srgb("#efe9d9"))])
    col = nb.hsv(col, 0.5, 1.0, nb.maprange(mid, 0.3, 0.7, 0.95, 1.04))
    # 貝殻片の混じる粗い砂のムラ
    coarse = nb.smooth(T.noise(0.45, 3, 0.6, off=37.0), 0.55, 0.7)
    col = nb.mix(col, srgb("#d9cbac"), nb.mul(coarse, 0.45))
    col = nb.hsv(col, 0.5, 1.0, nb.maprange(fine, 0.35, 0.65, 0.96, 1.03))

    # 細かいサンゴ片・貝片（明るい粒）、星砂・赤いサンゴ片（まばら）
    cd = T.voronoi(20.0)
    cr = nb.sep(T.voronoi(20.0, out="Color"))
    grain = nb.smooth(cd, 0.2, 0.08)
    white = nb.mul(grain, nb.math("GREATER_THAN", cr[0], 0.72))
    pink = nb.mul(grain, nb.math("GREATER_THAN", cr[1], 0.94))
    grey = nb.mul(grain, nb.math("GREATER_THAN", cr[2], 0.975))
    col = nb.mix(col, srgb("#fbf8f1"), nb.mul(white, 0.85))
    col = nb.mix(col, nb.mix(srgb("#e3b1a2"), srgb("#d8a26e"), cr[0]), nb.mul(pink, 0.75))
    col = nb.mix(col, srgb("#a8a092"), nb.mul(grey, 0.6))
    # 大きめの貝殻・サンゴ片（数 cm）
    fd = T.voronoi(4.5, off=5.0)
    fr = nb.sep(T.voronoi(4.5, out="Color", off=5.0))
    frag = nb.mul(nb.smooth(fd, 0.13, 0.06), nb.math("GREATER_THAN", fr[0], 0.8))
    col = nb.mix(col, nb.mix(srgb("#f7f2e6"), srgb("#eadcc8"), fr[1]), frag)

    # 風紋（整数波数で周期を保つ。向きは斜め）
    k = math.tau / TILE
    warp = nb.add(nb.mul(T.noise(0.22, 2, 0.5, off=51.0), 16.0), nb.mul(T.noise(1.3, 2, 0.5, off=53.0), 3.0))
    phase = nb.add(nb.add(nb.mul(T.x, 120 * k), nb.mul(T.y, 187 * k)), warp)
    s1 = nb.math("SINE", phase)
    s2 = nb.math("SINE", nb.mul(phase, 2.0))
    ripple = nb.add(nb.mul(s1, 0.5), nb.mul(s2, 0.18))      # 非対称な断面
    wind_mask = nb.smooth(T.noise(0.07, 2, 0.5, off=63.0), 0.42, 0.58)
    wind_mask = nb.mul(wind_mask, nb.maprange(mid, 0.3, 0.7, 0.4, 1.0))

    height = nb.add(nb.mul(nb.mul(ripple, wind_mask), 0.004), nb.mul(fine, 0.004))
    height = nb.add(height, nb.mul(white, 0.0015))
    height = nb.add(height, nb.mul(frag, 0.005))
    rough = nb.maprange(nb.add(nb.mul(white, 0.2), nb.mul(frag, 0.25)), 0, 0.3, 0.92, 0.72)
    return dict(col=col, rough=rough, height=height, mid=mid, fine=fine, big=big,
                white=white, frag=frag, ripple=nb.mul(ripple, wind_mask))


def sand_material(name, shore):
    def fn(nb):
        T = Torus(nb)
        d = _dry_sand(nb, T)
        col, rough, height = d["col"], d["rough"], d["height"]
        cavity = None
        if shore:
            z = T.z
            ta = nb.sep(nb.attr("Terr"))
            wrack_band = ta[0]
            # 濡れた帯（上端をノイズで波打たせる）
            top = nb.add(0.55, nb.mul(nb.sub(T.noise(0.3, 2, 0.5, off=71.0), 0.5), 0.35))
            dz_top = nb.sub(z, top)
            edge = nb.smooth(dz_top, 0.0, -0.06)                      # 濡れた範囲の上端（くっきり）
            depth = nb.smooth(dz_top, -0.05, nb.mul(top, -1.0))       # 上端 0 → 水際 1
            wet = nb.mul(edge, nb.add(0.45, nb.mul(depth, 0.55)))
            under = nb.smooth(z, -0.25, -0.75)
            wet_col = nb.hsv(nb.mix(col, srgb("#c2b69c"), 0.3), 0.5, 1.0, 0.7)
            # 上端の細い濡れ線
            wline = nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.add(dz_top, 0.01)), 0.03, 0.0),
                           nb.smooth(T.noise(0.8, 2, 0.5, off=77.0), 0.3, 0.5))
            # 水中: 暖かく澄んだ白砂（粒の明暗も弱い）
            uw_col = nb.ramp(d["big"], [(0.3, srgb("#e2d7bf")), (0.5, srgb("#eadfc6")), (0.7, srgb("#efe6cf"))])
            uw_col = nb.mix(uw_col, srgb("#f7f2e4"), nb.mul(d["white"], 0.6))
            uw_col = nb.mix(uw_col, srgb("#efe4d0"), nb.mul(d["frag"], 0.8))
            # 浜の汀線の筋（遡上限界の線）
            swash = None
            for zk, off in ((0.22, 81.0), (0.42, 83.0), (0.66, 87.0)):
                n = T.noise(0.4, 2, 0.5, off=off)
                dz = nb.math("ABSOLUTE", nb.sub(nb.sub(z, zk), nb.mul(nb.sub(n, 0.5), 0.16)))
                line = nb.mul(nb.smooth(dz, 0.02, 0.004), nb.smooth(T.noise(0.6, 2, 0.5, off=off + 1), 0.4, 0.55))
                swash = line if swash is None else nb.math("MAXIMUM", swash, line)
            grit = nb.smooth(T.noise(40.0, 2, 0.5, off=91.0), 0.45, 0.65)
            swash_col = nb.mix(srgb("#c3baa5"), srgb("#8e8674"), nb.mul(grit, 0.6))
            col = nb.mix(col, wet_col, wet)
            col = nb.mix(col, srgb("#a69d88"), nb.mul(wline, 0.35))
            col = nb.mix(col, swash_col, nb.mul(swash, 0.55))
            col = nb.mix(col, uw_col, under)
            # 打ち上げられた海藻・漂着物（バーム頂部）
            clump = nb.smooth(T.noise(2.2, 4, 0.6, off=101.0), 0.56, 0.66)
            patch = nb.smooth(T.noise(0.28, 2, 0.5, off=103.0), 0.4, 0.6)
            wrack = nb.mul(nb.mul(nb.smooth(wrack_band, 0.3, 0.8), patch), clump)
            weed = nb.mix(srgb("#8a7c58"), srgb("#b3a57e"), T.noise(9.0, 2, 0.5, off=107.0))
            col = nb.mix(col, weed, nb.mul(wrack, 0.75))
            bits = nb.mul(nb.smooth(T.voronoi(9.0, off=9.0), 0.12, 0.05), nb.smooth(wrack_band, 0.3, 0.8))
            col = nb.mix(col, srgb("#f4efe3"), nb.mul(bits, nb.mul(patch, 0.7)))

            # 水中の砂漣（汀線に平行、先端の尖った断面）
            warp = nb.add(nb.mul(T.noise(0.22, 3, 0.5, off=121.0), 10.0), nb.mul(T.noise(1.4, 2, 0.5, off=123.0), 1.6))
            ph = nb.add(nb.mul(T.y, math.tau / 0.34), warp)
            rip = nb.sub(1.0, nb.math("ABSOLUTE", nb.math("SINE", nb.mul(ph, 0.5))))
            rip_mask = nb.mul(nb.smooth(z, -0.45, -1.3), nb.maprange(T.noise(0.15, 2, 0.5, off=127.0), 0.3, 0.6, 0.45, 1.0))
            # 砂漣の谷には細かい貝片がたまり少し明るい
            col = nb.mix(col, srgb("#f3ecdc"), nb.mul(nb.mul(nb.sub(1.0, rip), rip_mask), 0.12))

            dry_h = nb.mul(height, nb.sub(1.0, nb.math("MAXIMUM", wet, under)))
            height = nb.add(dry_h, nb.mul(nb.mul(rip, rip_mask), 0.014))
            height = nb.add(height, nb.mul(swash, 0.002))
            height = nb.add(height, nb.mul(wrack, 0.012))
            height = nb.add(height, nb.mul(nb.mul(d["fine"], nb.math("MAXIMUM", wet, under)), 0.002))
            rough = nb.mixf(rough, nb.mixf(0.6, 0.35, depth), edge)
            rough = nb.mixf(rough, 0.5, under)
            rough = nb.mixf(rough, 0.85, wrack)
            cavity = nb.maprange(nb.mul(nb.sub(1.0, rip), rip_mask), 0.0, 1.0, 1.0, 0.9)
        else:
            cavity = nb.maprange(d["ripple"], -0.7, 0.7, 0.95, 1.0)
        return dict(color=col, rough=rough, height=height, height_scale=1.0, cavity=cavity)

    return pbr_material(name, fn, res=2048, uv="keep", ao_samples=16, ao_distance=1.0, ao_strength=0.5)


# ---------------------------------------------------------------------------
# プレビュー（タイルを並べ、水を張った状態で確認）
# ---------------------------------------------------------------------------
def _water_material():
    mat = bpy.data.materials.new("PreviewWater")
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Transmission Weight"].default_value = 1.0
    bsdf.inputs["Roughness"].default_value = 0.03
    bsdf.inputs["IOR"].default_value = 1.33
    vol = nt.nodes.new("ShaderNodeVolumeAbsorption")
    vol.inputs["Color"].default_value = (*srgb("#4fd6d0"), 1.0)
    vol.inputs["Density"].default_value = 0.35
    out = nt.nodes["Material Output"]
    nt.links.new(vol.outputs[0], out.inputs["Volume"])
    return mat


def _preview_extra(objs):
    import os
    scene = bpy.context.scene
    g = bpy.data.objects.get("PreviewGround")
    if g:
        bpy.data.objects.remove(g)
    meshes = [o for o in objs if o.type == "MESH"]
    shore = next(o for o in meshes if o.name.startswith("BeachTerrain_Shore"))
    flat = next(o for o in meshes if o.name.startswith("BeachTerrain_Flat"))
    for o in meshes:
        o.parent = None
    shore.location = (0, 0, 0)
    flat.location = (0, TILE, 0)
    # 左右に複製してタイルの継ぎ目を確認
    for dx in [k * TILE for k in (-4, -3, -2, -1, 1, 2, 3, 4)]:
        for src in (shore, flat):
            o = bpy.data.objects.new(src.name + "_dup", src.data)
            C.link_object(o)
            o.location = (dx, src.location.y, 0)
    # 沖の海底と、陸側の奥の砂（タイルの外側を埋める）
    def plane(name, size, loc, mat):
        bpy.ops.mesh.primitive_plane_add(size=1.0)
        o = bpy.context.object
        o.name = name
        o.scale = (*size, 1)
        o.location = loc
        C.assign(o, mat)
        return o

    sand = bpy.data.materials.new("PreviewSand")
    sand.use_nodes = True
    sand.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*srgb("#e3d9c2"), 1)
    plane("PreviewSeaFloor", (600, 400), (0, -TILE / 2 - 198.0, -2.95), sand)
    plane("PreviewInland", (600, 400), (0, TILE * 1.5 + 200, 1.5), sand)
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    water = bpy.context.object
    water.name = "PreviewWater"
    water.scale = (600, 420, 5.0)
    water.location = (0, 2.0 - 210, -2.5)
    C.assign(water, _water_material())
    scene.cycles.volume_bounces = 1
    cam = scene.camera
    # 確認用: 真上から（継ぎ目）と水際の近景
    dbg = os.environ.get("OKI_DEBUG_DIR")
    if dbg:
        water.hide_render = True
        cam.location = (0, 20, 170)
        C.look_at(cam, (0, 20, 0))
        cam.data.lens = 35
        C.render(os.path.join(dbg, "terrain_top.jpg"), samples=16)
        water.hide_render = False
        cam.location = (6, -12, 3.2)
        C.look_at(cam, (-2, 2, 0.4))
        C.render(os.path.join(dbg, "terrain_close.jpg"), samples=24)
    cam.location = (-26, -48, 13)
    C.look_at(cam, (4, 6, 0))
    cam.data.lens = 30
    scene.view_settings.exposure = -0.6


PREVIEW = dict(cam_dir=(0.0, -1.0, 0.5), lens=35, extra=_preview_extra)


def build():
    s = _grid_mesh("BeachTerrain_Shore", shore, _shore_attr)
    C.assign(s, sand_material("BeachSandShore", True))
    f = _grid_mesh("BeachTerrain_Flat", backshore)
    C.assign(f, sand_material("BeachSandFlat", False))
    return {"BeachTerrain_Shore": [s], "BeachTerrain_Flat": [f]}
