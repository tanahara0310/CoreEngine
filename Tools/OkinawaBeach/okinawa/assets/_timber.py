"""木製アセット（サバニ・桟橋・パラソル）で共有する補助

- MeshBuilder / loft / lathe: 頂点と面頂点(CORNER)属性を直接積んでメッシュを作る
- 木目: 部材ごとのローカル座標を "WoodCo" 属性（x = 繊維方向）に書き、シェーダーで年輪・繊維・割れを作る。
  Object 座標だと部材の向きで木目の方向が変わるため、板の向きに依らず木目が長手方向に通るようにする。
  "WoodId" 属性 = (乱数1, 乱数2, フラグ) は部材ごとの色ムラや塗装の有無などに使う。
- 属性はすべて CORNER / FLOAT_COLOR で統一する（結合時にドメインが食い違わないように）。
"""
import math
import random

import bmesh
import numpy as np
from mathutils import Matrix, Vector

from .. import common as C
from .. import geo
from ..common import srgb


# ---------------------------------------------------------------------------
# メッシュ
# ---------------------------------------------------------------------------
class MeshBuilder:
    """頂点・面と、面頂点ごとの属性を積んでからメッシュ化する"""

    def __init__(self):
        self.verts = []
        self.faces = []
        self.attrs = {}  # name -> [面ごとの (角ごとの値リスト)]

    def v(self, co):
        self.verts.append(tuple(co))
        return len(self.verts) - 1

    def f(self, idx, **attrs):
        """attrs: name=値。値は tuple（面で一定）か、頂点 index -> tuple の関数"""
        self.faces.append(tuple(idx))
        fi = len(self.faces) - 1
        for k, val in attrs.items():
            self.attrs.setdefault(k, {})[fi] = val
        return fi

    def build(self, name, fix_normals=True):
        obj = C.mesh_object(name, self.verts, self.faces)
        me = obj.data
        for aname, per_face in self.attrs.items():
            arr = np.zeros((len(me.loops), 4), np.float32)
            arr[:, 3] = 1.0
            for p in me.polygons:
                val = per_face.get(p.index)
                if val is None:
                    continue
                for li in p.loop_indices:
                    vi = me.loops[li].vertex_index
                    c = val(vi) if callable(val) else val
                    arr[li, :len(c)] = c
            a = me.color_attributes.new(aname, "FLOAT_COLOR", "CORNER")
            a.data.foreach_set("color", arr.ravel())
        if fix_normals:
            recalc_normals(obj)
        return obj


def recalc_normals(obj):
    """閉じたメッシュの面の向きを外向きに揃える"""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def signed_volume(obj):
    """符号付き体積（閉じたメッシュで正なら面が外向き）"""
    me = obj.data
    me.calc_loop_triangles()
    vol = 0.0
    for t in me.loop_triangles:
        a, b, c = (me.vertices[i].co for i in t.vertices)
        vol += a.dot(b.cross(c)) / 6.0
    return vol


def loft(name, rings, cap_start=True, cap_end=True, closed=True, fix_normals=True):
    """同じ頂点数の断面リングを順につないだ筒。closed=False なら断面は開いた折れ線"""
    mb = MeshBuilder()
    n = len(rings[0])
    ids = [[mb.v(p) for p in r] for r in rings]
    seg = n if closed else n - 1
    for i in range(len(rings) - 1):
        for j in range(seg):
            j1 = (j + 1) % n
            mb.f((ids[i][j], ids[i][j1], ids[i + 1][j1], ids[i + 1][j]))
    if cap_start:
        mb.f(tuple(reversed(ids[0])))
    if cap_end:
        mb.f(tuple(ids[-1]))
    return mb.build(name, fix_normals)


def lathe(name, profile, segs=16, fix_normals=True):
    """(r, z) の断面を Z 軸まわりに回した回転体。r=0 の端は 1 点にまとめる"""
    mb = MeshBuilder()
    rows = []
    for r, z in profile:
        if r <= 1e-6:
            rows.append([mb.v((0, 0, z))])
        else:
            rows.append([mb.v((r * math.cos(a), r * math.sin(a), z))
                         for a in (k / segs * math.tau for k in range(segs))])
    for i in range(len(rows) - 1):
        a, b = rows[i], rows[i + 1]
        for k in range(segs):
            k1 = (k + 1) % segs
            if len(a) == 1 and len(b) == 1:
                continue
            if len(a) == 1:
                mb.f((a[0], b[k1], b[k]))
            elif len(b) == 1:
                mb.f((a[k], a[k1], b[0]))
            else:
                mb.f((a[k], a[k1], b[k1], b[k]))
    if len(rows[0]) > 1:
        mb.f(tuple(reversed(rows[0])))
    if len(rows[-1]) > 1:
        mb.f(tuple(rows[-1]))
    return mb.build(name, fix_normals)


def rod(name, p0, p1, radius, sides=8, radius1=None):
    """2 点間の丸棒（両端キャップ付き）"""
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    q = d.to_track_quat("Z", "Y")
    obj = lathe(name, [(0.0, 0.0), (radius, 0.0), (radius if radius1 is None else radius1, d.length),
                       (0.0, d.length)], segs=sides)
    obj.matrix_world = Matrix.Translation(p0) @ q.to_matrix().to_4x4()
    return obj


def sweep(name, pts, profile, up=(0, 0, 1), cap_start=True, cap_end=True):
    """点列に沿って 2D 断面 [(a, b), ...] を掃引。a = 横(接線×up), b = up 方向。
    戻り値 (obj, 各頂点の (長さ方向距離, a, b))"""
    pts = [Vector(p) for p in pts]
    upv = Vector(up)
    rings = []
    acc = 0.0
    info = []
    for i, p in enumerate(pts):
        if i:
            acc += (p - pts[i - 1]).length
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        side = t.cross(upv).normalized()
        u = side.cross(t).normalized()
        rings.append([p + side * a + u * b for a, b in profile])
        info += [(acc, a, b) for a, b in profile]
    obj = loft(name, rings, cap_start, cap_end)
    return obj, info


# ---------------------------------------------------------------------------
# 属性
# ---------------------------------------------------------------------------
def set_attr(obj, name, fn):
    """fn(co_local: Vector, normal: Vector) -> tuple を CORNER 属性として書く（ローカル座標で評価）"""
    me = obj.data
    arr = np.zeros((len(me.loops), 4), np.float32)
    arr[:, 3] = 1.0
    cache = {}
    for p in me.polygons:
        for li in p.loop_indices:
            vi = me.loops[li].vertex_index
            if vi not in cache:
                cache[vi] = fn(me.vertices[vi].co, p.normal)
            c = cache[vi]
            arr[li, :len(c)] = c
    a = me.color_attributes.get(name)
    if a is None or a.domain != "CORNER":
        if a is not None:
            me.color_attributes.remove(a)
        a = me.color_attributes.new(name, "FLOAT_COLOR", "CORNER")
    a.data.foreach_set("color", arr.ravel())


_PERM = {"X": (0, 1, 2), "Y": (1, 0, 2), "Z": (2, 0, 1)}


def wood_attrs(obj, rnd, axis="X", flag=0.0, pith=(0.12, 0.45)):
    """WoodCo = ローカル座標（axis が繊維方向 → x に並べ替え）+ 板ごとのずらし、WoodId = (乱数, 乱数, flag)

    年輪の中心（髄）は板の外 pith 範囲の距離に置く（板目・柾目のばらつき）
    """
    pa = _PERM[axis]
    ang = rnd.uniform(0, math.tau)
    dist = rnd.uniform(*pith)
    off = (rnd.uniform(0, 50), math.cos(ang) * dist, math.sin(ang) * dist)
    set_attr(obj, "WoodCo", lambda co, n: (co[pa[0]] + off[0], co[pa[1]] + off[1], co[pa[2]] + off[2]))
    r1, r2 = rnd.random(), rnd.random()
    set_attr(obj, "WoodId", lambda co, n: (r1, r2, flag))
    return obj


def board(name, size, loc=(0, 0, 0), rot=(0, 0, 0), rnd=None, bevel=0.004, flag=0.0, axis=None, smooth=True):
    """角材・板。木目は最も長い軸（axis で指定可）"""
    rnd = rnd or random.Random(hash(name) & 0xFFFF)
    obj = geo.box(name, size, loc, rot)
    if bevel:
        geo.bevel(obj, width=min(bevel, min(size) * 0.3), segments=1, limit_angle=30)
    if axis is None:
        axis = "XYZ"[max(range(3), key=lambda k: size[k])]
    wood_attrs(obj, rnd, axis=axis, flag=flag)
    if smooth:
        C.set_smooth(obj, True, angle=35)
    return obj


def orient(obj, loc, direction, roll=0.0):
    """ローカル Z を direction に向けて loc に置く"""
    q = Vector(direction).to_track_quat("Z", "Y")
    m = q.to_matrix().to_4x4() @ Matrix.Rotation(roll, 4, "Z")
    obj.matrix_world = Matrix.Translation(Vector(loc)) @ m
    return obj


# ---------------------------------------------------------------------------
# シェーダー部品
# ---------------------------------------------------------------------------
def white(nb, w, dims="1D"):
    n = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions=dims)
    nb.link(w, n.inputs["W"] if dims == "1D" else n.inputs["Vector"])
    return n.outputs["Value"]


def wood_grain(nb, co, rings=30.0, fibre=1.0, crack_amt=1.0, knots=0.25):
    """co = 木目座標（x が繊維方向、m 単位）。戻り値 dict:
      ring  晩材（年輪の濃い線）0..1   fib  繊維の筋 0..1   crack 干割れ 0..1
      knot  節 0..1                    var  低周波のムラ 0..1
    """
    x, y, z = nb.sep(co)
    warp = nb.noise(nb.mapping(co, scale=(0.3, 2.2, 2.2)), 1.0, 3, 0.55)
    r = nb.vmath("LENGTH", nb.comb(0.0, y, z), out=1)
    # 節のまわりで年輪がうねる
    kv = nb.voronoi(nb.mapping(co, scale=(0.4, 1.0, 1.0)), 3.2, out="Distance")
    kc = nb.bw(nb.voronoi(nb.mapping(co, scale=(0.4, 1.0, 1.0)), 3.2, out="Color"))
    has_knot = nb.math("LESS_THAN", kc, knots)
    knot = nb.mul(nb.smooth(kv, 0.10, 0.035), has_knot)
    swirl = nb.mul(nb.smooth(kv, 0.35, 0.05), has_knot)
    r = nb.add(r, nb.mul(nb.sub(warp, 0.5), 0.07))
    r = nb.add(r, nb.mul(swirl, 0.02))
    f = nb.math("FRACT", nb.mul(r, rings))
    ring = nb.pow(f, 3.0)
    fib = nb.noise(nb.mapping(co, scale=(1.2 * fibre, 70.0 * fibre, 70.0 * fibre)), 1.0, 4, 0.6)
    fib2 = nb.noise(nb.mapping(co, scale=(3.0 * fibre, 260.0 * fibre, 260.0 * fibre)), 1.0, 2, 0.5)
    fib = nb.add(nb.mul(fib, 0.6), nb.mul(fib2, 0.4))
    cn = nb.noise(nb.mapping(co, scale=(0.45, 10.0, 10.0)), 1.0, 2, 0.5, distortion=0.3)
    cmask = nb.smooth(nb.noise(nb.mapping(co, scale=(0.6, 2.5, 2.5), loc=(7, 3, 1)), 1.0, 2, 0.5), 0.56, 0.66)
    crack = nb.mul(nb.mul(nb.smooth(nb.math("ABSOLUTE", nb.sub(cn, 0.5)), 0.010, 0.0), cmask), crack_amt)
    var = nb.noise(nb.mapping(co, scale=(0.25, 1.6, 1.6)), 1.0, 3, 0.5)
    return dict(ring=ring, fib=fib, crack=crack, knot=knot, var=var)


def marine(nb, co, top=0.3):
    """潮間帯〜水中の付着物（z=0 が水面の Object 座標）。戻り値 dict:
      wet   濡れ 0..1         cover  藻や付着物の被覆 0..1
      col   付着物の色        height 付着物の凹凸      rough  付着物の粗さ
      barn  フジツボ 0..1
    """
    x, y, z = nb.sep(co)
    n = nb.noise(co, 2.5, 4, 0.6)
    edge = nb.add(z, nb.mul(nb.sub(n, 0.5), 0.3))
    wet = nb.smooth(edge, top + 0.25, top - 0.05)
    # 藻（上端はまだら、下へ行くほど厚い）
    patch = nb.noise(nb.mapping(co, scale=(1, 1, 0.6)), 4.0, 5, 0.65)
    algae = nb.mul(nb.smooth(edge, top - 0.05, top - 0.55), nb.smooth(patch, 0.3, 0.55))
    algae = nb.math("MAXIMUM", algae, nb.smooth(edge, -0.6, -1.4))
    slime = nb.noise(co, 30.0, 4, 0.6)
    # フジツボ（2 サイズ、潮間帯に密）
    band = nb.mul(nb.smooth(edge, top + 0.02, top - 0.2), nb.smooth(edge, -1.4, -0.45))
    band = nb.math("MAXIMUM", band, nb.mul(nb.smooth(edge, -0.3, -0.6), 0.25))   # 水中は疎ら
    dens = nb.mul(band, nb.smooth(nb.noise(co, 3.0, 3, 0.5), 0.25, 0.65))
    barn = None
    height = nb.mul(algae, nb.mul(slime, 0.4))
    for sc, dd in ((26.0, 0.0), (58.0, 0.25)):
        d = nb.voronoi(co, sc, out="Distance")
        cr = nb.bw(nb.voronoi(co, sc, out="Color"))
        on = nb.math("LESS_THAN", cr, nb.sub(dens, dd))
        cone = nb.smooth(d, 0.44, 0.14)
        crater = nb.smooth(d, 0.13, 0.05)
        b = nb.mul(on, nb.sub(cone, nb.mul(crater, 0.7)))
        barn = b if barn is None else nb.math("MAXIMUM", barn, b)
    height = nb.add(height, nb.mul(barn, 1.5))
    acol = nb.mix(srgb("#1d2418"), srgb("#3c5226"), nb.smooth(slime, 0.35, 0.75))
    acol = nb.mix(acol, srgb("#141612"), nb.smooth(edge, -0.8, -2.0))
    bcol = nb.mix(srgb("#4a453c"), srgb("#aaa392"), nb.smooth(barn, 0.1, 0.6))
    col = nb.mix(acol, bcol, nb.smooth(barn, 0.02, 0.2))
    cover = nb.math("MAXIMUM", nb.mul(algae, 0.95), nb.smooth(barn, 0.02, 0.2))
    rough = nb.mixf(0.42, 0.85, nb.smooth(barn, 0.02, 0.2))
    return dict(wet=wet, cover=cover, col=col, height=height, rough=rough, barn=barn)


# ---------------------------------------------------------------------------
# UV
# ---------------------------------------------------------------------------
def unwrap(obj, seam_angle=40.0, extra_seams=None, margin=0.004, method="ANGLE_BASED"):
    """シーム付きで展開して "Bake" UV に詰める（uv="keep" 用）。

    Smart UV は細長い曲面（船体・杭）を平面投影して潰れたり詰め効率が悪いので、
    鋭い辺 + extra_seams(edge) の辺をシームにして展開し、島の縮尺を揃えてから詰める。
    """
    import bpy
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    lim = math.radians(seam_angle)
    for e in bm.edges:
        if len(e.link_faces) != 2 or e.calc_face_angle(0.0) > lim or (extra_seams and extra_seams(e)):
            e.seam = True
    bm.to_mesh(me)
    bm.free()
    layer = me.uv_layers.get("Bake") or me.uv_layers.new(name="Bake")
    me.uv_layers.active = layer
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.unwrap(method=method, margin=margin)
    bpy.ops.uv.average_islands_scale()
    try:
        bpy.ops.uv.pack_islands(udim_source="CLOSEST_UDIM", rotate=True, margin=margin, shape_method="CONCAVE")
    except TypeError:
        bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")
    return layer


def mark_seams(obj, pred):
    """pred(edge) が真の辺をシームにする（結合前に印を付けておくと unwrap で使われる）"""
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    for e in bm.edges:
        if pred(e):
            e.seam = True
    bm.to_mesh(me)
    bm.free()


def ring_cuts(obj, n, rings):
    """loft/sweep で作った筒の、指定した断面リング番号の周の辺をシームにする"""
    rings = set(rings)
    mark_seams(obj, lambda e: e.verts[0].index // n == e.verts[1].index // n and e.verts[0].index // n in rings)
