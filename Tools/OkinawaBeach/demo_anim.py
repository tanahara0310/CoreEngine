"""揺れ・泳ぎのアニメーションを Blender で確認するデモ。動画（MP4）と GIF を書き出す

    python3 Tools/OkinawaBeach/demo_anim.py                 # beach と underwater の両方
    python3 Tools/OkinawaBeach/demo_anim.py underwater      # 片方だけ
    OKI_DEMO_SAMPLES=24 OKI_DEMO_RES=960x540                # 既定値（試しに軽くするときに変える）
    OKI_DEMO_FRAMES=12                                      # 先頭の数コマだけ描く（構図の確認用）

出力:
    Tools/OkinawaBeach/Previews/anim_<clip>.mp4（24 fps）と anim_<clip>.gif（12 fps、幅 640）
    Tools/OkinawaBeach/Blend/AnimDemo_<clip>.blend（開いて再生できる。テクスチャは Models の PNG を参照）

中身（エンジンに渡る glTF をそのまま読み込んで動かす）
    beach     : 後浜のヤシ・アダン・ハイビスカス・ブーゲンビリアが海風で揺れる（4 秒でループ）
    underwater: 礁池のパッチリーフ。ウミガメが泳いで横切り、魚の群れがホバリングし、海草が波で揺れる（6 秒）
- 植物・海草: okinawa/motion.py の OkiWind（glTF の TEXCOORD_1/2 の揺れデータを読むジオメトリノード）
- 魚: 1 匹ずつのモデルを配置 JSON（Models/Okinawa/FishSchools）どおりに並べ、OkiFishSwim で泳がせる
- ウミガメ: glTF に入っている骨のアニメーション "Swim"（3 秒）をループ再生しながら前へ進める
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
from bpy_extras import anim_utils  # noqa: E402
from mathutils import Vector  # noqa: E402
from PIL import Image  # noqa: E402

import assemble as A  # noqa: E402
from okinawa import common as C  # noqa: E402
from okinawa import motion  # noqa: E402

FPS = 24
FRAMES_DIR = os.environ.get("OKI_DEMO_OUT") or os.path.join(C.TOOL_DIR, "logs", "demo_frames")
ANIM_PLANTS = ("CoconutPalm", "Adan", "Hibiscus", "Bougainvillea")
WIND = Vector((0.35, 1.0, 0.0))        # 海から陸へ吹く昼の海風（+Y が陸側）
CURRENT = Vector((-0.5, -1.0, 0.0))    # 礁池の波の寄せ返しの向き

CLIPS = {
    # region: (x0, y0, x1, y1) この範囲の LAYOUT だけ置く
    "beach": dict(seconds=4.0, camera=((-0.4, -5.2, 1.6), (-6.0, 11.0, 5.4), 20),
                  region=(-45.0, -12.0, 15.0, 30.0)),
    "underwater": dict(seconds=6.0, camera=((-11.4, -40.2, -1.35), (-17.2, -47.4, -2.15), 26),
                       region=(-30.0, -60.0, -5.0, -34.0)),
}
# 海の中のデモだけに足すもの: 手前の藻場と枝サンゴ（揺れ・泳ぎを近くで見せる）
UNDERWATER_EXTRA = [
    ("Seagrass_Patch_B", (-12.6, -43.4, 0.0), 20, 1.0),
    ("Seagrass_Patch_A", (-17.4, -40.9, 0.0), 160, 0.8),
    ("Coral_Branch_A", (-13.08, -41.41, 0.0), 30, 0.8),
]
# 群れ（配置 JSON, (x, y, 地形からの高さ), Z 回転）。小さな魚の泳ぎが見えるようカメラの近くに置く。
# 頭（+X）を画面の左（-35 度）へ向けて体の横を見せる
UNDERWATER_SCHOOLS = [
    ("FishSchool_Green", (-13.08, -41.41, 0.6), -35),
    ("FishPair_Butterfly", (-11.77, -42.09, 0.62), -40),
    ("FishSchool_Blue", (-13.37, -45.04, 1.2), -30),
]
# ウミガメの泳ぐ道筋（地形からの高さ付き）。テーブルサンゴの上を画面の右から左へ、6 秒で約 3.6 m（0.6 m/s）
TURTLE_PATH = ((-15.43, -42.34, 1.25), (-12.63, -44.6, 1.2))


def _inside(loc, region):
    x0, y0, x1, y1 = region
    return x0 <= loc[0] <= x1 and y0 <= loc[1] <= y1


def _tile_near(loc, region, pad=20.0):
    x0, y0, x1, y1 = region
    return x0 - pad <= loc[0] <= x1 + pad and y0 - pad <= loc[1] <= y1 + pad


def _phase(o):
    """オブジェクトごとの位相（位置から決める。同じ株でも場所が違えばずれる）"""
    p = o.matrix_world.translation
    return (p.x * 0.371 + p.y * 0.613) % 1.0 * math.tau


def add_wind(o, loop, world_dir, water=False):
    """植物に OkiWind を付ける。風向きはオブジェクトの回転を戻してオブジェクト空間へ"""
    group = bpy.data.node_groups.get("OkiWind") or motion.wind_group()
    rot = o.matrix_world.to_3x3().normalized()
    d = (rot.inverted() @ world_dir).normalized()
    kw = {"Wind Dir": tuple(d), "Strength": 1.0, "Phase": _phase(o), "Loop": loop}
    if water:
        # 水中: 上下には揺れず、寄せ返しの向きへ往復する
        kw.update({"Branch Up": 0.0, "Bias": 0.25, "Main Scale": 0.0,
                   "Branch Freq": 1.0, "Branch Freq 2": 3.0, "Flutter Freq": 10.0})
    return motion.add_modifier(o, group, **kw)


def _driver(o, path, index, expr):
    fc = o.driver_add(path, index)
    fc.driver.type = "SCRIPTED"
    fc.driver.expression = expr
    return fc


def animate_school(objs, loop, data_swim):
    """群れの魚: 泳ぎの周波数をループ長の整数倍にそろえ、前後・上下にゆっくり漂わせる（ドライバー）"""
    for o in objs:
        mod = next((m for m in o.modifiers if m.type == "NODES"), None)
        if mod is None:
            continue
        ph = o["phase"]
        g = mod.node_group
        for item in g.interface.items_tree:
            if item.item_type != "SOCKET" or item.in_out != "INPUT":
                continue
            if item.name == "Body Hz":
                mod[item.identifier] = motion.loop_hz(data_swim["bodyWaveHz"], loop)
            elif item.name == "Fin Hz":
                mod[item.identifier] = motion.loop_hz(data_swim["finHz"], loop)
        fwd = o.matrix_world.to_3x3() @ Vector((1.0, 0.0, 0.0))
        fwd.z = 0.0
        fwd = fwd.normalized() * 0.03 if fwd.length > 1e-6 else Vector()
        loc = o.matrix_world.translation.copy()
        w1 = math.tau / (loop * FPS)          # 1 ループで 1 往復（ラジアン / フレーム）
        w2 = 2 * w1
        a = ph * math.tau
        for i in range(3):
            base = loc[i]
            amp = fwd[i]
            bob = 0.012 if i == 2 else 0.0
            _driver(o, "location", i, f"{base:.5f} + {amp:.5f}*sin(frame*{w1:.6f} + {a:.4f})"
                                      f" + {bob:.4f}*sin(frame*{w2:.6f} + {a * 1.3:.4f})")


def add_turtle(loop, seconds):
    """ウミガメ: 骨のアニメーションをループさせ、アーマチュアごと道筋に沿って進める"""
    path = os.path.join(C.MODELS_DIR, "SeaTurtle", "SeaTurtle.gltf")
    if not os.path.exists(path):
        print("[warn] SeaTurtle.gltf が無い")
        return None
    objs = C.import_gltf(path)
    arm = next((o for o in objs if o.type == "ARMATURE"), None)
    if arm is None or arm.animation_data is None:
        print("[warn] SeaTurtle に骨のアニメーションが無い（sealife を再ビルドする）")
        return None
    act = arm.animation_data.action
    cb = anim_utils.action_get_channelbag_for_slot(act, arm.animation_data.action_slot)
    for fc in cb.fcurves:
        fc.modifiers.new("CYCLES")
    (x0, y0, dz0), (x1, y1, dz1) = TURTLE_PATH
    p0 = Vector((x0, y0, A.terrain_height(x0, y0) + dz0))
    p1 = Vector((x1, y1, A.terrain_height(x1, y1) + dz1))
    head = (p1 - p0).normalized()
    arm.rotation_mode = "XYZ"
    arm.rotation_euler = (0.0, math.radians(-4.0), math.atan2(head.y, head.x))
    # 道筋の位置はドライバーで直線補間（アクションは骨のもの。オブジェクトの動きは別にする）
    n = seconds * FPS
    for i in range(3):
        _driver(arm, "location", i, f"{p0[i]:.5f} + ({p1[i] - p0[i]:.5f})*frame/{n:.1f}")
    return arm


def build_scene(clip):
    cfg = CLIPS[clip]
    loop = cfg["seconds"]
    C.reset_scene()
    scene = bpy.context.scene
    scene.render.fps = FPS
    cache = {}
    region = cfg["region"]
    tiles = [e for e in A.LAYOUT if e[0] in A.NO_SNAP and _tile_near(e[1], region)]
    for n, loc, rot, sc in tiles:
        A.place(n, loc, rot, sc, cache)
    A.add_seabed()
    bpy.context.view_layer.update()
    layout = [e for e in A.LAYOUT if e[0] not in A.NO_SNAP and _inside(e[1], region)]
    if clip == "underwater":
        layout += UNDERWATER_EXTRA
    items = [(n, (loc[0], loc[1], A.terrain_height(loc[0], loc[1]) + loc[2]), rot, sc) for n, loc, rot, sc in layout]
    schools = [(n, (loc[0], loc[1], A.terrain_height(loc[0], loc[1]) + loc[2]), rot)
               for n, loc, rot in UNDERWATER_SCHOOLS] if clip == "underwater" else []
    turtle = add_turtle(loop, loop) if clip == "underwater" else None
    n_wind = 0
    for n, loc, rot, sc in items:
        if n == "SeaTurtle":
            continue
        o = A.place(n, loc, rot, sc, cache, snap=False)
        if o is None:
            continue
        if n.startswith(ANIM_PLANTS):
            add_wind(o, loop, WIND)
            n_wind += 1
        elif n.startswith("Seagrass"):
            add_wind(o, loop, CURRENT, water=True)
            n_wind += 1
    n_fish = 0
    for n, loc, rot in schools:
        objs = A.place_school(n, loc, rot, cache)
        if not objs:
            print(f"[warn] {n}.json が無い")
            continue
        data = A.F.load_school(os.path.join(C.MODELS_DIR, "FishSchools", f"{n}.json"))[0]
        animate_school(objs, loop, data["swim"])
        n_fish += len(objs)
    A.add_water()
    C.setup_preview_world(strength=1.0, sun_elev=52, sun_rot=200)
    # 静止画（assemble.py）より跳ね返りを減らして 1 コマの時間を抑える（見た目はほぼ変わらない）
    scene.cycles.volume_bounces = 1
    scene.cycles.diffuse_bounces = 3
    scene.cycles.glossy_bounces = 3
    scene.cycles.transmission_bounces = 6
    scene.cycles.max_bounces = 8
    scene.cycles.volume_step_rate = 4.0
    scene.view_settings.look = "AgX - Punchy"
    loc, tgt, lens = cfg["camera"]
    C.add_camera(Vector(loc), Vector(tgt), lens)
    scene.frame_start, scene.frame_end = 0, int(loop * FPS) - 1
    scene.frame_set(0)
    print(f"[demo] {clip}: {len(items)} objects, {n_wind} swaying, {n_fish} fish, turtle={turtle is not None}")
    return scene


def configure_render(scene, clip):
    out_dir = os.path.join(FRAMES_DIR, clip)
    os.makedirs(out_dir, exist_ok=True)
    # 途中で止まったコマの空のプレースホルダーを消す（上書きしない設定なので残っていると飛ばされる）
    for f in os.listdir(out_dir):
        if os.path.getsize(os.path.join(out_dir, f)) == 0:
            os.remove(os.path.join(out_dir, f))
    w, h = (int(v) for v in os.environ.get("OKI_DEMO_RES", "960x540").split("x"))
    scene.render.engine = "CYCLES"
    scene.cycles.samples = int(os.environ.get("OKI_DEMO_SAMPLES", "24"))
    scene.cycles.use_denoising = True
    scene.cycles.transparent_max_bounces = 16
    scene.render.resolution_x, scene.render.resolution_y = w, h
    scene.render.resolution_percentage = 100
    scene.render.use_persistent_data = True       # 動かない物の BVH などをコマ間で使い回す
    scene.render.image_settings.file_format = "PNG"
    scene.render.use_overwrite = False            # 途中で止まっても続きから描ける
    scene.render.use_placeholder = True
    scene.render.filepath = os.path.join(out_dir, "f_####")
    return out_dir


def render_frames(scene, out_dir, count=None):
    end = scene.frame_end
    if count:
        scene.frame_end = min(end, scene.frame_start + count - 1)
    bpy.ops.render.render(animation=True)
    scene.frame_end = end
    return out_dir


def encode(clip, frames_dir, fps=FPS):
    """PNG 連番 → MP4（Blender の FFmpeg）と GIF（Pillow、12 fps・幅 640）"""
    files = sorted(f for f in os.listdir(frames_dir) if f.startswith("f_") and f.endswith(".png")
                   and os.path.getsize(os.path.join(frames_dir, f)) > 0)
    if not files:
        return None, None
    w, h = Image.open(os.path.join(frames_dir, files[0])).size
    scn = bpy.data.scenes.new("Encode")
    se = scn.sequence_editor_create()
    coll = se.strips if hasattr(se, "strips") else se.sequences
    st = coll.new_image("frames", os.path.join(frames_dir, files[0]), channel=1, frame_start=1)
    for f in files[1:]:
        st.elements.append(f)
    scn.frame_start, scn.frame_end = 1, len(files)
    scn.render.fps = fps
    scn.render.resolution_x, scn.render.resolution_y = w, h
    scn.render.resolution_percentage = 100
    ims = scn.render.image_settings
    if hasattr(ims, "media_type"):
        ims.media_type = "VIDEO"
    ims.file_format = "FFMPEG"
    ff = scn.render.ffmpeg
    ff.format = "MPEG4"
    ff.codec = "H264"
    ff.constant_rate_factor = "HIGH"
    ff.ffmpeg_preset = "GOOD"
    mp4 = os.path.join(C.PREVIEW_DIR, f"anim_{clip}.mp4")
    scn.render.filepath = mp4
    scn.render.use_file_extension = False
    bpy.ops.render.render(animation=True, scene=scn.name)
    # GIF: 1 コマおき（12 fps）、全体で共通のパレット
    frames = [Image.open(os.path.join(frames_dir, f)).convert("RGB") for f in files[::2]]
    gw = 640
    frames = [im.resize((gw, round(h * gw / w)), Image.LANCZOS) for im in frames]
    strip = Image.new("RGB", (gw, frames[0].height * min(len(frames), 8)))
    for i, im in enumerate(frames[:: max(1, len(frames) // 8)][:8]):
        strip.paste(im, (0, i * frames[0].height))
    pal = strip.quantize(colors=255, method=Image.MEDIANCUT)
    q = [im.quantize(palette=pal, dither=Image.FLOYDSTEINBERG) for im in frames]
    gif = os.path.join(C.PREVIEW_DIR, f"anim_{clip}.gif")
    q[0].save(gif, save_all=True, append_images=q[1:], duration=round(2000 / fps), loop=0, optimize=True)
    print(f"[encode] {mp4} ({os.path.getsize(mp4) // 1024} KB), {gif} ({os.path.getsize(gif) // 1024} KB)")
    return mp4, gif


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    clips = [a for a in args if a in CLIPS] or list(CLIPS)
    count = int(os.environ.get("OKI_DEMO_FRAMES", "0")) or None
    for clip in clips:
        scene = build_scene(clip)
        frames_dir = configure_render(scene, clip)
        C.save_blend(f"AnimDemo_{clip}")
        render_frames(scene, frames_dir, count)
        if count is None:
            encode(clip, frames_dir)


if __name__ == "__main__":
    main()
