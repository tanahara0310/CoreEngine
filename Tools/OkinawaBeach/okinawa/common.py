"""沖縄の海辺アセット生成の共通処理

Blender (bpy) 上でプロシージャル材質を組み、PBR テクスチャ
（BaseColor / Normal / ORM）へベイクして glTF (.gltf + .bin + PNG) を書き出す。

エンジン側の読み込み仕様（Project/Engine/Src/Graphics/Model/ModelLoader.cpp）
  - Assimp で読むため glb の埋め込みテクスチャは使えない → GLTF_SEPARATE
  - metallicRoughness は G = roughness, B = metallic
  - occlusion は R チャンネル（ORM を 1 枚にまとめて両方から参照する）
  - 全マテリアルで alpha <= alphaCutoff(0.5) を破棄する → 不透明物の alpha は必ず 1
"""
import math
import os
import random

import bpy
import bmesh
import numpy as np
from mathutils import Vector
from PIL import Image

TOOL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_ROOT = os.path.dirname(os.path.dirname(TOOL_DIR))
MODELS_DIR = os.path.join(REPO_ROOT, "Projects", "Sandbox", "Application", "Assets", "Models", "Okinawa")
PREVIEW_DIR = os.path.join(TOOL_DIR, "Previews")
BLEND_DIR = os.path.join(TOOL_DIR, "Blend")

# マテリアル名 → ベイク設定（build 中に登録される）
_REGISTRY = {}


# ---------------------------------------------------------------------------
# シーン
# ---------------------------------------------------------------------------
def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _REGISTRY.clear()
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.unit_settings.system = "METRIC"
    return scene


def seed(value):
    random.seed(value)
    np.random.seed(value)


def link_object(obj, collection=None):
    (collection or bpy.context.scene.collection).objects.link(obj)
    return obj


def mesh_object(name, verts, faces, uvs=None, collection=None):
    """頂点・面リストからメッシュを作る。uvs は面ごとのループ UV リスト"""
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    if uvs is not None:
        layer = me.uv_layers.new(name="Proc")
        i = 0
        for poly_uvs in uvs:
            for uv in poly_uvs:
                layer.data[i].uv = uv
                i += 1
    me.update()
    obj = bpy.data.objects.new(name, me)
    return link_object(obj, collection)


def from_bmesh(name, bm, collection=None):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    return link_object(obj, collection)


def set_smooth(obj, smooth=True, angle=None):
    for p in obj.data.polygons:
        p.use_smooth = smooth
    if smooth and angle is not None:
        with bpy.context.temp_override(object=obj, selected_editable_objects=[obj]):
            bpy.ops.object.shade_smooth_by_angle(angle=math.radians(angle))


def apply_modifiers(obj):
    bpy.context.view_layer.objects.active = obj
    for m in list(obj.modifiers):
        with bpy.context.temp_override(object=obj):
            bpy.ops.object.modifier_apply(modifier=m.name)


def apply_transform(obj):
    bpy.context.view_layer.update()  # 直前に設定した location/rotation を matrix_world に反映
    mw = obj.matrix_world.copy()
    obj.data.transform(mw)
    obj.matrix_world.identity()
    if obj.data.is_editmode:
        return
    obj.data.update()


def assign(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    return obj


def join(objs, name):
    objs = [o for o in objs if o is not None]
    bpy.context.view_layer.update()
    for o in objs:
        apply_transform(o)
    base = objs[0]
    if len(objs) > 1:
        with bpy.context.temp_override(active_object=base, object=base,
                                       selected_objects=objs, selected_editable_objects=objs):
            bpy.ops.object.join()
    base.name = name
    base.data.name = name
    return base


def add_proc_uv_from_generated(obj):
    """Proc UV が無いパーツ用（参照されないがレイヤー数を揃える）"""
    if "Proc" not in obj.data.uv_layers:
        obj.data.uv_layers.new(name="Proc")


def set_color_attribute(obj, name, fn):
    """頂点ごとの値 fn(co, normal) -> (r,g,b) を POINT カラー属性として書く"""
    me = obj.data
    attr = me.color_attributes.get(name) or me.color_attributes.new(name, "FLOAT_COLOR", "POINT")
    for v in me.vertices:
        c = fn(v.co, v.normal)
        attr.data[v.index].color = (c[0], c[1], c[2], 1.0)
    return attr


# ---------------------------------------------------------------------------
# ノード組み立て補助
# ---------------------------------------------------------------------------
class NB:
    """シェーダーノードを短く書くためのヘルパー"""

    def __init__(self, mat):
        self.mat = mat
        self.nt = mat.node_tree
        self.x = -1400

    def node(self, kind, **props):
        n = self.nt.nodes.new(kind)
        n.location = (self.x, random.uniform(-600, 600))
        self.x += 40
        for k, v in props.items():
            setattr(n, k, v)
        return n

    def link(self, a, b):
        self.nt.links.new(a, b)

    def _in(self, node, key, value):
        if value is None:
            return
        sock = node.inputs[key]
        if isinstance(value, bpy.types.NodeSocket):
            self.link(value, sock)
        else:
            if isinstance(value, (tuple, list)) and len(value) == 3 and len(sock.default_value) == 4:
                value = (*value, 1.0)
            sock.default_value = value

    # 座標系 ---------------------------------------------------------------
    def coord(self, kind="Object"):
        return self.node("ShaderNodeTexCoord").outputs[kind]

    def uv(self, name="Proc"):
        return self.node("ShaderNodeUVMap", uv_map=name).outputs["UV"]

    def attr(self, name, out="Color"):
        return self.node("ShaderNodeAttribute", attribute_name=name).outputs[out]

    def mapping(self, vec, scale=(1, 1, 1), loc=(0, 0, 0), rot=(0, 0, 0)):
        n = self.node("ShaderNodeMapping")
        self._in(n, 0, vec)
        n.inputs["Location"].default_value = loc
        n.inputs["Rotation"].default_value = rot
        n.inputs["Scale"].default_value = scale
        return n.outputs[0]

    def sep(self, vec):
        n = self.node("ShaderNodeSeparateXYZ")
        self._in(n, 0, vec)
        return n.outputs

    def comb(self, x=0.0, y=0.0, z=0.0):
        n = self.node("ShaderNodeCombineXYZ")
        self._in(n, 0, x)
        self._in(n, 1, y)
        self._in(n, 2, z)
        return n.outputs[0]

    # テクスチャ -----------------------------------------------------------
    def noise(self, vec=None, scale=5.0, detail=4.0, rough=0.55, distortion=0.0, dims="3D", out="Fac",
              w=None, lacunarity=2.0):
        n = self.node("ShaderNodeTexNoise", noise_dimensions=dims)
        self._in(n, "Vector", vec)
        if w is not None:
            self._in(n, "W", w)
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        n.inputs["Lacunarity"].default_value = lacunarity
        n.inputs["Distortion"].default_value = distortion
        return n.outputs[out]

    def voronoi(self, vec=None, scale=5.0, feature="F1", metric="EUCLIDEAN", out="Distance", rand=1.0, dims="3D",
                detail=0.0):
        n = self.node("ShaderNodeTexVoronoi", feature=feature, distance=metric, voronoi_dimensions=dims)
        self._in(n, "Vector", vec)
        n.inputs["Scale"].default_value = scale
        n.inputs["Randomness"].default_value = rand
        n.inputs["Detail"].default_value = detail
        return n.outputs[out]

    def wave(self, vec=None, scale=5.0, distortion=2.0, detail=3.0, kind="BANDS", direction="X", profile="SIN",
             out="Fac", detail_scale=1.0, phase=0.0):
        n = self.node("ShaderNodeTexWave", wave_type=kind, bands_direction=direction, wave_profile=profile)
        n.rings_direction = direction if direction in ("X", "Y", "Z", "SPHERICAL") else "X"
        self._in(n, "Vector", vec)
        n.inputs["Scale"].default_value = scale
        n.inputs["Distortion"].default_value = distortion
        n.inputs["Detail"].default_value = detail
        n.inputs["Detail Scale"].default_value = detail_scale
        n.inputs["Phase Offset"].default_value = phase
        return n.outputs[out]

    def image(self, img, vec=None, interp="Linear", ext="REPEAT"):
        n = self.node("ShaderNodeTexImage", image=img, interpolation=interp, extension=ext)
        self._in(n, "Vector", vec)
        return n.outputs

    # 演算 ----------------------------------------------------------------
    def math(self, op, a, b=0.0, c=0.0, clamp=False):
        n = self.node("ShaderNodeMath", operation=op, use_clamp=clamp)
        self._in(n, 0, a)
        self._in(n, 1, b)
        if len(n.inputs) > 2:
            self._in(n, 2, c)
        return n.outputs[0]

    def add(self, a, b): return self.math("ADD", a, b)
    def mul(self, a, b): return self.math("MULTIPLY", a, b)
    def sub(self, a, b): return self.math("SUBTRACT", a, b)
    def pow(self, a, b): return self.math("POWER", a, b)

    def maprange(self, v, a0, a1, b0=0.0, b1=1.0, clamp=True, interp="LINEAR"):
        n = self.node("ShaderNodeMapRange", clamp=clamp, interpolation_type=interp)
        self._in(n, "Value", v)
        self._in(n, "From Min", a0)
        self._in(n, "From Max", a1)
        self._in(n, "To Min", b0)
        self._in(n, "To Max", b1)
        return n.outputs[0]

    def smooth(self, v, a0, a1):
        return self.maprange(v, a0, a1, 0.0, 1.0, True, "SMOOTHSTEP")

    def vmath(self, op, a, b=None, out=0):
        n = self.node("ShaderNodeVectorMath", operation=op)
        self._in(n, 0, a)
        if b is not None:
            self._in(n, 1, b)
        return n.outputs[out]

    def mix(self, a, b, fac, blend="MIX"):
        """カラー mix（a→b を fac で）"""
        n = self.node("ShaderNodeMix", data_type="RGBA", blend_type=blend, clamp_result=True)
        self._in(n, 0, fac)
        self._in(n, 6, a)
        self._in(n, 7, b)
        return n.outputs[2]

    def mixf(self, a, b, fac):
        n = self.node("ShaderNodeMix", data_type="FLOAT")
        self._in(n, 0, fac)
        self._in(n, 2, a)
        self._in(n, 3, b)
        return n.outputs[0]

    def ramp(self, fac, stops, interp="LINEAR"):
        """stops = [(pos, (r,g,b)), ...]"""
        n = self.node("ShaderNodeValToRGB")
        cr = n.color_ramp
        cr.interpolation = interp
        while len(cr.elements) > 1:
            cr.elements.remove(cr.elements[-1])
        for i, (p, c) in enumerate(stops):
            e = cr.elements[0] if i == 0 else cr.elements.new(p)
            e.position = p
            e.color = (*c, 1.0) if len(c) == 3 else c
        self._in(n, "Fac", fac)
        return n.outputs["Color"]

    def hsv(self, col, h=0.5, s=1.0, v=1.0):
        n = self.node("ShaderNodeHueSaturation")
        self._in(n, "Color", col)
        self._in(n, "Hue", h)
        self._in(n, "Saturation", s)
        self._in(n, "Value", v)
        return n.outputs[0]

    def bw(self, col):
        n = self.node("ShaderNodeRGBToBW")
        self._in(n, 0, col)
        return n.outputs[0]


def srgb(hexstr):
    """'#RRGGBB' → リニア RGB"""
    h = hexstr.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple((x / 12.92) if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def pbr_material(name, fn, res=1024, ao_samples=64, ao_strength=1.0, uv="smart", bake=True, bump_distance=0.02,
                 double_sided=False, ao_distance=1.5, atlas_size=(1.0, 1.0)):
    """プロシージャル PBR 材質を作る。

    fn(nb) は dict を返す:
      color  : カラーソケット or (r,g,b) リニア
      rough  : ソケット or float
      metal  : ソケット or float（省略時 0）
      height : ソケット（凹凸、省略可）  height_scale で強さ
      alpha  : ソケット（葉などのカットアウト、省略可）
      cavity : ソケット（AO にかける追加の陰影 0..1、省略可）
    uv: "smart" = ベイク時に Smart UV Project で展開
        "keep"  = Bake UV（無ければ Proc UV）をそのままベイク先に使う
        "atlas" = Proc UV の 0..1 を 1 枚の平面に焼く（葉などの共有テクスチャ。AO は cavity のみ）
    double_sided: エンジンは常に裏面カリングするので、裏向きの面を複製して両面にする
    atlas_size: atlas 用平面の実寸(m)。バンプの強さの基準になる
    """
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nb = NB(mat)
    bsdf = nt.nodes["Principled BSDF"]
    out = fn(nb)
    nb._in(bsdf, "Base Color", out["color"])
    nb._in(bsdf, "Roughness", out.get("rough", 0.8))
    nb._in(bsdf, "Metallic", out.get("metal", 0.0))
    if out.get("height") is not None:
        bump = nb.node("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 1.0
        bump.inputs["Distance"].default_value = out.get("height_scale", bump_distance)
        nb.link(out["height"], bump.inputs["Height"])
        if out.get("normal") is not None:
            nb.link(out["normal"], bump.inputs["Normal"])
        nb.link(bump.outputs[0], bsdf.inputs["Normal"])
    elif out.get("normal") is not None:
        nb.link(out["normal"], bsdf.inputs["Normal"])
    if out.get("alpha") is not None:
        nb._in(bsdf, "Alpha", out["alpha"])
    _REGISTRY[mat.name] = dict(out=out, res=res, ao_samples=ao_samples, ao_strength=ao_strength, uv=uv,
                               bake=bake, bsdf=bsdf, double_sided=double_sided, ao_distance=ao_distance,
                               atlas_size=atlas_size)
    return mat


# ---------------------------------------------------------------------------
# テクスチャ画像入出力
# ---------------------------------------------------------------------------
def _to_srgb(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1.0 / 2.4) - 0.055)


def _img_to_np(img):
    w, h = img.size
    arr = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(arr)
    return arr.reshape(h, w, 4)[::-1]  # 上下反転（Blender は下から）


def save_png(path, arr, mode):
    arr8 = (np.clip(arr, 0, 1) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(arr8, mode).save(path, optimize=True)


def _new_bake_image(name, res):
    img = bpy.data.images.new(name, res, res, alpha=True, float_buffer=True)
    img.generated_color = (0.0, 0.0, 0.0, 0.0)  # ベイクされた画素だけ alpha=1 になる
    img.colorspace_settings.name = "Non-Color"
    return img


def _filled(arr, erode=0):
    """ベイクされなかった画素（alpha=0）を最寄りの画素で埋める

    erode: 島の縁の画素を何 px 捨ててから埋めるか（AO は縁が 0 に焼けやすい）
    """
    mask = arr[..., 3] > 0.5
    if erode:
        from scipy import ndimage
        eroded = ndimage.binary_erosion(mask, iterations=erode)
        if eroded.any():
            mask = eroded
    return dilate(arr, mask)


def dilate(rgb, mask, iterations=None):
    """UV 島の外側を最も近い島の色で埋める（ミップ時のにじみ対策）"""
    from scipy import ndimage
    if mask.all() or not mask.any():
        return rgb
    _, (iy, ix) = ndimage.distance_transform_edt(~mask, return_indices=True)
    return rgb[iy, ix]


# ---------------------------------------------------------------------------
# ベイク
# ---------------------------------------------------------------------------
def _select_only(objs, active):
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active


def smart_uv(obj, angle=40.0, margin=0.004, uv_name="Bake"):
    me = obj.data
    layer = me.uv_layers.get(uv_name) or me.uv_layers.new(name=uv_name)
    me.uv_layers.active = layer
    _select_only([obj], obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(angle), island_margin=margin, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    try:
        bpy.ops.uv.pack_islands(udim_source="CLOSEST_UDIM", rotate=True, margin=margin, shape_method="CONCAVE")
    except TypeError:
        bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")
    return layer


def _bake(obj, img, kind, samples, mat):
    nt = mat.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    for n in nt.nodes:
        n.select = False
    tex.select = True
    nt.nodes.active = tex
    scene = bpy.context.scene
    scene.cycles.samples = samples
    scene.render.bake.margin = 0
    scene.render.bake.use_clear = False
    scene.render.bake.target = "IMAGE_TEXTURES"
    _select_only([obj], obj)
    if kind == "NORMAL":
        scene.render.bake.normal_space = "TANGENT"
        bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", margin=0, use_clear=False)
    else:
        bpy.ops.object.bake(type=kind, margin=0, use_clear=False)
    nt.nodes.remove(tex)


def _bake_socket(obj, mat, socket_value, res, samples=4, tag="v"):
    """任意のソケットを Emission 経由でベイクして (H,W,4) 配列を返す"""
    nt = mat.node_tree
    outn = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL" and n.is_active_output)
    old = outn.inputs["Surface"].links[0].from_socket if outn.inputs["Surface"].links else None
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = 1.0
    if isinstance(socket_value, bpy.types.NodeSocket):
        nt.links.new(socket_value, em.inputs["Color"])
    else:
        v = socket_value
        if not isinstance(v, (tuple, list)):
            v = (v, v, v)
        em.inputs["Color"].default_value = (*v[:3], 1.0)
    nt.links.new(em.outputs[0], outn.inputs["Surface"])
    img = _new_bake_image(f"_bake_{tag}", res)
    _bake(obj, img, "EMIT", samples, mat)
    arr = _filled(_img_to_np(img))
    bpy.data.images.remove(img)
    nt.nodes.remove(em)
    if old is not None:
        nt.links.new(old, outn.inputs["Surface"])
    return arr


def _coverage_mask(obj, res):
    """UV 島が覆う画素のマスク（dilate 用）"""
    from PIL import ImageDraw
    me = obj.data
    uvl = me.uv_layers.active.data
    im = Image.new("L", (res, res), 0)
    d = ImageDraw.Draw(im)
    for p in me.polygons:
        pts = [(uvl[li].uv[0] * res, (1.0 - uvl[li].uv[1]) * res) for li in p.loop_indices]
        d.polygon(pts, fill=255)
    return np.asarray(im) > 0


def bake_part(obj, mat, out_dir, prefix):
    """1 マテリアル分のパーツをベイクして PNG を書く。戻り値は画像パス dict"""
    info = _REGISTRY[mat.name]
    res = max(64, int(info["res"] * float(os.environ.get("OKI_RES_SCALE", "1"))))
    out = info["out"]
    part = obj
    if info["uv"] == "atlas":
        # 0..1 の UV を持つ平面に焼く
        sx, sy = info["atlas_size"]
        obj = mesh_object("_atlas", [(0, 0, 0), (sx, 0, 0), (sx, sy, 0), (0, sy, 0)], [(0, 1, 2, 3)],
                          [[(0, 0), (1, 0), (1, 1), (0, 1)]])
        obj.data.materials.append(mat)
        obj.data.uv_layers.active = obj.data.uv_layers["Proc"]
        part.data.uv_layers.active = part.data.uv_layers["Proc"]
    elif info["uv"] == "keep":
        me = obj.data
        layer = me.uv_layers.get("Bake") or me.uv_layers["Proc"]
        me.uv_layers.active = layer
    else:
        smart_uv(obj)

    color = _bake_socket(obj, mat, out["color"], res, samples=6, tag="col")[..., :3]
    rough = out.get("rough", 0.8)
    rough = _bake_socket(obj, mat, rough, res, samples=4, tag="rough")[..., 0] if isinstance(
        rough, bpy.types.NodeSocket) else np.full((res, res), rough, np.float32)
    metal = out.get("metal", 0.0)
    metal = _bake_socket(obj, mat, metal, res, samples=4, tag="metal")[..., 0] if isinstance(
        metal, bpy.types.NodeSocket) else np.full((res, res), metal, np.float32)
    alpha = None
    if out.get("alpha") is not None:
        alpha = _bake_socket(obj, mat, out["alpha"], res, samples=6, tag="alpha")[..., 0]
    cavity = None
    if out.get("cavity") is not None:
        cavity = _bake_socket(obj, mat, out["cavity"], res, samples=4, tag="cav")[..., 0]

    # 法線（バンプ込み）
    nimg = _new_bake_image("_bake_nrm", res)
    _bake(obj, nimg, "NORMAL", 4, mat)
    normal = _filled(_img_to_np(nimg))[..., :3]
    bpy.data.images.remove(nimg)

    # AO（自己遮蔽。葉の透過は無視されるので強さを調整できるようにする）
    if info["uv"] == "atlas":
        ao = np.ones((res, res), np.float32)
        bpy.data.objects.remove(obj)
    else:
        aimg = _new_bake_image("_bake_ao", res)
        bpy.context.scene.world.light_settings.distance = info["ao_distance"]
        # 葉カードなどアルファ付きのパーツは AO を真っ黒にするので隠す
        cutouts = [o for o in bpy.context.scene.objects if o is not obj and o.type == "MESH" and not o.hide_render
                   and any(m and (m.get("cutout") or _REGISTRY.get(m.name, {}).get("out", {}).get("alpha") is not None)
                           for m in o.data.materials)]
        for o in cutouts:
            o.hide_render = True
        _bake(obj, aimg, "AO", info["ao_samples"], mat)
        for o in cutouts:
            o.hide_render = False
        ao = _filled(_img_to_np(aimg), erode=1)[..., 0]
        bpy.data.images.remove(aimg)
        ao = 1.0 - (1.0 - ao) * info["ao_strength"]
    if cavity is not None:
        ao = ao * cavity

    orm = np.stack([ao, rough, metal], -1)

    os.makedirs(out_dir, exist_ok=True)
    paths = {}
    if alpha is not None:
        rgba = np.concatenate([_to_srgb(color), alpha[..., None]], -1)
        paths["color"] = os.path.join(out_dir, f"{prefix}_BaseColor.png")
        save_png(paths["color"], rgba, "RGBA")
    else:
        paths["color"] = os.path.join(out_dir, f"{prefix}_BaseColor.png")
        save_png(paths["color"], _to_srgb(color), "RGB")
    paths["normal"] = os.path.join(out_dir, f"{prefix}_Normal.png")
    save_png(paths["normal"], normal, "RGB")
    paths["orm"] = os.path.join(out_dir, f"{prefix}_ORM.png")
    save_png(paths["orm"], orm, "RGB")
    paths["alpha"] = alpha is not None
    paths["double_sided"] = info["double_sided"]
    return paths


def make_double_sided(obj):
    """裏向きの面を複製する（エンジンは裏面カリング固定のため）"""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    ret = bmesh.ops.duplicate(bm, geom=list(bm.faces))
    new_faces = [g for g in ret["geom"] if isinstance(g, bmesh.types.BMFace)]
    bmesh.ops.reverse_faces(bm, faces=new_faces)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def _gltf_output_group():
    name = "glTF Material Output"
    g = bpy.data.node_groups.get(name)
    if g is None:
        g = bpy.data.node_groups.new(name, "ShaderNodeTree")
        g.interface.new_socket("Occlusion", in_out="INPUT", socket_type="NodeSocketFloat")
        t = g.interface.new_socket("Thickness", in_out="INPUT", socket_type="NodeSocketFloat")
        t.default_value = 0.0
        g.nodes.new("NodeGroupOutput")
        g.nodes.new("NodeGroupInput").location = (-200, 0)
    return g


def final_material(name, paths, double_sided=False):
    """ベイク済み PNG を使う glTF 互換マテリアル"""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.use_backface_culling = not double_sided
    # ベイク済みの葉パーツも、後続パーツの AO ベイクで隠せるように印を付ける
    mat["cutout"] = bool(paths.get("alpha"))
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]

    def load(p, noncolor):
        img = bpy.data.images.load(p, check_existing=True)
        img.colorspace_settings.name = "Non-Color" if noncolor else "sRGB"
        if not noncolor:
            img.alpha_mode = "STRAIGHT"
        return img

    col = nt.nodes.new("ShaderNodeTexImage")
    col.image = load(paths["color"], False)
    col.location = (-700, 300)
    nt.links.new(col.outputs["Color"], bsdf.inputs["Base Color"])
    if paths.get("alpha"):
        rnd = nt.nodes.new("ShaderNodeMath")
        rnd.operation = "ROUND"
        rnd.location = (-350, 100)
        nt.links.new(col.outputs["Alpha"], rnd.inputs[0])
        nt.links.new(rnd.outputs[0], bsdf.inputs["Alpha"])

    orm = nt.nodes.new("ShaderNodeTexImage")
    orm.image = load(paths["orm"], True)
    orm.location = (-900, -100)
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    sep.location = (-600, -100)
    nt.links.new(orm.outputs["Color"], sep.inputs[0])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    grp = nt.nodes.new("ShaderNodeGroup")
    grp.node_tree = _gltf_output_group()
    grp.location = (0, -400)
    nt.links.new(sep.outputs["Red"], grp.inputs["Occlusion"])

    nrm = nt.nodes.new("ShaderNodeTexImage")
    nrm.image = load(paths["normal"], True)
    nrm.location = (-900, -450)
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nm.location = (-500, -450)
    nt.links.new(nrm.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs[0], bsdf.inputs["Normal"])
    return mat


# ---------------------------------------------------------------------------
# アセット 1 個分の処理
# ---------------------------------------------------------------------------
def _ensure_world():
    scene = bpy.context.scene
    if scene.world is None:
        scene.world = bpy.data.worlds.new("World")
    return scene.world


def bake_and_export(name, objs, out_root=MODELS_DIR, keep_other_visible=False):
    """objs を 1 メッシュにまとめ、マテリアルごとにベイクして glTF を書き出す"""
    _ensure_world()
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)
    for f in os.listdir(out_dir):
        if f.endswith((".png", ".gltf", ".bin")):
            os.remove(os.path.join(out_dir, f))

    # 他のバリエーションは AO に写り込まないよう隠す
    hidden = []
    if not keep_other_visible:
        for o in bpy.context.scene.objects:
            if o not in objs and not o.hide_render:
                o.hide_render = True
                hidden.append(o)

    for o in objs:
        if "Proc" not in o.data.uv_layers:
            o.data.uv_layers.new(name="Proc")
    obj = join([o for o in objs], name)
    # 5 角以上の面（チューブのキャップなど）があると glTF 書き出しでタンジェントの計算に失敗するので、
    # その面だけ三角形に割る（エンジンは読み込み時に計算し直すが、他のツールでも使えるように）
    if any(len(p.vertices) > 4 for p in obj.data.polygons):
        tri = obj.modifiers.new("TriangulateNgons", "TRIANGULATE")
        tri.min_vertices = 5
        tri.keep_custom_normals = True
        apply_modifiers(obj)

    # マテリアルごとに分割
    _select_only([obj], obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.separate(type="MATERIAL")
    bpy.ops.object.mode_set(mode="OBJECT")
    parts = [o for o in bpy.context.selected_objects]

    finals = []
    for p in parts:
        mat = p.data.materials[p.data.polygons[0].material_index] if p.data.polygons else p.data.materials[0]
        # 使っていないスロットを落とす
        p.data.materials.clear()
        p.data.materials.append(mat)
        for poly in p.data.polygons:
            poly.material_index = 0
        mname = mat.name.split(".")[0]
        # 面積 0 の三角形は Cycles が壊れた法線を焼き、それが隙間埋めで広がるので消す
        bm = bmesh.new()
        bm.from_mesh(p.data)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-6, edges=bm.edges)
        bm.to_mesh(p.data)
        bm.free()
        paths = bake_part(p, mat, out_dir, f"{name}_{mname}")
        fm = final_material(f"{name}_{mname}", paths, double_sided=paths["double_sided"])
        p.data.materials[0] = fm
        if paths["double_sided"]:
            make_double_sided(p)
        # ベイク先 UV だけを残す
        me = p.data
        bake_uv = me.uv_layers.active.name
        for lname in [l.name for l in me.uv_layers if l.name != bake_uv and not l.name.startswith(".")]:
            me.uv_layers.remove(me.uv_layers[lname])
        me.uv_layers[bake_uv].name = "UVMap"
        for aname in [a.name for a in me.color_attributes]:
            me.color_attributes.remove(me.color_attributes[aname])
        finals.append(p)

    obj = join(finals, name)
    for o in hidden:
        o.hide_render = False

    path = os.path.join(out_dir, f"{name}.gltf")
    _select_only([obj], obj)
    bpy.ops.export_scene.gltf(
        filepath=path, export_format="GLTF_SEPARATE", use_selection=True, export_apply=True,
        export_yup=True, export_texcoords=True, export_normals=True, export_tangents=True,
        export_materials="EXPORT", export_image_format="AUTO", export_texture_dir="",
        export_animations=False, export_skins=False, export_morph=False, export_extras=False,
        export_cameras=False, export_lights=False)
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    print(f"[export] {name}: {tris} tris -> {path}")
    return obj, path, tris


# ---------------------------------------------------------------------------
# プレビュー
# ---------------------------------------------------------------------------
def setup_preview_world(strength=1.0, sun_elev=48.0, sun_rot=35.0):
    scene = bpy.context.scene
    world = _ensure_world()
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    sky = nt.nodes.new("ShaderNodeTexSky")
    try:
        sky.sky_type = "MULTIPLE_SCATTERING"
    except TypeError:
        pass
    sky.sun_elevation = math.radians(sun_elev)
    sky.sun_rotation = math.radians(sun_rot)
    try:
        sky.sun_disc = False
    except AttributeError:
        pass
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs["Strength"].default_value = 0.1 * strength
    out = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(sky.outputs[0], bg.inputs[0])
    nt.links.new(bg.outputs[0], out.inputs[0])
    sun_data = bpy.data.lights.new("Sun", "SUN")
    sun_data.energy = 4.0 * strength
    sun_data.angle = math.radians(1.5)
    sun_data.color = (1.0, 0.96, 0.9)
    sun = bpy.data.objects.new("Sun", sun_data)
    link_object(sun)
    # 空テクスチャの太陽方向に合わせる
    el = math.radians(sun_elev)
    az = math.radians(sun_rot)
    d = Vector((math.sin(az) * math.cos(el), math.cos(az) * math.cos(el), math.sin(el)))
    sun.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    scene.view_settings.view_transform = "AgX"
    try:
        scene.view_settings.look = "AgX - Medium High Contrast"
    except TypeError:
        pass
    scene.view_settings.exposure = 0.0
    return sun


def look_at(cam, target):
    d = Vector(target) - cam.location
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


def add_camera(loc, target, lens=50.0):
    data = bpy.data.cameras.new("Cam")
    data.lens = lens
    data.clip_end = 2000
    cam = bpy.data.objects.new("Cam", data)
    link_object(cam)
    cam.location = loc
    look_at(cam, target)
    bpy.context.scene.camera = cam
    return cam


def render(path, res=(1280, 720), samples=64):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.cycles.max_bounces = 6
    scene.cycles.transparent_max_bounces = 16
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    tmp = path + ".tmp.png"
    scene.render.filepath = tmp
    bpy.ops.render.render(write_still=True)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im = Image.open(tmp).convert("RGB")
    if path.lower().endswith(".jpg"):
        im.save(path, quality=90)
    else:
        im.save(path)
    os.remove(tmp)
    return path


def import_gltf(path):
    before = set(bpy.data.objects)
    # 画像を .blend に埋め込まず、Models フォルダの PNG を参照させる
    bpy.ops.import_scene.gltf(filepath=path, import_pack_images=False)
    return [o for o in bpy.data.objects if o not in before]


def preview_ground(size=40.0, color="#d8cdb2"):
    """プレビュー用の白砂の床"""
    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 30.0, 6, 0.6)
        return dict(color=nb.mix(srgb(color), srgb("#c2b597"), nb.mul(n, 0.6)), rough=0.9)

    mat = pbr_material("PreviewGround", fn, bake=False)
    bpy.ops.mesh.primitive_plane_add(size=size)
    g = bpy.context.object
    g.name = "PreviewGround"
    assign(g, mat)
    return g


def bounds(objs):
    mn = Vector((1e9, 1e9, 1e9))
    mx = Vector((-1e9, -1e9, -1e9))
    for o in objs:
        if o.type != "MESH":
            continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            mn = Vector(map(min, mn, w))
            mx = Vector(map(max, mx, w))
    return mn, mx


def contact_sheet(asset_dir, out_path, thumb=256):
    """テクスチャ一覧画像"""
    files = sorted(f for f in os.listdir(asset_dir) if f.endswith(".png"))
    if not files:
        return None
    cols = 3
    rows = math.ceil(len(files) / cols)
    sheet = Image.new("RGB", (cols * thumb, rows * (thumb + 18)), (24, 28, 36))
    from PIL import ImageDraw, ImageFont
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf", 11)
    except OSError:
        font = None
    for i, f in enumerate(files):
        im = Image.open(os.path.join(asset_dir, f))
        if im.mode == "RGBA":
            bg = Image.new("RGBA", im.size, (60, 60, 60, 255))
            checker = Image.new("RGBA", im.size, (90, 90, 90, 255))
            bg = Image.alpha_composite(bg, im)
            im = bg
        im = im.convert("RGB").resize((thumb, thumb), Image.LANCZOS)
        x = (i % cols) * thumb
        y = (i // cols) * (thumb + 18)
        sheet.paste(im, (x, y + 18))
        d.text((x + 4, y + 3), f[-40:], fill=(230, 230, 230), font=font)
    sheet.save(out_path, quality=90)
    return out_path


def save_blend(name):
    os.makedirs(BLEND_DIR, exist_ok=True)
    path = os.path.join(BLEND_DIR, f"{name}.blend")
    bpy.ops.wm.save_as_mainfile(filepath=path, relative_remap=True, compress=True)
    return path
