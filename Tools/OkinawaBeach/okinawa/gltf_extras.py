"""書き出した glTF のマテリアルに、エンジンの頂点アニメーションの既定値（extras）を書く

    "extras": {"vertexAnimation": "plant", "vertexAnimSpeed": 1.0909}

CoreEngine の ModelLoader::ApplyGltfMaterialExtras が読み、モデルの既定のマテリアルに入れる。
植物・海草・魚のモデルは、エンジンに置くだけで揺れる・泳ぐ（MaterialComponent で個別に変えられる）。
bpy を使わないので、書き出し済みのファイルにも単体で使える。
"""
import json

# エンジンの VertexAnimationType と同じ並び
MODES = ("none", "plant", "seagrass", "fish")


def has_anim_data(data):
    """頂点アニメーションの値（TEXCOORD_1）を持つプリミティブがあるか"""
    return any("TEXCOORD_1" in prim.get("attributes", {})
               for mesh in data.get("meshes", []) for prim in mesh.get("primitives", []))


def tag_vertex_animation(path, mode, speed=1.0, strength=1.0):
    """path の glTF の全マテリアルに頂点アニメーションの既定値を書く

    頂点アニメーションの値を持たないモデルには書かない（書いても動かず、レイトレーシングの変形が無駄になる）。
    書き出し元（Blender の glTF 書き出し）と同じ整形（タブのインデント・区切りの空白なし・末尾の改行）で書き戻す。
    戻り値: 書いたら True"""
    if mode not in MODES:
        raise ValueError(f"unknown vertex animation mode: {mode}")
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if mode != "none" and not has_anim_data(data):
        return False
    for mat in data.get("materials", []):
        extras = dict(mat.get("extras") or {})
        extras["vertexAnimation"] = mode
        for key, val in (("vertexAnimStrength", strength), ("vertexAnimSpeed", speed)):
            if abs(val - 1.0) > 1e-6:
                extras[key] = round(float(val), 4)
            else:
                extras.pop(key, None)
        mat["extras"] = extras
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(data, indent="\t", separators=(",", ":"), ensure_ascii=False))
        f.write("\n")
    return True
