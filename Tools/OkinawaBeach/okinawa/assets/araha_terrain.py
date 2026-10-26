"""アラハビーチの地形（地図の浜の形から作る砂浜・海底・公園の芝生、5 バリエーション）

data/araha_map.json（OpenStreetMap の浜・海岸線）から高さを決め、波打ち際に沿って曲がる格子で 1 枚の地形を作り、
面の種類ごとにモデルを分ける。テクスチャは 40 m 周期の継ぎ目の無いタイルを焼き、UV = (x, y) / 40 で繰り返して貼る。

座標: シーンの原点（浜の中央）。x = 東、y = 北、z=0 が平均水面。どのモデルも原点に回転なしで置く。
  ArahaTerrain_SandDry  : 乾いた砂（浜の z > 0.3 m）
  ArahaTerrain_SandWet  : 濡れた砂（浜と水際の -0.5〜0.3 m）
  ArahaTerrain_SandUnder: 水中の砂（-3〜-0.5 m）
  ArahaTerrain_Seabed   : 深い海底（-3 m より深い）
  ArahaTerrain_Lawn     : 護岸から陸側（公園の芝生。遊歩道の下にも敷く）
高さの基準: 遊歩道の舗装面 = PROMENADE（護岸 seawall の z=0 をここに置く）。浜の砂は護岸の下端で PROMENADE - 0.9。
地図の浜の陸側の縁を護岸の天端の前の線、海側の縁を波打ち際（z=0）とする。
"""
import math
import os

import bpy
import numpy as np

from .. import araha_map as AM
from .. import common as C
from ..common import pbr_material, srgb
from . import terrain as TR

TILE = TR.TILE
SURFACES = ["SandDry", "SandWet", "SandUnder", "Seabed", "Lawn"]


def build_meshes(field):
    P = AM.grid_points(field.m)
    ns, nt, _ = P.shape
    px, py = P[..., 0], P[..., 1]
    region = field.classify(px, py)
    Z = field.height(px, py, region)
    # 法線は高さの式の勾配から
    e = 0.25
    hx = (field.height(px + e, py) - field.height(px - e, py)) / (2 * e)
    hy = (field.height(px, py + e) - field.height(px, py - e)) / (2 * e)
    nrm = np.stack([-hx, -hy, np.ones_like(hx)], -1)
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    # 四角形ごとの面の種類（中心の点で決める）
    cx = (px[:-1, :-1] + px[1:, :-1] + px[1:, 1:] + px[:-1, 1:]) / 4
    cy = (py[:-1, :-1] + py[1:, :-1] + py[1:, 1:] + py[:-1, 1:]) / 4
    creg = field.classify(cx, cy)
    cz = field.height(cx, cy, creg)
    # 街の地面（公園の外）は build_araha_town.py が同じ格子で書き出すので、ここでは作らない
    csurf = field.surface(creg, cz, cx, cy)
    out = {}
    for si, sname in enumerate(SURFACES):
        mask = csurf == si
        if not mask.any():
            continue
        used = np.zeros((ns, nt), bool)
        used[:-1, :-1] |= mask
        used[1:, :-1] |= mask
        used[1:, 1:] |= mask
        used[:-1, 1:] |= mask
        idx = -np.ones((ns, nt), int)
        idx[used] = np.arange(used.sum())
        verts = np.stack([px[used], py[used], Z[used]], -1)
        ii, jj = np.nonzero(mask)
        a = idx[ii, jj]
        b = idx[ii + 1, jj]
        c = idx[ii + 1, jj + 1]
        d = idx[ii, jj + 1]
        faces = np.stack([a, b, c, d], -1)
        # 上を向く並びにそろえる（格子は s が北向き、t が海側向き）
        e1 = verts[faces[0, 1]] - verts[faces[0, 0]]
        e2 = verts[faces[0, 3]] - verts[faces[0, 0]]
        if np.cross(e1, e2)[2] < 0:
            faces = faces[:, ::-1]
        obj = C.mesh_object(f"ArahaTerrain_{sname}", verts.tolist(), faces.tolist())
        me = obj.data
        layer = me.uv_layers.new(name="UVMap")
        lv = np.empty(len(me.loops), np.int32)
        me.loops.foreach_get("vertex_index", lv)
        layer.data.foreach_set("uv", (verts[lv, :2] / TILE).astype(np.float32).ravel())
        for p in me.polygons:
            p.use_smooth = True
        me.normals_split_custom_set_from_vertices([tuple(v) for v in nrm[used]])
        me.update()
        out[sname] = obj
    return out


# ---------------------------------------------------------------------------
# タイルの材質（40 m 周期）
# ---------------------------------------------------------------------------
def tile_mesh(name):
    return TR._grid_mesh(name, lambda X, Y: np.zeros_like(X))


def wet_material(name):
    def fn(nb):
        T = TR.Torus(nb)
        d = TR._dry_sand(nb, T)
        col = nb.hsv(nb.mix(d["col"], srgb("#bfb39a"), 0.35), 0.5, 1.0, 0.68)
        film = nb.smooth(T.noise(0.8, 3, 0.55, off=201.0), 0.45, 0.7)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(film, 0, 1, 1.0, 0.9))
        rough = nb.mixf(0.45, 0.3, film)
        height = nb.mul(d["fine"], 0.002)
        return dict(color=col, rough=rough, height=height, height_scale=1.0)

    return pbr_material(name, fn, res=1024, uv="keep", ao_strength=0.0)


def under_material(name, deep):
    """水中の白砂。汀線に平行（南北）な砂漣。deep は海草の群落と礫が混じる"""
    def fn(nb):
        T = TR.Torus(nb)
        d = TR._dry_sand(nb, T)
        col = nb.ramp(d["big"], [(0.3, srgb("#e2d7bf")), (0.5, srgb("#eadfc6")), (0.7, srgb("#efe6cf"))])
        col = nb.mix(col, srgb("#f7f2e4"), nb.mul(d["white"], 0.6))
        col = nb.mix(col, srgb("#efe4d0"), nb.mul(d["frag"], 0.8))
        k = math.tau / TILE
        warp = nb.add(nb.mul(T.noise(0.22, 3, 0.5, off=121.0), 10.0), nb.mul(T.noise(1.4, 2, 0.5, off=123.0), 1.6))
        waves = 118 if not deep else 64
        ph = nb.add(nb.add(nb.mul(T.x, waves * k), nb.mul(T.y, 9 * k)), warp)
        rip = TR.ripple(nb, ph)
        mask = nb.maprange(T.noise(0.15, 2, 0.5, off=127.0), 0.3, 0.6, 0.45, 1.0)
        col = nb.mix(col, srgb("#f3ecdc"), nb.mul(nb.mul(nb.sub(1.0, rip), mask), TR.RIPPLE_TINT))
        height = nb.mul(nb.mul(rip, mask), TR.RIPPLE_HEIGHT * (1.0 if not deep else 2.0))
        rough = 0.55
        if deep:
            # 海草（リュウキュウスガモ）の群落と、サンゴ礫の散らばり
            patch = nb.smooth(T.noise(0.18, 4, 0.6, off=301.0), 0.56, 0.62)
            blades = nb.mix(srgb("#3f5a2a"), srgb("#6b7d3a"), T.noise(14.0, 3, 0.6, off=303.0))
            col = nb.mix(col, blades, nb.mul(patch, 0.85))
            col = nb.hsv(col, 0.5, 0.9, 0.85)
            rub = nb.mul(nb.smooth(T.voronoi(2.5, off=7.0), 0.18, 0.08), nb.smooth(T.noise(0.3, 2, 0.5, off=305.0), 0.5, 0.7))
            col = nb.mix(col, srgb("#cfc4ad"), nb.mul(rub, 0.6))
            height = nb.add(height, nb.add(nb.mul(patch, 0.01), nb.mul(rub, 0.006)))
            rough = 0.75
        return dict(color=col, rough=rough, height=height, height_scale=1.0)

    return pbr_material(name, fn, res=1024, uv="keep", ao_strength=0.0)


def lawn_material(name):
    """公園の芝生（セントオーガスチングラス）。黄ばんだ所、砂の見える所、クローバー"""
    def fn(nb):
        T = TR.Torus(nb)
        big = T.noise(0.12, 3, 0.5)
        mid = T.noise(0.9, 4, 0.55, off=11.0)
        fine = T.noise(18.0, 3, 0.6, off=23.0)
        col = nb.ramp(big, [(0.3, srgb("#5a6a33")), (0.5, srgb("#6c7c3d")), (0.7, srgb("#848f4b"))])
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(mid, 0.3, 0.7, 0.88, 1.08))
        dry = nb.smooth(T.noise(0.35, 3, 0.55, off=41.0), 0.58, 0.72)
        col = nb.mix(col, srgb("#a39c62"), nb.mul(dry, 0.65))
        bare = nb.smooth(T.noise(0.5, 4, 0.6, off=43.0), 0.7, 0.78)
        col = nb.mix(col, srgb("#bfae86"), nb.mul(bare, 0.7))
        # 葉の株（明暗の粒）
        cd = T.voronoi(55.0)
        cr = nb.sep(T.voronoi(55.0, out="Color"))
        tuft = nb.smooth(cd, 0.35, 0.05)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(nb.mul(tuft, cr[0]), 0, 1, 0.85, 1.15))
        clover = nb.mul(nb.smooth(T.voronoi(9.0, off=3.0), 0.2, 0.1), nb.smooth(T.noise(0.6, 2, 0.5, off=47.0), 0.6, 0.7))
        col = nb.mix(col, srgb("#3c5a24"), nb.mul(clover, 0.6))
        height = nb.add(nb.mul(tuft, 0.6), nb.mul(fine, 0.4))
        return dict(color=col, rough=0.88, height=height, height_scale=0.012)

    return pbr_material(name, fn, res=2048, uv="keep", ao_strength=0.0)


MATERIALS = {
    "SandDry": lambda: TR.sand_material("ArahaSandDry", False),
    "SandWet": lambda: wet_material("ArahaSandWet"),
    "SandUnder": lambda: under_material("ArahaSandUnder", False),
    "Seabed": lambda: under_material("ArahaSeabed", True),
    "Lawn": lambda: lawn_material("ArahaLawn"),
}
MAT_NAMES = {"SandDry": "ArahaSandDry", "SandWet": "ArahaSandWet", "SandUnder": "ArahaSandUnder",
             "Seabed": "ArahaSeabed", "Lawn": "ArahaLawn"}


# ---------------------------------------------------------------------------
# ビルド: タイルを焼いた後、同じテクスチャで地形のモデルを書き出し直す
# ---------------------------------------------------------------------------
def _preview_extra(objs):
    scene = bpy.context.scene
    g = bpy.data.objects.get("PreviewGround")
    if g:
        bpy.data.objects.remove(g)
    for o in objs:
        if o.parent is None:
            o.location = (0, 0, 0)
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    water = bpy.context.object
    water.name = "PreviewWater"
    water.scale = (1400, 1400, 10.0)
    water.location = (0, 0, -5.0)
    C.assign(water, TR._water_material())
    scene.cycles.volume_bounces = 1
    cam = scene.camera
    cam.data.lens = 28
    cam.data.clip_end = 5000
    cam.location = (-330, -420, 260)
    C.look_at(cam, (10, 30, 0))
    scene.view_settings.exposure = -0.6


PREVIEW = dict(extra=_preview_extra)


def build():
    out = {}
    for s in SURFACES:
        t = tile_mesh(f"ArahaTerrain_{s}_tile")
        C.assign(t, MATERIALS[s]())
        out[f"ArahaTerrain_{s}"] = [t]
    return out


def after_export(exported):
    """焼いたタイルのテクスチャを使い、地図から作った地形を同じ場所へ書き出し直す"""
    field = AM.Field(AM.Map())
    meshes = build_meshes(field)
    result = []
    for vname, path in exported:
        sname = vname.split("_", 1)[1]
        obj = meshes.get(sname)
        if obj is None:
            continue
        out_dir = os.path.dirname(path)
        prefix = f"{vname}_{MAT_NAMES[sname]}"
        paths = {"color": os.path.join(out_dir, f"{prefix}_BaseColor.png"),
                 "normal": os.path.join(out_dir, f"{prefix}_Normal.png"),
                 "orm": os.path.join(out_dir, f"{prefix}_ORM.png"), "alpha": False, "double_sided": False}
        C.assign(obj, C.final_material(prefix, paths))
        obj.name = vname
        obj.data.name = vname
        for f in os.listdir(out_dir):
            if f.endswith((".gltf", ".bin")):
                os.remove(os.path.join(out_dir, f))
        for o in bpy.context.view_layer.objects:
            o.select_set(False)
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.gltf(
            filepath=path, export_format="GLTF_SEPARATE", use_selection=True, export_apply=True, export_yup=True,
            export_texcoords=True, export_normals=True, export_tangents=True, export_materials="EXPORT",
            export_image_format="AUTO", export_texture_dir="", export_animations=False, export_skins=False,
            export_morph=False, export_extras=False, export_cameras=False, export_lights=False)
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        print(f"[export] {vname}: {tris} tris (地形) -> {path}")
        result.append(obj)
    return result
