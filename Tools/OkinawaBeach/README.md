# OkinawaBeach アセット生成ツール

沖縄の海辺を構成する 3D モデルを Blender (bpy) のスクリプトで生成します。
テクスチャはすべて Blender 上のプロシージャル材質からベイクしており、外部素材は使っていません。
アラハビーチのまわりの街（`ArahaTown_*`）だけは、地図データから Python（numpy・Pillow）で形とテクスチャを作ります（Blender は使いません）。

## 出力

| 種類 | 場所 |
|---|---|
| glTF モデル（.gltf + .bin + PNG） | `Projects/Sandbox/Application/Assets/Models/Okinawa/<Variant>/` |
| 魚の群れの配置（JSON） | `Projects/Sandbox/Application/Assets/Models/Okinawa/FishSchools/` |
| エンジンのシーン（確認用シーンと同じ配置） | `Projects/Sandbox/Application/Assets/Scenes/OkinawaBeachScene/` |
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

Windows の Blender 本体（4.4 で確認）で実行する場合は、同梱の Python に Pillow と SciPy を別のフォルダへ入れ、
`--factory-startup` で起動する（UI が日本語の設定のままだと、新しく作るノードの名前が訳されて `Principled BSDF` が見つからない）。

```sh
"C:/Program Files/Blender Foundation/Blender 4.4/4.4/python/bin/python.exe" -m pip install --target <フォルダ> pillow scipy
# 入った numpy は消す（Blender 同梱の numpy を使う）
PYTHONPATH=<フォルダ> blender -b --factory-startup --python-use-system-env -P Tools/OkinawaBeach/build.py -- araha_island
```

## モジュール構成

```
build.py                 ビルドのエントリ（ASSETS にモジュール名を並べる）
okinawa/common.py        シーン操作・ノード組み立て(NB)・ベイク・glTF 出力・プレビュー
okinawa/geo.py           形状生成の補助（ノイズ、チューブ、箱、円柱、デシメート…）
okinawa/materials.py     共有マテリアル（琉球石灰岩など）
okinawa/assets/<name>.py アセットごとのスクリプト

（以下は Blender を使わない）
okinawa/araha_map.py     アラハビーチの地図と地形の高さ
okinawa/araha_town.py    まわりの街のデータ（建物・道路・面・海岸線・標高）、海と陸、地面のテクスチャ
okinawa/town_mesh.py     街の建物の形と、地面の格子・縫い合わせ
okinawa/town_textures.py 建物の外壁と屋上のタイルテクスチャ
okinawa/gltf_write.py    glTF の書き出し（Blender の書き出しと同じ座標・UV・接線の決まり）
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

### アラハビーチ（北谷町）

実在のアラハビーチ（安良波公園）にある物。高さの基準はモジュールごとに違うので「原点・置き方」を見る。

| モジュール | バリエーション | 原点・置き方 |
|---|---|---|
| `araha_island` | ArahaIsland（沖のアラハ島。3 つの石灰岩の塊、長さ約 22 m） | 島の中心の真下。z=0 が平均水面、海底は z=-2.2 |
| `jetty` | Jetty_Straight（直線 10 m）, Jetty_HeadR / Jetty_HeadL（+X / -X へ 75° 曲がる鉤形の先端） | 陸側の端の天端の中心の真下。z=0 が平均水面、天端 z=+2.0。-Y（海側）へ伸びる。Straight は y-10 ごとにつなぐ |
| `seawall` | Seawall_Straight, Seawall_Stairs（中央に幅 3 m の階段） | z=0 が遊歩道の舗装面、y=0 が護岸の天端の前の線、浜の砂は z=-0.9。X 方向に 8 m で、x+8 ごとにつなぐ |
| `lifeguard` | LifeguardTower（水色の監視小屋・手すり・旗竿・浮き輪） | seawall と同じ基準（z=0 が舗装面、y=0 が護岸の線）。台は -Y へ 3.2 m 張り出す |
| `swim_net` | SwimNet（浮きのロープとハブクラゲ防止ネット、10 m）, FloatingPier（白いキューブフロートの浮き桟橋 12 m × 2.5 m、+X の端にはしご）, FloatingPier_Walk（はしごの無い通路用） | z=0 が平均水面。SwimNet は x+10 ごと、FloatingPier_Walk は x+12 ごとにつなぐ |
| `shisa_sign` | ShisaSign（入口の「ようこそ」の看板を抱えるシーサー） | 台の底。正面（文字の面）は -Y |
| `indian_oak` | IndianOak_Ship（インディアン・オーク号の遊具一式）, IndianOak_Monument（由来の石碑） | 地面。船首は +X、遊具の付く側（正面）は -Y |
| `araha_terrain` | ArahaTerrain_SandDry / SandWet / SandUnder / Seabed / Lawn（乾いた砂・濡れた砂・水中の砂・深い海底・公園の芝生） | シーンの原点（浜の中央）に回転なしで置く。x = 東、y = 北 |
| `araha_path` | ArahaPath（公園の園路。灰色の平板の帯） | `araha_terrain` と同じ（シーンの原点に回転なしで置く） |
| `park_tree` | Gajumaru（ガジュマル。気根が垂れる）, Kobateishi_A / Kobateishi_B（コバテイシ。段になる枝と紅葉が混じる葉）, Fukugi（フクギ）, Terihaboku（テリハボク）, Hououboku（ホウオウボク。朱色の花が混じる）, Sotetsu（ソテツ。2 株立ち） | 接地点 |
| `amphitheater` | ArahaAmphitheater（野外ステージ。涙形のステージと浅い噴水池、半円の芝の段、壁画の壁と上の通路、貝のモニュメントから段々に落ちる噴水と両脇の階段・花壇、受けの池と広場） | ステージの中心の地面。客席と噴水は +X 側（客席はステージ = -X を向く） |
| `park_house` | ArahaHouse（赤瓦の寄棟屋根の管理棟 2 棟と通路、海側の深い軒、自動販売機、外のシャワー）, ParkToilet（公園のトイレ） | 建物の中心の地面。+Y が長手、-X が海側 |
| `park_shelter` | Pergola（Y 字の方杖で格子屋根を支える東屋）, UmbrellaGazebo（茶色の方形屋根の傘形の東屋）, BBQShelter（白い幕の屋根とテーブル・コンロ）, ParkLamp（公園の街灯。灯具の腕が +X）, ParkBench（ベンチ。座る向きが -Y） | 床の中心の地面 |
| `basket_court` | ArahaCourts（多目的広場の 3on3 のバスケットコート。半面 5 面とゴール） | 広場の中心の地面。+Y が長手（北）、ゴールは -X の辺と +Y の端 |
| `playground` | PlayTower（やぐら・すべり台・登り網と青いゴムチップ舗装）, SpringLion / SpringHippo（スプリング遊具） | 地面。すべり台は +X へ降りる。スプリング遊具の顔は -Y |

#### 地図とシーン

- `fetch_araha_map.py`（Blender 不要）: OpenStreetMap の浜・海岸線・公園・歩道の線を取り、浜の中央を原点にした座標で `data/araha_map.json` に書く。地図データは © OpenStreetMap contributors（ODbL 1.0）
- `okinawa/araha_map.py`（Blender 不要）: 地図の読み込みと地形の高さ（浜・陸・海の範囲、波打ち際から護岸へ上がる浜、170 m あたりから深くなる海、平らな公園）。地形の生成とシーンの書き出しで共有する
- `araha_terrain`: 波打ち際に沿って曲がる格子（浜の近くほど細かい）で地形を作り、面の種類ごとにモデルを分ける。テクスチャは 40 m 周期の継ぎ目の無いタイルを焼き、UV = (x, y) / 40 で繰り返す（タイルを焼いた後、`build.py` が `after_export` を呼んで地形のモデルを同じフォルダへ書き出し直す）
- `araha_path`: 地図の歩道の線のうち、公園の中で遊歩道から離れ、施設の範囲（`araha_map.PARK_FACILITIES`）にかからない部分を幅 2.6 m の帯にして芝生の少し上に敷く。テクスチャは 8 m 周期のタイルで、地形と同じく `after_export` で書き出し直す。`araha_terrain` と同じく `build.py` の一覧には入れていないので、名前を指定してビルドする
- `fetch_araha_town.py`（Blender 不要）: まわりの街（約 5.7 km × 8 km）の建物・道路・土地の使い方・海岸線を OpenStreetMap から、標高を国土地理院の標高タイル（DEM10B）から取り、`data/araha_town.json` と `data/araha_dem.npz`（10 m 格子）に書く。標高は国土地理院の標高タイルを加工したもの
- `build_araha_town.py`（Blender 不要）: まわりの街のモデルを書き出す（下の「まわりの街」）
- `export_araha_scene.py`（Blender 不要）: Sandbox の `ArahaBeachScene` を書き出す。護岸は地図の浜の陸側の縁に 8 m ずつ、突堤は両端の鉤の形に合わせて直線＋先端、沖の岩・浮き桟橋・ネット・ヤシ並木・海賊船の遊具などを置く。公園の施設は `araha_map.PARK_FACILITIES` の位置と向き（航空写真と地図で合わせた）に置き、木は航空写真で見た植栽の区域ごとに間隔を空けて散らす（園路・施設・遊歩道を避ける）。太陽・水面・見た目の設定は OkinawaBeachScene のものを使う。まわりの街（`ArahaTown_Ground` と `ArahaTown_Block_*`）はシーンの最後に原点へ置く

#### まわりの街（C 群）

`build_araha_town.py` が `Models/Okinawa` へ書き出す。どれもシーンの原点に回転なしで置く（頂点はシーンの座標のまま）。

| モデル | 中身 |
|---|---|
| `ArahaTown_Ground` | 公園の外の街の地面と、その外の遠くの丘・海底。今の地形の格子（`araha_map.grid_points`）のうち公園の外の升目（`Field.surface` の 5）はそのまま使い、その外は 16 m（遠くは 64 m）の格子で覆う。外側の格子は今の地形の格子にかかる升目を抜き、抜いた所の縁と今の地形の格子の外周を三角形で縫い合わせる（すき間も重なりも無い）。高さは陸が `LAWN` から標高に沿って上がり（公園から 20〜120 m で上げ始める）、海は陸からの距離で深くなる。テクスチャは地図の道路（車線と歩道）・駐車場・緑地・建物の敷地を描き、木立と空き地の草をむらで足した 1 枚もの（公園の裏は 512 m 四方、16 m 格子の範囲は 1024 m 四方、その外は全体で 1 枚） |
| `ArahaTown_Block_<区画>` | 1024 m 四方の区画ごとの建物（名前は区画の南西の角の座標）。外壁は 1 階分 × 12 m のタイルを周長と高さで繰り返す。屋上は手すり壁で、浜から 900 m 以内は手すり壁の内側と給水タンク・塔屋・室外機も作る |
| `ArahaTown_Textures` | 外壁（戸建て・マンション・ガラスの手すりのマンション・ホテル・学校や事務所・1 階の店・大きな店と倉庫・アメリカンビレッジの色壁・米軍の建物）と屋上のテクスチャ。区画のモデルが `../ArahaTown_Textures/` で共有する |

- 建物の種類と高さ（`town_mesh.classify`）: 階数（`building:levels`）か高さのある建物はその高さ。無い建物は種類（`building=*`）と広さで決める（戸建ては 2 階、種類の分からない建物は広さと大きな道からの距離で戸建て・マンション・1 階が店の建物・大きな店に分ける）。基地（`landuse=military`）の中は米軍の建物、美浜アメリカンビレッジの範囲（`town_mesh.AV_BOX`）の店は色壁、9 階以上とホテルは高層の外壁
- 外壁の色は建物ごとに種類の候補から選び、タイルの位置も建物ごとにずらす
- 浜から 2.6 km より遠い 45 m² 未満の建物は作らない
- 地形の `ArahaTerrain_Lawn` は公園の中と護岸の線から 12 m までだけになった（その外は `ArahaTown_Ground`）
- 地図データは © OpenStreetMap contributors（ODbL 1.0）、標高は国土地理院の標高タイルを加工したもの

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

### 植物・海草の揺れ（CoconutPalm / Adan / Hibiscus / Bougainvillea / 公園の木（park_tree） / Seagrass_Patch）

| glTF | 値 | 内容 |
|---|---|---|
| `TEXCOORD_1.x` | R | 葉先・葉縁の細かい震えの振幅 [m]（付け根 0） |
| `TEXCOORD_1.y` | G | 位相 0..1（葉・葉柄ごとの乱数。同じ葉の頂点は同じ値） |
| `TEXCOORD_2.x` | B | 葉・枝のしなりの振幅 [m]（付け根 0 → 先端で最大） |
| `TEXCOORD_2.y` | A | 株全体の曲げの振幅 [m]（根元 0 → 樹冠で最大。葉と実は付け根の値を引き継ぐ。海草は 0） |

- 振幅は「風の強さ 1（やや強い海風）」での目安（ヤシの樹冠で約 0.3 m、葉先で約 0.35 m）。シェーダーで強さを掛ける
- 幹と葉のようにつながる部品はつなぎ目の値が一致しているので、揺らしても裂けない
- glTF の UV は v を `1 - v` で格納する規約なので、ファイル上は `(R, 1-G)`, `(B, 1-A)`。
  Assimp は glTF の読み込みで v を `1 - v`（左下原点）にし、`aiProcess_FlipUVs` でさらに `1 - v` にするので、
  テクスチャ座標と同じく読み込み後もファイル上の値のまま。CoreEngine の `ModelLoader` は `TEXCOORD_1/2` の
  v だけ `1 - v` で元の `(R, G)`, `(B, A)` に戻している（ほかのエンジンでも、読み込み後の v の向きに合わせて戻すこと）
- テクスチャ用の UV は従来どおり `TEXCOORD_0`（マテリアルもこれを参照）

CoreEngine では実装済み（下の「[エンジン（CoreEngine）での使い方](#エンジンcoreengineでの使い方)」）。
ほかのエンジンで使うときの頂点シェーダーの例（オブジェクト空間、Y-up、原点 = 根元。CoreEngine の `VertexAnimation.hlsli` と同じ式）:

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
    float3 side = cross(windDirOS, up);              // Blender の cross(Z, 風向き) をエンジン座標にしたもの
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

1 匹ずつのまっすぐなモデル（頭 +X、背 +Z。エンジン座標では頭 +X・背 +Y・体の左 +Z）。
Blender の確認用シーンでは群れの配置 JSON どおりに並べる。CoreEngine では 1 匹ずつ MeshRenderer で置くと、
マテリアルの頂点アニメーション（魚）で泳ぐ。

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

## エンジン（CoreEngine）での使い方

### シーン（OkinawaBeachScene）

Sandbox のエディタで「シーンを開く」→ `OkinawaBeachScene` を選ぶと、確認用シーン（`Blend/OkinawaBeachScene.blend`）と
同じ配置で全アセットが並ぶ。

| オブジェクト | 中身 |
|---|---|
| `Sun` | 太陽。向きは確認用シーンの太陽と同じ（高さ 52°）。明るさは WaterTestScene と同じ |
| `MainCamera` | ゲームの視点。確認用カメラ `main`（全景）と同じ位置・向き・縦の視野角 |
| `WaterPlane` | FFT の海（WaterTestScene と同じ設定、4 km 四方）。平均水面の高さ 0 に置く |
| `Terrain` / `Rocks` / `Vegetation` / `Village` / `BeachProps` / `Pier` / `Reef` | 種類ごとのグループ（空のオブジェクト）。下に 1 つずつのモデル（MeshRenderer） |
| `SeaLife` | ウミガメ（Animator で `Swim` を流す） |

見た目の設定（`_environment.json`）と `_scene.json` は WaterTestScene と同じ。
作り直すときは `python3 Tools/OkinawaBeach/export_engine_scene.py` を実行する（`export_engine_scene.py` の説明を参照）。
- 置く物は `.blend` のワールド行列をそのまま使い、`assemble.py` の配置表から求めた位置・大きさと突き合わせてから書く。
  魚の群れは配置 JSON と確認用シーンの行列を突き合わせるだけで、エンジンのシーンへは書かない。
  書いた値をエンジンと同じ式で組み直し、Blender の配置と一致することも確かめる。
- シーンは GUID とパスでアセットを指すので、`Models/Okinawa` のアセットに `.meta` が無ければ作る
  （GUID はパスから決まるので、作り直しても同じ値）。
- 書き出し先のオブジェクトの JSON は作り直す（エディタで動かした内容は上書きされる）。
- ウミガメは骨を入れた後に `.blend` を組み直していないので `.blend` に無く、配置表の位置に置く。

### 置くだけで動く（glTF のマテリアルの extras）

揺れ・泳ぎの値を持つモデルは、glTF のマテリアルに頂点アニメーションの種類と速さを書いてある
（`okinawa/gltf_extras.py`。`build.py` がアセットのモジュールの `VERTEX_ANIMATION` / `VERTEX_ANIM_SPEED` から書く）。

```json
"extras": {"vertexAnimation": "fish", "vertexAnimSpeed": 0.6364}
```

`ModelLoader::ApplyGltfMaterialExtras` がこれを読んでモデルの既定のマテリアルに入れるので、
**メッシュ描画（MeshRenderer）にモデルを置くだけで揺れる・泳ぐ**。

| モデル | 種類 | 速さ | 動き |
|---|---|---|---|
| CoconutPalm_A/B/C, Adan_A/B, Hibiscus_A/B, Bougainvillea_A | `plant`（植物） | 1 | 風で株が曲がり、葉がしなり、葉先が震える（基本 4 秒周期） |
| Seagrass_Patch_A/B | `seagrass`（海草） | 1 | うねりの向きにゆっくり寄せ返す（6 秒周期） |
| Fish_SapphireDevil_F/M | `fish`（魚） | 1.091 | 体の波 2.4 Hz・胸びれ 4.9 Hz |
| Fish_BlueGreenChromis_A/B | `fish` | 1 | 体の波 2.2 Hz・胸びれ 4.5 Hz（エンジンの基準） |
| Fish_ThreadfinButterfly | `fish` | 0.636 | 体の波 1.4 Hz・胸びれ 2.9 Hz（ゆったり） |

- 個体ごとの位相はワールド位置から作るので、同じモデルを並べてもそろって動かない（同じモデルはまとめてインスタンス描画される）
- 揺れで元の形からはみ出す分だけモデルの AABB を広げてある（視錐台・Hi-Z で葉先が欠けない）

### 個体ごとの調整（マテリアル コンポーネント）

「マテリアル」コンポーネントの **頂点アニメーション**（なし / 植物（風） / 海草（波の寄せ返し） / 魚（泳ぎ））、
**揺れの強さ**（振幅の倍率 0〜3）、**揺れの速さ**（0.1〜3）で上書きできる（全マテリアルスロットに入り、シーンに保存される）。
止めたい個体は「なし」にする。

### 風と波（CVar）

植物は海（FFT）と同じ風で揺れる。風速が `r.VertexAnim.ReferenceWindSpeed`（既定 15 m/s）のとき「強さ 1」（上限 2）。

| CVar | 既定 | 内容 |
|---|---|---|
| `r.Water.FFT.WindDirection` / `r.Water.FFT.WindSpeed` | 海と共通 | 植物の風下の向きと強さ |
| `r.Water.Swell.Direction`（`r.Water.Swell.Enabled` のとき） | 海と共通 | 海草の寄せ返しの向き（うねりが無ければ風向き） |
| `r.VertexAnim.Enabled` | on | 頂点アニメーション全体（off で止まる） |
| `r.VertexAnim.WindScale` / `r.VertexAnim.WaterScale` | 1 | 植物 / 海草の揺れの倍率 |
| `r.RT.DynamicGeometry` | on | 揺れる植物・海草とスキンモデルの今の形をレイトレーシングに反映する |

### ウミガメ（スキンモデル）

「アニメーション」（`Animator`）コンポーネントでスキンモデルとして読み、`Swim` を再生する。コードからは:

```cpp
// turtle: シーンに登録した GameObject。メッシュ描画は Animator が自動で足してスキンモデルにする
turtle->AddComponent<AnimatorComponent>("SeaTurtle.gltf", "Swim");
```

### レイトレーシング（影・水面の反射・コースティクス）

- 揺れる植物・海草: 描画と同じ式（`VertexAnimation.hlsli`）を CS（`Shaders/RayTracing/VertexAnimationDeform.CS.hlsl`）で実行して
  個体ごとの頂点バッファ（VertexData の並びで位置だけ差し替え）を作り、個体ごとの BLAS を毎フレーム更新（refit）する。
  影が揺れに付いてくるうえ、揺れた葉から出たレイが動く前の葉に当たる縞状のセルフシャドウも出ない
- スキンモデル（ウミガメ）: GPU スキニングの出力をそのまま BLAS の頂点にして更新する（姿勢が変わったフレームだけ）
- 水面の映り込みのヒットシェーディング（当たった三角形の頂点と材質を読んで照らす）も、揺れる植物・スキンモデルは
  BLAS と同じ変形後の頂点を読む（当たった点の位置・法線と、そこから撃つ影のレイが画面の形と一致する）
- 魚（MeshRenderer で置いたもの）: 体のくねりは 1 cm ほど（影のバイアス 5 cm より小さい）なので、静止形の BLAS で置く
- 120 回更新するごとに BLAS を作り直して木構造の質を保つ（個体ごとにずらす）

### 検証

エンジンは Windows（MSVC・DXC・D3D12）でしかビルドできないため、Linux では次の方法で確かめた
（`pip install slangpy` のうえ `python3 Tools/OkinawaBeach/verify_engine_anim.py` で再現できる）:

- `VertexAnimationDeform.CS.hlsl`（= 描画の頂点シェーダーと同じ `ApplyVertexAnimation`）を slang の CPU バックエンドで実行し、
  書き出した glTF（CoconutPalm_A, Adan_A, Hibiscus_A, Seagrass_Patch_A, Fish_SapphireDevil_F, Fish_ThreadfinButterfly）の全頂点で、
  Blender の確認用ジオメトリノード（`okinawa/motion.py`）と同じ式の結果と比べた。
  回転・移動した個体、風向き・強さ・速さの倍率を変えても差は最大 2.6 µm（float の丸め誤差）
- C++ は clang（MinGW ターゲット）の構文チェックで、変更前と比べて新しいエラーが無いことを確かめた（MSVC でのビルドと実行は未確認）

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
魚の群れは `SCHOOLS`（配置 JSON の名前・群れの中心・向き）から 1 匹ずつのモデルを並べ、泳ぎのジオメトリノードで個体ごとに体を曲げる。
`python3 Tools/OkinawaBeach/assemble.py none` のように存在しないカメラ名を渡すと、描かずに `.blend` だけ保存する。
配置表（`LAYOUT`）の回転（3 つ目の値）は今の確認用シーンでは効いていない（glTF から読んだ物は回転をクォータニオンで持つので、
`place()` が入れる `rotation_euler` が使われない）。そのため置いた物はすべて回転 0 で、エンジンのシーンも同じにしている。
