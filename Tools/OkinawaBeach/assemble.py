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
NO_SNAP = {"BeachTerrain_Shore", "BeachTerrain_Flat", "Pier_Straight", "Pier_End",
           "ReefTerrain_Lagoon", "ReefTerrain_Edge", "ReefTerrain_Deep"}
REEF_TILES = ("ReefTerrain_Lagoon", "ReefTerrain_Edge", "ReefTerrain_Deep")
LAYOUT = [
    # 地形: 40m 角タイル。Shore の陸側に Flat をつなぐ
    *[("BeachTerrain_Shore", (x, 0, 0), 0, 1.0) for x in (-80, -40, 0, 40, 80)],
    *[("BeachTerrain_Flat", (x, y, 0), 0, 1.0) for x in (-80, -40, 0, 40, 80) for y in (40, 80)],
    # リーフ地形: Shore の沖側に 礁池 → リーフエッジ → 深場 をつなぐ
    *[(t, (x, y, 0), 0, 1.0) for x in (-80, -40, 0, 40, 80)
      for t, y in (("ReefTerrain_Lagoon", -40), ("ReefTerrain_Edge", -80), ("ReefTerrain_Deep", -120))],
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
    # ビーチロック（波打ち際）
    ("BeachRock_A", (-9, -1.2, -0.05), 0, 1.0),
    ("BeachRock_B", (-3.5, -1.8, -0.05), 8, 1.0),
    ("BeachRock_C", (-13.5, -2.2, -0.05), -10, 1.0),
    # 礁池: 岸寄りの海草藻場と砂地の生き物
    ("Seagrass_Patch_A", (-10, -26, 0), 0, 1.0),
    ("Seagrass_Patch_A", (6, -28, 0), 70, 1.1),
    ("Seagrass_Patch_A", (28, -25, 0), 150, 0.9),
    ("Seagrass_Patch_B", (-2, -24, 0), 0, 1.0),
    ("Seagrass_Patch_B", (14, -31, 0), 40, 1.0),
    ("Seagrass_Patch_B", (-18, -30, 0), 200, 1.2),
    ("SeaCucumber", (-6, -32, 0), 20, 1.0),
    ("SeaCucumber", (2, -36, 0), 110, 1.0),
    ("SeaCucumber", (12, -33.5, 0), 250, 0.9),
    ("SeaCucumber", (-10, -40, 0), 60, 1.1),
    ("SeaCucumber", (18, -36, 0), 300, 1.0),
    ("BlueStarfish", (-8, -36, 0), 0, 1.0),
    ("BlueStarfish", (15, -30, 0), 72, 1.0),
    ("BlueStarfish", (21, -34.5, 0), 140, 0.9),
    ("SeaTurtle", (-14.0, -45.2, 1.6), -40, 1.0),
    # パッチリーフ A（マイクロアトールを中心とした群落）
    ("Coral_MicroAtoll", (-18, -46, 0), 0, 1.0),
    ("Coral_Table_A", (-14.2, -43.2, 0), 20, 1.0),
    ("Coral_Table_B", (-21.8, -49.2, 0), 100, 1.0),
    ("Coral_Branch_A", (-14.8, -49.6, 0), 45, 1.0),
    ("Coral_Branch_B", (-20.8, -42.6, 0), 200, 1.0),
    ("Coral_Massive_B", (-22.6, -45.8, 0), 0, 1.0),
    ("Coral_Brain", (-16.6, -51.8, 0), 0, 1.0),
    ("Coral_Soft", (-13.2, -46.8, 0), 0, 1.0),
    ("Coral_Branch_B", (-18.5, -52.3, 0), 120, 0.9),
    ("GiantClam", (-16.0, -43.9, 0), 160, 1.0),
    ("SeaUrchin", (-19.8, -51.2, 0), 0, 1.0),
    ("SeaUrchin", (-12.9, -44.6, 0), 0, 0.9),
    ("Anemone_Clownfish", (-12.6, -48.9, 0), 90, 1.0),
    ("FishSchool_Green", (-14.8, -49.6, 1.2), 30, 1.0),
    ("Fish_Butterfly", (-16.4, -45.8, 0.9), 200, 1.0),
    ("FishSchool_Blue", (-19.6, -44.6, 1.0), 120, 1.0),
    # パッチリーフ B（枝サンゴ中心）
    ("Coral_Branch_A", (26, -40, 0), 0, 1.0),
    ("Coral_Branch_B", (28.6, -37.8, 0), 60, 1.0),
    ("Coral_Branch_A", (23.2, -38.2, 0), 170, 0.8),
    ("Coral_Table_A", (23.4, -42.4, 0), 80, 0.9),
    ("Coral_Massive_A", (29.2, -43.2, 0), 30, 0.8),
    ("Coral_Brain", (24.2, -36.2, 0), 90, 0.8),
    ("Coral_Soft", (27.6, -44.6, 0), 0, 1.0),
    ("SeaUrchin", (24.6, -44.2, 0), 0, 1.0),
    ("FishSchool_Blue", (26, -40, 1.4), 200, 1.0),
    # パッチリーフ C（ハマサンゴとテーブルサンゴ）
    ("Coral_Massive_A", (6, -54, 0), 0, 1.0),
    ("Coral_Table_B", (2.8, -51.4, 0), 40, 1.0),
    ("Coral_Table_A", (9.6, -56.6, 0), 150, 1.1),
    ("Coral_Branch_A", (2.4, -56.2, 0), 250, 0.9),
    ("Coral_Branch_B", (9.2, -51.0, 0), 10, 1.0),
    ("Coral_Brain", (6.6, -58.8, 0), 0, 1.0),
    ("Coral_Soft", (3.8, -58.2, 0), 0, 0.9),
    ("Coral_Massive_B", (10.8, -53.4, 0), 70, 0.9),
    ("GiantClam", (8.2, -53.2, 0), 300, 1.0),
    ("Fish_Butterfly", (7, -52, 0.9), 120, 1.0),
    # 礁原の手前（バックリーフ、水深約 2 m）。外側の急斜面は傾きが 40° を超え、立てた群体の根元が浮くので置かない
    ("Coral_Table_A", (-10, -62, 0), 0, 1.3),
    ("Coral_Massive_A", (15, -62.5, 0), 0, 1.1),
    ("Coral_Branch_A", (0, -61.5, 0), 0, 1.2),
    ("Coral_Table_B", (30, -62, 0), 0, 1.3),
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
    # 海の中・リーフ地形
    "reef_aerial": ((6, 45, 40), (4, -58, -6), 26),
    # 水面をかすめる角度だと空の映り込みで海の中が見えないので、斜め上から見下ろす
    "reef_lagoon": ((-11.5, -39.5, 6.5), (-18, -48, -2.4), 32),
    "reef_under": ((-12.2, -40.8, -1.5), (-17.6, -47.6, -1.4), 22),
}


def water_material():
    """確認用の海（Cycles）

    - 体積吸収: 赤が最も速く、青が最も遅く吸収される → 浅瀬はターコイズ、深場は紺碧
    - 影のレイは素通し: Cycles は屈折面越しの太陽光を拾えず海底が暗くなるため（コースティクスはエンジン側）
    """
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
    return mat


def add_water():
    bpy.ops.mesh.primitive_cube_add(size=1)
    w = bpy.context.object
    w.name = "PreviewWater"
    w.scale = (3000, 3000, 80)
    w.location = (0, 0, -40.0)
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
    if os.path.exists(os.path.join(C.MODELS_DIR, "ReefTerrain_Deep", "ReefTerrain_Deep.gltf")):
        g.scale = (2000, 1000, 1)
        g.location = (0, -639.9, -36.0)
    else:
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
    scene.cycles.transmission_bounces = 8
    scene.cycles.max_bounces = 10
    scene.cycles.volume_step_rate = 4.0
    scene.view_settings.look = "AgX - Punchy"
    print("placed:", sorted(set(placed)))
    for cam_name, (loc, tgt, lens) in CAMERAS.items():
        if args and cam_name not in args:
            continue
        C.add_camera(loc, tgt, lens)
        C.render(os.path.join(C.PREVIEW_DIR, f"scene_{cam_name}.jpg"), res=(1600, 900),
                 samples=int(os.environ.get("OKI_SCENE_SAMPLES", "96")))
    C.save_blend("OkinawaBeachScene")


if __name__ == "__main__":
    main()
