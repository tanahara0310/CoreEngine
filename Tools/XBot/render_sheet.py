"""XBot.blend のアクションを、横と正面から等間隔に描いて 1 枚の画像に並べる（Blender で流す）

    blender -b Tools/XBot/XBot.blend --python Tools/XBot/render_sheet.py -- <出力フォルダ> [Idle,Walk,Run] [1 本あたりの枚数]

出力フォルダに 1 コマずつの PNG と、並べた sheet.png を書く。地面には 0.25m おきに線を引く。
"""
import math
import os
import sys

import bpy
import numpy as np

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if not ARGS:
    raise SystemExit("使い方: blender -b Tools/XBot/XBot.blend --python Tools/XBot/render_sheet.py -- <出力フォルダ> [Idle,Walk,Run] [枚数]")
OUT = ARGS[0]
ACTIONS = ARGS[1].split(",") if len(ARGS) > 1 else ["Idle", "Walk", "Run"]
COUNT = int(ARGS[2]) if len(ARGS) > 2 else 8
os.makedirs(OUT, exist_ok=True)

scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == 'ARMATURE')
for track in arm.animation_data.nla_tracks:
    track.mute = True

scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'MATERIAL'
scene.render.resolution_x = 220
scene.render.resolution_y = 340
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
if scene.world is None:
    scene.world = bpy.data.worlds.new("World")


def add_plane(location, scale, color):
    bpy.ops.mesh.primitive_plane_add(size=1, location=location)
    plane = bpy.context.active_object
    plane.scale = scale
    material = bpy.data.materials.new("Line")
    material.diffuse_color = color
    plane.data.materials.append(material)


add_plane((0, 0, -0.001), (0.25, 0.125, 1), (0.25, 0.25, 0.25, 1))
for i in range(-8, 9):
    add_plane((0, i * 0.25, 0.0), (0.6, 0.01, 1), (0.9, 0.9, 0.2, 1))


def make_camera(name, location, rotation):
    data = bpy.data.cameras.new(name)
    data.type = 'ORTHO'
    data.ortho_scale = 2.3
    camera = bpy.data.objects.new(name, data)
    scene.collection.objects.link(camera)
    camera.location = location
    camera.rotation_euler = rotation
    return camera


cameras = [make_camera("Side", (6, 0, 0.95), (math.radians(90), 0, math.radians(90))),
           make_camera("Front", (0, -6, 0.95), (math.radians(90), 0, 0))]

rows = []
for name in ACTIONS:
    action = bpy.data.actions[name]
    arm.animation_data.action = action
    first, last = action.frame_range
    frames = [int(round(first + (last - first) * k / COUNT)) for k in range(COUNT)]
    for camera in cameras:
        scene.camera = camera
        row = []
        for frame in frames:
            scene.frame_set(frame)
            scene.render.filepath = os.path.join(OUT, f"{name}_{camera.name}_{frame:03d}.png")
            bpy.ops.render.render(write_still=True)
            row.append(scene.render.filepath)
        rows.append(row)

W, H = scene.render.resolution_x, scene.render.resolution_y
sheet = np.zeros((H * len(rows), W * COUNT, 4), dtype=np.float32)
for r, row in enumerate(rows):
    for c, path in enumerate(row):
        image = bpy.data.images.load(path)
        pixels = np.array(image.pixels[:], dtype=np.float32).reshape(H, W, 4)
        top = (len(rows) - 1 - r) * H
        sheet[top:top + H, c * W:(c + 1) * W, :] = pixels
        bpy.data.images.remove(image)
result = bpy.data.images.new("sheet", W * COUNT, H * len(rows), alpha=True)
result.pixels = sheet.ravel()
result.filepath_raw = os.path.join(OUT, "sheet.png")
result.file_format = 'PNG'
result.save()
print("書き出し:", result.filepath_raw)
