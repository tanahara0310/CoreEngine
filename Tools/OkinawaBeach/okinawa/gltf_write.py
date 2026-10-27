"""glTF（.gltf + .bin + 外部 PNG）を Blender を使わずに書き出す

座標・UV・接線は Blender の glTF 書き出しと同じ決まりにそろえる:
  位置と向き (x, y, z)（Blender の Z-up）→ glTF の (x, z, -y)
  UV (u, v) → (u, 1 - v)（v は上向き）
  接線は u が増える向き、w = +1（cross(法線, 接線) が v の増える向き）
"""
import json
import os

import numpy as np


class Primitive:
    """1 つの材質の三角形の集まり（位置・法線・UV・接線は Blender の座標で持つ）"""

    def __init__(self):
        self.pos = []
        self.nrm = []
        self.uv = []
        self.tan = []
        self.idx = []
        self.count = 0

    def add(self, pos, nrm, uv, tan, tris):
        """pos [n,3]・nrm [n,3]・uv [n,2]・tan [n,3] の頂点と、その頂点の番号の三角形 tris [m,3] を足す"""
        pos = np.asarray(pos, np.float64)
        n = len(pos)
        if n == 0:
            return
        self.pos.append(pos)
        self.nrm.append(np.broadcast_to(np.asarray(nrm, np.float64), (n, 3)))
        self.uv.append(np.asarray(uv, np.float64))
        self.tan.append(np.broadcast_to(np.asarray(tan, np.float64), (n, 3)))
        self.idx.append(np.asarray(tris, np.int64) + self.count)
        self.count += n

    def arrays(self):
        P = np.concatenate(self.pos)
        N = np.concatenate(self.nrm)
        UV = np.concatenate(self.uv)
        T = np.concatenate(self.tan)
        idx = np.concatenate(self.idx).reshape(-1)
        N = N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
        T = T / np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-12)
        conv = lambda a: np.stack([a[:, 0], a[:, 2], -a[:, 1]], -1)  # noqa: E731
        return (conv(P).astype(np.float32), conv(N).astype(np.float32),
                np.stack([UV[:, 0], 1.0 - UV[:, 1]], -1).astype(np.float32),
                np.concatenate([conv(T), np.ones((len(T), 1))], -1).astype(np.float32), idx)


class Material:
    def __init__(self, name, color=None, normal=None, orm=None, base=(1.0, 1.0, 1.0, 1.0), rough=0.9, metal=0.0):
        """color・normal・orm は PNG のパス（gltf からの相対パス）。無ければ係数だけの材質"""
        self.name = name
        self.color = color
        self.normal = normal
        self.orm = orm
        self.base = base
        self.rough = rough
        self.metal = metal


def write(path, name, prims):
    """prims = [(Material, Primitive), ...] を 1 つのメッシュとして path（.gltf）へ書き出す。三角形の数を返す"""
    out_dir = os.path.dirname(path)
    os.makedirs(out_dir, exist_ok=True)
    bin_name = os.path.splitext(os.path.basename(path))[0] + ".bin"
    blob = bytearray()
    views, accessors, materials, textures, images, primitives = [], [], [], [], [], []
    image_index = {}

    def view(data, target=None):
        while len(blob) % 4:
            blob.append(0)
        off = len(blob)
        blob.extend(data)
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            v["target"] = target
        views.append(v)
        return len(views) - 1

    def accessor(arr, kind, comp, target, minmax=False):
        a = {"bufferView": view(arr.tobytes(), target), "componentType": comp, "count": len(arr), "type": kind}
        if minmax:
            a["min"] = [float(v) for v in arr.min(axis=0)]
            a["max"] = [float(v) for v in arr.max(axis=0)]
        accessors.append(a)
        return len(accessors) - 1

    def texture(uri):
        if uri not in image_index:
            images.append({"mimeType": "image/png", "name": os.path.splitext(os.path.basename(uri))[0], "uri": uri})
            textures.append({"sampler": 0, "source": len(images) - 1})
            image_index[uri] = len(textures) - 1
        return image_index[uri]

    tris = 0
    mat_index = {}
    for mat, prim in prims:
        if prim.count == 0:
            continue
        if mat.name not in mat_index:
            m = {"name": mat.name, "pbrMetallicRoughness": {}}
            pbr = m["pbrMetallicRoughness"]
            if mat.color:
                pbr["baseColorTexture"] = {"index": texture(mat.color)}
            if tuple(mat.base) != (1.0, 1.0, 1.0, 1.0):
                pbr["baseColorFactor"] = [float(v) for v in mat.base]
            if mat.orm:
                pbr["metallicRoughnessTexture"] = {"index": texture(mat.orm)}
                m["occlusionTexture"] = {"index": texture(mat.orm)}
            else:
                pbr["roughnessFactor"] = float(mat.rough)
                pbr["metallicFactor"] = float(mat.metal)
            if mat.normal:
                m["normalTexture"] = {"index": texture(mat.normal)}
            materials.append(m)
            mat_index[mat.name] = len(materials) - 1
        P, N, UV, T, idx = prim.arrays()
        big = idx.max() >= 65535
        I = idx.astype(np.uint32 if big else np.uint16)
        primitives.append({"attributes": {"POSITION": accessor(P, "VEC3", 5126, 34962, True),
                                          "NORMAL": accessor(N, "VEC3", 5126, 34962),
                                          "TEXCOORD_0": accessor(UV, "VEC2", 5126, 34962),
                                          "TANGENT": accessor(T, "VEC4", 5126, 34962)},
                           "indices": accessor(I, "SCALAR", 5125 if big else 5123, 34963),
                           "material": mat_index[mat.name]})
        tris += len(idx) // 3
    doc = {"asset": {"generator": "CoreEngine OkinawaBeach build_araha_town", "version": "2.0"},
           "scene": 0, "scenes": [{"name": "Scene", "nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
           "meshes": [{"name": name, "primitives": primitives}], "materials": materials,
           "accessors": accessors, "bufferViews": views,
           "buffers": [{"byteLength": len(blob), "uri": bin_name}]}
    if textures:
        doc["textures"] = textures
        doc["images"] = images
        doc["samplers"] = [{"magFilter": 9729, "minFilter": 9987}]
    with open(os.path.join(out_dir, bin_name), "wb") as f:
        f.write(bytes(blob))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
    return tris
