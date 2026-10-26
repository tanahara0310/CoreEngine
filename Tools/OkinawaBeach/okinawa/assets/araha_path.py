"""安良波公園の園路（地図の歩道の線から作る舗装の帯）

data/araha_map.json の歩道の線のうち、公園の中で護岸の遊歩道から離れ、施設（araha_map.PARK_FACILITIES）に
かからない部分を、幅 PATH_W の帯にして芝生の少し上に敷く。テクスチャは 8 m 周期の継ぎ目の無いタイルを焼き、
UV = (x, y) / 8 で繰り返して貼る。

座標: シーンの原点（浜の中央）。x = 東、y = 北。モデルは原点に回転なしで置く。
"""
import math
import os

import bpy
import numpy as np

from .. import araha_map as AM
from .. import common as C
from ..common import pbr_material, srgb

PATH_TILE = 8.0
PATH_W = 2.6
LIFT = 0.025
WALL_CLEAR = 8.0


class Torus8:
    """(x, y) をトーラスに埋め込んだ 4D 座標（周期 PATH_TILE のノイズ用）"""

    def __init__(self, nb):
        co = nb.coord("Object")
        s = nb.sep(co)
        self.x, self.y = s[0], s[1]
        k = math.tau / PATH_TILE
        R = PATH_TILE / math.tau
        ax, ay = nb.mul(self.x, k), nb.mul(self.y, k)
        self.vec = nb.comb(nb.mul(nb.math("COSINE", ax), R), nb.mul(nb.math("SINE", ax), R),
                           nb.mul(nb.math("COSINE", ay), R))
        self.w = nb.mul(nb.math("SINE", ay), R)
        self.nb = nb

    def noise(self, scale, detail=3.0, rough=0.55, off=0.0):
        nb = self.nb
        vec = self.vec if not off else nb.vmath("ADD", self.vec, (off, off * 1.7, off * 0.6))
        return nb.noise(vec, scale, detail, rough, dims="4D", w=self.w)


def path_material(name):
    """灰色の平板（30 x 30 cm を馬目地）。日焼けのむら、黒ずみ、目地の砂"""
    def fn(nb):
        T = Torus8(nb)
        bw = PATH_TILE / 25
        row = nb.math("FLOOR", nb.math("DIVIDE", T.y, bw))
        u = nb.add(nb.math("DIVIDE", T.x, bw), nb.mul(nb.math("FLOORED_MODULO", row, 2.0), 0.5))
        coln = nb.math("FLOORED_MODULO", nb.math("FLOOR", u), 25.0)
        fx = nb.math("FRACT", u)
        fy = nb.math("FRACT", nb.math("DIVIDE", T.y, bw))
        edge = nb.mul(nb.math("MINIMUM", nb.math("MINIMUM", fx, nb.sub(1.0, fx)), nb.math("MINIMUM", fy, nb.sub(1.0, fy))),
                      bw)
        joint = nb.smooth(edge, 0.008, 0.003)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(nb.comb(coln, nb.math("FLOORED_MODULO", row, 25.0), 0.0), wn.inputs["Vector"])
        h1 = wn.outputs["Value"]
        col = nb.ramp(h1, [(0.0, srgb("#9a968c")), (0.5, srgb("#aca89d")), (1.0, srgb("#bcb7aa"))])
        big = T.noise(0.4, 3, 0.5)
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(big, 0.3, 0.7, 0.9, 1.06))
        grime = nb.smooth(T.noise(1.6, 4, 0.6, off=11.0), 0.55, 0.72)
        col = nb.mix(col, srgb("#6e6a60"), nb.mul(grime, 0.35))
        sand = nb.smooth(T.noise(2.2, 4, 0.6, off=23.0), 0.6, 0.75)
        col = nb.mix(col, srgb("#c9bd9c"), nb.mul(sand, 0.35))
        col = nb.mix(col, srgb("#7d7564"), joint)
        grain = T.noise(60.0, 3, 0.6, off=31.0)
        height = nb.add(nb.smooth(edge, 0.0, 0.012), nb.mul(grain, 0.12))
        return dict(color=col, rough=nb.mixf(0.8, 0.93, joint), height=height, height_scale=0.004,
                    cavity=nb.maprange(joint, 0, 1, 1.0, 0.7))

    return pbr_material(name, fn, res=2048, uv="keep", ao_strength=0.0)


def tile_mesh(name):
    h = PATH_TILE / 2
    obj = C.mesh_object(name, [(-h, -h, 0), (h, -h, 0), (h, h, 0), (-h, h, 0)], [(0, 1, 2, 3)],
                        [[(0, 0), (1, 0), (1, 1), (0, 1)]])
    obj.data.uv_layers["Proc"].name = "Bake"
    return obj


def path_mesh(m):
    verts, faces = [], []
    z = AM.LAWN + LIFT
    for line in AM.park_footways(m, wall_clear=WALL_CLEAR):
        c = AM.smooth_curve(line, passes=6)
        n = AM.normals(c)
        left = c + n * PATH_W / 2
        right = c - n * PATH_W / 2
        o = len(verts)
        for a, b in zip(left, right):
            verts += [(a[0], a[1], z), (b[0], b[1], z)]
        for i in range(len(c) - 1):
            a = o + 2 * i
            faces.append((a + 1, a + 3, a + 2, a))
    obj = C.mesh_object("ArahaPath", verts, faces)
    me = obj.data
    # 上を向く並びにそろえる
    for p in me.polygons:
        if p.normal.z < 0:
            p.flip()
    layer = me.uv_layers.new(name="UVMap")
    lv = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get("vertex_index", lv)
    co = np.array(verts)
    layer.data.foreach_set("uv", (co[lv, :2] / PATH_TILE).astype(np.float32).ravel())
    for p in me.polygons:
        p.use_smooth = False
    me.update()
    print(f"  path: {len(faces)} quads")
    return obj


def build():
    t = tile_mesh("ArahaPath_tile")
    C.assign(t, path_material("ArahaPathStone"))
    return {"ArahaPath": [t]}


def after_export(exported):
    """焼いたタイルのテクスチャで、地図から作った園路を同じ場所へ書き出し直す"""
    m = AM.Map()
    for vname, path in exported:
        obj = path_mesh(m)
        out_dir = os.path.dirname(path)
        prefix = f"{vname}_ArahaPathStone"
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
        print(f"[export] {vname}: {tris} tris (園路) -> {path}")
