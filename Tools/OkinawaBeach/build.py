"""沖縄の海辺アセットをビルドする

使い方（Blender 同梱 Python、または pip の bpy モジュール）:
    blender -b -P Tools/OkinawaBeach/build.py -- rocks palm
    python3 Tools/OkinawaBeach/build.py rocks palm
    python3 Tools/OkinawaBeach/build.py all

アセットごとに
  Projects/Sandbox/Application/Assets/Models/Okinawa/<Variant>/<Variant>.gltf (+ .bin, PNG)
  Tools/OkinawaBeach/Blend/<module>.blend
  Tools/OkinawaBeach/Previews/<module>.jpg, <module>_textures.jpg
を書き出す。
"""
import importlib
import math
import os
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

from okinawa import common as C  # noqa: E402

ASSETS = [
    "rocks", "palm", "adan", "hibiscus", "azumaya", "shisa", "ishigaki",
    "sabani", "pier", "parasol", "props", "tetrapod", "terrain",
    # 海の中・リーフ地形
    "reef_terrain", "beachrock", "coral", "sealife", "seagrass", "fish",
]


def preview(module_name, exported, cam_dir=(1.0, -1.6, 0.75), lens=50.0, spacing=1.35, extra=None):
    """書き出した glTF を読み直して並べ、レンダリングする（エンジンへ渡るものの確認）"""
    C.reset_scene()
    objs_all = []
    x = 0.0
    items = []
    for name, path in exported:
        objs = C.import_gltf(path)
        mn, mx = C.bounds(objs)
        items.append((objs, mn, mx))
    total = sum((mx.x - mn.x) for _, mn, mx in items) * spacing
    x = -total / 2
    for objs, mn, mx in items:
        w = (mx.x - mn.x) * spacing
        for o in objs:
            if o.parent is None:
                o.location.x += x + w / 2 - (mn.x + mx.x) / 2
                o.location.y -= (mn.y + mx.y) / 2
        x += w
        objs_all += objs
    bpy.context.view_layer.update()
    mn, mx = C.bounds(objs_all)
    size = max(mx.x - mn.x, mx.y - mn.y, (mx.z - mn.z) * 1.6)
    C.preview_ground(size=size * 12 + 20)
    C.setup_preview_world()
    center = Vector(((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) * 0.42))
    d = Vector(cam_dir).normalized()
    radius = (mx - mn).length / 2
    dist = radius / math.sin(math.atan(12 / lens)) * 0.95
    C.add_camera(center + d * dist, center, lens)
    if extra:
        extra(objs_all)
    out = os.path.join(C.PREVIEW_DIR, f"{module_name}.jpg")
    C.render(out, samples=48)
    return out


def build(module_name):
    t0 = time.time()
    mod = importlib.import_module(f"okinawa.assets.{module_name}")
    C.reset_scene()
    C.seed(zlib.crc32(module_name.encode()) & 0xFFFF)
    variants = mod.build()
    exported = []
    finals = []
    for vname, objs in variants.items():
        obj, path, tris = C.bake_and_export(vname, objs)
        exported.append((vname, path))
        finals.append(obj)
        print(f"  {vname}: {tris} tris ({time.time() - t0:.0f}s)")
    C.slim_scene(finals)
    C.save_blend(module_name)
    os.makedirs(C.PREVIEW_DIR, exist_ok=True)
    C.contact_sheet(os.path.join(C.MODELS_DIR, exported[0][0]),
                    os.path.join(C.PREVIEW_DIR, f"{module_name}_textures.jpg"))
    kw = getattr(mod, "PREVIEW", {})
    preview(module_name, exported, **kw)
    print(f"[done] {module_name} in {time.time() - t0:.0f}s")


def main():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else argv[1:]
    if not args or args == ["all"]:
        args = ASSETS
    for a in args:
        build(a)


if __name__ == "__main__":
    main()
