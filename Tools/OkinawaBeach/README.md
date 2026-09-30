# OkinawaBeach アセット生成ツール

沖縄の海辺を構成する 3D モデルを Blender (bpy) のスクリプトで生成します。
テクスチャはすべて Blender 上のプロシージャル材質からベイクしており、外部素材は使っていません。

## 出力

| 種類 | 場所 |
|---|---|
| glTF モデル（.gltf + .bin + PNG） | `Projects/Sandbox/Application/Assets/Models/Okinawa/<Variant>/` |
| 魚の群れの配置（JSON） | `Projects/Sandbox/Application/Assets/Models/Okinawa/FishSchools/` |
| アニメーションのデモ（MP4 / GIF） | `Tools/OkinawaBeach/Previews/anim_*.mp4`, `anim_*.gif` |
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

座標は Blender（Z-up）。z=0 は平均水面、+Y が陸側、-Y が沖側。エンジンへは (x, z, y) で対応する。

### 浜と陸

| モジュール | バリエーション | 原点・置き方 |
|---|---|---|
| `rocks` | NotchRock_A/B（キノコ岩）, ReefRock_A/B/C（磯の岩） | 接地点 |
| `palm` | CoconutPalm_A/B/C | 接地点 |
| `adan` | Adan_A/B | 接地点 |
| `hibiscus` | Hibiscus_A/B, Bougainvillea_A | 接地点 |
| `azumaya` | Azumaya_A（赤瓦の東屋。屋根にシーサー） | 床の中心。正面は -Y |
| `shisa` | Shisa_Agyo, Shisa_Ungyo | 台座の底の中心。正面は -Y |
| `ishigaki` | Ishigaki_Straight/Corner/Low | 壁の始点の中心線。+X へ伸びる |
| `sabani` | Sabani_A/B | 船体の中心。船首は +X |
| `pier` | Pier_Straight, Pier_End | z=0 が水面。モジュールは +Y へ 4 m |
| `parasol` | BeachParasol_A/B, DeckChair_A | 接地点。チェアは頭側が -X |
| `props` | Driftwood_A/B, Shell_Cowrie, Shell_SpiderConch, CoralPiece_A/B, Coconut_Husk, GlassFloat | 接地点 |
| `tetrapod` | Tetrapod_A/B | 接地面 |
| `terrain` | BeachTerrain_Shore, BeachTerrain_Flat | 40 m 角タイルの中心。X 方向に周期的 |

### 海の中・リーフ地形

| モジュール | バリエーション | 原点・置き方 |
|---|---|---|
| `reef_terrain` | ReefTerrain_Lagoon（礁池）, ReefTerrain_Edge（リーフエッジ〜ドロップオフ）, ReefTerrain_Deep（深場） | 40 m 角タイルの中心。Shore を (x, 0) に置いたら Lagoon を (x, -40)、Edge を (x, -80)、Deep を (x, -120) |
| `beachrock` | BeachRock_A/B/C | 砂との接地点。波打ち際に置く |
| `coral` | Coral_Table_A/B, Coral_Branch_A/B, Coral_Massive_A/B, Coral_MicroAtoll, Coral_Brain, Coral_Soft | 根元の中心（少し埋まる） |
| `sealife` | GiantClam, SeaCucumber, SeaUrchin, BlueStarfish, Anemone_Clownfish, SeaTurtle | 海底の生き物は接地点。ウミガメは体の中心（水中に浮かべる）。ウミガメは骨と泳ぎのアニメーション付き |
| `seagrass` | Seagrass_Patch_A/B | 藻場の中心（砂面） |
| `fish` | Fish_SapphireDevil_F/M, Fish_BlueGreenChromis_A/B, Fish_ThreadfinButterfly（1 匹ずつ） | 体の中央。頭が +X。群れは `FishSchools/*.json` の配置で並べる |

## アニメーション用のデータ

植物・海草・魚は **頂点シェーダーで動かす** ための値を頂点に焼き込み、ウミガメは **骨（スキン）とアニメーション** を持つ。
Blender 上での確認は `demo_anim.py`（下記）。

### 植物・海草の揺れ（CoconutPalm / Adan / Hibiscus / Bougainvillea / Seagrass_Patch）

| glTF | 値 | 内容 |
|---|---|---|
| `TEXCOORD_1.x` | R | 葉先・葉縁の細かい震えの振幅 [m]（付け根 0） |
| `TEXCOORD_1.y` | G | 位相 0..1（葉・葉柄ごとの乱数。同じ葉の頂点は同じ値） |
| `TEXCOORD_2.x` | B | 葉・枝のしなりの振幅 [m]（付け根 0 → 先端で最大） |
| `TEXCOORD_2.y` | A | 株全体の曲げの振幅 [m]（根元 0 → 樹冠で最大。葉と実は付け根の値を引き継ぐ。海草は 0） |

- 振幅は「風の強さ 1（やや強い海風）」での目安（ヤシの樹冠で約 0.3 m、葉先で約 0.35 m）。シェーダーで強さを掛ける
- 幹と葉のようにつながる部品はつなぎ目の値が一致しているので、揺らしても裂けない
- glTF の UV は v を `1 - v` で格納する規約なので、ファイル上は `(R, 1-G)`, `(B, 1-A)`。
  エンジンの Assimp は `aiProcess_FlipUVs` で読むので、読み込み後は元の `(R, G)`, `(B, A)` に戻る
- テクスチャ用の UV は従来どおり `TEXCOORD_0`（マテリアルもこれを参照）

エンジン側で使うには `ModelLoader::ConvertVertex` で `mTextureCoords[1]`, `[2]` を読み、`VertexData` と入力レイアウトに
`TEXCOORD1`, `TEXCOORD2`（float2）を足す。頂点シェーダーの例（オブジェクト空間、Y-up、原点 = 根元）:

```hlsl
// windDirOS: 風向き（オブジェクト空間の水平な単位ベクトル）, strength: 風の強さ, objPhase: 個体ごとの位相（ワールド位置から作る等）
// 木:   branchUp = 0.8, bias = 0.6,  freq = (4, 9, 28), period = 4 秒
// 海草: branchUp = 0,   bias = 0.25, freq = (1, 3, 10), period = 6 秒（波の寄せ返しでゆっくり往復）
float3 WindDisplace(float3 posOS, float3 nrmOS, float2 uv1, float2 uv2, float3 windDirOS,
                    float strength, float time, float objPhase, float branchUp, float bias,
                    float3 freq, float period)
{
    const float TAU = 6.2831853;
    float w0 = TAU / period;                         // 基本の角振動数（各揺れはこの整数倍）
    float R = uv1.x, G = uv1.y, B = uv2.x, A = uv2.y;
    float3 up = float3(0, 1, 0);
    float3 side = cross(up, windDirOS);
    // 1) 株全体の曲げ。根元からの距離を保って幹が伸びないようにする
    float gust = strength * (0.55 + 0.30 * sin(w0 * time + objPhase) + 0.15 * sin(3 * w0 * time + 1.3 * objPhase));
    float sway = strength * 0.25 * sin(2 * w0 * time + 2.0 * objPhase);
    float len = length(posOS);
    float3 p = posOS + (windDirOS * gust + side * sway) * A;
    p = len > 1e-5 ? normalize(p) * len : p;
    // 2) 葉・枝のしなり（葉ごとの位相 G で少しずつずらす）
    float phB = G * TAU + objPhase;
    float wb = 0.65 * sin(freq.x * w0 * time + phB) + 0.35 * sin(freq.y * w0 * time + 1.7 * phB);
    p += (up * (wb * branchUp) + windDirOS * (strength * bias + 0.4 * wb)) * (B * strength);
    // 3) 葉先の震え。両面化した裏の面（法線が逆）と同じ向きに動かす
    float3 n = nrmOS * (nrmOS.y >= 0 ? 1.0 : -1.0);
    float wf = sin(freq.z * w0 * time + G * 5 * TAU + dot(posOS, float3(1.3, 2.1, 1.7)) * 3);
    p += n * (R * strength * wf);
    return p;
}
```

### 魚（Fish_*）と群れの配置（FishSchools/*.json）

1 匹ずつのまっすぐなモデル（頭 +X、背 +Z。エンジン座標では頭 +X・背 +Y・体の左 +Z）を、
群れの配置 JSON どおりにインスタンス描画する。

| glTF | 内容 |
|---|---|
| `TEXCOORD_1.x` | t: 吻端 0 → 尾の先 1 |
| `TEXCOORD_1.y` | 体の横揺れの振幅 [m]（頭 1.5% → 尾の先 10%（全長比）） |
| `TEXCOORD_2.x` | 胸びれの羽ばたきの振幅 [m]（付け根 0 → 先） |
| `TEXCOORD_2.y` | 胸びれの左右（左 +1 / 右 -1 / それ以外 0） |

```hlsl
// posOS: 頭 +X・背 +Y・左 +Z。phase は配置 JSON の個体ごとの値、bodyHz / finHz / wavelength は JSON の swim
float3 SwimDisplace(float3 posOS, float2 uv1, float2 uv2, float time, float phase,
                    float bodyHz, float finHz, float wavelength)
{
    const float TAU = 6.2831853;
    float flap = TAU * (finHz * time + 1.7 * phase);
    posOS.z += uv1.y * sin(TAU * (uv1.x / wavelength - bodyHz * time + phase))   // 頭から尾へ進む波
             + uv2.y * uv2.x * sin(flap);                                        // 胸びれ（外側が正）
    posOS.x -= 0.5 * uv2.x * cos(flap);                                           // 前後へ少し漕ぐ
    return posOS;
}
```

配置 JSON（`Models/Okinawa/FishSchools/<名前>.json`）:

| キー | 内容 |
|---|---|
| `models` | 使うモデル名 → glTF の相対パス |
| `swim` | `bodyWaveHz`, `finHz`, `wavelength`, `cruiseSpeed` の目安 |
| `instances[]` | `model`, `position` [m], `rotation` [x, y, z, w], `scale`（モデルの全長に対する倍率）, `phase` 0..1 |

座標は **エンジン座標**（glTF を Assimp の `ConvertToLeftHanded` で読んだときと同じ Y-up・左手系）で、群れの中心が原点。
Blender の `(x, y, z)` は `(x, z, y)`、回転は Assimp の変換と同じ（Blender のクォータニオン `(w, x, y, z)` → `[-x, -z, -y, w]`）。

| 群れ | 内容 |
|---|---|
| `FishSchool_Blue` | ルリスズメダイ 34 匹（オス 35%）のゆるい群れ |
| `FishSchool_Green` | デバスズメダイ 52 匹の密な群れ（枝サンゴの真上に置く） |
| `FishPair_Butterfly` | トゲチョウチョウウオのペア |

### ウミガメ（SeaTurtle）

- 骨 13 本: `Root`（体）→ `Neck` → `Head`、前ヒレ `FlipperFR1..3` / `FlipperFL1..3`（肩・手首・先）、後ろヒレ `FlipperRR1..2` / `FlipperRL1..2`
- 1 頂点あたりの重みは最大 2 本（エンジンの上限 4 本以内）。甲羅はすべて `Root`
- アニメーション `Swim`: 3 秒でループする泳ぎ（24 fps で 73 キー。最初と最後が同じ姿勢）。
  前ヒレは翼のように打ち下ろして後ろへ掃き（先の節ほど遅れてしなる）、後ろヒレはかじ取りのように小さく漕ぎ、首と体がわずかに上下する。
  その場で泳ぐので、前へ進める移動はゲーム側で行う（目安 0.3〜0.6 m/s）
- レストポーズは前ヒレを振り上げた姿勢（アニメーションの 0 フレームと同じ）

### 確認用デモ

`python3 Tools/OkinawaBeach/demo_anim.py [beach] [underwater]` で、書き出した glTF を読み込んで動かし、
`Previews/anim_<clip>.mp4` / `.gif` と `Blend/AnimDemo_<clip>.blend`（開いて再生できる）を作る。
植物と魚は `okinawa/motion.py` のジオメトリノード（上の HLSL と同じ式）、ウミガメは glTF のアクション `Swim` で動かしている。

## 他のモジュールで使い回せる補助

| 場所 | 内容 |
|---|---|
| `terrain.py` | `PNoise`（40 m で厳密に周期的なノイズ）, `_grid_mesh`（格子 + 平面 UV + 解析的法線）, `Torus`（模様を周期化する 4D ノイズ座標）, `shore()` |
| `shisa.py` | `unwrap_smooth_proxy()`（有機的な形を大きな UV 島で展開） |
| `adan.py` | `MeshAcc`（小さな部品を 1 メッシュに集める）, `BarkPacker`（複数チューブのベイク UV を 1 枚に詰める） |
| `props.py` | `_union` / `_remesh`（ボクセルで形を合成）, `_pack_tubes` |
| `_timber.py` | `MeshBuilder`, `loft`, `lathe`, `sweep`, 板の木目 |
| `okinawa/anim.py` | 揺れの頂点データ（`Anim` 属性 → TEXCOORD_1/2）の書き込み・変換 |
| `okinawa/motion.py` | 揺れ・泳ぎのジオメトリノード（Blender での確認用） |

`geo.tube_along` はチューブごとに 0..1 全体を使う `Bake` UV を作るので、同じ uv="keep" マテリアルで複数のチューブを使うときは `BarkPacker` などで詰め直すこと。

## 確認用シーン

`python3 Tools/OkinawaBeach/assemble.py [カメラ名 ...]` で全アセットを配置してレンダリングする（`Blend/OkinawaBeachScene.blend` も保存）。
確認用の海は Cycles の体積吸収で、浅瀬はターコイズ、深場は紺碧になる。コースティクスはエンジン側の表現なので、ここでは影のレイだけ水面を素通しにしている。
