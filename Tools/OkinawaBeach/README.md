# OkinawaBeach アセット生成ツール

沖縄の海辺を構成する 3D モデルを Blender (bpy) のスクリプトで生成します。
テクスチャはすべて Blender 上のプロシージャル材質からベイクしており、外部素材は使っていません。

## 出力

| 種類 | 場所 |
|---|---|
| glTF モデル（.gltf + .bin + PNG） | `Projects/Sandbox/Application/Assets/Models/Okinawa/<Variant>/` |
| Blender ファイル | `Tools/OkinawaBeach/Blend/<module>.blend` |
| プレビュー画像 | `Tools/OkinawaBeach/Previews/<module>.jpg`, `<module>_textures.jpg` |

モデルは 1 バリエーション = 1 フォルダ。原点は接地点で、1 unit = 1 m です。

## PBR テクスチャ

マテリアルごとに次の 3 枚を書き出します（glTF metallic-roughness 準拠）。

| ファイル | 内容 |
|---|---|
| `*_BaseColor.png` | sRGB。葉や花は A にカットアウト用アルファ |
| `*_Normal.png` | タンジェント空間ノーマル（OpenGL / glTF 準拠 +Y） |
| `*_ORM.png` | R = AO, G = Roughness, B = Metallic（glTF の occlusion と metallicRoughness の両方から参照） |

エンジン（`ModelLoader.cpp` / `ObjectMaterial.hlsli`）の仕様に合わせた点:

- glb の埋め込みテクスチャは読めないので `.gltf` + 外部 PNG で出力
- 全マテリアルで `alpha <= 0.5` を破棄するため、不透明物のベースカラーは RGB（アルファ無し）
- オブジェクトは常に裏面カリングされるため、葉などの薄い板は裏向きの面を複製して両面化

## ビルド

```sh
pip install bpy pillow scipy        # Blender 5.0 の Python モジュール版
python3 Tools/OkinawaBeach/build.py rocks palm   # 個別
python3 Tools/OkinawaBeach/build.py all          # 全部
OKI_RES_SCALE=0.25 python3 Tools/OkinawaBeach/build.py palm   # テクスチャ 1/4 で試し焼き
```

Blender 本体で実行する場合: `blender -b -P Tools/OkinawaBeach/build.py -- rocks`

## モジュール構成

```
build.py                 ビルドのエントリ（ASSETS にモジュール名を並べる）
okinawa/common.py        シーン操作・ノード組み立て(NB)・ベイク・glTF 出力・プレビュー
okinawa/geo.py           形状生成の補助（ノイズ、チューブ、箱、円柱、デシメート…）
okinawa/materials.py     共有マテリアル（琉球石灰岩など）
okinawa/assets/<name>.py アセットごとのスクリプト
```

### アセットモジュールの書き方

```python
from .. import common as C
from .. import geo
from ..common import pbr_material, srgb

PREVIEW = dict(cam_dir=(0.3, -1.0, 0.4), lens=40)   # プレビューのカメラ向き（任意）

def build():
    # {バリエーション名: [オブジェクト, ...]} を返す。各バリエーションは原点に置いてよい
    # （ベイク時は他のバリエーションを隠す）
    obj = geo.box("Crate", (1, 1, 1), loc=(0, 0, 0.5))
    C.assign(obj, my_material())
    return {"Crate_A": [obj]}
```

`build()` が返したオブジェクトは、バリエーションごとに

1. 変形を適用して 1 メッシュに結合
2. マテリアルごとに分割し、UV を用意してベイク（BaseColor / Normal / AO / Roughness / Metallic / Alpha）
3. PNG を書き出し、画像を使う glTF 互換マテリアルに差し替え
4. 再結合して `.gltf` を出力

の順で処理されます。プロシージャル材質は Object 座標（= ワールド座標。変形適用後）か、
`Proc` という名前の UV レイヤーを参照します。

### マテリアル

```python
def my_material():
    def fn(nb):                         # nb: common.NB（ノード組み立てヘルパー）
        co = nb.coord("Object")
        n = nb.noise(co, scale=4.0, detail=6)
        col = nb.ramp(n, [(0.3, srgb("#8a7a60")), (0.7, srgb("#c8b89a"))])
        return dict(
            color=col,                   # 必須: カラーソケット or (r,g,b) リニア
            rough=nb.maprange(n, 0, 1, 0.6, 0.9),   # ソケット or float
            metal=0.0,                   # ソケット or float
            height=n, height_scale=0.01, # 任意: バンプ（Normal に焼かれる）
            alpha=None,                  # 任意: カットアウト
            cavity=None,                 # 任意: AO に掛ける追加の陰影 (0..1)
        )
    return pbr_material("Crate", fn, res=1024, uv="smart")
```

`uv` の指定:

| 値 | 用途 |
|---|---|
| `"smart"` | Smart UV Project で自動展開してベイク（岩・小物など） |
| `"keep"` | スクリプトが作った `Bake` UV（無ければ `Proc`）をそのまま使う。`geo.tube_along` は周長に合わせた短冊状の `Bake` UV を自動で作る |
| `"atlas"` | `Proc` UV の 0..1 を 1 枚の平面に焼く。複数の葉カードが同じテクスチャを共有する場合。AO は焼かず `cavity` のみ |

その他の引数: `double_sided=True`（両面化）、`ao_distance`（AO の届く距離 m）、`atlas_size`（atlas の実寸 m）。

## 収録アセット

`build.py` の `ASSETS` を参照。
