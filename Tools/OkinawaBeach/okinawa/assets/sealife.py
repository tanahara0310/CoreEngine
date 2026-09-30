"""海の生き物（礁池の底生生物とウミガメ）6 バリエーション

  GiantClam          : ヒメシャコガイ（Tridacna maxima, 長さ ~30 cm）。灰白色の厚い殻に深い放射状の襞と鱗片、
                       開いた殻口から青〜ターコイズ〜緑の外套膜（黒い斑点と波状の縞、出水管の穴）
  SeaCucumber        : クロナマコ（Holothuria atra, ~30 cm）。黒く柔らかい円筒形、腹側は平ら、背に砂をまぶす
  SeaUrchin          : ガンガゼ（Diadema setosum）。殻径 ~7 cm の黒い殻に 15〜25 cm の細長い棘、
                       肛門のオレンジの輪と青い斑点
  BlueStarfish       : アオヒトデ（Linckia laevigata, 差し渡し ~30 cm）。円柱状の 5 本の腕、鮮やかな青、細かい顆粒
  Anemone_Clownfish  : センジュイソギンチャク（Heteractis magnifica, ~50 cm）＋ 上に浮かぶカクレクマノミ 3 匹
  SeaTurtle          : アオウミガメ（甲長 ~1.0 m）。前ヒレを振り上げた遊泳姿勢。
                       骨 13 本（体・首・頭・前ヒレ 3 節 x2・後ろヒレ 2 節 x2）とループする泳ぎ "Swim"（3 秒）付き

有機的な形は符号付き距離関数 (SDF) を格子で評価して Surface Nets でメッシュ化する（shisa と同じ仕組み）。
細長い部品（棘・触手・ヒレ）はパラメトリックに組む。模様は頂点属性（部位ごとのローカル座標）から
シェーダーで描いてベイクする。水が赤を吸収するので、ベースカラーはやや明るめ。

原点: 底生生物は接地点（z=0, 少し砂に埋める）。ウミガメは体の中心で頭が +X（中層に置く）。
"""
import math
import os
import random

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import pbr_material, srgb
from . import shisa
from .adan import MeshAcc
from .props import _pack_tubes
from ._timber import unwrap

UP = Vector((0, 0, 1))


# ---------------------------------------------------------------------------
# 共通の補助
# ---------------------------------------------------------------------------
def _np_verts(me):
    a = np.empty(len(me.vertices) * 3, np.float64)
    me.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)


def _set_verts(me, V):
    me.vertices.foreach_set("co", np.asarray(V, np.float32).ravel())
    me.update()


def _set_attr(obj, name, vals):
    """頂点ごとの値 (N, 1..4) を POINT の FLOAT_COLOR 属性に書く（シェーダーの模様用）"""
    me = obj.data
    vals = np.asarray(vals, np.float32)
    if vals.ndim == 1:
        vals = vals[:, None]
    arr = np.ones((len(me.vertices), 4), np.float32)
    arr[:, :vals.shape[1]] = vals
    old = me.color_attributes.get(name)
    if old is not None:
        me.color_attributes.remove(old)
    a = me.color_attributes.new(name, "FLOAT_COLOR", "POINT")
    a.data.foreach_set("color", arr.ravel())
    return a


def _smooth01(x, a, b):
    t = np.clip((np.asarray(x, np.float64) - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def _tris(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def _sdf_mesh(name, grid, smooth=(0.4, 2), remesh=False):
    """SDF 格子 → Surface Nets → 重複頂点の除去 → 軽いスムーズ。
    remesh: 薄い部分で Surface Nets が作る非多様体の辺や小さな穴を、同じ解像度のボクセルリメッシュで閉じる"""
    obj = shisa.mesh_from_sdf(name, grid)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=grid.h * 0.05)
    bm.to_mesh(obj.data)
    bm.free()
    _fill_holes(obj)
    if remesh:
        m = obj.modifiers.new("Remesh", "REMESH")
        m.mode = "VOXEL"
        m.voxel_size = grid.h
        m.adaptivity = 0.0
        C.apply_modifiers(obj)
    if smooth:
        geo.smooth_mod(obj, factor=smooth[0], iterations=smooth[1])
    return obj


def _finish_sdf(obj, target_tris, uv_angle=62.0, disp=None, smooth_angle=None):
    """高密度のうちに UV を展開 → 細部の凹凸 → UV 境界を保ってデシメート（shisa と同じ手順）"""
    shisa.unwrap_smooth_proxy(obj, uv_angle)
    if disp is not None:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        geo.displace_along_normals(bm, disp)
        bm.to_mesh(obj.data)
        bm.free()
    m = obj.modifiers.new("Decimate", "DECIMATE")
    m.ratio = min(1.0, target_tris / max(_tris(obj), 1))
    m.use_collapse_triangulate = True
    m.delimit = {"UV"}
    C.apply_modifiers(obj)
    _fill_holes(obj)
    C.set_smooth(obj, True, angle=smooth_angle)
    return obj


def _fill_holes(obj):
    """デシメートが非多様体の所に残すひれ状の面や小さな穴を片付ける（エンジンは裏面カリングで穴が抜けて見える）。
    境界辺と非多様体辺（3 面以上）の両方に接する面を消してから、残った穴を塞ぐ"""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    n0 = sum(1 for e in bm.edges if e.is_boundary)
    for _ in range(6):
        bad = [f for f in bm.faces if any(e.is_boundary for e in f.edges)
               and any(len(e.link_faces) > 2 for e in f.edges)]
        if not bad:
            break
        bmesh.ops.delete(bm, geom=bad, context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context="VERTS")
    holes = [e for e in bm.edges if e.is_boundary]
    if holes:
        ret = bmesh.ops.holes_fill(bm, edges=holes, sides=24)
        bmesh.ops.triangulate(bm, faces=ret["faces"])
    n1 = sum(1 for e in bm.edges if e.is_boundary)
    if n0 or n1:
        print(f"  [{obj.name}] open edges {n0} -> {n1}")
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def _weld(obj, dist=1e-5):
    """geo.tube_along の周の継ぎ目（同じ位置の重複頂点）を溶接して、法線の段差を無くす"""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=dist)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    return obj


def _join_repack(objs, name, margin=0.004):
    """別々に展開した部品を結合し、Bake UV の島を同じ密度で 1 枚に詰め直す"""
    bpy.context.view_layer.update()
    obj = C.join(objs, name)
    me = obj.data
    me.uv_layers.active = me.uv_layers["Bake"]
    C._select_only([obj], obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.average_islands_scale()
    try:
        bpy.ops.uv.pack_islands(udim_source="CLOSEST_UDIM", rotate=True, margin=margin, shape_method="CONCAVE")
    except TypeError:
        bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")
    return obj


def _field_grid(bmin, bmax, h):
    """SDFGrid と、その格子点の座標軸"""
    g = shisa.SDFGrid(bmin, bmax, h)
    axes = [g.o[d] + np.arange(g.n[d]) * h for d in range(3)]
    return g, axes


def _ellipsoid_R(ax, ay, az):
    """列がローカル軸の 3x3（shisa.sd_ellipsoid 用）"""
    return np.column_stack([np.asarray(ax, float), np.asarray(ay, float), np.asarray(az, float)])


def _nrm(v):
    v = np.asarray(v, np.float64)
    return v / max(np.linalg.norm(v), 1e-12)


def _seam_unwrap(obj, seam_edges, margin=0.004):
    """指定した辺 (頂点番号の組) をシームにして展開し Bake UV に詰める"""
    s = {tuple(sorted(e)) for e in seam_edges}
    unwrap(obj, seam_angle=179.0, margin=margin,
           extra_seams=lambda e: tuple(sorted((e.verts[0].index, e.verts[1].index))) in s)


def _ring_mesh(name, rows, uvs=None):
    """rows: 各行が (m,3) の環か 1 点（極）。隣り合う行を四角形（極は三角形の扇）でつないだ閉じた面。
    行は下→上（または前→後ろ）、環は反時計回りに並べると面が外を向く"""
    verts, idx = [], []
    for r in rows:
        r = np.atleast_2d(np.asarray(r, np.float64))
        idx.append(list(range(len(verts), len(verts) + len(r))))
        verts += [tuple(p) for p in r]
    faces = []
    for a, b in zip(idx[:-1], idx[1:]):
        if len(a) == 1 and len(b) > 1:
            m = len(b)
            faces += [(a[0], b[(j + 1) % m], b[j]) for j in range(m)]
        elif len(b) == 1 and len(a) > 1:
            m = len(a)
            faces += [(a[j], a[(j + 1) % m], b[0]) for j in range(m)]
        else:
            m = len(a)
            faces += [(a[j], a[(j + 1) % m], b[(j + 1) % m], b[j]) for j in range(m)]
    return C.mesh_object(name, verts, faces, uvs), idx


def _signed_volume(obj):
    me = obj.data
    me.calc_loop_triangles()
    V = _np_verts(me)
    T = np.array([t.vertices[:] for t in me.loop_triangles])
    a, b, c = V[T[:, 0]], V[T[:, 1]], V[T[:, 2]]
    return float(np.einsum("ij,ij->i", a, np.cross(b, c)).sum() / 6.0)


def _outward(obj):
    """閉じたメッシュの面が内向きなら反転する（エンジンは常に裏面カリング）"""
    if _signed_volume(obj) < 0:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        print(f"  [flip] {obj.name}")
    return obj


# ---------------------------------------------------------------------------
# シャコガイ（ヒメシャコガイ Tridacna maxima）
# ---------------------------------------------------------------------------
# 座標: X = 殻の長軸、Y = 左右の殻、Z = 上（殻口が上、蝶番は砂の中）
CL_ZU = -0.074      # 殻頂（蝶番）の高さ
CL_B = 0.19         # 殻頂から殻縁（真上）までの距離
CL_W = 0.068        # 殻の膨らみ（片側）
CL_G = 0.0175       # 殻口の開き（片側）
CL_F = 0.021        # 襞の深さ（殻縁で）
CL_NF = 15.0        # 襞の角周波数（fan 角 1 rad あたり）
CL_PH = 0.35
CL_OV = 0.016       # 外套膜が殻縁からはみ出す量
CL_MT = 0.0085      # 外套膜の厚み（半分）
CL_M0 = -0.0015     # 外套膜の断面中心（殻縁からの高さ）
CL_ZCUT = -0.028    # 砂に埋める深さ（底を平らに切る）
CL_SIPHON = -0.40   # 出水管の fan 角
CL_SLIT = (0.02, 0.5)   # 入水管（スリット）の fan 角の範囲


def _cl_a(th):
    return 0.15 + 0.012 * np.tanh(3.0 * th)      # 後端（+X）がやや長い


def _cl_R(th):
    """殻頂から殻縁までの距離（楕円。襞による波打ちは呼び出し側で掛ける）"""
    return 1.0 / np.sqrt((np.sin(th) / _cl_a(th)) ** 2 + (np.cos(th) / CL_B) ** 2)


def _cl_wave(th, side=1.0):
    """襞の波（+1 = その殻の外への山）。山は丸く広く、谷は狭い。襞ごとに少し不揃い。
    +Y 殻の山と -Y 殻の谷が噛み合う（殻口がジグザグになる）"""
    ph = CL_NF * th + CL_PH + (0.0 if side > 0 else math.pi)
    f = 2.0 * ((1.0 + np.cos(ph)) * 0.5) ** 0.6 - 1.0
    return f * (1.0 + 0.12 * np.sin(3.1 * th + 0.7))


def _cl_zig(th):
    """殻口の中心線のずれ（両殻の山谷の平均）"""
    return 0.5 * (_cl_wave(th, 1.0) - _cl_wave(th, -1.0))


def _cl_bulge(s, th):
    s1 = np.clip(s, 0.0, 1.0)
    return CL_W * np.sin(np.pi * s1 ** 0.75) * np.cos(th * 0.8) ** 0.5 + CL_G * s1 ** 2


def _cl_valve_y(s, th, side):
    """殻の外面の y（side=+1: +Y 殻, -1: -Y 殻）。襞は殻頂から殻縁へ深くなる"""
    fold = CL_F * np.clip(s, 0.0, 1.3) ** 0.9 * _cl_wave(th, side)
    return side * (_cl_bulge(s, th) + fold)


def _cl_mantle_frame(th):
    """外套膜の帯: 中心線の y と半幅（襞に合わせてジグザグ、縁は細かく波打つ）"""
    yc = (CL_F - 0.45 * CL_OV) * _cl_zig(th)
    taper = np.clip((1.36 - np.abs(th)) / 0.32, 0.0, 1.0) ** 0.5
    half = (CL_G + 0.55 * CL_OV) * (1.0 + 0.08 * np.sin(th * 31.0 + 1.3) + 0.05 * np.sin(th * 57.0)) * taper
    return yc, np.maximum(half, 1e-4)


def _cl_fan(x, z):
    dz = z - CL_ZU
    th = np.arctan2(x, dz)
    rho = np.sqrt(x * x + dz * dz)
    return th, rho


def _cl_parts(x, y, z, h):
    """殻と外套膜の近似距離（x, z は (nx,1,nz)、y は (1,ny,1) の放送形）"""
    th, rho = _cl_fan(x, z)
    R = _cl_R(th) * (1.0 + 0.02 * _cl_zig(th))
    s = rho / R
    yp = _cl_valve_y(s, th, 1.0)
    ym = _cl_valve_y(s, th, -1.0)
    # 殻面の傾きで割って距離らしくする（襞の斜面で値が大きくなりすぎないように）
    norm = []
    for yy in (yp, ym):
        gx = np.gradient(yy, h, axis=0)
        gz = np.gradient(yy, h, axis=2)
        norm.append(np.sqrt(1.0 + gx * gx + gz * gz))
    d_shell = np.maximum(np.maximum((y - yp) / norm[0], (ym - y) / norm[1]), rho - R)
    # 外套膜: 殻縁に沿った超楕円断面の帯
    yc, half = _cl_mantle_frame(th)
    e = rho - R
    qy = np.abs(y - yc) / half
    # 外套膜の上面はゆるく波打つ
    e0 = CL_M0 + 0.0022 * np.sin(th * 41.0 + 0.5) * np.cos(np.clip(qy, 0, 1) * 1.2)
    qe = np.abs(e - e0) / CL_MT
    d_mant = ((qy ** 4 + qe ** 4) ** 0.25 - 1.0) * np.minimum(half, CL_MT)
    return d_shell, d_mant


def _cl_surface_point(th, s, side):
    """殻の外面上の点と外向き法線（鱗片を置く位置）"""
    def pt(t, ss):
        R = float(_cl_R(np.array(t)) * (1.0 + 0.02 * _cl_zig(np.array(t))))
        rho = ss * R
        return np.array([rho * math.sin(t), float(_cl_valve_y(np.array(ss), np.array(t), side)),
                         CL_ZU + rho * math.cos(t)])
    p = pt(th, s)
    e = 1e-3
    dt = pt(th + e, s) - pt(th - e, s)
    ds = pt(th, s + e) - pt(th, s - e)
    n = _nrm(np.cross(dt, ds)) * side
    if n[1] * side < 0:
        n = -n
    return p, n, _nrm(dt), _nrm(ds)


def _clam_sdf(h=0.0016):
    g, (ax, ay, az) = _field_grid((-0.18, -0.13, CL_ZCUT - 0.004), (0.18, 0.13, 0.17), h)
    d_shell, d_mant = _cl_parts(ax[:, None, None], ay[None, :, None], az[None, None, :], h)
    g.F = shisa.smin(d_shell, d_mant, 0.004)
    del d_shell, d_mant
    # 殻の鱗片: 襞の山に沿って、殻縁に近い側に屋根瓦状に並ぶ薄い板
    rnd = random.Random(5)
    for side in (1.0, -1.0):
        base = 0.0 if side > 0 else math.pi
        for k in range(-5, 6):
            th0 = (2 * math.pi * k - CL_PH - base) / CL_NF
            if abs(th0) > 1.2:
                continue
            for s in np.arange(0.46, 0.975, 0.036):
                grow = (s - 0.42) / 0.55
                # 山の上に 1〜2 枚並ぶ屋根瓦状の低い鱗片
                offs = (0.0,) if s < 0.66 else (-0.045, 0.045)
                for o in offs:
                    th = th0 + o + rnd.uniform(-0.008, 0.008)
                    p, n, t, u = _cl_surface_point(th, s + rnd.uniform(-0.006, 0.006), side)
                    if p[2] < 0.006:
                        continue
                    lean = math.radians(50)
                    n2 = n * math.cos(lean) + u * math.sin(lean)
                    u2 = -n * math.sin(lean) + u * math.cos(lean)
                    c = p + n2 * 0.0006
                    wid = (0.0085 if len(offs) == 1 else 0.0062) + 0.004 * grow
                    radii = (wid * rnd.uniform(0.85, 1.1), 0.0021 + 0.0013 * grow, 0.0024)
                    g.ellipsoid(c, radii, k=0.0018, R=_ellipsoid_R(t, n2, u2))
    # 出水管: 外套膜の上に立つ短い筒と、その穴
    th = CL_SIPHON
    R = float(_cl_R(np.array(th)) * (1 + 0.02 * _cl_zig(np.array(th))))
    yc, half = _cl_mantle_frame(np.array(th))
    radial = np.array([math.sin(th), 0.0, math.cos(th)])
    top = np.array([0.0, float(yc), CL_ZU]) + radial * (R + CL_M0 + CL_MT)
    g.cone(top - radial * 0.008, top + radial * 0.0065, 0.013, 0.0092, k=0.007)
    g.cone(top - radial * 0.012, top + radial * 0.03, 0.0048, 0.0056, k=0.002, op="sub")
    # 入水管: 外套膜の中央を走るスリット
    pts, rr = [], []
    for t in np.linspace(*CL_SLIT, 12):
        R = float(_cl_R(np.array(t)) * (1 + 0.02 * _cl_zig(np.array(t))))
        yc, half = _cl_mantle_frame(np.array(t))
        radial = np.array([math.sin(t), 0.0, math.cos(t)])
        pts.append(np.array([0.0, float(yc), CL_ZU]) + radial * (R + CL_M0 + CL_MT + 0.0005))
        f = (t - CL_SLIT[0]) / (CL_SLIT[1] - CL_SLIT[0])
        rr.append(0.001 + 0.0032 * math.sin(math.pi * f) ** 0.6)
    g.tube(pts, rr, k=0.002, op="sub")
    # 砂に埋まる底を平らに切る
    zz = az[None, None, :]
    g.F = np.maximum(g.F, CL_ZCUT - zz)
    return g


def _clam_attrs(obj):
    """ClamA = (fan 角, 殻頂からの相対距離 s, 外套膜の横断位置 v), ClamB = (外套膜マスク, 出水管距離, スリット距離)"""
    V = _np_verts(obj.data)
    x, y, z = V[:, 0], V[:, 1], V[:, 2]
    th, rho = _cl_fan(x, z)
    R = _cl_R(th) * (1.0 + 0.02 * _cl_zig(th))
    s = rho / R
    yc, half = _cl_mantle_frame(th)
    v = (y - yc) / np.maximum(half, 1e-3)
    # 距離場の成分から、外套膜側の面かどうかを決める（格子の代わりに頂点で評価）
    yp = _cl_valve_y(s, th, 1.0)
    ym = _cl_valve_y(s, th, -1.0)
    d_shell = np.maximum(np.maximum(y - yp, ym - y), rho - R)
    qy = np.abs(y - yc) / np.maximum(half, 1e-4)
    e0 = CL_M0 + 0.0022 * np.sin(th * 41.0 + 0.5) * np.cos(np.clip(qy, 0, 1) * 1.2)
    qe = np.abs((rho - R) - e0) / CL_MT
    d_mant = ((qy ** 4 + qe ** 4) ** 0.25 - 1.0) * np.minimum(half, CL_MT)
    mask = _smooth01(d_shell - d_mant, -0.0015, 0.0025)
    # 出水管の中心からの距離、スリット（中心線）からの距離
    ths = CL_SIPHON
    Rs = float(_cl_R(np.array(ths)) * (1 + 0.02 * _cl_zig(np.array(ths))))
    ycs, _ = _cl_mantle_frame(np.array(ths))
    rad = np.array([math.sin(ths), 0.0, math.cos(ths)])
    sip = np.array([0.0, float(ycs), CL_ZU]) + rad * (Rs + CL_M0 + CL_MT)
    rel = V - sip
    d_sip = np.linalg.norm(rel - np.outer(rel @ rad, rad), axis=1)
    inslit = (_smooth01(th, CL_SLIT[0] - 0.02, CL_SLIT[0] + 0.03) *
              (1 - _smooth01(th, CL_SLIT[1] - 0.03, CL_SLIT[1] + 0.02)))
    d_slit = np.abs(y - yc) + (1.0 - inslit) * 0.05
    _set_attr(obj, "ClamA", np.stack([th, s, v], -1))
    _set_attr(obj, "ClamB", np.stack([mask, d_sip, d_slit], -1))


def clam_material():
    def fn(nb):
        a = nb.sep(nb.attr("ClamA"))
        th, s, v = a[0], a[1], a[2]
        b = nb.sep(nb.attr("ClamB"))
        mask, dsip, dslit = b[0], b[1], b[2]
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        geom = nb.node("ShaderNodeNewGeometry")
        aon = nb.node("ShaderNodeAmbientOcclusion", samples=16, only_local=True)
        aon.inputs["Distance"].default_value = 0.02
        crev = nb.smooth(aon.outputs["AO"], 0.95, 0.5)      # 1 = 奥まった所
        mant = nb.smooth(mask, 0.35, 0.65)

        # --- 殻: 灰白色。成長線（同心円）と細い放射肋、下の方は藻と石灰藻で汚れる ---
        big = nb.noise(co, 9.0, 4, 0.6)
        fine = nb.noise(co, 140.0, 3, 0.6)
        gl_w = nb.noise(co, 30.0, 2, 0.5)
        gl_n = nb.noise(nb.comb(nb.mul(th, 6.0), nb.mul(s, 40.0), 0.0), 2.0, 3, 0.6)
        growth = nb.math("FRACT", nb.add(nb.mul(s, 42.0), nb.add(nb.mul(gl_w, 2.0), nb.mul(gl_n, 1.5))))
        gline = nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(growth, 0.5)), 0.5, 0.3),
                       nb.smooth(nb.noise(co, 25.0, 2, 0.5), 0.3, 0.5))
        gband = nb.noise(nb.comb(nb.mul(s, 9.0), 0.3, 0.7), 1.0, 3, 0.5)
        riblet = nb.mul(nb.noise(nb.comb(nb.mul(th, 55.0), nb.mul(s, 3.0), 0.0), 1.0, 3, 0.5),
                        nb.smooth(nb.noise(co, 8.0, 2, 0.5), 0.4, 0.6))
        shell = nb.ramp(big, [(0.25, srgb("#aaa396")), (0.5, srgb("#c7c1b3")), (0.78, srgb("#ddd8cb"))])
        shell = nb.mix(shell, srgb("#e9e6dc"), nb.mul(nb.smooth(s, 0.7, 0.98), 0.5))     # 若い殻縁は白い
        pt = geom.outputs["Pointiness"]
        shell = nb.mix(shell, srgb("#f2efe7"), nb.mul(nb.smooth(pt, 0.52, 0.6), 0.7))      # 鱗片の縁
        shell = nb.mix(shell, srgb("#8e8676"), nb.mul(gline, 0.4))
        shell = nb.hsv(shell, 0.5, 1.0, nb.maprange(gband, 0.3, 0.7, 0.9, 1.06))
        shell = nb.mix(shell, srgb("#6a6553"), nb.mul(crev, 0.7))
        # 石灰藻（桃紫）と緑褐色の藻の膜
        cor = nb.smooth(nb.noise(co, 11.0, 5, 0.65, distortion=0.4), 0.6, 0.7)
        shell = nb.mix(shell, nb.mix(srgb("#b78796"), srgb("#9b6f86"), fine), nb.mul(cor, 0.75))
        alg = nb.mul(nb.smooth(z, 0.05, 0.0), nb.smooth(nb.noise(co, 6.0, 4, 0.6), 0.35, 0.55))
        shell = nb.mix(shell, srgb("#6f7552"), nb.mul(alg, 0.65))
        shell = nb.mix(shell, srgb("#cbbf9f"), nb.smooth(z, 0.012, -0.01))                # 砂際
        shell = nb.hsv(shell, 0.5, 1.0, nb.maprange(fine, 0.3, 0.7, 0.92, 1.06))

        # --- 外套膜: 青〜ターコイズ〜緑。縁に沿う途切れた暗い波縞、黒斑、縁の眼点、虹色のきらめき ---
        av = nb.math("ABSOLUTE", v)
        warp = nb.noise(nb.comb(nb.mul(th, 6.0), nb.mul(v, 1.5), 0.0), 3.0, 4, 0.6)
        mco = nb.comb(nb.mul(th, 0.2), nb.mul(v, 0.035), nb.mul(s, 0.05))
        patch = nb.noise(co, 20.0, 4, 0.6, distortion=0.8)
        mcol = nb.ramp(av, [(0.0, srgb("#1e4ea8")), (0.3, srgb("#2262c8")), (0.62, srgb("#2a86de")),
                            (0.88, srgb("#2a9fd6")), (1.0, srgb("#33b3b4"))])
        mcol = nb.mix(mcol, srgb("#3aa77e"), nb.mul(nb.smooth(patch, 0.58, 0.72), 0.7))   # 緑の斑
        mcol = nb.mix(mcol, srgb("#16408f"), nb.mul(nb.smooth(patch, 0.42, 0.3), 0.55))
        # 縁に平行な波縞（ところどころ途切れる）
        band = nb.math("SINE", nb.add(nb.mul(av, 30.0), nb.mul(warp, 6.0)))
        seg = nb.smooth(nb.noise(nb.comb(nb.mul(th, 24.0), nb.mul(av, 4.0), 1.7), 1.0, 3, 0.5), 0.42, 0.58)
        stripe = nb.mul(nb.mul(nb.smooth(band, 0.62, 0.93), seg), nb.smooth(av, 0.12, 0.3))
        stripe = nb.mul(stripe, nb.smooth(av, 0.9, 0.76))
        mcol = nb.mix(mcol, srgb("#0c1f4a"), nb.mul(stripe, 0.75))
        # 黒い斑点（大小）
        spots = None
        for sc, thr in ((95.0, 0.6), (190.0, 0.5)):
            sp_d = nb.voronoi(co, sc)
            sp_r = nb.sep(nb.voronoi(co, sc, out="Color"))[0]
            sp = nb.mul(nb.smooth(sp_d, 0.3, 0.17), nb.math("GREATER_THAN", sp_r, thr))
            spots = sp if spots is None else nb.math("MAXIMUM", spots, sp)
        mcol = nb.mix(mcol, srgb("#0a1734"), nb.mul(spots, nb.mul(nb.smooth(av, 0.92, 0.75), 0.9)))
        # 虹色のきらめき（虹色素胞の細かい点）
        sk_d = nb.voronoi(co, 420.0)
        sk_r = nb.sep(nb.voronoi(co, 420.0, out="Color"))[1]
        sparkle = nb.mul(nb.smooth(sk_d, 0.3, 0.1), nb.math("GREATER_THAN", sk_r, 0.5))
        mcol = nb.mix(mcol, srgb("#9fe9ff"), nb.mul(sparkle, 0.45))
        # 縁の眼点（小さな淡い点が縁に並ぶ）と縁の細い黄緑の線
        eye_t = nb.math("FRACT", nb.add(nb.mul(th, 1.0 / 0.024), nb.mul(warp, 0.6)))
        eyes = nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(eye_t, 0.5)), 0.14, 0.05),
                      nb.smooth(nb.math("ABSOLUTE", nb.sub(av, 0.87)), 0.035, 0.01))
        eyes = nb.mul(eyes, nb.smooth(nb.noise(nb.comb(nb.mul(th, 40.0), 0.0, 0.0), 1.0, 2, 0.5), 0.35, 0.5))
        mcol = nb.mix(mcol, srgb("#b9dfe6"), nb.mul(eyes, 0.6))
        rim = nb.mul(nb.smooth(av, 0.94, 0.985), nb.smooth(av, 1.06, 1.0))
        mcol = nb.mix(mcol, srgb("#8fc98e"), nb.mul(rim, 0.45))
        # 出水管: 縁は淡く、穴の中は暗い
        sip_lip = nb.mul(nb.smooth(dsip, 0.014, 0.009), nb.smooth(dsip, 0.0045, 0.0065))
        mcol = nb.mix(mcol, srgb("#7fb7c9"), nb.mul(sip_lip, 0.7))
        mcol = nb.mix(mcol, srgb("#0d1a24"), nb.smooth(dsip, 0.0056, 0.0042))
        # 入水管のスリット: 中は暗く、両側に淡い房
        mcol = nb.mix(mcol, srgb("#0e1820"), nb.smooth(dslit, 0.0024, 0.0012))
        fringe = nb.mul(nb.smooth(dslit, 0.0065, 0.0035), nb.smooth(dslit, 0.0015, 0.0028))
        fr_n = nb.smooth(nb.math("SINE", nb.mul(th, 500.0)), 0.0, 0.8)
        mcol = nb.mix(mcol, srgb("#c4d8b0"), nb.mul(nb.mul(fringe, fr_n), 0.8))
        mcol = nb.hsv(mcol, 0.5, 1.0, nb.maprange(nb.noise(mco, 40.0, 3, 0.5), 0.3, 0.7, 0.9, 1.08))

        col = nb.mix(shell, mcol, mant)
        # 高さ: 殻の成長線・放射肋、外套膜の細かい乳頭と縁のしわ
        pap = nb.smooth(nb.voronoi(co, 260.0), 0.35, 0.0)
        wr = nb.math("SINE", nb.add(nb.mul(th, 180.0), nb.mul(warp, 4.0)))
        h_shell = nb.add(nb.add(nb.mul(gline, -0.8), nb.mul(riblet, 0.15)), nb.mul(fine, 0.45))
        h_mant = nb.add(nb.mul(pap, 0.35), nb.mul(nb.mul(wr, nb.smooth(av, 0.75, 0.95)), 0.35))
        height = nb.mixf(h_shell, h_mant, mant)
        rough = nb.mixf(nb.maprange(fine, 0.3, 0.7, 0.72, 0.86), 0.28, mant)
        rough = nb.mixf(rough, 0.5, nb.mul(sparkle, mant))
        cavity = nb.mixf(nb.maprange(crev, 0.0, 1.0, 1.0, 0.55), 1.0, mant)
        return dict(color=col, rough=rough, height=height, height_scale=0.0009, cavity=cavity)

    return pbr_material("GiantClam", fn, res=2048, ao_distance=0.12, uv="keep")


def giant_clam():
    g = _clam_sdf()
    obj = _sdf_mesh("GiantClam", g, smooth=(0.4, 2))
    print(f"  clam sdf: {len(obj.data.vertices)} verts")
    off = Vector((1.7, 3.3, 0.4))
    _finish_sdf(obj, 9000, uv_angle=60,
                disp=lambda co, n: 0.0006 * geo.fbm(co * 45.0 + off, 3))
    _clam_attrs(obj)
    C.assign(obj, clam_material())
    return [obj]


# ---------------------------------------------------------------------------
# クロナマコ（Holothuria atra）
# ---------------------------------------------------------------------------
CU_L = 0.30          # 体長
CU_W = 0.029         # 最大の半幅
CU_H = 0.026         # 背の高さ（断面中心から）
CU_HB = 0.012        # 腹側の厚み（平たい足裏）


def _cuke_profile(t):
    """体の太さ（t=0 頭 → 1 尾）。両端は丸く、頭側はやや細い"""
    p = (1.0 - np.abs(2.0 * t - 1.0) ** 2.4) ** (1.0 / 2.4)
    return p * (0.86 + 0.14 * np.sin(np.pi * np.clip(t * 1.15, 0, 1)) ** 0.5) * (0.92 + 0.1 * t)


def sea_cucumber():
    rnd = random.Random(21)
    n, m = 74, 22
    u = np.linspace(0.0, 1.0, n)
    t = u - 0.55 * np.sin(2 * np.pi * u) / (2 * np.pi)          # 両端を細かく
    # 中心線: 砂の上でゆるく S 字に曲がる（頭は +X）
    cx = (0.5 - t) * CU_L
    cy = 0.022 * np.sin(2 * np.pi * (t * 0.85 + 0.1)) - 0.01 * t
    pts = np.stack([cx, cy], -1)
    tng = np.gradient(pts, axis=0)
    tng /= np.linalg.norm(tng, axis=1, keepdims=True)
    lat = np.stack([-tng[:, 1], tng[:, 0]], -1)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    ell = np.concatenate([[0.0], np.cumsum(seg)])                 # 弧長
    prof = _cuke_profile(t)
    # ゆるい太さのむら（体の部分的な収縮）
    lump = 1.0 + 0.07 * np.sin(ell * 23.0 + 0.6) * np.sin(ell * 9.0 + 2.0)
    off = Vector((rnd.uniform(0, 40), rnd.uniform(0, 40), 1.7))
    verts = []
    rows = []
    for i in range(n):
        W = CU_W * prof[i] * lump[i]
        Hh = CU_H * prof[i] * lump[i]
        Hb = CU_HB * prof[i] ** 0.8
        zc = Hb - 0.003
        if i in (0, n - 1):
            # 両端は 1 点に閉じる（尾端は少し持ち上がる）
            end = np.array([cx[i], cy[i], zc + (0.006 if i == n - 1 else 0.0)])
            d = np.array([tng[i, 0], tng[i, 1], 0.0]) * (-0.004 if i == 0 else 0.004)
            rows.append([len(verts)])
            verts.append(end + d)
            continue
        row = []
        for j in range(m):
            a = -math.pi / 2 + 2 * math.pi * j / m
            c, sn = math.cos(a), math.sin(a)
            if sn >= 0:
                yy, zz = W * c, Hh * sn
            else:
                e = 2.0 / 3.2
                yy = W * math.copysign(abs(c) ** e, c)
                zz = -Hb * abs(sn) ** e
            # 横しわ（背側ほど深い）と小さなこぶ
            top = max(0.0, sn) ** 0.7
            ph = ell[i] / 0.0045 + 3.0 * geo.fbm(Vector((ell[i] * 9, j * 0.25, 0)) + off, 2)
            wr = 1.0 + 0.022 * top * math.sin(ph) * (0.6 + 0.4 * geo.fbm(Vector((ell[i] * 20, j * 0.4, 3)) + off, 2))
            p0 = Vector((cx[i] + lat[i, 0] * yy, cy[i] + lat[i, 1] * yy, zc + zz))
            bump = 1.0 + 0.05 * geo.fbm(p0 * 28.0 + off, 3) * (0.3 + 0.7 * top)
            k = wr * bump
            yy, zz = yy * k, (zz * k if sn >= 0 else zz)
            row.append(len(verts))
            verts.append(np.array([cx[i] + lat[i, 0] * yy, cy[i] + lat[i, 1] * yy, zc + zz]))
        rows.append(row)
    faces, uvs = [], []
    seams = []
    for i in range(n - 1):
        a_, b_ = rows[i], rows[i + 1]
        for j in range(m):
            j1 = (j + 1) % m
            u0, u1 = j / m, (j + 1) / m
            if len(a_) == 1:
                faces.append((a_[0], b_[j1], b_[j]))
                uvs.append([((u0 + u1) / 2, ell[i]), (u1, ell[i + 1]), (u0, ell[i + 1])])
            elif len(b_) == 1:
                faces.append((a_[j], a_[j1], b_[0]))
                uvs.append([(u0, ell[i]), (u1, ell[i]), ((u0 + u1) / 2, ell[i + 1])])
            else:
                faces.append((a_[j], a_[j1], b_[j1], b_[j]))
                uvs.append([(u0, ell[i]), (u1, ell[i]), (u1, ell[i + 1]), (u0, ell[i + 1])])
        seams.append((a_[0], b_[0]))                                 # 足裏の中線をシームに
    obj = C.mesh_object("SeaCucumber", verts, faces, uvs)
    _outward(obj)
    C.set_smooth(obj, True)
    _seam_unwrap(obj, seams)
    # 模様用: CukeA = (弧長, 頭端/尾端までの距離, 0)
    V = _np_verts(obj.data)
    head = np.array([cx[0], cy[0], CU_HB * 0.2])
    tail = np.array([cx[-1], cy[-1], CU_HB])
    _set_attr(obj, "CukeA", np.stack([np.linalg.norm(V - head, axis=1), np.linalg.norm(V - tail, axis=1),
                                      np.zeros(len(V))], -1))
    C.assign(obj, cucumber_material())
    return [obj]


def cucumber_material():
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, Vl = uv[0], uv[1]
        a = nb.mul(U, math.tau)
        cyl = nb.comb(nb.mul(nb.math("COSINE", a), 0.03), nb.mul(nb.math("SINE", a), 0.03), Vl)
        co = nb.coord("Object")
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        ends = nb.sep(nb.attr("CukeA"))
        d_head, d_tail = ends[0], ends[1]
        # 黒い肌: わずかに茶色がかったむら、乳頭状の小突起
        big = nb.noise(cyl, 25.0, 4, 0.6)
        skin = nb.ramp(big, [(0.3, srgb("#161311")), (0.55, srgb("#1e1a16")), (0.8, srgb("#2b241e"))])
        pap_d = nb.voronoi(cyl, 170.0)
        pap = nb.smooth(pap_d, 0.32, 0.05)
        skin = nb.mix(skin, srgb("#0f0e0d"), nb.mul(pap, 0.5))
        wr = nb.math("SINE", nb.add(nb.mul(Vl, math.tau / 0.006), nb.mul(nb.noise(cyl, 30.0, 3, 0.5), 5.0)))
        # 足裏（腹側）は灰色がかって、管足の小さな点
        sole = nb.smooth(nz, -0.45, -0.8)
        tf = nb.smooth(nb.voronoi(cyl, 420.0), 0.3, 0.1)
        sole_col = nb.mix(srgb("#2e2a26"), srgb("#4a443c"), nb.mul(tf, 0.6))
        skin = nb.mix(skin, sole_col, sole)
        # 砂: 背に厚く、脇は斑に。乳頭のまわりは黒く抜ける（2 列に並ぶ黒い輪）
        dors = nb.smooth(nz, -0.15, 0.55)
        patch = nb.noise(co, 13.0, 5, 0.65, distortion=0.35)
        grain_n = nb.noise(co, 380.0, 2, 0.5)
        patch2 = nb.noise(co, 34.0, 4, 0.6)
        cover = nb.add(nb.mul(dors, 0.5), nb.mul(nb.sub(patch, 0.5), 1.4))
        cover = nb.add(cover, nb.mul(nb.sub(patch2, 0.5), 0.5))
        cover = nb.add(cover, nb.mul(nb.sub(grain_n, 0.5), 0.35))
        cover = nb.smooth(cover, 0.4, 0.52)
        bare_d = nb.voronoi(cyl, 75.0)
        bare_r = nb.sep(nb.voronoi(cyl, 75.0, out="Color"))[0]
        bare = nb.mul(nb.smooth(bare_d, 0.2, 0.13), nb.math("GREATER_THAN", bare_r, 0.25))
        cover = nb.mul(cover, nb.sub(1.0, bare))
        cover = nb.mul(cover, nb.sub(1.0, nb.mul(nb.smooth(pap_d, 0.12, 0.0), 0.8)))
        # 両端（口と肛門）は砂が付かない
        cover = nb.mul(cover, nb.mul(nb.smooth(d_head, 0.012, 0.03), nb.smooth(d_tail, 0.01, 0.025)))
        # 砂粒: 白・ベージュ・灰・サンゴ片の桃色
        gv = nb.sep(nb.voronoi(co, 950.0, out="Color"))
        gcol = nb.ramp(gv[0], [(0.0, srgb("#e9e4d6")), (0.35, srgb("#d6c9a8")), (0.6, srgb("#bfb193")),
                               (0.8, srgb("#9d978a")), (0.93, srgb("#d8b3a7"))], interp="CONSTANT")
        gcol = nb.mix(gcol, srgb("#cfc4a6"), 0.35)
        gcol = nb.hsv(gcol, 0.5, 1.0, nb.maprange(nb.noise(co, 60.0, 3, 0.5), 0.3, 0.7, 0.85, 1.08))
        col = nb.mix(skin, gcol, cover)
        # 砂の縁の薄いところは黒が透ける
        thin = nb.mul(nb.smooth(cover, 0.0, 0.5), nb.smooth(cover, 1.0, 0.6))
        col = nb.mix(col, srgb("#5a534a"), nb.mul(thin, 0.35))
        # 肛門（尾端）と口（頭の下）
        col = nb.mix(col, srgb("#050505"), nb.smooth(d_tail, 0.0045, 0.002))
        col = nb.mix(col, srgb("#3a352e"), nb.mul(nb.smooth(d_head, 0.012, 0.004), 0.8))
        gh = nb.smooth(nb.voronoi(co, 950.0), 0.45, 0.0)
        height = nb.add(nb.mul(pap, 0.7), nb.mul(wr, 0.2))
        height = nb.add(height, nb.mul(cover, nb.add(0.35, nb.mul(gh, 0.5))))
        rough = nb.mixf(0.44, 0.93, cover)
        rough = nb.mixf(rough, 0.55, sole)
        return dict(color=col, rough=rough, height=height, height_scale=0.0012,
                    cavity=nb.maprange(pap, 0.0, 1.0, 1.0, 0.9))

    return pbr_material("SeaCucumber", fn, res=1024, ao_distance=0.06, uv="keep")


# ---------------------------------------------------------------------------
# ガンガゼ（Diadema setosum）
# ---------------------------------------------------------------------------
UR_R = 0.035        # 殻の半径（水平）
UR_TOP = 0.024      # 殻の上半分の高さ
UR_BOT = 0.014      # 下半分（口側は平たい）
UR_Z = 0.046        # 殻の中心の高さ（下向きの棘で砂の上に立つ）


def _urchin_test_point(al, ph):
    """殻の表面（al: 天頂からの角, ph: 方位）。5 放射の歩帯でわずかに五角形"""
    sa, ca = math.sin(al), math.cos(al)
    r = UR_R * sa * (1.0 + 0.03 * math.cos(5 * ph) * sa)
    z = UR_Z + (UR_TOP if ca > 0 else UR_BOT) * ca
    # 肛門の袋（頂上の小さな膨らみ）
    rho = UR_R * sa
    z += 0.0055 * math.exp(-(rho / 0.0075) ** 2) * (1.0 if ca > 0 else 0.0)
    return np.array([r * math.cos(ph), r * math.sin(ph), z])


def urchin_test_material():
    def fn(nb):
        co = nb.coord("Object")
        x, y, z = nb.sep(co)
        rho = nb.vmath("LENGTH", nb.comb(x, y, 0.0), out=1)
        top = nb.smooth(z, UR_Z + 0.01, UR_Z + 0.02)
        ang = nb.math("ARCTAN2", y, x)
        base = nb.mix(srgb("#121214"), srgb("#1e1b28"), nb.noise(co, 90.0, 3, 0.5))
        # 疣（棘の付け根の小突起）
        tub = nb.smooth(nb.voronoi(co, 260.0), 0.3, 0.05)
        col = nb.mix(base, srgb("#2a2733"), nb.mul(tub, 0.5))
        # 青い虹色の斑点（間歩帯に並ぶ）
        blue_d = nb.voronoi(co, 115.0)
        blue_r = nb.sep(nb.voronoi(co, 115.0, out="Color"))[2]
        inter = nb.smooth(nb.math("COSINE", nb.mul(nb.sub(ang, 0.6283), 5.0)), 0.3, 0.9)
        blue = nb.mul(nb.mul(nb.smooth(blue_d, 0.3, 0.16), nb.math("GREATER_THAN", blue_r, 0.3)),
                      nb.mul(inter, nb.mul(top, nb.smooth(rho, 0.012, 0.016))))
        col = nb.mix(col, nb.mix(srgb("#2c7dff"), srgb("#5fd0ff"), nb.noise(co, 400.0, 2, 0.5)), blue)
        # 頂上の 5 つの白い点（生殖板）
        wd = nb.math("COSINE", nb.mul(ang, 5.0))
        white = nb.mul(nb.mul(nb.smooth(wd, 0.8, 0.95), nb.smooth(nb.math("ABSOLUTE", nb.sub(rho, 0.0128)),
                                                                    0.0032, 0.0016)), top)
        col = nb.mix(col, srgb("#e6e4dc"), white)
        # 肛門の袋を囲むオレンジの輪
        ring = nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(rho, 0.0068)), 0.0024, 0.0012), top)
        col = nb.mix(col, srgb("#ff7a26"), ring)
        col = nb.mix(col, srgb("#3a3346"), nb.mul(nb.smooth(rho, 0.0045, 0.002), top))
        height = nb.add(nb.mul(tub, 0.6), nb.mul(ring, 0.2))
        rough = nb.mixf(0.34, 0.25, nb.math("MAXIMUM", blue, ring))
        return dict(color=col, rough=rough, height=height, height_scale=0.0006)

    return pbr_material("UrchinTest", fn, res=512, ao_distance=0.05)


def urchin_spine_material():
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        Vl = uv[1]
        # 黒く艶のある棘。根元はわずかに紫、細かい輪紋（棘の小さな鋸歯の列）
        bands = nb.math("SINE", nb.mul(Vl, math.tau / 0.0022))
        col = nb.mix(srgb("#1c1624"), srgb("#101012"), nb.smooth(Vl, 0.0, 0.03))
        faint = nb.smooth(nb.math("SINE", nb.add(nb.mul(Vl, math.tau / 0.018), 0.4)), 0.7, 1.0)
        col = nb.mix(col, srgb("#2c2a30"), nb.mul(faint, 0.25))
        return dict(color=col, rough=0.3, height=bands, height_scale=0.00015)

    return pbr_material("UrchinSpine", fn, res=512, ao_distance=0.04, uv="keep")


def sea_urchin():
    rnd = random.Random(9)
    # 殻
    rows = [np.array([0.0, 0.0, UR_Z - UR_BOT])]
    nr, m = 16, 30
    for i in range(1, nr):
        al = math.pi - math.pi * i / nr
        rows.append(np.array([_urchin_test_point(al, 2 * math.pi * j / m) for j in range(m)]))
    rows.append(_urchin_test_point(0.0, 0.0))
    test, _ = _ring_mesh("UrchinTest", rows)
    _outward(test)
    C.set_smooth(test, True)
    C.assign(test, urchin_test_material())
    # 棘: 上半分と赤道に長い棘、下側は短く砂に届く棘
    spines = []
    N = 56
    golden = math.pi * (3 - math.sqrt(5))
    c0, c1 = math.cos(math.radians(17)), math.cos(math.radians(128))   # 頂上（肛門まわり）には棘がない
    for k in range(N):
        ca = c0 + (c1 - c0) * (k + 0.5) / N
        al = math.acos(ca) + rnd.uniform(-0.05, 0.05)
        ph = k * golden + rnd.uniform(-0.1, 0.1)
        base = _urchin_test_point(al, ph)
        radial = _nrm(base - np.array([0, 0, UR_Z]))
        deg = math.degrees(al)
        if deg < 95:
            d = _nrm(radial + np.array([0, 0, 0.35 - deg / 400.0]) +
                     np.array([rnd.gauss(0, 0.08), rnd.gauss(0, 0.08), rnd.gauss(0, 0.05)]))
            L = rnd.uniform(0.17, 0.235) * (1.0 - 0.28 * max(0.0, deg - 55) / 40)
            r0 = 0.0017
        else:
            d = _nrm(radial + np.array([0, 0, -0.25]) + np.array([rnd.gauss(0, 0.06), rnd.gauss(0, 0.06), 0]))
            L = rnd.uniform(0.07, 0.11)
            r0 = 0.0015
        p0 = base - radial * 0.002
        if d[2] < 0:
            L = min(L, (p0[2] + 0.001) / -d[2])          # 下向きの棘の先は砂に少し刺さる
        ts = [0.0, 0.05, 0.3, 0.65, 1.0]
        pts = [Vector(p0 + d * L * t) for t in ts]
        radii = [r0, r0 * 1.15, r0 * 0.78, r0 * 0.45, 0.00022]
        o = geo.tube_along(pts, radii, sides=6, name=f"spine{k}")
        spines.append(o)
    _pack_tubes(spines)
    for o in spines:
        _weld(o)
    smat = urchin_spine_material()
    for o in spines:
        C.set_smooth(o, True)
        C.assign(o, smat)
    return [test] + spines


# ---------------------------------------------------------------------------
# アオヒトデ（Linckia laevigata）
# ---------------------------------------------------------------------------
ST_R = 0.15          # 中心から腕先まで
ST_RA = 0.0122       # 腕の付け根の半径
ST_FLAT = 0.84       # 断面の扁平率（高さ / 幅）


def _star_arms(rnd):
    """5 本の腕の中心線（砂の上でゆるく曲がり、先がわずかに持ち上がる）"""
    arms = []
    a0 = math.radians(90 + rnd.uniform(-8, 8))
    for k in range(5):
        a = a0 + k * math.tau / 5 + math.radians(rnd.uniform(-7, 7))
        L = ST_R * rnd.uniform(0.93, 1.04)
        bend = rnd.uniform(-0.25, 0.25) * (1.6 if k == 2 else 1.0)
        lift = rnd.uniform(0.0, 0.009) if k != 1 else 0.014
        pts, radii = [], []
        n = 12
        for i in range(n):
            t = i / (n - 1)
            aa = a + bend * t * t
            r = 0.012 + (L - 0.012) * t
            z = ST_RA * ST_FLAT + lift * t ** 2.5 + 0.0015 * math.sin(t * 7 + k)
            pts.append(np.array([r * math.cos(aa), r * math.sin(aa), z / ST_FLAT]))
            radii.append(ST_RA * (1.0 - 0.22 * t) * (1.0 + 0.04 * math.sin(t * 11 + k * 2)))
        arms.append((pts, radii))
    return arms


def blue_starfish():
    rnd = random.Random(17)
    arms = _star_arms(rnd)
    h = 0.0011
    g, (ax, ay, az) = _field_grid((-0.17, -0.17, -0.004), (0.17, 0.17, 0.05), h)
    g.sphere((0.0, 0.0, ST_RA * 0.95), 0.021, k=0.0)                        # 盤
    for pts, radii in arms:
        g.tube(pts, radii, k=0.009)
        g.sphere(pts[-1], radii[-1] * 1.02, k=0.003)                          # 丸い腕先
    # 腹側は平ら（z 方向は後で扁平にするので、ここでは切る高さを割り戻しておく）
    zz = az[None, None, :]
    g.F = np.maximum(g.F, (0.0006 / ST_FLAT) - zz)
    obj = _sdf_mesh("BlueStarfish", g, smooth=(0.4, 2))
    V = _np_verts(obj.data)
    V[:, 2] = V[:, 2] * ST_FLAT - 0.0012                                      # 断面を扁平に、少し砂に沈める
    _set_verts(obj.data, V)
    off = Vector((2.3, 0.7, 5.1))
    _finish_sdf(obj, 3200, uv_angle=66,
                disp=lambda co, n: 0.00035 * geo.fbm(co * 160.0 + off, 2) * max(0.0, n.z + 0.3))
    # 模様用: StarA = (腕に沿った位置 0..1, 腕の中心線からの横ずれ / 半径, 腕番号)
    V = _np_verts(obj.data)
    best = np.full(len(V), 1e9)
    attr = np.zeros((len(V), 3))
    for k, (pts, radii) in enumerate(arms):
        P = np.array(pts) * np.array([1, 1, ST_FLAT])
        for i in range(len(P) - 1):
            a, b = P[i], P[i + 1]
            ab = b - a
            t = np.clip(((V - a) @ ab) / (ab @ ab), 0, 1)
            q = a + t[:, None] * ab
            d = np.linalg.norm((V - q)[:, :2], axis=1)
            sel = d < best
            best[sel] = d[sel]
            tt = (i + t) / (len(P) - 1)
            side = np.sign(np.cross(ab[:2], (V - q)[:, :2]))
            r = radii[i] + (radii[i + 1] - radii[i]) * t
            attr[sel, 0] = tt[sel]
            attr[sel, 1] = (side * d / r)[sel]
            attr[sel, 2] = k
    _set_attr(obj, "StarA", attr)
    C.assign(obj, starfish_material())
    return [obj]


def starfish_material():
    def fn(nb):
        co = nb.coord("Object")
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        st = nb.sep(nb.attr("StarA"))
        t, lat = st[0], st[1]
        # 細かい顆粒（石灰質の小板）と、その隙間の暗い青
        gd = nb.voronoi(co, 620.0)
        gran = nb.smooth(gd, 0.42, 0.08)
        big = nb.noise(co, 18.0, 4, 0.6)
        col = nb.ramp(big, [(0.3, srgb("#2356bd")), (0.55, srgb("#2c66cf")), (0.8, srgb("#3a7bde"))])
        col = nb.mix(col, srgb("#17398a"), nb.mul(nb.sub(1.0, gran), 0.55))
        # 背の皮鰓域（小さな孔の集まり）が腕に沿って並ぶ暗い点
        pd = nb.voronoi(nb.mapping(co, loc=(0.3, 0.7, 0.1)), 150.0)
        pr = nb.sep(nb.voronoi(nb.mapping(co, loc=(0.3, 0.7, 0.1)), 150.0, out="Color"))[0]
        pores = nb.mul(nb.smooth(pd, 0.22, 0.1), nb.math("GREATER_THAN", pr, 0.45))
        pores = nb.mul(pores, nb.smooth(nz, 0.2, 0.6))
        col = nb.mix(col, srgb("#1a3278"), nb.mul(pores, 0.6))
        # 背の中央は少し明るく、脇は深い青
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(nz, -0.2, 0.9, 0.82, 1.08))
        # 腹側: 淡い青、歩帯溝（中央の筋）に黄褐色の管足
        under = nb.smooth(nz, -0.35, -0.75)
        ucol = nb.mix(srgb("#4f7fcf"), srgb("#6b90d0"), nb.noise(co, 60.0, 3, 0.5))
        groove = nb.smooth(nb.math("ABSOLUTE", lat), 0.28, 0.12)
        ucol = nb.mix(ucol, srgb("#b38a4e"), nb.mul(groove, nb.smooth(t, 0.05, 0.12)))
        col = nb.mix(col, ucol, under)
        # 腕先の眼点（赤い小さな点）と盤の多孔板
        col = nb.mix(col, srgb("#a8443a"), nb.mul(nb.mul(nb.smooth(t, 0.992, 1.0), nb.smooth(nz, -0.1, -0.5)), 0.8))
        mad = nb.smooth(nb.vmath("DISTANCE", co, nb.comb(0.009, -0.006, 0.0), out=1), 0.0035, 0.002)
        col = nb.mix(col, srgb("#9fb4dc"), nb.mul(mad, 0.8))
        height = nb.add(nb.mul(gran, 0.8), nb.mul(pores, -0.6))
        height = nb.add(height, nb.mul(groove, nb.mul(under, -1.0)))
        rough = nb.maprange(gran, 0.0, 1.0, 0.72, 0.5)
        return dict(color=col, rough=rough, height=height, height_scale=0.00045,
                    cavity=nb.maprange(gran, 0.0, 1.0, 0.8, 1.0))

    return pbr_material("BlueStarfish", fn, res=1024, ao_distance=0.04, uv="keep")


# ---------------------------------------------------------------------------
# センジュイソギンチャク（Heteractis magnifica）+ カクレクマノミ（Amphiprion ocellaris）
# ---------------------------------------------------------------------------
AN_M = 48            # 体の周方向の分割
AN_NCOL = 4          # 触手アトラスの列数（色違い）
AN_TR = 0.0052       # 触手の半径
AN_TL = 0.062        # 触手の標準の長さ


def _an_rim(ph):
    """口盤の縁の半径と高さ（大きく波打つ）"""
    r = 0.2 * (1.0 + 0.05 * np.sin(4 * ph + 2.0) + 0.03 * np.sin(7 * ph + 0.4))
    z = 0.118 + 0.026 * np.sin(3 * ph + 0.4) + 0.011 * np.sin(5 * ph + 1.7) + 0.006 * np.sin(8 * ph)
    return r, z


AN_ZM = 0.112        # 口の高さ


def _an_disc_z(r, ph):
    """口盤の上面の高さ（口から縁へ、縁の波打ちがしだいに強くなる）"""
    rr, zr = _an_rim(ph)
    f = np.clip(r / rr, 0.0, 1.0)
    return AN_ZM + 0.004 + (zr + 0.009 - AN_ZM - 0.004) * f ** 1.6


def _an_profile(ph):
    """方位 ph の断面（下の極 → 柱部 → 縁 → 口盤 → 口の極）"""
    rr, zr = _an_rim(ph)
    # 柱部はゆるく凸凹した鉢形（方位でふくらみが変わる）
    lump = 1.0 + 0.045 * math.sin(3 * ph + 1.1) + 0.025 * math.sin(7 * ph + 0.3)
    col = [(0.104, -0.004), (0.114, 0.01), (0.12, 0.028), (0.128, 0.046), (0.14, zr - 0.05)]
    pts = [(0.06, -0.012), (0.098, -0.012)]
    pts += [(r * lump, z) for r, z in col]
    pts += [(0.158 * (1 + 0.5 * (lump - 1)), zr - 0.031), (0.178, zr - 0.017), (rr - 0.006, zr - 0.007),
            (rr + 0.003, zr + 0.001), (rr - 0.001, zr + 0.009)]
    for f in (0.86, 0.7, 0.53, 0.36, 0.22):
        r = rr * f
        pts.append((r, float(_an_disc_z(r, ph))))
    pts += [(0.026, AN_ZM + 0.009), (0.013, AN_ZM + 0.01)]
    return pts


AN_RIM_ROW = 13      # 縁の上の行（ここで柱部と口盤の UV 島を分ける）


def anemone_body():
    m = AN_M
    prof = [_an_profile(2 * math.pi * j / m) for j in range(m)]
    nrow = len(prof[0])
    rows = [np.array([0.0, 0.0, -0.012])]
    for i in range(nrow):
        rows.append(np.array([[prof[j][i][0] * math.cos(2 * math.pi * j / m),
                               prof[j][i][0] * math.sin(2 * math.pi * j / m), prof[j][i][1]] for j in range(m)]))
    rows.append(np.array([0.0, 0.0, AN_ZM + 0.006]))
    obj, idx = _ring_mesh("AnemoneBody", rows)
    _outward(obj)
    C.set_smooth(obj, True)
    seams = []
    for a, b in zip(idx[:-1], idx[1:]):
        seams.append((a[0], b[0]))
    ring = idx[AN_RIM_ROW]
    seams += [(ring[j], ring[(j + 1) % m]) for j in range(m)]
    _seam_unwrap(obj, seams)
    # 模様用: AnemA = (断面上の位置 0 下 → 1 口, 口からの距離, 0)
    V = _np_verts(obj.data)
    tau = np.zeros(len(V))
    for i, r in enumerate(idx):
        tau[r] = i / (len(idx) - 1)
    _set_attr(obj, "AnemA", np.stack([tau, np.linalg.norm(V[:, :2], axis=1), np.zeros(len(V))], -1))
    C.assign(obj, anemone_body_material())
    return obj


def _poisson_disc(rnd, rmin, rmax, spacing, n_max, tries=6000):
    pts = []
    cell = spacing / math.sqrt(2)
    grid = {}
    for _ in range(tries):
        r = math.sqrt(rnd.uniform(rmin ** 2, rmax ** 2))
        a = rnd.uniform(0, math.tau)
        p = (r * math.cos(a), r * math.sin(a))
        gx, gy = int(p[0] // cell), int(p[1] // cell)
        ok = True
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                q = grid.get((gx + dx, gy + dy))
                if q is not None and (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 < spacing ** 2:
                    ok = False
                    break
            if not ok:
                break
        if ok:
            grid[(gx, gy)] = p
            pts.append(p)
            if len(pts) >= n_max:
                break
    return pts


def anemone_tentacles(rnd):
    """口盤を覆う指状の触手（5 角断面、先が少し膨らむ）。Proc UV は色違いアトラス用"""
    acc = MeshAcc()
    sides = 5
    flow = Vector((0.35, 0.12, 0.0))                  # ゆるい流れで同じ向きにそよぐ
    base_pts = _poisson_disc(rnd, 0.024, 0.205, 0.0151, 410, tries=40000)
    for (x, y) in base_pts:
        r = math.hypot(x, y)
        ph = math.atan2(y, x)
        rr, _ = _an_rim(ph)
        if r > rr * 0.99:
            continue
        z = float(_an_disc_z(r, ph))
        e = 0.002
        dzr = (float(_an_disc_z(r + e, ph)) - float(_an_disc_z(r - e, ph))) / (2 * e)
        dzp = (float(_an_disc_z(r, ph + e / max(r, 0.01))) - float(_an_disc_z(r, ph - e / max(r, 0.01)))) / (2 * e)
        radial = Vector((math.cos(ph), math.sin(ph), 0.0))
        tang = Vector((-math.sin(ph), math.cos(ph), 0.0))
        nrm = (UP - radial * dzr - tang * dzp).normalized()
        f = r / rr
        lean = 0.15 + 0.9 * f ** 1.6
        d = (nrm + radial * lean + flow * 0.5 + Vector((rnd.gauss(0, 0.18), rnd.gauss(0, 0.18), 0))).normalized()
        L = AN_TL * rnd.uniform(0.8, 1.15) * (0.55 + 0.45 * min(1.0, (r - 0.03) / 0.05))
        rad = AN_TR * rnd.uniform(0.88, 1.1)
        droop = (radial * 0.6 + flow).normalized() if f > 0.6 else flow.normalized()
        droop = (droop + Vector((rnd.gauss(0, 0.5), rnd.gauss(0, 0.5), 0))).normalized()
        base = Vector((x, y, z)) - nrm * 0.006
        col = rnd.randrange(AN_NCOL)
        u0 = col / AN_NCOL
        ts = (0.0, 0.5, 0.9)
        rs = (1.0, 0.92, 0.97)                     # 先は少し膨らんで丸い
        rings = []
        cv = rnd.uniform(0.3, 0.55) * (0.5 + 0.9 * f)
        for t, k in zip(ts, rs):
            bend = droop * (cv * t * t * L)
            c = base + d * (L * t) + bend - UP * (0.25 * t * t * L * f * f)
            rings.append((c, rad * k))
        tipc = rings[-1][0] + ((rings[-1][0] - rings[-2][0]).normalized()) * (L * 0.1)
        tdir = (rings[-1][0] - rings[0][0]).normalized()
        side = tdir.cross(UP)
        if side.length < 1e-3:
            side = Vector((1, 0, 0))
        side.normalize()
        up2 = side.cross(tdir).normalized()
        o = len(acc.verts)
        for (c, rad_k) in rings:
            for j in range(sides):
                a = -j / sides * math.tau                  # この向きで面が外を向く
                acc.verts.append(tuple(c + (side * math.cos(a) + up2 * math.sin(a)) * rad_k))
        acc.verts.append(tuple(tipc))
        tip_i = len(acc.verts) - 1
        for i in range(len(rings) - 1):
            for j in range(sides):
                j1 = (j + 1) % sides
                a0, a1 = o + i * sides + j, o + i * sides + j1
                b0, b1 = a0 + sides, a1 + sides
                acc.faces.append((a0, a1, b1, b0))
                uu0 = u0 + j / sides / AN_NCOL
                uu1 = u0 + (j + 1) / sides / AN_NCOL
                acc.uvs.append([(uu0, ts[i]), (uu1, ts[i]), (uu1, ts[i + 1]), (uu0, ts[i + 1])])
        last = o + (len(rings) - 1) * sides
        for j in range(sides):
            j1 = (j + 1) % sides
            acc.faces.append((last + j, last + j1, tip_i))
            uu0 = u0 + j / sides / AN_NCOL
            uu1 = u0 + (j + 1) / sides / AN_NCOL
            acc.uvs.append([(uu0, ts[-1]), (uu1, ts[-1]), ((uu0 + uu1) / 2, 1.0)])
        acc.faces.append(tuple(o + j for j in reversed(range(sides))))   # 付け根のふた（口盤の中に隠れる）
        acc.uvs.append([(u0 + 0.5 / AN_NCOL, 0.0)] * sides)
    obj = acc.build("AnemoneTentacles")
    C.set_smooth(obj, True)
    print(f"  tentacles: {len(base_pts)} placed, {_tris(obj)} tris")
    C.assign(obj, anemone_tentacle_material())
    return obj


def anemone_body_material():
    def fn(nb):
        a = nb.sep(nb.attr("AnemA"))
        tau, rho = a[0], a[1]
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        x, y, _ = nb.sep(co)
        ang = nb.math("ARCTAN2", y, x)
        rim_t = AN_RIM_ROW / (len(_an_profile(0.0)) + 1)
        disc = nb.smooth(tau, rim_t - 0.02, rim_t + 0.03)
        # 柱部: 紫〜藤色。細い縦筋と色むら。砂際は暗く砂をかぶる
        streak = nb.noise(nb.comb(nb.mul(nb.math("COSINE", ang), 6.0), nb.mul(nb.math("SINE", ang), 6.0),
                                  nb.mul(z, 3.0)), 4.0, 3, 0.5)
        vline = nb.math("SINE", nb.mul(ang, 64.0))
        colm = nb.ramp(z, [(0.0, srgb("#6c2f6c")), (0.04, srgb("#8a4188")), (0.08, srgb("#a2529c")),
                           (0.12, srgb("#b96cae"))])
        colm = nb.hsv(colm, 0.5, 1.0, nb.maprange(nb.noise(co, 14.0, 4, 0.6), 0.3, 0.7, 0.88, 1.1))
        mott = nb.smooth(nb.voronoi(co, 180.0), 0.25, 0.1)
        colm = nb.mix(colm, srgb("#c58cc0"), nb.mul(mott, 0.25))
        colm = nb.hsv(colm, 0.5, 1.0, nb.maprange(streak, 0.3, 0.7, 0.88, 1.1))
        colm = nb.hsv(colm, 0.5, 1.0, nb.maprange(vline, -1.0, 1.0, 0.95, 1.04))
        colm = nb.mix(colm, srgb("#c78bbd"), nb.mul(nb.smooth(tau, rim_t - 0.12, rim_t - 0.02), 0.35))   # 縁の返し
        colm = nb.mix(colm, srgb("#5a3a4e"), nb.smooth(z, 0.02, -0.004))
        colm = nb.mix(colm, srgb("#cdbf9f"), nb.mul(nb.smooth(z, 0.006, -0.008), 0.8))
        # 口盤: 触手の付け根と同じ緑褐色、放射状の筋
        rad = nb.math("SINE", nb.mul(ang, 48.0))
        cold = nb.mix(srgb("#6e7342"), srgb("#8b8a55"), nb.noise(co, 40.0, 3, 0.5))
        cold = nb.hsv(cold, 0.5, 1.0, nb.maprange(rad, -1.0, 1.0, 0.85, 1.08))
        # 口: 淡い口丘と暗い口の裂け目
        mouth = nb.smooth(rho, 0.021, 0.009)
        cold = nb.mix(cold, srgb("#c9b995"), mouth)
        slit = nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(nb.mul(x, 0.8), nb.mul(y, 0.6))), 0.0022, 0.0008),
                      nb.smooth(rho, 0.012, 0.009))
        cold = nb.mix(cold, srgb("#2a1f1a"), slit)
        col = nb.mix(colm, cold, disc)
        height = nb.add(nb.mul(vline, nb.mul(nb.sub(1.0, disc), 0.3)), nb.mul(rad, nb.mul(disc, 0.3)))
        height = nb.add(height, nb.mul(nb.noise(co, 120.0, 3, 0.5), 0.4))
        rough = nb.mixf(0.36, 0.5, disc)
        return dict(color=col, rough=rough, height=height, height_scale=0.0008)

    return pbr_material("AnemoneBody", fn, res=1024, ao_distance=0.08, uv="keep")


def anemone_tentacle_material():
    """触手アトラス。U: 4 列（色違い）× 周方向, V: 付け根 0 → 先端 1"""
    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        colf = nb.math("FLOOR", nb.mul(U, AN_NCOL))
        x = nb.math("FRACT", nb.mul(U, AN_NCOL))
        a = nb.mul(x, math.tau)
        cyl = nb.comb(nb.mul(nb.math("COSINE", a), AN_TR), nb.mul(nb.math("SINE", a), AN_TR), nb.mul(V, AN_TL))
        cyl = nb.vmath("ADD", cyl, nb.comb(nb.mul(colf, 3.1), 0.0, 0.0))
        # 付け根は緑褐色（褐虫藻）→ 中ほど淡緑 → 先はクリーム色
        base = nb.ramp(V, [(0.0, srgb("#5f6a3a")), (0.3, srgb("#8c9a5c")), (0.62, srgb("#b5c283")),
                           (0.86, srgb("#d8dca8")), (1.0, srgb("#ece8c6"))])
        # 列ごとの色違い（黄緑寄り〜緑寄り〜クリーム寄り）
        colc = nb.sub(colf, (AN_NCOL - 1) / 2.0)
        col = nb.hsv(base, nb.add(0.5, nb.mul(colc, 0.012)), nb.sub(1.0, nb.mul(colc, 0.1)),
                     nb.add(1.0, nb.mul(colc, 0.02)))
        # 縦の細い筋と、先端の少し膨らんだ所の明るさ
        st = nb.math("SINE", nb.mul(a, 6.0))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(st, -1.0, 1.0, 0.94, 1.03))
        n = nb.noise(cyl, 400.0, 3, 0.5)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(n, 0.3, 0.7, 0.92, 1.06))
        tipg = nb.smooth(V, 0.88, 0.99)
        col = nb.mix(col, srgb("#f3efd6"), nb.mul(tipg, 0.35))
        height = nb.add(nb.mul(st, 0.25), nb.mul(n, 0.4))
        cavity = nb.maprange(V, 0.0, 0.55, 0.45, 1.0)
        return dict(color=col, rough=0.34, height=height, height_scale=0.0004, cavity=cavity)

    return pbr_material("AnemoneTentacle", fn, res=512, uv="atlas", atlas_size=(AN_NCOL * AN_TR * math.tau, AN_TL))


# --- カクレクマノミ ------------------------------------------------------------
CF_X0, CF_BL = 0.42, 0.80      # 吻端の x と、吻端から尾柄までの長さ（全長比）


def _cf_profiles():
    from scipy.interpolate import PchipInterpolator
    qk = [0.0, 0.05, 0.15, 0.3, 0.45, 0.6, 0.75, 0.9, 1.0]
    zt = PchipInterpolator(qk, [0.0, 0.078, 0.158, 0.218, 0.232, 0.212, 0.158, 0.098, 0.078])
    zb = PchipInterpolator(qk, [-0.012, -0.062, -0.13, -0.18, -0.192, -0.168, -0.122, -0.084, -0.074])
    wd = PchipInterpolator(qk, [0.012, 0.044, 0.078, 0.097, 0.093, 0.079, 0.06, 0.042, 0.035])
    return zt, zb, wd


def _cf_body_pt(prof, q, a, L):
    zt, zb, wd = prof
    t, b, w = float(zt(q)), float(zb(q)), float(wd(q))
    zc, hh = (t + b) / 2, (t - b) / 2
    return np.array([CF_X0 - q * CF_BL, w * math.cos(a), zc + hh * math.sin(a)]) * L


def _fish_co(V, L):
    """FishCo = (吻端→尾柄の位置 q, z/L, y/L)"""
    q = (CF_X0 - V[:, 0] / L) / CF_BL
    return np.stack([q, V[:, 2] / L, V[:, 1] / L], -1)


def _fin_sheet(acc, base_pts, edge_pts, fin_id, rows=3, bulge=None):
    """付け根の点列 → 縁の点列の間を張るヒレ。Proc UV: U = 付け根方向 + 2*fin_id, V = 付け根 0 → 縁 1"""
    n = len(base_pts)
    grid, uvs = [], []
    for i in range(rows):
        t = i / (rows - 1)
        row = []
        for j in range(n):
            p = base_pts[j] * (1 - t) + edge_pts[j] * t
            if bulge is not None:
                p = p + bulge * math.sin(math.pi * t) * math.sin(math.pi * j / (n - 1))
            row.append(p)
        grid.append(row)
        uvs.append([(j / (n - 1) + 2.0 * fin_id, t) for j in range(n)])
    acc.grid(grid, uvs)


def clownfish(prefix, L, rnd, bmat, fmat):
    prof = _cf_profiles()
    zt, zb, wd = prof
    qs = [0.02, 0.05, 0.09, 0.14, 0.2, 0.27, 0.35, 0.44, 0.53, 0.62, 0.71, 0.79, 0.86, 0.92, 0.97]
    m = 14
    rows = [np.array([CF_X0 * L, 0.0, -0.004 * L])]
    for q in qs:
        ring = []
        for j in range(m):
            a = -2 * math.pi * j / m
            p = _cf_body_pt(prof, q, a, L)
            ring.append(p)
        rows.append(np.array(ring))
    rows.append(np.array([(CF_X0 - 0.975 * CF_BL) * L, 0.0, float(zt(0.97) + zb(0.97)) / 2 * L]))
    body, _ = _ring_mesh(prefix + "_body", rows)
    # 目の膨らみ
    V = _np_verts(body.data)
    for sy in (1, -1):
        ec = _cf_body_pt(prof, 0.13, 0.47 if sy > 0 else math.pi - 0.47, L)
        d = np.linalg.norm(V - ec, axis=1)
        nrm = np.array([0.15, sy * 1.0, 0.1])
        V += np.outer(np.exp(-(d / (0.045 * L)) ** 2) * 0.012 * L, _nrm(nrm))
    _set_verts(body.data, V)
    _outward(body)
    C.set_smooth(body, True)
    _set_attr(body, "FishCo", _fish_co(_np_verts(body.data), L))
    C.assign(body, bmat)

    acc = MeshAcc()

    def top_pt(q, inset=0.012):
        p = _cf_body_pt(prof, q, math.pi / 2, L)
        return p - np.array([0, 0, inset * L])

    def bot_pt(q, inset=0.012):
        p = _cf_body_pt(prof, q, -math.pi / 2, L)
        return p + np.array([0, 0, inset * L])

    # 背ビレ: 棘条部（低い）→ 切れ込み → 軟条部（高く丸い）。鰭条は後ろへ傾く
    qd = np.linspace(0.27, 0.91, 14)
    hd = np.interp(qd, [0.27, 0.33, 0.45, 0.54, 0.58, 0.68, 0.8, 0.88, 0.91],
                   [0.0, 0.1, 0.105, 0.072, 0.1, 0.14, 0.13, 0.07, 0.0])
    base = [top_pt(q) for q in qd]
    edge = [b + np.array([-np.sin(0.45) * h, 0.0, np.cos(0.45) * h]) * L for b, h in zip(base, hd)]
    _fin_sheet(acc, base, edge, 0, rows=3, bulge=np.array([0, 0.004 * L, 0]))
    # 尻ビレ
    qa = np.linspace(0.6, 0.9, 8)
    ha = np.interp(qa, [0.6, 0.66, 0.74, 0.84, 0.9], [0.0, 0.1, 0.125, 0.09, 0.0])
    base = [bot_pt(q) for q in qa]
    edge = [b + np.array([-np.sin(0.5) * h, 0.0, -np.cos(0.5) * h]) * L for b, h in zip(base, ha)]
    _fin_sheet(acc, base, edge, 1, rows=3, bulge=np.array([0, -0.003 * L, 0]))
    # 尾ビレ（丸い扇形）
    xq = (CF_X0 - 0.955 * CF_BL) * L
    zc = float(zt(0.955) + zb(0.955)) / 2 * L
    hz = float(zt(0.955) - zb(0.955)) / 2 * L * 0.8
    sway = rnd.uniform(-0.25, 0.25)
    base, edge = [], []
    for j in range(9):
        f = j / 8 * 2 - 1
        base.append(np.array([xq, 0.0, zc + f * hz]))
        ps = math.radians(58) * f
        R = 0.235 * L * (0.93 + 0.07 * math.cos(ps * 2.2))
        edge.append(np.array([xq - R * math.cos(ps), R * math.sin(sway) * 0.3, zc + R * math.sin(ps)]))
    _fin_sheet(acc, base, edge, 2, rows=4)
    # 胸ビレ（体の横で開く丸いうちわ）と腹ビレ
    for sy in (1, -1):
        c0 = _cf_body_pt(prof, 0.3, sy * -0.3 if sy > 0 else math.pi + 0.3, L)
        c0 = c0 - np.array([0, sy * 0.012 * L, 0])
        spread = rnd.uniform(0.45, 0.75)
        d = _nrm([-0.85, sy * spread, -0.12])
        up = np.array([0, 0, 1.0])
        base, edge = [], []
        for j in range(6):
            f = j / 5 * 2 - 1
            b = c0 + up * f * 0.04 * L
            ang = f * 0.75
            dd = _nrm(d * math.cos(ang) + up * math.sin(ang))
            R = 0.17 * L * (0.8 + 0.2 * math.cos(f * 1.3))
            base.append(b)
            edge.append(b + dd * R)
        _fin_sheet(acc, base, edge, 3, rows=3, bulge=np.array([0, sy * 0.006 * L, 0]))
        c1 = _cf_body_pt(prof, 0.34, -math.pi / 2 + sy * 0.35, L) + np.array([0, 0, 0.012 * L])
        d = _nrm([-0.6, sy * 0.3, -0.75])
        base = [c1 + np.array([0.02 * L * f, 0, 0]) for f in (1, 0, -1)]
        edge = [b + d * 0.12 * L * k for b, k in zip(base, (0.75, 1.0, 0.85))]
        _fin_sheet(acc, base, edge, 4, rows=3)
    fins = acc.build(prefix + "_fins")
    C.set_smooth(fins, True)
    _set_attr(fins, "FishCo", _fish_co(_np_verts(fins.data), L))
    C.assign(fins, fmat)
    return [body, fins]


def _band_masks(nb, q, zz):
    """3 本の白帯（黒い縁取り）。戻り値 (white, black)"""
    white = None
    black = None
    b1 = nb.add(0.235, nb.mul(nb.pow(nb.math("DIVIDE", zz, 0.2), 2.0), 0.045))
    b2 = nb.sub(0.535, nb.mul(nb.math("EXPONENT", nb.mul(nb.pow(nb.math("DIVIDE", zz, 0.085), 2.0), -1.0)), 0.07))
    for c, w in ((b1, 0.04), (b2, 0.046), (0.905, 0.034)):
        d = nb.math("ABSOLUTE", nb.sub(q, c))
        wt = nb.smooth(d, w, w - 0.006)
        bk = nb.smooth(d, w + 0.017, w + 0.011)
        white = wt if white is None else nb.math("MAXIMUM", white, wt)
        black = bk if black is None else nb.math("MAXIMUM", black, bk)
    return white, black


def clownfish_material():
    def fn(nb):
        fc = nb.sep(nb.attr("FishCo"))
        q, zz, yy = fc[0], fc[1], fc[2]
        orange = nb.mix(srgb("#ff7418"), srgb("#ff8a2a"), nb.smooth(zz, 0.05, -0.12))
        orange = nb.mix(orange, srgb("#ffa03a"), nb.mul(nb.smooth(zz, -0.08, -0.18), 0.5))   # 腹は黄味
        orange = nb.mix(orange, srgb("#e8601a"), nb.mul(nb.smooth(zz, 0.12, 0.22), 0.4))   # 背は濃く
        # 鱗（小さな網目）
        sc = nb.voronoi(nb.comb(nb.mul(q, 30.0), nb.mul(zz, 26.0), nb.mul(yy, 20.0)), 1.0)
        orange = nb.hsv(orange, 0.5, 1.0, nb.maprange(sc, 0.0, 0.5, 1.04, 0.92))
        white, black = _band_masks(nb, q, zz)
        col = nb.mix(orange, srgb("#141210"), black)
        col = nb.mix(col, srgb("#f5f2ea"), white)
        # 目: 黒い瞳、橙褐色の虹彩、黒い縁
        de = nb.vmath("LENGTH", nb.comb(nb.mul(nb.sub(q, 0.13), CF_BL), nb.sub(zz, 0.072), 0.0), out=1)
        col = nb.mix(col, srgb("#1a1512"), nb.smooth(de, 0.05, 0.043))
        col = nb.mix(col, srgb("#c86a1e"), nb.smooth(de, 0.042, 0.037))
        col = nb.mix(col, srgb("#b8b098"), nb.mul(nb.smooth(de, 0.036, 0.032), nb.smooth(de, 0.02, 0.026)))
        col = nb.mix(col, srgb("#050505"), nb.smooth(de, 0.024, 0.02))
        # 口
        mouth = nb.mul(nb.smooth(q, 0.03, 0.0), nb.smooth(nb.math("ABSOLUTE", nb.add(zz, 0.012)), 0.012, 0.004))
        col = nb.mix(col, srgb("#7a3a14"), nb.mul(mouth, 0.7))
        eye = nb.smooth(de, 0.045, 0.035)
        rough = nb.mixf(0.32, 0.08, eye)
        height = nb.add(nb.mul(sc, 0.5), nb.mul(eye, 0.4))
        return dict(color=col, rough=rough, height=height, height_scale=0.0002)

    return pbr_material("Clownfish", fn, res=1024, ao_distance=0.02)


def clownfin_material():
    def fn(nb):
        fc = nb.sep(nb.attr("FishCo"))
        q, zz = fc[0], fc[1]
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        fid = nb.math("FLOOR", nb.mul(U, 0.5))
        u = nb.math("FRACT", nb.mul(U, 0.5))
        col = nb.mix(srgb("#ff7e22"), srgb("#ff9a3a"), nb.mul(V, 0.5))
        # 鰭条（付け根から縁への細い筋）
        rays = nb.math("SINE", nb.mul(u, 70.0))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(rays, -1.0, 1.0, 0.9, 1.04))
        white, black = _band_masks(nb, q, zz)
        # 中央の白帯は背ビレにも続く
        col = nb.mix(col, srgb("#141210"), nb.mul(black, nb.smooth(V, 0.5, 0.2)))
        col = nb.mix(col, srgb("#f5f2ea"), nb.mul(white, nb.smooth(V, 0.75, 0.45)))
        # 黒い縁（胸ビレは細く淡い）
        is_pec = nb.math("COMPARE", fid, 3.0, 0.5)
        edge = nb.smooth(V, nb.mixf(0.8, 0.9, is_pec), nb.mixf(0.9, 0.97, is_pec))
        col = nb.mix(col, srgb("#161311"), nb.mul(edge, nb.mixf(1.0, 0.7, is_pec)))
        col = nb.mix(col, srgb("#ffb05a"), nb.mul(is_pec, nb.mul(nb.sub(1.0, edge), 0.25)))
        return dict(color=col, rough=0.35, height=rays, height_scale=0.00015)

    return pbr_material("ClownfishFin", fn, res=512, ao_distance=0.02, double_sided=True)


def anemone_clownfish():
    rnd = random.Random(33)
    parts = [anemone_body(), anemone_tentacles(rnd)]
    bmat, fmat = clownfish_material(), clownfin_material()
    # 触手の上に浮かぶ 3 匹（大きい雌・雄・小さい若魚）
    for k, (L, loc, yaw, pitch, roll) in enumerate((
            (0.088, (0.02, -0.05, 0.245), 160, -4, 6),
            (0.078, (-0.1, 0.06, 0.232), 35, 6, -8),
            (0.062, (0.1, 0.11, 0.222), -110, -10, 4))):
        for o in clownfish(f"Clown{k}", L, rnd, bmat, fmat):
            o.matrix_world = (Matrix.Translation(loc) @
                              Matrix.Rotation(math.radians(yaw), 4, "Z") @
                              Matrix.Rotation(math.radians(pitch), 4, "Y") @
                              Matrix.Rotation(math.radians(roll), 4, "X"))
            parts.append(o)
    return parts


# ---------------------------------------------------------------------------
# アオウミガメ（Chelonia mydas）
# ---------------------------------------------------------------------------
# 体の座標: +X 前（頭）, +Z 上, 原点は体の中心。最後に全体へ遊泳姿勢の傾きを掛ける
TU_M = 64            # 甲羅の周方向の分割
TU_X0 = 0.03         # 甲羅の輪郭の中心（最大幅がやや前）
TU_H = 0.2          # 背甲の高さ（縁から）
TU_ZP = -0.142       # 腹甲の底
TU_EXT = 0.56        # 甲羅の模様画像の範囲（±m）
TU_POSE = (math.radians(-5.0), math.radians(3.0))   # 頭上げ（Y 軸）と横の傾き（X 軸）
TU_ZC = 0.035        # 甲羅の上下の中央（ここを原点に下げる）


def _tu_R(ph):
    """背甲の輪郭（中心からの距離）。前縁は項甲でわずかにくぼみ、後ろはやや尖る"""
    ph = np.asarray(ph, np.float64)
    c, s = np.cos(ph), np.sin(ph)
    a = 0.5 - 0.025 * np.tanh(4 * c)
    n = 2.15 + 0.2 * np.tanh(4 * c)
    R = 1.0 / ((np.abs(c) / a) ** n + (np.abs(s) / 0.418) ** n) ** (1.0 / n)
    dph = np.angle(np.exp(1j * (ph - math.pi)))
    return R * (1 - 0.02 * np.exp(-(ph / 0.13) ** 2)) * (1 - 0.012 * np.exp(-(dph / 0.05) ** 2))


def _tu_zrim(ph, rho=1.0):
    """縁の高さ（前が高く後ろが低い）。rho で中心へ向かって傾きを消す"""
    c = np.cos(ph)
    return 0.012 + 0.03 * c * rho + 0.008 * np.cos(2 * ph) * rho ** 2


def _tu_top(rho, ph):
    """背甲の上面: 中央は丸く、脇へなだらかに下り、縁甲板はゆるいつば状"""
    rho = np.asarray(rho, np.float64)
    c = np.cos(ph)
    f = np.clip(1 - rho ** 2.6, 0, 1) * (1 - 0.2 * rho ** 4)
    return _tu_zrim(ph, rho) + TU_H * (1 + 0.04 * c * rho) * f


def _tu_rho_p(ph):
    """腹甲（と橋）の縁。前後は短く、首・ヒレの出る開口を残す"""
    c = np.cos(ph)
    return 0.82 - 0.17 * np.maximum(c, 0) ** 1.5 - 0.1 * np.maximum(-c, 0) ** 1.5


def _tu_bottom(rho, ph):
    c = np.cos(ph)
    rp = _tu_rho_p(ph)
    zr = _tu_zrim(ph)
    zm = zr - 0.012 - 0.03 * np.clip((1 - rho) / (1 - rp), 0, 1.5)
    zpl = TU_ZP + (0.05 + 0.05 * np.maximum(c, 0) ** 2 + 0.03 * np.maximum(-c, 0) ** 2) * rho ** 2.2
    w = _smooth01(rho, rp - 0.08, rp + 0.05)
    return zpl * (1 - w) + zm * w


# 鱗板の配置（上から見た x, y）: 椎甲板 5 枚と肋甲板 4 対
TU_VERT = [(0.335, 0.0), (0.168, 0.0), (0.0, 0.0), (-0.168, 0.0), (-0.325, 0.0)]
TU_COST = [(x, s * 0.245) for s in (1, -1) for x in (0.245, 0.082, -0.085, -0.24)]
TU_W = [0.0] * 5 + [0.008] * 8     # 重み付きボロノイの重み（肋甲板を大きく）
TU_RHO_M = 0.845     # 縁甲板の内縁


def _tu_marg_angles():
    """縁甲板の継ぎ目の方位（片側 11 枚 + 項甲 + 臀甲）"""
    ang = [0.11]
    for k in range(1, 12):
        ang.append(0.11 + (math.pi - 0.11 - 0.07) * k / 11.5)
    ang.append(math.pi - 0.0)
    ang = sorted(set(ang + [-a for a in ang]))
    return np.array(ang)


def _value_noise(shape, cells, rng, octaves=3):
    """格子の乱数を 3 次補間で拡大した値ノイズ（0..1）"""
    from scipy.ndimage import zoom
    out = np.zeros(shape)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = cells * 2 ** o
        g = rng.random((n + 3, n + 3))
        z = zoom(g, (shape[0] / n, shape[1] / n), order=3)[:shape[0], :shape[1]]
        out += amp * z
        tot += amp
        amp *= 0.5
    return np.clip(out / tot, 0, 1)


def _tu_scute_geom(X, Y):
    """画素ごとの鱗板の幾何: 継ぎ目までの距離、縁甲板か、鱗板番号、成長の中心からの位置"""
    ph = np.arctan2(Y, X - TU_X0)
    R = _tu_R(ph)
    rho = np.hypot(X - TU_X0, Y) / R
    seeds = np.array(TU_VERT + TU_COST)
    # 小甲（成長の中心）: 椎甲板は後ろ寄り、肋甲板は後ろ上寄り
    areo = seeds + np.array([[-0.035, 0.0]] * 5 + [[-0.04, -np.sign(y) * 0.05] for _, y in TU_COST])
    P = np.stack([X, Y], -1)
    pw = np.sum((P[:, :, None, :] - seeds[None, None]) ** 2, axis=-1) - np.array(TU_W)[None, None]
    order = np.argsort(pw, axis=-1)
    i1, i2 = order[..., 0], order[..., 1]
    p1 = np.take_along_axis(pw, i1[..., None], -1)[..., 0]
    p2 = np.take_along_axis(pw, i2[..., None], -1)[..., 0]
    sep = np.linalg.norm(seeds[i2] - seeds[i1], axis=-1)
    edge_v = (p2 - p1) / (2 * sep)                  # 2 枚の鱗板の境界（重み付き）までの距離
    # 縁甲板
    ang = _tu_marg_angles()
    marg = rho > TU_RHO_M
    rim_d = np.abs(rho - TU_RHO_M) * R
    dph = np.min(np.abs(np.angle(np.exp(1j * (ph[..., None] - ang[None, None])))), axis=-1)
    edge_m = np.minimum(dph * R, rim_d)
    edge = np.where(marg, edge_m, np.minimum(edge_v, rim_d))
    sid = np.where(marg, 100 + np.searchsorted(ang, ph), i1)
    # 縁甲板の縞の中心は外縁、それ以外は小甲
    ax = np.where(marg[..., None], np.stack([TU_X0 + np.cos(ph) * R * 1.02, np.sin(ph) * R * 1.02], -1), areo[i1])
    rel = P - ax
    return edge, edge_m, marg, sid, np.linalg.norm(rel, axis=-1), np.arctan2(rel[..., 1], rel[..., 0])


def _tu_scute_images(res, chunk=128):
    """甲羅を真上・真下から見た模様画像（RGB = リニアの色, A = 高さ）を作る。
    上: 鱗板ごとに成長の中心（小甲）から放射状に伸びる黄褐色の縞、淡い継ぎ目。下: 淡黄色の腹甲。
    メモリを抑えるため行のまとまりごとに計算する（ノイズだけは画像全体で作る）"""
    rng = np.random.default_rng(7)
    xs = (np.arange(res) + 0.5) / res * 2 * TU_EXT - TU_EXT
    noise = _value_noise((res, res), 18, rng, 4).astype(np.float32)
    fine = _value_noise((res, res), 90, rng, 2).astype(np.float32)
    warp = _value_noise((res, res), 9, rng, 2).astype(np.float32)
    brk = _value_noise((res, res), 26, rng, 3).astype(np.float32)
    # 鱗板ごとの縞の周波数・位相と明るさ
    tabs = [(np.round(rng.uniform(5, 10, 200) * mul), rng.uniform(0, 6.28, 200)) for mul in (1.0, 1.9, 3.3)]
    sc_tab = rng.uniform(0.85, 1.12, 200)
    lin = lambda h: np.array(srgb(h))              # noqa: E731
    dark, mid_c, light = lin("#3a3520"), lin("#6a5a30"), lin("#ab8a47")
    top = np.zeros((res, res, 4), np.float32)
    bot = np.zeros((res, res, 4), np.float32)
    for r0 in range(0, res, chunk):
        r1 = min(res, r0 + chunk)
        X, Y = np.meshgrid(xs, xs[r0:r1])          # 行 = y（下から）、列 = x
        nz, fn, wp, bk = noise[r0:r1], fine[r0:r1], warp[r0:r1], brk[r0:r1]
        edge, edge_m, marg, sid, rr, th = _tu_scute_geom(X, Y)
        # 縞は中心から離れるほど少し蛇行し、ところどころ途切れる
        th = th + (wp - 0.5) * 0.6 * np.clip(rr / 0.1, 0, 1)
        streak = np.zeros(X.shape)
        for k, (fr, pv) in enumerate(tabs):
            f, v = fr[sid % 200], pv[sid % 200]
            streak += np.sin(th * f + v + 1.2 * np.sin(rr * (20 + 12 * k) + v * 2)) / (1 + k * 0.7)
        streak = (streak / 2.0 * 0.5 + 0.5) * (0.55 + 0.9 * bk)
        # 小甲（成長の中心）のまわりは縞が消えてまだらになる
        t = np.clip((streak - 0.35) / 0.5, 0, 1) * (0.12 + 0.88 * _smooth01(rr, 0.012, 0.085))
        t = np.clip(t + (nz - 0.5) * 0.6 + (fn - 0.5) * 0.25 * (1 - _smooth01(rr, 0.02, 0.07)), 0, 1)[..., None]
        col = (dark * (1 - t) + light * t) * 0.7 + mid_c * 0.3 * (1 - t)
        col = col * (sc_tab[sid % 200] * (0.85 + 0.3 * fn))[..., None]
        # 継ぎ目: 内側に暗い縁、中心に淡い線
        seam = np.clip(1 - edge / 0.0017, 0, 1)[..., None]
        halo = np.clip(1 - edge / 0.012, 0, 1)[..., None] * (1 - seam)
        col = col * (1 - 0.35 * halo)
        col = col * (1 - seam * 0.85) + lin("#b3a47a") * seam * 0.85
        grow = 0.5 + 0.5 * np.sin(rr / 0.01 * math.tau + nz * 4)
        top[r0:r1, :, :3] = col
        top[r0:r1, :, 3] = 0.55 + 0.2 * nz + 0.04 * grow - 0.5 * np.clip(1 - edge / 0.004, 0, 1)
        # --- 腹甲（下から見た図）: 淡黄色、中線と横の継ぎ目、橋の下縁甲板 ---
        ay = np.abs(Y)
        dl = np.min(np.abs(X[..., None] - np.array([0.3, 0.17, -0.02, -0.2, -0.32])), axis=-1)
        e2 = np.minimum(dl, ay)
        infra = (ay > 0.25) & (ay < 0.34) & (np.abs(X) < 0.22)
        e2 = np.where(infra, np.minimum(np.abs(ay - 0.25),
                                        np.min(np.abs(X[..., None] - np.array([-0.11, 0.0, 0.11])), axis=-1)), e2)
        e2 = np.where(marg, edge_m, e2)
        seam2 = np.clip(1 - e2 / 0.0025, 0, 1)[..., None]
        pl = lin("#e2cf93") * (0.9 + 0.2 * nz[..., None]) * (0.95 + 0.1 * fn[..., None])
        pl = pl * (1 - 0.3 * seam2) + lin("#a89266") * 0.3 * seam2
        pl = np.where(marg[..., None], lin("#d4bc84") * (0.9 + 0.2 * nz[..., None]), pl)
        bot[r0:r1, :, :3] = pl * (1 - 0.25 * seam2)
        bot[r0:r1, :, 3] = 0.6 + 0.2 * nz - 0.4 * seam2[..., 0]
    return top, bot


def _np_image(name, arr):
    h, w, _ = arr.shape
    img = bpy.data.images.new(name, w, h, alpha=True, float_buffer=True)
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(arr.ravel())
    img.pack()
    return img


def turtle_shell_material():
    # 模様画像はベイク解像度に合わせる（試し焼きでは小さく）
    res = max(256, int(2048 * float(os.environ.get("OKI_RES_SCALE", "1"))))
    top, bot = _tu_scute_images(min(res, 2048))
    itop, ibot = _np_image("TurtleScuteTop", top), _np_image("TurtleScuteBottom", bot)

    def fn(nb):
        a = nb.sep(nb.attr("ShellA"))
        x, y, is_top = a[0], a[1], a[2]
        b = nb.sep(nb.attr("ShellB"))
        skin = b[0]
        uvv = nb.comb(nb.math("DIVIDE", nb.add(x, TU_EXT), 2 * TU_EXT),
                      nb.math("DIVIDE", nb.add(y, TU_EXT), 2 * TU_EXT), 0.0)
        t = nb.image(itop, uvv, ext="EXTEND")
        bo = nb.image(ibot, uvv, ext="EXTEND")
        wt = nb.smooth(is_top, 0.35, 0.65)
        col = nb.mix(bo[0], t[0], wt)
        h = nb.mixf(bo[1], t[1], wt)
        co = nb.coord("Object")
        # 首・ヒレの出る開口の皮膚（灰色がかったクリーム色、しわ）
        wr = nb.noise(nb.mapping(co, scale=(3.0, 3.0, 12.0)), 30.0, 4, 0.6)
        scol = nb.mix(srgb("#8f8468"), srgb("#c2b48e"), nb.smooth(wr, 0.35, 0.7))
        col = nb.mix(col, scol, nb.smooth(skin, 0.3, 0.7))
        h = nb.mixf(h, wr, nb.smooth(skin, 0.3, 0.7))
        # 薄い藻の膜（上面の一部）
        alg = nb.mul(nb.smooth(nb.noise(co, 4.0, 4, 0.6), 0.58, 0.72), wt)
        col = nb.mix(col, srgb("#4f5a34"), nb.mul(alg, 0.35))
        fine = nb.noise(co, 180.0, 3, 0.5)
        h = nb.add(h, nb.mul(fine, 0.12))
        rough = nb.mixf(nb.mixf(0.5, 0.42, wt), 0.62, nb.smooth(skin, 0.3, 0.7))
        rough = nb.add(rough, nb.mul(alg, 0.15))
        return dict(color=col, rough=rough, height=h, height_scale=0.002)

    return pbr_material("TurtleShell", fn, res=2048, ao_distance=0.3, uv="keep")


def turtle_shell():
    m = TU_M
    phs = np.array([2 * math.pi * j / m for j in range(m)])
    R = _tu_R(phs)
    zr = _tu_zrim(phs)

    def ring(rho, z):
        return np.stack([TU_X0 + np.cos(phs) * R * rho, np.sin(phs) * R * rho, z], -1)

    rows, kinds = [], []
    rows.append(np.array([TU_X0, 0.0, float(_tu_bottom(0.0, 0.0))]))
    kinds.append((0, 0.0))
    for rho in (0.12, 0.26, 0.4, 0.5, 0.57, 0.63, 0.69, 0.75, 0.82, 0.89, 0.95, 0.985):
        rows.append(ring(rho, _tu_bottom(rho, phs)))
        kinds.append((0, rho))
    rows.append(ring(1.0, zr - 0.008))
    kinds.append((0, 1.0))
    rows.append(ring(1.013, zr - 0.0005))
    kinds.append((0.5, 1.0))
    rows.append(ring(1.0, zr + 0.007))
    kinds.append((1, 1.0))
    for rho in (0.985, 0.95, 0.9, 0.83, 0.75, 0.66, 0.56, 0.45, 0.33, 0.2, 0.08):
        rows.append(ring(rho, _tu_top(rho, phs)))
        kinds.append((1, rho))
    rows.append(np.array([TU_X0, 0.0, float(_tu_top(0.0, 0.0))]))
    kinds.append((1, 0.0))
    obj, idx = _ring_mesh("TurtleShell", rows)
    _outward(obj)
    C.set_smooth(obj, True)
    # UV: 縁で上下に分け、さらに正中線で左右に分ける（半楕円 4 枚は詰めやすい）
    rim_i = next(i for i, k in enumerate(kinds) if k[0] == 0.5)
    ring_ids = idx[rim_i]
    seams = [(ring_ids[j], ring_ids[(j + 1) % m]) for j in range(m)]
    for j in (0, m // 2):
        col = [idx[0][0]] + [r[j] for r in idx[1:-1]] + [idx[-1][0]]
        seams += list(zip(col[:-1], col[1:]))
    _seam_unwrap(obj, seams)
    # 腹側（あまり見えない）は密度を 0.7 倍にして背甲に面積を回す
    kind_v = np.zeros(len(obj.data.vertices))
    for (kt, _), ids in zip(kinds, idx):
        kind_v[ids] = kt
    me = obj.data
    uvl = me.uv_layers["Bake"].data
    uv = np.empty(len(uvl) * 2)
    uvl.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    li_bot = []
    for poly in me.polygons:
        if np.mean(kind_v[list(poly.vertices)]) < 0.5:
            li_bot += list(poly.loop_indices)
    li_bot = np.array(li_bot)
    cen = uv[li_bot].mean(0)
    uv[li_bot] = cen + (uv[li_bot] - cen) * 0.7
    uvl.foreach_set("uv", uv.ravel())
    C._select_only([obj], obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(udim_source="CLOSEST_UDIM", rotate=True, margin=0.004, shape_method="CONCAVE")
    bpy.ops.object.mode_set(mode="OBJECT")
    # 属性: ShellA = (x, y, 上面か), ShellB = (開口部の皮膚, rho, 0)
    V = _np_verts(obj.data)
    top = np.zeros(len(V))
    rr = np.zeros(len(V))
    for (kt, rho), ids in zip(kinds, idx):
        top[ids] = kt
        rr[ids] = rho
    ph = np.arctan2(V[:, 1], V[:, 0] - TU_X0)
    c = np.cos(ph)
    rp = _tu_rho_p(ph)
    band = _smooth01(rr, rp - 0.1, rp - 0.02) * _smooth01(rr, rp + 0.12, rp + 0.05)
    opening = np.maximum(_smooth01(c, 0.35, 0.6), _smooth01(-c, 0.55, 0.75))
    skin = band * opening * (top < 0.25)
    _set_attr(obj, "ShellA", np.stack([V[:, 0], V[:, 1], top], -1))
    _set_attr(obj, "ShellB", np.stack([skin, rr, np.zeros(len(V))], -1))
    C.assign(obj, turtle_shell_material())
    return obj


TU_HC0 = np.array([0.655, 0.0, 0.022])   # 頭の形を決めたときの中心
TU_HC = np.array([0.628, 0.0, 0.024])    # 実際の頭の中心（首を短く）
TU_HS = 1.08                             # 頭の拡大率


def _tu_hd(p):
    """頭の部品の位置を、基準の頭から実際の頭へ移す"""
    return TU_HC + (np.asarray(p, np.float64) - TU_HC0) * TU_HS


def turtle_head(h=0.0034):
    """頭・首・肩の皮膚（SDF）。肩は甲羅の前の開口を埋め、ヒレの付け根が刺さる"""
    g, (ax, ay, az) = _field_grid((0.17, -0.34, -0.15), (0.79, 0.34, 0.12), h)
    hd = _tu_hd
    g.ellipsoid((0.34, 0.0, -0.045), (0.13, 0.22, 0.072), k=0.0)                      # 肩
    g.cone((0.3, 0.0, -0.035), hd((0.572, 0.0, 0.01)), 0.09, 0.07, k=0.045)          # 首（太く短い）
    g.ellipsoid(hd((0.655, 0.0, 0.022)), np.array((0.083, 0.061, 0.056)) * TU_HS, k=0.03)   # 頭
    g.ellipsoid(hd((0.712, 0.0, 0.006)), np.array((0.048, 0.043, 0.041)) * TU_HS, k=0.02)   # 吻
    g.ellipsoid(hd((0.66, 0.0, -0.026)), np.array((0.07, 0.05, 0.03)) * TU_HS, k=0.02)      # 下あご
    g.ellipsoid(hd((0.6, 0.0, -0.03)), np.array((0.062, 0.056, 0.037)) * TU_HS, k=0.03)     # 喉
    # 口の線（嘴の縁）
    pts = [(0.752, 0.0, -0.012), (0.735, 0.03, -0.012), (0.705, 0.043, -0.008), (0.672, 0.047, -0.004)]
    for sy in (1, -1):
        g.tube([hd((p[0], sy * p[1], p[2])) for p in pts], [0.0028, 0.003, 0.0028, 0.002], k=0.002, op="sub")
    # 目: くぼみ + 眼球 + 上まぶた
    for sy in (1, -1):
        e = hd((0.692, sy * 0.049, 0.035))
        g.sphere(e + np.array([0, sy * 0.004, 0]), 0.019 * TU_HS, k=0.005, op="sub")
        g.sphere(e, 0.0145 * TU_HS, k=0.002)
        g.tube([e + np.array([0.014, sy * 0.001, 0.008]) * TU_HS, e + np.array([0.0, sy * 0.006, 0.0145]) * TU_HS,
                e + np.array([-0.016, sy * 0.001, 0.006]) * TU_HS], [0.004, 0.0048, 0.0035], k=0.004)
    obj = _sdf_mesh("TurtleHead", g, smooth=(0.45, 3), remesh=True)
    off = Vector((3.3, 1.1, 0.4))
    _finish_sdf(obj, 3000, uv_angle=64, disp=lambda co, n: 0.0008 * geo.fbm(co * 40.0 + off, 3))
    V = _np_verts(obj.data)
    _set_attr(obj, "SkinA", V)
    _set_attr(obj, "SkinB", np.zeros((len(V), 3)))
    return obj


def turtle_skin_material():
    def fn(nb):
        a = nb.attr("SkinA")
        ax_, ay_, az_ = nb.sep(a)
        b = nb.sep(nb.attr("SkinB"))
        part, dors = b[0], b[1]
        nz = nb.sep(nb.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        is_head = nb.math("LESS_THAN", part, 0.5)
        # --- 頭・首: 茶色の多角形の鱗、淡黄色の縁取り。首は細かい鱗としわ ---
        headw = nb.smooth(ax_, float(_tu_hd((0.56, 0, 0))[0]), float(_tu_hd((0.61, 0, 0))[0]))   # 1 = 頭
        v_big = nb.voronoi(a, 52.0, feature="DISTANCE_TO_EDGE")
        v_sm = nb.voronoi(a, 125.0, feature="DISTANCE_TO_EDGE")
        c_big = nb.voronoi(a, 52.0, out="Color")
        c_sm = nb.voronoi(a, 125.0, out="Color")
        edge_h = nb.mixf(nb.smooth(v_sm, 0.1, 0.03), nb.smooth(v_big, 0.07, 0.02), headw)
        cv = nb.mixf(nb.bw(c_sm), nb.bw(c_big), headw)
        hcol = nb.mix(srgb("#4a3a26"), srgb("#6b5433"), cv)
        hcol = nb.mix(hcol, srgb("#d9c27e"), nb.mul(edge_h, nb.mixf(0.55, 0.9, headw)))
        # 首・肩は灰色がかって淡い
        neck = nb.sub(1.0, headw)
        ncol = nb.mix(srgb("#8a8166"), srgb("#a79c7c"), cv)
        ncol = nb.mix(ncol, srgb("#cfc39e"), nb.mul(edge_h, 0.5))
        hcol = nb.mix(hcol, ncol, nb.mul(neck, 0.75))
        # 腹側（喉・首の下）は淡黄色
        belly = nb.smooth(nz, -0.1, -0.6)
        hcol = nb.mix(hcol, srgb("#e2d3a5"), nb.mul(belly, 0.85))
        # 嘴（角質の鞘）
        beak = nb.mul(nb.smooth(ax_, float(_tu_hd((0.715, 0, 0))[0]), float(_tu_hd((0.735, 0, 0))[0])),
                      nb.smooth(az_, 0.02, 0.005))
        hcol = nb.mix(hcol, srgb("#b89a5c"), nb.mul(beak, 0.85))
        # 目: 暗い眼球
        eye = None
        for sy in (1, -1):
            ec = _tu_hd((0.692, sy * 0.049, 0.035))
            de = nb.vmath("DISTANCE", a, nb.comb(*[float(v) for v in ec]), out=1)
            e = nb.smooth(de, 0.0158 * TU_HS, 0.0138 * TU_HS)
            eye = e if eye is None else nb.math("MAXIMUM", eye, e)
        hcol = nb.mix(hcol, srgb("#16110c"), eye)
        # 首のしわ
        wr = nb.math("SINE", nb.add(nb.mul(ax_, 480.0), nb.mul(nb.noise(a, 25.0, 3, 0.5), 6.0)))
        h_head = nb.add(nb.mul(edge_h, -0.8), nb.mul(nb.mul(wr, neck), 0.35))

        # --- ヒレ: 上面は暗褐色の大小の鱗に淡い縁、下面は淡黄色 ---
        span, chord = ax_, ay_
        fq = nb.comb(span, chord, 0.0)
        f_big = nb.voronoi(fq, 30.0, feature="DISTANCE_TO_EDGE", dims="2D")
        f_sm = nb.voronoi(fq, 68.0, feature="DISTANCE_TO_EDGE", dims="2D")
        fc_big = nb.bw(nb.voronoi(fq, 30.0, out="Color", dims="2D"))
        fc_sm = nb.bw(nb.voronoi(fq, 68.0, out="Color", dims="2D"))
        u = nb.sep(nb.attr("SkinC"))
        un, xc = u[0], u[1]
        bigm = nb.math("GREATER_THAN", nb.mul(nb.mul(nb.smooth(un, 0.2, 0.32), nb.smooth(un, 0.92, 0.8)),
                                              nb.smooth(xc, 0.62, 0.45)), 0.5)
        fe = nb.mixf(nb.smooth(f_sm, 0.09, 0.03), nb.smooth(f_big, 0.06, 0.02), bigm)
        fcv = nb.mixf(fc_sm, fc_big, bigm)
        fcol = nb.mix(srgb("#3f3222"), srgb("#65502f"), fcv)
        fcol = nb.mix(fcol, srgb("#d6c188"), nb.mul(fe, 0.85))
        tedge = nb.smooth(xc, 0.9, 0.97)
        fcol = nb.mix(fcol, srgb("#cdb989"), nb.mul(tedge, 0.8))
        ucol = nb.mix(srgb("#d3c49a"), srgb("#e4d7b2"), fcv)
        ucol = nb.mix(ucol, srgb("#8e8062"), nb.mul(fe, 0.7))
        ucol = nb.mix(ucol, srgb("#9c8c6a"), nb.mul(nb.smooth(un, 0.75, 1.0), 0.45))      # 先は灰褐色
        # 下面の前縁寄りには暗い鱗が少し混じる
        lead = nb.mul(nb.smooth(xc, 0.25, 0.08), nb.math("GREATER_THAN", fcv, 0.6))
        ucol = nb.mix(ucol, srgb("#6d5c40"), nb.mul(lead, 0.6))
        top_f = nb.smooth(dors, -0.3, 0.3)
        fcol = nb.mix(ucol, fcol, top_f)
        # 爪
        claw = nb.math("COMPARE", part, 3.0, 0.5)
        fcol = nb.mix(fcol, srgb("#2e261c"), claw)
        h_fl = nb.mul(fe, nb.mixf(-0.3, -0.9, top_f))
        col = nb.mix(fcol, hcol, is_head)
        height = nb.mixf(h_fl, h_head, is_head)
        rough = nb.mixf(0.5, nb.mixf(0.52, 0.12, eye), is_head)
        return dict(color=col, rough=rough, height=height, height_scale=0.0012)

    return pbr_material("TurtleSkin", fn, res=2048, ao_distance=0.25, uv="keep")


def _flipper(name, L, table, bend, sections=18, stations=None):
    """翼断面のヒレをロフトで作る（ローカル: X = 付け根→先, Y = 前縁側, Z = 背側）。
    table: u の各点での (翼弦, 厚み)。bend(u) -> (上下の曲げ角, 前後の曲げ角)（付け根からの累積, rad）
    戻り値: 頂点、面、頂点ごとの (u, 翼弦上の位置 0 前縁 → 1 後縁, 背側 ±1)"""
    us, chords, thicks = (np.array(v) for v in zip(*table))
    if stations is None:
        stations = np.concatenate([np.linspace(us[0], 0.3, 7)[:-1], np.linspace(0.3, 0.93, 10), [0.975]])
    ang = np.linspace(0, 2 * math.pi, sections, endpoint=False)
    xcs = (1 - np.cos(ang)) / 2
    sgn = np.where(np.sin(ang) >= 0, 1.0, -1.0)
    sgn[0] = 1.0
    P = np.zeros(3)
    T, Cn, Dn = np.array([1.0, 0, 0]), np.array([0, 1.0, 0]), np.array([0, 0, 1.0])
    rows, info = [], []
    prev = stations[0]
    a_prev = bend(prev)
    for i, u in enumerate(stations):
        if i:
            a_cur = bend(u)
            dd, ds = a_cur[0] - a_prev[0], a_cur[1] - a_prev[1]
            # 上下の曲げ（翼弦軸まわり）と前後の曲げ（背軸まわり）
            T, Dn = T * math.cos(dd) + Dn * math.sin(dd), -T * math.sin(dd) + Dn * math.cos(dd)
            T, Cn = T * math.cos(ds) - Cn * math.sin(ds), T * math.sin(ds) + Cn * math.cos(ds)
            P = P + T * (u - prev) * L
            a_prev, prev = a_cur, u
        ch = float(np.interp(u, us, chords))
        th = float(np.interp(u, us, thicks))
        e = float(_smooth01(u, 0.22, 0.04))                   # 付け根は楕円断面
        yt = 5 * (0.2969 * np.sqrt(xcs) - 0.126 * xcs - 0.3516 * xcs ** 2 + 0.2843 * xcs ** 3 - 0.1036 * xcs ** 4)
        half = th * (yt * (1 - e) + np.sqrt(np.clip(1 - (2 * xcs - 1) ** 2, 0, 1)) * 0.5 * e)
        half = np.maximum(half, 0.0016)
        cc = (0.33 - xcs) * ch
        ring = P[None] + Cn[None] * cc[:, None] + Dn[None] * (sgn * half)[:, None]
        rows.append(ring)
        info.append(np.stack([np.full(sections, u), xcs, sgn], -1))
    tip = P + T * (1.0 - stations[-1]) * L + Cn * (-0.05 * float(np.interp(stations[-1], us, chords)))
    root = rows[0].mean(0) - T * 0.01
    rows = [root] + rows + [tip]
    info = [np.array([[stations[0], 0.5, 0.0]])] + info + [np.array([[1.0, 0.5, 0.0]])]
    obj, idx = _ring_mesh(name, rows)
    V = _np_verts(obj.data)
    inf = np.zeros((len(V), 3))
    for ids, f in zip(idx, info):
        inf[ids] = f
    # 後縁の線をシームにして 1 枚の島に展開（背側と腹側が前縁でつながる）
    k = sections // 2
    line = [idx[0][0]] + [r[k] for r in idx[1:-1]] + [idx[-1][0]]
    _seam_unwrap(obj, list(zip(line[:-1], line[1:])))
    return obj, inf, idx


def _place(obj, M, loc):
    """ローカルの形を回転 M・位置 loc で体の座標へ移す（変形はメッシュに適用）"""
    mw = Matrix.Translation(Vector(loc)) @ M.to_4x4()
    obj.data.transform(mw)
    obj.data.update()


def _mirror_y(obj, name):
    o2 = obj.copy()
    o2.data = obj.data.copy()
    o2.name = name
    C.link_object(o2)
    o2.data.transform(Matrix.Scale(-1, 4, Vector((0, 1, 0))))
    bm = bmesh.new()
    bm.from_mesh(o2.data)
    bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.to_mesh(o2.data)
    bm.free()
    o2.data.update()
    return o2


def _frame_to_body(span, chord):
    """ローカル X→span, Y→chord, Z→span×chord の回転"""
    x = _nrm(span)
    y = _nrm(np.asarray(chord) - x * np.dot(chord, x))
    z = np.cross(x, y)
    return Matrix([[x[0], y[0], z[0]], [x[1], y[1], z[1]], [x[2], y[2], z[2]]])


def _flipper_attrs(o, inf, part, L):
    """SkinA = (付け根からの長さ m, 翼弦上の位置 m, 0), SkinB = (部位, 背側 ±1, 0), SkinC = (u, 翼弦 0..1, 0)"""
    _outward(o)
    n = len(inf)
    _set_attr(o, "SkinA", np.stack([inf[:, 0] * L, inf[:, 1] * 0.15, np.zeros(n)], -1))
    _set_attr(o, "SkinB", np.stack([np.full(n, part), inf[:, 2], np.zeros(n)], -1))
    _set_attr(o, "SkinC", np.stack([inf[:, 0], inf[:, 1], np.zeros(n)], -1))


def turtle_flippers():
    """前ヒレ（振り上げた打ち上げの姿勢）と後ろヒレ（後ろへ流す）。右側を作って左右反転する"""
    out = []
    # --- 前ヒレ: 上腕は前寄りの開口から水平に出て、手首で持ち上がり、先は下がりつつ後ろへ流れる ---
    front = [(-0.14, 0.085, 0.056), (0.0, 0.09, 0.05), (0.1, 0.1, 0.043), (0.2, 0.122, 0.036),
             (0.32, 0.152, 0.029), (0.45, 0.158, 0.025), (0.6, 0.14, 0.02), (0.75, 0.108, 0.016),
             (0.87, 0.074, 0.012), (0.95, 0.045, 0.009), (1.0, 0.02, 0.006)]

    def fbend(u):
        up = 0.82 * _smooth01(u, 0.05, 0.34) - 0.3 * _smooth01(u, 0.42, 1.0)
        back = 0.1 * _smooth01(u, 0.0, 0.2) + 0.62 * _smooth01(u, 0.25, 1.0)
        return up, back

    fr, finf, fidx = _flipper("FlipFR", 0.52, front, fbend)
    _flipper_attrs(fr, finf, 1.0, 0.52)
    # 前縁の爪（上腕の先、手首のあたりに 1 本）
    ring = fidx[1 + 7]
    V = _np_verts(fr.data)
    le = V[ring[0]]
    nxt = V[fidx[1 + 9][0]]
    d = _nrm(nxt - le)
    cl = geo.tube_along([Vector(le - d * 0.004), Vector(le + d * 0.012 + np.array([0, 0.004, 0])),
                         Vector(le + d * 0.024 + np.array([0, 0.009, -0.002]))], [0.0055, 0.0038, 0.0008],
                        sides=6, name="ClawR")
    _weld(cl)
    unwrap(cl, seam_angle=50.0)
    ca = np.zeros((len(cl.data.vertices), 3))
    _set_attr(cl, "SkinA", ca)
    _set_attr(cl, "SkinB", np.stack([np.full(len(ca), 3.0), np.ones(len(ca)), np.zeros(len(ca))], -1))
    _set_attr(cl, "SkinC", ca)
    fr = C.join([fr, cl], "FlipFR")
    # 体の右側（-Y）: 付け根から外やや前へ。前縁は前、背側は上。少しひねる
    span = np.array([0.5, -1.0, 0.0])
    chord = np.array([1.0, 0.4, 0.0])
    M = _frame_to_body(span, chord) @ Matrix.Rotation(math.radians(-8), 3, "X")
    _place(fr, M, (0.39, -0.17, -0.03))
    # --- 後ろヒレ: 短い丸いうちわ。後ろ外へ流し、少し下げる ---
    rear = [(-0.2, 0.075, 0.048), (-0.05, 0.08, 0.043), (0.1, 0.1, 0.034), (0.3, 0.125, 0.025),
            (0.5, 0.135, 0.019), (0.7, 0.122, 0.015), (0.85, 0.092, 0.011), (0.95, 0.052, 0.008),
            (1.0, 0.02, 0.005)]

    def rbend(u):
        return -0.12 * _smooth01(u, 0.1, 1.0), 0.3 * _smooth01(u, 0.1, 1.0)

    rr, rinf, _ = _flipper("FlipRR", 0.25, rear, rbend,
                        stations=np.concatenate([np.linspace(-0.2, 0.4, 5)[:-1], np.linspace(0.4, 0.95, 7)]))
    span = np.array([-0.72, -0.62, -0.1])
    chord = np.array([0.6, -0.72, 0.0])
    M = _frame_to_body(span, chord) @ Matrix.Rotation(math.radians(6), 3, "X")
    _place(rr, M, (-0.31, -0.15, -0.055))
    _flipper_attrs(rr, rinf, 2.0, 0.25)
    out += [fr, rr]
    # 左側は鏡像（わずかに角度を変えて左右対称を崩す）
    fl = _mirror_y(fr, "FlipFL")
    fl.data.transform(Matrix.Translation((0.39, 0.17, -0.03)) @ Matrix.Rotation(math.radians(-5), 4, "X") @
                      Matrix.Translation((-0.39, -0.17, 0.03)))
    rl = _mirror_y(rr, "FlipRL")
    out += [fl, rl]
    return out


# ---------------------------------------------------------------------------
# ウミガメの骨（リグ）と泳ぎのアニメーション
# ---------------------------------------------------------------------------
TU_FPS = 24
TU_SWIM_FRAMES = 72          # 1 ストローク = 3 秒でループ
# ヒレの部品名 → (骨の名前, 骨の境目の u)。u はヒレの付け根（甲羅の縁）0 → 先 1
TU_CHAINS = {
    "FlipFR": (("FlipperFR1", "FlipperFR2", "FlipperFR3"), (0.0, 0.34, 0.68)),
    "FlipFL": (("FlipperFL1", "FlipperFL2", "FlipperFL3"), (0.0, 0.34, 0.68)),
    "FlipRR": (("FlipperRR1", "FlipperRR2"), (0.0, 0.45)),
    "FlipRL": (("FlipperRL1", "FlipperRL2"), (0.0, 0.45)),
}
TU_CLAW_U = 0.37             # 前ヒレの爪の位置（手首のあたり）
TU_NECK = ((0.40, 0.0, -0.03), (0.545, 0.0, 0.012), (0.75, 0.0, 0.01))   # 首の付け根・頭の付け根・吻の先


def _read_attr(obj, name):
    a = obj.data.color_attributes[name]
    arr = np.empty(len(obj.data.vertices) * 4, np.float32)
    a.data.foreach_get("color", arr)
    return arr.reshape(-1, 4)


def _add_weights(obj, weights):
    """weights: {骨の名前: 頂点ごとの重み}。結合すると同名の頂点グループがまとまる"""
    for bname, w in weights.items():
        idx = np.nonzero(w > 1e-4)[0]
        if len(idx) == 0:
            continue
        vg = obj.vertex_groups.get(bname) or obj.vertex_groups.new(name=bname)
        for i in idx:
            vg.add([int(i)], float(w[i]), "REPLACE")


def _chain_weights(u, edges, blend=0.06):
    """付け根から先へ骨を切り替える重み。境目の前後 blend で隣の骨と 2 本で分け合う"""
    S = [_smooth01(u, e - blend, e + blend) for e in edges]
    ws = [S[k] - (S[k + 1] if k + 1 < len(S) else 0.0) for k in range(len(S))]
    return 1.0 - S[0], ws


def _turtle_weights(shell, head, flippers):
    """部品ごとに骨の重みを付け、骨の位置（体の座標）を返す"""
    _add_weights(shell, {"Root": np.ones(len(shell.data.vertices))})
    V = _np_verts(head.data)
    x = V[:, 0]
    w_neck = _smooth01(x, 0.38, 0.46)
    w_head = _smooth01(x, 0.52, 0.58)
    _add_weights(head, {"Root": 1.0 - w_neck, "Neck": w_neck - w_head, "Head": w_head})
    chains = {}
    for o in flippers:
        names, edges = TU_CHAINS[o.name.split(".")[0]]
        V = _np_verts(o.data)
        u = _read_attr(o, "SkinC")[:, 0].astype(np.float64)
        part = _read_attr(o, "SkinB")[:, 0]
        claw = part > 2.5
        u[claw] = TU_CLAW_U
        w_root, ws = _chain_weights(u, edges)
        _add_weights(o, {"Root": w_root, **dict(zip(names, ws))})
        # 骨の位置: 各断面の中心を u に沿って補間（爪は除く）
        keep = ~claw
        us = np.unique(np.round(u[keep], 5))
        cl = np.array([V[keep & (np.abs(u - s) < 1e-4)].mean(0) for s in us])

        def at(t, us=us, cl=cl):
            return Vector([float(np.interp(t, us, cl[:, k])) for k in range(3)])

        chains[names] = [at(e) for e in edges] + [at(1.0)]
    return chains


def _stroke(w):
    """前ヒレの打ち下ろし量: 0 = 振り上げ（レストポーズ）→ 1 = 打ち下ろし。打ち下ろし 45%、戻し 55%"""
    w = w % 1.0
    return float(_smooth01(w, 0.0, 0.45)) if w < 0.45 else 1.0 - float(_smooth01(w, 0.45, 1.0))


def _stroke_twist(w, e=1e-3):
    """回内（前縁を下げる）量 -1..1。打ち下ろしの途中で最大、戻しでは逆向き"""
    d = (_stroke(w + e) - _stroke(w - e)) / (2 * e)
    return max(-1.0, min(1.0, d / 3.3))


def _swim_pose(w):
    """体の座標での各骨の回転（親に対する相対、レストの向きの軸）と Root の移動。w = 0..1 のストローク位相"""
    rad = math.radians
    tau = 2 * math.pi
    sn = math.sin
    rot = {}
    # 前ヒレ: 翼のように打ち下ろして後ろへ掃き、回内してから戻る。先の節ほど遅れてしなる
    s0, s1, s2 = _stroke(w), _stroke(w - 0.07), _stroke(w - 0.13)
    front = [dict(dep=rad(48) * s0, sweep=rad(28) * _stroke(w - 0.06), twist=rad(16) * _stroke_twist(w)),
             dict(dep=rad(48) * 0.7 * (s1 - s0), sweep=0.0, twist=rad(10) * _stroke_twist(w - 0.07)),
             dict(dep=rad(48) * 0.6 * (s2 - s1), sweep=0.0, twist=rad(6) * _stroke_twist(w - 0.13))]
    # 後ろヒレ: かじ取りのように小さく漕ぐ
    r0 = sn(tau * (w + 0.1))
    r1 = sn(tau * (w + 0.02))
    rear = [dict(dep=rad(9) * r0, sweep=rad(5) * sn(tau * (w - 0.15)), twist=rad(6) * sn(tau * w)),
            dict(dep=rad(7) * (r1 - r0), sweep=0.0, twist=0.0)]
    for side in ("R", "L"):
        for k in range(3):
            rot[f"FlipperF{side}{k + 1}"] = front[k]
        for k in range(2):
            rot[f"FlipperR{side}{k + 1}"] = rear[k]
    # 首・頭: 打ち下ろしに合わせてわずかにうなずく
    rot["Neck"] = dict(pitch=rad(3.5) * sn(tau * (w - 0.3)), yaw=rad(1.5) * sn(tau * w))
    rot["Head"] = dict(pitch=rad(2.5) * sn(tau * (w - 0.42)), yaw=0.0)
    # 体: 打ち下ろしで持ち上がり、少し前後に揺れる
    rot["Root"] = dict(pitch=rad(2.2) * sn(tau * (w - 0.05)), yaw=0.0)
    move = Vector((0.008 * sn(tau * (w - 0.3)), 0.0, 0.012 * sn(tau * (w - 0.15))))
    return rot, move


def _turtle_rig(chains, M):
    """アーマチュア（骨の位置はメッシュと同じ最終の座標）と泳ぎのアクション"""
    M3 = M.to_3x3()
    arm_data = bpy.data.armatures.new("TurtleRig")
    rig = bpy.data.objects.new("TurtleRig", arm_data)
    C.link_object(rig)
    up = M3 @ Vector((0, 0, 1))
    C._select_only([rig], rig)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_data.edit_bones

    def bone(name, head, tail, parent=None):
        b = eb.new(name)
        b.head, b.tail = M @ Vector(head), M @ Vector(tail)
        b.align_roll(up)
        if parent is not None:
            b.parent = eb[parent]
            b.use_connect = (b.head - eb[parent].tail).length < 1e-5
        return b

    bone("Root", (0.0, 0.0, 0.0), (0.25, 0.0, 0.0))
    bone("Neck", TU_NECK[0], TU_NECK[1], "Root")
    bone("Head", TU_NECK[1], TU_NECK[2], "Neck")
    for names, pts in chains.items():
        parent = "Root"
        for k, nm in enumerate(names):
            bone(nm, pts[k], pts[k + 1], parent)
            parent = nm
    bpy.ops.object.mode_set(mode="OBJECT")

    # 軸は体の座標で決めてから最終の座標へ回す
    X, Y, Z = (M3 @ Vector(a) for a in ((1, 0, 0), (0, 1, 0), (0, 0, 1)))
    rests = {b.name: b.matrix_local.to_3x3() for b in arm_data.bones}
    prev = {}
    for pb in rig.pose.bones:
        pb.rotation_mode = "QUATERNION"
    for f in range(TU_SWIM_FRAMES + 1):
        rot, move = _swim_pose(f / TU_SWIM_FRAMES)
        for pb in rig.pose.bones:
            R0 = rests[pb.name]
            r = rot[pb.name]
            if "dep" in r:
                d = (R0 @ Vector((0, 1, 0))).normalized()           # 骨の向き（付け根 → 先）
                side = 1.0 if d.dot(Y) < 0 else -1.0                 # 右 +1 / 左 -1
                h = Z.cross(d).normalized()                          # 正で先が下がる軸
                R = (Matrix.Rotation(-side * r["sweep"], 3, Z) @ Matrix.Rotation(r["dep"], 3, h) @
                     Matrix.Rotation(-side * r["twist"], 3, d))
            else:
                R = Matrix.Rotation(r["yaw"], 3, Z) @ Matrix.Rotation(r["pitch"], 3, Y)
            q = (R0.transposed() @ R @ R0).to_quaternion()
            if pb.name in prev:
                q.make_compatible(prev[pb.name])
            prev[pb.name] = q
            pb.rotation_quaternion = q
            pb.keyframe_insert("rotation_quaternion", frame=f)
            if pb.name == "Root":
                pb.location = R0.transposed() @ (M3 @ move)
                pb.keyframe_insert("location", frame=f)
    act = rig.animation_data.action
    act.name = "Swim"
    scene = bpy.context.scene
    scene.render.fps = TU_FPS
    scene.frame_start, scene.frame_end = 0, TU_SWIM_FRAMES
    scene.frame_set(0)
    return rig


def sea_turtle():
    shell = turtle_shell()
    head = turtle_head()
    V = _np_verts(head.data)
    _set_attr(head, "SkinC", np.zeros((len(V), 3)))
    smat = turtle_skin_material()
    C.assign(head, smat)
    fl = turtle_flippers()
    # 骨の重み（頂点グループ）は結合前の部品ごとに付ける（結合で同名のグループがまとまる）
    chains = _turtle_weights(shell, head, fl)
    skin = _join_repack([head] + fl, "TurtleSkin")
    C.set_smooth(skin, True)
    C.assign(skin, smat)
    parts = [shell, skin]
    pitch, roll = TU_POSE
    M = (Matrix.Rotation(pitch, 4, "Y") @ Matrix.Rotation(roll, 4, "X") @
         Matrix.Translation((0.0, 0.0, -TU_ZC)))
    for o in parts:
        o.matrix_world = M @ o.matrix_world
    return parts + [_turtle_rig(chains, M)]


# ---------------------------------------------------------------------------
# プレビュー: 砂地に並べ、ウミガメは奥の中層に浮かべる
# ---------------------------------------------------------------------------
LAYOUT = {
    "Anemone_Clownfish": ((0.02, 0.36, 0.0), 0),
    "GiantClam": ((-0.7, 0.16, 0.0), 28),
    "SeaUrchin": ((0.78, 0.6, 0.0), 0),
    "BlueStarfish": ((0.55, -0.28, 0.0), 12),
    "SeaCucumber": ((-0.36, -0.34, 0.0), -24),
    "SeaTurtle": ((0.1, 1.15, 0.72), -24),
}


def _preview_extra(objs):
    scene = bpy.context.scene
    for o in objs:
        if o.type != "MESH":
            continue
        key = next((k for k in LAYOUT if o.name.startswith(k)), None)
        if key is None:
            continue
        (x, y, z), rz = LAYOUT[key]
        if o.parent is not None and o.parent.type == "ARMATURE":
            o = o.parent  # ウミガメはアーマチュアごと動かす
        o.parent = None
        o.location = (x, y, z)
        o.rotation_euler.z += math.radians(rz)
    cam = scene.camera
    cam.location = (0.25, -1.95, 1.1)
    C.look_at(cam, (0.06, 0.4, 0.27))
    cam.data.lens = 29


PREVIEW = dict(cam_dir=(0.1, -1.0, 0.55), lens=32, extra=_preview_extra)


# ---------------------------------------------------------------------------
# ビルド
# ---------------------------------------------------------------------------
def build():
    return {
        "GiantClam": giant_clam(),
        "SeaCucumber": sea_cucumber(),
        "SeaUrchin": sea_urchin(),
        "BlueStarfish": blue_starfish(),
        "Anemone_Clownfish": anemone_clownfish(),
        "SeaTurtle": sea_turtle(),
    }
