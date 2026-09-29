"""書き出した glTF を読み込んで沖縄の海辺シーンを組み、確認用にレンダリングする

    python3 Tools/OkinawaBeach/assemble.py            # 全カメラ
    python3 Tools/OkinawaBeach/assemble.py main        # 指定カメラのみ

出力: Tools/OkinawaBeach/Previews/scene_<camera>.jpg, Tools/OkinawaBeach/Blend/OkinawaBeachScene.blend
まだ無いアセットは飛ばす（途中経過の確認用）。
座標: Blender Z-up, z=0 が平均水面, +Y が陸側, -Y が海側。
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

from okinawa import common as C  # noqa: E402

# (アセット名, 位置, Z 回転[度], 一様スケール)
LAYOUT = [
    # 地形（Shore を X 方向に並べ、陸側に Flat）
    ("BeachTerrain_Shore", (-40, 0, 0), 0, 1.0),
    ("BeachTerrain_Shore", (0, 0, 0), 0, 1.0),
    ("BeachTerrain_Shore", (40, 0, 0), 0, 1.0),
    ("BeachTerrain_Flat", (-40, 40, 0), 0, 1.0),
    ("BeachTerrain_Flat", (0, 40, 0), 0, 1.0),
    ("BeachTerrain_Flat", (40, 40, 0), 0, 1.0),
    # 岬側の岩場（左）
    ("NotchRock_A", (-24, -9, -0.3), 20, 1.0),
    ("NotchRock_B", (-16, -14, -0.5), -35, 1.0),
    ("ReefRock_A", (-19, -6, -0.1), 10, 1.0),
    ("ReefRock_B", (-13, -8, -0.3), 70, 1.0),
    ("ReefRock_C", (-21, -3.5, 0.0), 0, 1.0),
    ("ReefRock_B", (-27, -4, 0.1), 140, 1.2),
    ("ReefRock_C", (-11, -11, -0.4), 45, 1.3),
    ("Tetrapod_B", (30, -9, -0.6), 15, 1.0),
    ("Tetrapod_A", (32.5, -7.5, -0.4), 70, 1.0),
    ("Tetrapod_B", (34, -10.5, -0.9), 130, 1.0),
    # 植生
    ("CoconutPalm_A", (-8, 10, 1.2), 0, 1.0),
    ("CoconutPalm_B", (-4, 13, 1.5), 60, 1.0),
    ("CoconutPalm_C", (-12, 14, 1.6), 200, 1.0),
    ("CoconutPalm_A", (14, 15, 1.6), 150, 0.9),
    ("Adan_A", (-18, 11, 1.4), 0, 1.0),
    ("Adan_B", (-22, 9, 1.2), 90, 1.0),
    ("Adan_B", (6, 17, 1.7), 200, 1.0),
    ("Hibiscus_A", (2, 18, 1.7), 0, 1.0),
    ("Hibiscus_B", (4, 19, 1.7), 40, 1.0),
    ("Bougainvillea_A", (24, 18, 1.7), 0, 1.0),
    # 東屋と石垣・シーサー
    ("Azumaya_A", (10, 20, 1.8), 10, 1.0),
    ("Ishigaki_Straight", (18, 24, 1.8), 0, 1.0),
    ("Ishigaki_Straight", (22, 24, 1.8), 0, 1.0),
    ("Ishigaki_Corner", (26, 24, 1.8), 0, 1.0),
    ("Ishigaki_Low", (2, 24, 1.8), 0, 1.0),
    ("Shisa_Agyo", (15.2, 23, 1.8), 190, 1.0),
    ("Shisa_Ungyo", (17.0, 23, 1.8), 170, 1.0),
    # 浜辺
    ("Sabani_A", (3, 2.2, 0.35), 75, 1.0),
    ("Sabani_B", (6, 3.5, 0.5), 95, 1.0),
    ("BeachParasol_A", (-2, 6, 0.9), 0, 1.0),
    ("DeckChair_A", (-2.5, 4.8, 0.85), 185, 1.0),
    ("DeckChair_A", (-0.8, 4.9, 0.85), 175, 1.0),
    ("BeachParasol_B", (9, 8, 1.1), 0, 1.0),
    # 桟橋（z=0 が水面）
    ("Pier_Straight", (18, 2, 0), 0, 1.0),
    ("Pier_Straight", (18, -2, 0), 0, 1.0),
    ("Pier_Straight", (18, -6, 0), 0, 1.0),
    ("Pier_End", (18, -10, 0), 0, 1.0),
    # 小物
    ("Driftwood_A", (-6, 3, 0.45), 30, 1.0),
    ("Driftwood_B", (11, 4.5, 0.6), -60, 1.0),
    ("Coconut_Husk", (-7, 8.5, 1.0), 0, 1.0),
    ("Coconut_Husk", (-9.2, 9.0, 1.1), 90, 1.0),
    ("GlassFloat", (1, 3.6, 0.5), 0, 1.0),
    ("Shell_SpiderConch", (0.2, 2.2, 0.3), 40, 1.0),
    ("Shell_Cowrie", (-0.4, 2.5, 0.32), 0, 1.0),
    ("CoralPiece_A", (-3, 1.5, 0.2), 0, 1.0),
    ("CoralPiece_B", (4.5, 1.2, 0.18), 90, 1.0),
]

CAMERAS = {
    # 名前: (位置, 注視点, レンズ)
    "main": ((6, -38, 7.5), (2, 4, 1.5), 28),
    "beach": ((-3, -6, 1.8), (-8, 10, 3.0), 24),
    "rocks": ((-8, -24, 3.2), (-19, -8, 1.5), 32),
    "village": ((4, 6, 2.2), (14, 21, 2.2), 26),
}


def water_material():
    """確認用の海（Cycles）。浅瀬ほど砂の色が透ける透過 + 体積吸収"""
    mat = bpy.data.materials.new("PreviewWater")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (1, 1, 1, 1)
    bsdf.inputs["Roughness"].default_value = 0.02
    bsdf.inputs["IOR"].default_value = 1.333
    bsdf.inputs["Transmission Weight"].default_value = 1.0
    # 波の法線
    co = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0, 2.2, 1.0)
    nt.links.new(co.outputs["Object"], mp.inputs[0])
    wv = nt.nodes.new("ShaderNodeTexNoise")
    wv.inputs["Scale"].default_value = 0.9
    wv.inputs["Detail"].default_value = 8
    wv.inputs["Roughness"].default_value = 0.6
    nt.links.new(mp.outputs[0], wv.inputs["Vector"])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.25
    bump.inputs["Distance"].default_value = 0.05
    nt.links.new(wv.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs[0], bsdf.inputs["Normal"])
    nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
    vol = nt.nodes.new("ShaderNodeVolumeAbsorption")
    vol.inputs["Color"].default_value = (0.35, 0.86, 0.84, 1)
    vol.inputs["Density"].default_value = 0.35
    nt.links.new(vol.outputs[0], out.inputs["Volume"])
    return mat


def add_water():
    bpy.ops.mesh.primitive_cube_add(size=1)
    w = bpy.context.object
    w.name = "PreviewWater"
    w.scale = (400, 400, 10)
    w.location = (0, -180, -5.0)
    C.assign(w, water_material())
    return w


def add_fallback_ground():
    """地形アセットがまだ無いときの仮の浜"""
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 2.0, 6, 0.6)
        return dict(color=nb.mix(C.srgb("#e6dcc4"), C.srgb("#cfc2a3"), nb.mul(n, 0.5)), rough=0.9)

    mat = C.pbr_material("FallbackSand", fn, bake=False)
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=80, y_subdivisions=80, size=160)
    g = bpy.context.object
    g.name = "FallbackBeach"
    for v in g.data.vertices:
        y = v.co.y
        v.co.z = max(-4.0, min(1.8, 0.12 * y + 0.2))
    C.assign(g, mat)
    return g


def place(name, loc, rot, scale, cache):
    path = os.path.join(C.MODELS_DIR, name, f"{name}.gltf")
    if not os.path.exists(path):
        return None
    if name not in cache:
        objs = C.import_gltf(path)
        mesh = next(o for o in objs if o.type == "MESH")
        for o in objs:
            if o is not mesh:
                bpy.data.objects.remove(o)
        mesh.parent = None
        mesh.matrix_world.identity()
        cache[name] = mesh
        obj = mesh
    else:
        obj = cache[name].copy()  # メッシュは共有（リンク複製）
        C.link_object(obj)
    obj.location = loc
    obj.rotation_euler = (0, 0, math.radians(rot))
    obj.scale = (scale, scale, scale)
    return obj


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    C.reset_scene()
    cache = {}
    placed = [n for n, *rest in LAYOUT if place(n, *rest, cache) is not None]
    if not any(n.startswith("BeachTerrain") for n in placed):
        add_fallback_ground()
    add_water()
    C.setup_preview_world(strength=1.0, sun_elev=52, sun_rot=200)
    scene = bpy.context.scene
    scene.cycles.volume_bounces = 1
    scene.cycles.transmission_bounces = 6
    print("placed:", sorted(set(placed)))
    for cam_name, (loc, tgt, lens) in CAMERAS.items():
        if args and cam_name not in args:
            continue
        C.add_camera(loc, tgt, lens)
        C.render(os.path.join(C.PREVIEW_DIR, f"scene_{cam_name}.jpg"), res=(1600, 900), samples=96)
    C.save_blend("OkinawaBeachScene")


if __name__ == "__main__":
    main()
