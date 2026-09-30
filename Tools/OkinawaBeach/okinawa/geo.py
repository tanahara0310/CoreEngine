"""形状生成の補助（岩・幹・葉など）"""
import math
import random

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils import noise as mnoise

from . import common as C


def fbm(p, octaves=4, lac=2.0, gain=0.5, basis="PERLIN_ORIGINAL"):
    return mnoise.fractal(p, 1.0 - gain, lac, octaves, noise_basis=basis)


def ridged(p, octaves=4, lac=2.1, offset=1.0, gain=2.0):
    return mnoise.ridged_multi_fractal(p, 1.0, lac, octaves, offset, gain, noise_basis="PERLIN_NEW")


def cell(p):
    """ボロノイ F1, F2 距離"""
    d, _ = mnoise.voronoi(p, distance_metric="DISTANCE")
    return d[0], d[1]


def icosphere_bm(subdiv=5, radius=1.0):
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=radius)
    return bm


def displace_along_normals(bm, fn):
    bm.normal_update()
    new = [v.co + v.normal * fn(v.co, v.normal) for v in bm.verts]
    for v, c in zip(bm.verts, new):
        v.co = c


def decimate(obj, ratio=None, target_tris=None):
    if target_tris is not None:
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        ratio = min(1.0, target_tris / max(tris, 1))
    m = obj.modifiers.new("Decimate", "DECIMATE")
    m.ratio = ratio
    m.use_collapse_triangulate = True
    C.apply_modifiers(obj)
    return obj


def smooth_mod(obj, factor=0.5, iterations=2):
    m = obj.modifiers.new("Smooth", "SMOOTH")
    m.factor = factor
    m.iterations = iterations
    C.apply_modifiers(obj)


def subsurf(obj, levels=1):
    m = obj.modifiers.new("Subsurf", "SUBSURF")
    m.levels = levels
    m.render_levels = levels
    C.apply_modifiers(obj)


def bevel(obj, width=0.01, segments=2, limit_angle=30):
    m = obj.modifiers.new("Bevel", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(limit_angle)
    m.harden_normals = False
    C.apply_modifiers(obj)


def weld(obj, dist=0.0005):
    m = obj.modifiers.new("Weld", "WELD")
    m.merge_threshold = dist
    C.apply_modifiers(obj)


def tube_along(points, radii, sides=12, name="Tube", cap_start=True, cap_end=True, twist=0.0,
               uv_v_scale=1.0, ring_fn=None, bake_chunks=0, bake_margin=0.01):
    """点列に沿ったチューブ。Proc UV: u=周方向(0..1), v=長さ(m*uv_v_scale)

    ring_fn(i, j, angle) -> 半径倍率（幹の凹凸など）
    bake_chunks: >0 なら長さ方向を k 本の短冊に切って横に並べた "Bake" UV も作る
                 （細長い円柱を Smart UV で展開すると潰れるため）。0 = 自動
    """
    pts = [Vector(p) for p in points]
    n = len(pts)
    verts, faces, uvs = [], [], []
    # 平行移動フレーム
    tangents = []
    for i in range(n):
        if i == 0:
            t = pts[1] - pts[0]
        elif i == n - 1:
            t = pts[-1] - pts[-2]
        else:
            t = pts[i + 1] - pts[i - 1]
        tangents.append(t.normalized())
    up = Vector((0, 0, 1)) if abs(tangents[0].z) < 0.9 else Vector((1, 0, 0))
    normal = tangents[0].cross(up).normalized()
    frames = []
    for i in range(n):
        if i > 0:
            axis = tangents[i - 1].cross(tangents[i])
            if axis.length > 1e-6:
                ang = tangents[i - 1].angle(tangents[i])
                normal = (Matrix.Rotation(ang, 3, axis.normalized()) @ normal).normalized()
        binormal = tangents[i].cross(normal).normalized()
        frames.append((normal.copy(), binormal))
    length = [0.0]
    for i in range(1, n):
        length.append(length[-1] + (pts[i] - pts[i - 1]).length)
    for i in range(n):
        nrm, bin_ = frames[i]
        for j in range(sides + 1):
            a = j / sides * math.tau + twist * i
            r = radii[i] * (ring_fn(i, j % sides, a) if ring_fn else 1.0)
            verts.append(pts[i] + (nrm * math.cos(a) + bin_ * math.sin(a)) * r)
    ring = sides + 1
    for i in range(n - 1):
        for j in range(sides):
            a = i * ring + j
            faces.append((a, a + 1, a + ring + 1, a + ring))
            v0 = length[i] * uv_v_scale
            v1 = length[i + 1] * uv_v_scale
            uvs.append([(j / sides, v0), ((j + 1) / sides, v0), ((j + 1) / sides, v1), (j / sides, v1)])
    if cap_start:
        faces.append(tuple(reversed(range(0, sides))))
        uvs.append([(0.5 + 0.5 * math.cos(j / sides * math.tau), 0.5 + 0.5 * math.sin(j / sides * math.tau))
                    for j in reversed(range(sides))])
    if cap_end:
        base = (n - 1) * ring
        faces.append(tuple(base + j for j in range(sides)))
        uvs.append([(0.5 + 0.5 * math.cos(j / sides * math.tau), 0.5 + 0.5 * math.sin(j / sides * math.tau))
                    for j in range(sides)])
    obj = C.mesh_object(name, verts, faces, uvs)
    # ベイク用 UV（周長と長さの比から短冊の本数を決める）
    circ = sum(radii) / len(radii) * math.tau
    k = bake_chunks or max(1, round(math.sqrt(length[-1] / max(circ, 1e-4))))
    seg_len = length[-1] / k
    layer = obj.data.uv_layers.new(name="Bake")
    m = bake_margin
    # 短冊 1 本の UV 幅 w と高さ h を実寸比に合わせる
    w = (1.0 - m * (k + 1)) / k
    h = min(1.0 - 2 * m - 0.12, w * seg_len / circ) if circ > 0 else 1.0
    h = max(h, 0.05)
    scale_v = h / seg_len
    li = 0
    fi = 0
    for i in range(n - 1):
        mid = (length[i] + length[i + 1]) / 2
        c = min(k - 1, int(mid / seg_len))
        for j in range(sides):
            corners = [(j, i), (j + 1, i), (j + 1, i + 1), (j, i + 1)]
            for (jj, ii) in corners:
                u = m + c * (w + m) + jj / sides * w
                v = m + (length[ii] - c * seg_len) * scale_v
                layer.data[li].uv = (u, v)
                li += 1
    # キャップは下の余白に小さく置く
    cap_size = min(0.1, 1.0 - h - 3 * m)
    for ci in range(int(cap_start) + int(cap_end)):
        for jj in range(sides):
            a = jj / sides * math.tau
            layer.data[li].uv = (m + ci * (cap_size + m) + cap_size * (0.5 + 0.5 * math.cos(a)),
                                 1.0 - m - cap_size + cap_size * (0.5 + 0.5 * math.sin(a)))
            li += 1
    return obj


def bezier_points(p0, p1, p2, p3, count):
    out = []
    for i in range(count):
        t = i / (count - 1)
        u = 1 - t
        out.append(Vector(p0) * u ** 3 + Vector(p1) * 3 * u * u * t + Vector(p2) * 3 * u * t * t + Vector(p3) * t ** 3)
    return out


def box(name, size, loc=(0, 0, 0), rot=(0, 0, 0)):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
    obj = C.from_bmesh(name, bm)
    obj.location = loc
    obj.rotation_euler = rot
    return obj


def cylinder(name, radius, depth, loc=(0, 0, 0), rot=(0, 0, 0), verts=16, radius2=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=verts, radius1=radius,
                          radius2=radius if radius2 is None else radius2, depth=depth)
    obj = C.from_bmesh(name, bm)
    obj.location = loc
    obj.rotation_euler = rot
    return obj


def box_uv(obj, scale=1.0):
    """Proc UV に簡易ボックス投影（木目方向などに使う）"""
    me = obj.data
    layer = me.uv_layers.get("Proc") or me.uv_layers.new(name="Proc")
    for p in me.polygons:
        n = p.normal
        ax = max(range(3), key=lambda k: abs(n[k]))
        for li in p.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            if ax == 0:
                uv = (co.y, co.z)
            elif ax == 1:
                uv = (co.x, co.z)
            else:
                uv = (co.x, co.y)
            layer.data[li].uv = (uv[0] * scale, uv[1] * scale)
