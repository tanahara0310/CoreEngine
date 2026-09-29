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

# (アセット名, (x, y, z), Z 回転[度], 一様スケール)
# SNAP 以外のアセットは z を「地形の高さからのオフセット」として扱う（負で埋める）
NO_SNAP = {"BeachTerrain_Shore", "BeachTerrain_Flat", "Pier_Straight", "Pier_End"}
LAYOUT = [
    # 地形: 40m 角タイル。Shore の陸側に Flat をつなぐ
    *[("BeachTerrain_Shore", (x, 0, 0), 0, 1.0) for x in (-80, -40, 0, 40, 80)],
    *[("BeachTerrain_Flat", (x, y, 0), 0, 1.0) for x in (-80, -40, 0, 40, 80) for y in (40, 80)],
    # 左: 石灰岩の岩場（浅瀬に立つノッチ岩）
    ("NotchRock_A", (-25, -4.5, -0.2), 20, 1.0),
    ("NotchRock_B", (-16, -6.5, -0.15), -35, 1.0),
    ("NotchRock_B", (-33, -8, -0.2), 120, 0.8),
    ("ReefRock_A", (-21, -3.5, -0.15), 10, 1.0),
    ("ReefRock_B", (-14, -6, -0.1), 70, 1.0),
    ("ReefRock_C", (-22, -1.0, -0.05), 0, 1.0),
    ("ReefRock_B", (-29, -2.5, -0.1), 140, 1.2),
    ("ReefRock_C", (-11, -9, -0.1), 45, 1.3),
    ("ReefRock_A", (-36, -5, -0.2), 200, 1.3),
    # 右: 消波ブロック
    ("Tetrapod_B", (36, -6, -0.3), 15, 1.0),
    ("Tetrapod_A", (38.2, -4.3, -0.2), 70, 1.0),
    ("Tetrapod_B", (39.5, -7.5, -0.3), 130, 1.0),
    ("Tetrapod_A", (41.5, -5.0, -0.3), 200, 1.0),
    ("Tetrapod_B", (37.5, -9.0, -0.4), 300, 1.0),
    # 植生（後浜）
    ("CoconutPalm_A", (-8, 8, -0.05), 0, 1.0),
    ("CoconutPalm_B", (-3.5, 11, -0.05), 60, 1.0),
    ("CoconutPalm_C", (-11, 12.5, -0.05), 200, 1.0),
    ("CoconutPalm_A", (15, 12, -0.05), 150, 0.9),
    ("CoconutPalm_B", (27, 9, -0.05), 250, 1.05),
    ("CoconutPalm_C", (30, 14, -0.05), 20, 1.1),
    ("Adan_A", (-19, 8, -0.1), 0, 1.0),
    ("Adan_B", (-23, 6.5, -0.1), 90, 1.0),
    ("Adan_A", (-30, 9, -0.1), 160, 0.9),
    ("Adan_B", (4, 14, -0.1), 200, 1.0),
    ("Hibiscus_A", (1.0, 17, -0.05), 0, 1.0),
    ("Hibiscus_B", (3.2, 18, -0.05), 40, 1.0),
    ("Hibiscus_A", (19.5, 19, -0.05), 90, 0.9),
    ("Bougainvillea_A", (22.5, 19.6, -0.05), 0, 1.0),
    # 東屋・石垣・シーサー
    ("Azumaya_A", (10, 16, -0.02), 10, 1.0),
    ("Ishigaki_Straight", (14, 21, -0.05), 0, 1.0),
    ("Ishigaki_Straight", (18, 21, -0.05), 0, 1.0),
    ("Ishigaki_Corner", (22, 21, -0.05), 0, 1.0),
    ("Ishigaki_Low", (4, 21, -0.05), 0, 1.0),
    ("Ishigaki_Straight", (-2, 21, -0.05), 0, 1.0),
    ("Shisa_Agyo", (7.6, 20.2, -0.02), 0, 1.0),
    ("Shisa_Ungyo", (12.4, 20.2, -0.02), 0, 1.0),
    # 浜辺（サバニは船首 +X、チェアは頭側 -X）
    ("Sabani_A", (4, 1.2, -0.02), 100, 1.0),
    ("Sabani_B", (7.5, 2.4, -0.02), 80, 1.0),
    ("BeachParasol_A", (-2, 6.5, 0), 0, 1.0),
    ("DeckChair_A", (-3.2, 5.6, 0), 80, 1.0),
    ("DeckChair_A", (-1.0, 5.4, 0), 95, 1.0),
    ("BeachParasol_B", (8.5, 7.5, 0), 0, 1.0),
    ("DeckChair_A", (8.0, 6.2, 0), 90, 1.0),
    # 桟橋（z=0 が水面。モジュールは +Y へ 4m 伸びるので 180 度回して沖へ）
    ("Pier_Straight", (20, 2.5, 0), 180, 1.0),
    ("Pier_Straight", (20, -1.5, 0), 180, 1.0),
    ("Pier_Straight", (20, -5.5, 0), 180, 1.0),
    ("Pier_Straight", (20, -9.5, 0), 180, 1.0),
    ("Pier_End", (20, -13.5, 0), 180, 1.0),
    # 小物
    ("Driftwood_A", (-6, 2.5, 0), 30, 1.0),
    ("Driftwood_B", (12, 4.2, 0), -60, 1.0),
    ("Coconut_Husk", (-7, 7.0, 0), 0, 1.0),
    ("Coconut_Husk", (-9.2, 7.6, 0), 90, 1.0),
    ("GlassFloat", (1, 3.2, 0), 0, 1.0),
    ("Shell_SpiderConch", (0.2, 1.8, 0), 40, 1.0),
    ("Shell_Cowrie", (-0.4, 2.1, 0), 0, 1.0),
    ("CoralPiece_A", (-3, 1.0, 0), 0, 1.0),
    ("CoralPiece_B", (5.5, 0.6, 0), 90, 1.0),
    ("CoralPiece_A", (13, 1.5, 0), 120, 1.0),
]

CAMERAS = {
    # 名前: (位置, 注視点, レンズ)
    "main": ((2, -40, 8.0), (2, 6, 2.0), 26),
    "beach": ((-1, -3, 2.2), (-8, 12, 3.5), 22),
    "rocks": ((-8, -20, 2.6), (-23, -5, 2.2), 30),
    "village": ((3, 7, 2.4), (10, 17.5, 2.6), 30),
    "pier": ((29, -21, 3.5), (17, 0, 1.2), 28),
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
    w.scale = (3000, 3000, 10)
    w.location = (0, 0, -5.0)
    C.apply_transform(w)  # 波ノイズを実寸で効かせる
    C.assign(w, water_material())
    return w


def add_seabed():
    """地形タイルより沖の海底（確認用）"""
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 0.3, 4, 0.6)
        return dict(color=nb.mix(C.srgb("#d9cfb4"), C.srgb("#b9ab86"), nb.mul(n, 0.6)), rough=0.8)

    mat = C.pbr_material("PreviewSeabed", fn, bake=False)
    bpy.ops.mesh.primitive_plane_add(size=1)
    g = bpy.context.object
    g.name = "PreviewSeabed"
    g.scale = (2000, 1000, 1)
    g.location = (0, -519.9, -3.05)
    C.apply_transform(g)
    C.assign(g, mat)
    return g


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


def terrain_height(x, y):
    dg = bpy.context.evaluated_depsgraph_get()
    hit, loc, *_ = bpy.context.scene.ray_cast(dg, Vector((x, y, 100)), Vector((0, 0, -1)))
    return loc.z if hit else 0.0


def place(name, loc, rot, scale, cache, snap=True):
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
    if snap and name not in NO_SNAP:
        loc = (loc[0], loc[1], terrain_height(loc[0], loc[1]) + loc[2])
    obj.location = loc
    obj.rotation_euler = (0, 0, math.radians(rot))
    obj.scale = (scale, scale, scale)
    return obj


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    C.reset_scene()
    cache = {}
    # 先に地形を置き、ほかはその高さに合わせる
    placed = [n for n, *rest in LAYOUT if n in NO_SNAP and place(n, *rest, cache) is not None]
    if not any(n.startswith("BeachTerrain") for n in placed):
        add_fallback_ground()
    add_seabed()
    bpy.context.view_layer.update()
    # 高さは地形だけがある状態で先に全部調べる（後から置いた物にレイが当たらないように）
    items = [(n, (loc[0], loc[1], terrain_height(loc[0], loc[1]) + loc[2]), rot, sc)
             for n, loc, rot, sc in LAYOUT if n not in NO_SNAP]
    placed += [n for n, *rest in items if place(n, *rest, cache, snap=False) is not None]
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
