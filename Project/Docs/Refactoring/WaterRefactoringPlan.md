# Water リファクタリング計画書

## ステータス
- 状態: 計画中
- 優先度: 高
- 対象領域: `Application/Src/Sample/SampleScene/WaterTestScene/`、`Project/Application/Src/Sample/TestGameObject/Primitive/`、`Project/Engine/Src/Graphics/Water/`、`Project/Engine/Src/Graphics/Render/Pass/`、`Project/Engine/Src/Graphics/Render/RenderingTechnique/Lighting/`、`Project/Engine/Src/Graphics/RayTracing/`、`Project/Application/Assets/Shaders/Water/`、`Project/Engine/Assets/Shaders/PostProcess/`
- この文書の役割: Water 関連機能の責務分離、配置整理、段階的移行順を定義する親計画書

## 目的
現在の Water 関連実装は、Gerstner Wave、FFT Ocean、Reflection、Refraction、Caustics、Debug UI が機能単位ではなく実装都合で積み上がっており、アプリ層とエンジン層の境界も曖昧である。

この計画書の目的は、以下を満たす構造へ段階的に移行することにある。

- Water 機能を Engine 主導の明確なドメインとして再編する
- Application 側には SampleScene と Debug UI だけを残す
- 波の表現、GPU シミュレーション、描画パス、RayTracing、ShaderAsset の責務を分離する
- Gerstner と FFT が共通の surface abstraction を通じて扱えるようにする
- 今後の機能追加時に `WaterPlaneObject` や Scene へ特例ロジックが集中しない構造へ寄せる

---

## 1. 現状の主要問題

### 1-1. 配置が二重化している
Water 関連の C++ 実装が以下の 2 系統に分散している。

- `C:/CoreEngine/Application/...`
- `C:/CoreEngine/Project/Application/...`

特に `WaterSceneController`、`WaterSurfaceRuntimeController`、`WaterSurfaceParameterPanel`、`WaterSurfaceDebugPanel` は `Application` 直下にあり、`WaterTestScene` と `WaterReflectionPass` は `Project/Application` 側にある。

この構造は、保守対象・実体の所在・依存方向を不明瞭にし、将来の分割や移設を難しくする。

### 1-2. `.cpp` の直接 include が存在する
`Project/Application/Src/Sample/SampleScene/WaterTestScene/WaterTestScene.cpp` では、別ディレクトリにある Water 関連 `.cpp` を直接 `#include` している。

これは翻訳単位の境界を壊し、以下の問題を生む。

- ビルド依存がコード上で不透明になる
- 本来プロジェクト設定で解決すべき構成をソースで迂回している
- クラス分割・ファイル移動・テスト追加時の影響範囲が読みにくくなる

### 1-3. `WaterPlaneObject` に責務が集中している
`WaterPlaneObject` は現在、以下の責務を持っている。

- 水面メッシュ表現
- Gerstner Wave パラメータ保持
- UV スクロール更新
- Material 更新
- Reflection / Depth / SceneColor / Refraction / FFT SRV の保持
- GPU 定数バッファ更新
- カスタムシェーダ切り替え
- Depth Fade / Debug View の状態保持

これは Entity、Simulation Data、Material Facade、Render Binding、Debug State が混在している状態であり、最も分離優先度が高い。

### 1-4. RuntimeController が Scene 初期化と Water ランタイムを兼務している
`WaterSurfaceRuntimeController` では以下が同居している。

- Scene object 生成
- IBL 初期化
- Ground object 配置
- Water material 初期設定
- 毎フレームの SRV 同期
- DXR 用 `WaterSurfaceData` 構築

これは `Scene Setup`、`Water Runtime Sync`、`Surface Data Export` の責務が混ざっている。

### 1-5. UI が Engine 内部を直接操作している
`WaterSurfaceParameterPanel` と `WaterSurfaceDebugPanel` は、WaterPlaneObject だけでなく `FFTOceanManager` や `WaterCausticsTechnique`、`WaterRefractionRayTracingManager` に直接触れている。

この構造では UI が単なる表示ではなく、実質的に Water 機能の調停者になってしまう。

### 1-6. Engine 機能が Application 側シェーダに依存している
確認できた Water シェーダ配置は以下。

- `Project/Application/Assets/Shaders/Water/` に `Water.VS.hlsl`、`Water.PS.hlsl`、`FFTWater.VS.hlsl`、`FFTOcean*.CS.hlsl`
- `Project/Engine/Assets/Shaders/PostProcess/WaterCaustics.PS.hlsl`

一方で `FFTOceanManager` は Engine 側クラスであり、Application 側アセットへの依存は層逆転である。

### 1-7. Gerstner と FFT のデータモデルが整理されていない
`FFTOceanPass` は `context.waterRefractionSurfaceData->time` を参照しており、FFT 側の時間更新が Gerstner 由来の surface snapshot に引きずられている。

また Debug UI 上でも、DXR 屈折データと FFT 表示が完全一致しない可能性が明示されている。これは単なる表示上の注意ではなく、基底データモデルの責務未分離を示している。

---

## 2. リファクタリングの基本方針

### 2-1. 責務の大分類
Water ドメインは少なくとも以下に分離する。

1. **Surface Domain**
   - 水面共通データ
   - 光学特性
   - Debug view mode
   - Gerstner / FFT 共通の surface snapshot

2. **Simulation**
   - Gerstner 波生成
   - FFT Ocean GPU シミュレーション
   - シミュレーション時間管理

3. **Render Binding / Resources**
   - Reflection / Refraction / SceneColor / SceneDepth / FFT テクスチャ接続
   - GPU 定数バッファ転送
   - Shader binding

4. **Render Pass / Technique**
   - WaterSurfacePass
   - WaterCausticsPass
   - RTWaterRefractionPass
   - RTWaterCausticsPass
   - WaterCausticsTechnique

5. **RayTracing Surface Export**
   - DXR 向け surface data 生成
   - 共通 surface model からの変換

6. **Application Scene / UI**
   - SampleScene の object 配置
   - ImGui による調整と診断

### 2-2. 依存方向の原則
依存方向は以下で固定する。

- `Application -> Engine` は可
- `Engine -> Application` は不可
- `UI -> Controller/Facade -> Engine subsystem` は可
- `UI -> Manager/Technique 直操作` は最小化する
- `Scene -> RenderPass 実行制御` は不可
- `Scene -> View 要求` は可

### 2-3. シェーダ配置の原則
Water 機能の実装主体が Engine 側である場合、対応シェーダも Engine 配下へ集約する。

- Surface shader
- FFT simulation shader
- Caustics shader
- 将来の foam / SSR / debug 専用 shader

を `Project/Engine/Assets/Shaders/Water/` 以下にまとめる。

---

## 3. 目標ディレクトリ構成

```plaintext
Project
├─ Engine
│  ├─ Assets
│  │  └─ Shaders
│  │     └─ Water
│  │        ├─ Surface
│  │        │  ├─ WaterSurface.VS.hlsl
│  │        │  ├─ WaterSurface.PS.hlsl
│  │        │  └─ FFTWaterSurface.VS.hlsl
│  │        ├─ Simulation
│  │        │  ├─ FFTOceanTimeEvolution.CS.hlsl
│  │        │  ├─ FFTOceanIFFT.CS.hlsl
│  │        │  └─ FFTOceanFinalize.CS.hlsl
│  │        └─ Lighting
│  │           └─ WaterCaustics.PS.hlsl
│  └─ Src
│     └─ Graphics
│        └─ Water
│           ├─ Surface
│           │  ├─ WaterSurfaceData.h
│           │  ├─ WaterOpticalProperties.h
│           │  ├─ WaterDebugViewMode.h
│           │  ├─ GerstnerWaveParams.h
│           │  └─ WaterSurfaceSnapshot.h
│           ├─ Simulation
│           │  ├─ WaterSurfaceSimulator.h
│           │  ├─ GerstnerWaterSimulator.*
│           │  ├─ GerstnerWaveGenerator.*
│           │  ├─ FFTOceanManager.*
│           │  └─ FFTOceanSettings.h
│           ├─ Render
│           │  ├─ WaterRenderResources.*
│           │  ├─ WaterMaterialBinder.*
│           │  ├─ WaterShaderBinding.*
│           │  └─ WaterReflectionData.h
│           ├─ Pass
│           │  ├─ WaterSurfacePass.*
│           │  ├─ WaterCausticsPass.*
│           │  ├─ RTWaterRefractionPass.*
│           │  └─ RTWaterCausticsPass.*
│           ├─ Technique
│           │  └─ WaterCausticsTechnique.*
│           └─ RayTracing
│              ├─ WaterRefractionRayTracingManager.*
│              └─ WaterCausticsRayTracingManager.*
└─ Application
   └─ Src
	  └─ Sample
		 └─ SampleScene
			└─ WaterTestScene
			   ├─ WaterTestScene.*
			   ├─ WaterSceneController.*
			   ├─ WaterSceneSetup.*
			   ├─ WaterParameterPanel.*
			   └─ WaterDebugPanel.*
```

### 構成上のルール
- 水面描画に必要な基底データは Engine 側 `Graphics/Water` 配下に置く
- Sample 専用 object 生成や UI は Application 側に残す
- FFT / Gerstner の実装差分は `Simulation` 配下へ閉じ込める
- RenderPass と Technique は Water ドメインから見えるまとまりに寄せる

---

## 4. 段階的移行ステップ

### Phase 0: 事前整理
**目的:** 現状のビルド・配置・依存の歪みを解消し、後続の安全な分割を可能にする。

#### 作業項目
- `Application` と `Project/Application` の二重配置を棚卸しする
- Water 関連ファイルの正本を 1 箇所に決める
- `.cpp` 直接 include を廃止する
- プロジェクト設定側で Water 関連ソースを正しくコンパイル対象に登録する

#### 完了条件
- `WaterTestScene.cpp` から `.cpp` include が消えている
- Water 関連ファイルの所在が一意になっている
- ビルド設定で Water 関連ソースが正規にコンパイルされる

### Phase 1: データ定義の Engine 移管
**目的:** Water 共通データとデバッグ定義を Engine 側ドメインへ寄せる。

#### 作業項目
- `WaterConstantBuffer.h` から Engine 配置すべき定義を抽出する
- `WaterDebugViewMode` を独立定義へ分離する
- Gerstner 波パラメータと DXR 用 surface data の重複定義を整理する
- `WaterSurfaceSnapshot` 相当の共通 surface model を導入する

#### 完了条件
- Water 共通データが `Project/Engine/Src/Graphics/Water/Surface/` に集約される
- Application 側は Engine の共通定義を参照するだけになる

### Phase 2: `WaterPlaneObject` の軽量化
**目的:** 水面 object から描画バインド責務とシミュレーション責務を分離する。

#### 作業項目
- `WaterRenderResources` を導入する
- Reflection / Refraction / Depth / SceneColor / FFT SRV を object 外へ移す
- GPU バッファ更新処理を binder / resource updater へ移す
- `WaterPlaneObject` は mesh・transform・material 窓口・描画属性だけを持つ方向へ寄せる

#### 完了条件
- `WaterPlaneObject` が manager 的責務を持たない
- SRV/CBV バインドと object 表現が分離される

### Phase 3: Water runtime と Scene setup の分離
**目的:** RuntimeController から scene 構築責務を外す。

#### 作業項目
- `WaterSceneSetup` を追加する
- SkyBox / ground / water object の生成を setup 側へ移す
- `WaterSurfaceRuntimeController` は update / sync / export に限定する
- IBL 初期化を scene setup または専用初期化ユーティリティへ分離する

#### 完了条件
- `WaterSurfaceRuntimeController` が object 生成責務を持たない
- Scene 初期化と毎フレーム同期の境界が分かれる

### Phase 4: Simulation 層の明文化
**目的:** Gerstner と FFT を共通の契約で扱えるようにする。

#### 作業項目
- `WaterSurfaceSimulator` 抽象を定義する
- `GerstnerWaterSimulator` を導入する
- FFT 時間更新が Gerstner 系 `WaterSurfaceData` に依存しない構造へ変更する
- 共通 surface snapshot の更新責務を simulation 層へ集める

#### 完了条件
- Gerstner / FFT の切替が simulation 契約経由で可能になる
- FFT の更新が Gerstner 専用データモデルに依存しない

### Phase 5: UI の責務縮小
**目的:** UI を直接操作層から controller 利用層へ戻す。

#### 作業項目
- `WaterSceneController` または `WaterEditorFacade` を追加し、UI の更新窓口を一本化する
- `WaterSurfaceParameterPanel` / `WaterSurfaceDebugPanel` が manager / technique を直接触らない構造へ寄せる
- UI の表示用キャッシュと実適用ロジックを分離する

#### 完了条件
- UI が Engine 内部 manager を直接操作しない
- Water に関する変更適用ルートが統一される

### Phase 6: シェーダと Render 機能の整理
**目的:** 実装主体に合わせてシェーダと render まわりを再配置する。

#### 作業項目
- Water シェーダを `Project/Engine/Assets/Shaders/Water/` へ移動する
- 参照パスの解決方式を確認し、Engine 側から自然に読めるようにする
- `WaterCausticsTechnique` と `WaterSurfacePass` の Water ドメイン内配置を見直す
- 命名規則を `WaterSurface` / `FFTOcean` / `WaterCaustics` で統一する

#### 完了条件
- Engine の Water 機能が Application 側 shader asset に依存しない
- Water シェーダの配置規則が一貫している

### Phase 7: DXR / Caustics の surface 参照再設計
**目的:** DXR 屈折、Caustics、Surface 表示の食い違いを減らす。

#### 作業項目
- RayTracing 用の surface export を共通 snapshot から構築する
- Gerstner 専用変換関数を局所化する
- FFT 利用時にどの surface 情報を DXR に渡すかを明文化する
- Debug UI で表示している差異を仕様として説明できる状態にする

#### 完了条件
- DXR / Caustics / Surface shading が同じ基底 surface model から説明できる
- FFT 時の差異が設計上の意図として整理される

---

## 5. 推奨する作業順
最初に着手すべき順は以下。

1. **Phase 0**: 二重配置解消と `.cpp` include 廃止
2. **Phase 1**: 共通データ定義の Engine 側移管
3. **Phase 2**: `WaterPlaneObject` の軽量化
4. **Phase 3**: Scene setup 分離
5. **Phase 4**: Simulation 契約導入
6. **Phase 5**: UI 経路整理
7. **Phase 6**: Shader / Render 配置統一
8. **Phase 7**: DXR / Caustics surface 参照の統一

特に Phase 0 を飛ばして後続へ進むと、ファイル移動や責務分離のたびに include 経路とビルド構成が再度崩れる可能性が高い。

---

## 6. 実装時の注意点

### 6-1. 動いている機能を一度に壊さない
Water は Reflection、Refraction、Caustics、FFT など複数の描画機能にまたがるため、一括置換は避ける。

- 各 Phase ごとにビルド通過を確認する
- Debug UI の表示崩れを早期検知する
- Reflection / Refraction / Caustics / FFT の各経路を分けて確認する

### 6-2. `WaterPlaneObject` を消すのではなく役割を減らす
現時点で多くの呼び出しが `WaterPlaneObject` を前提にしているため、即削除ではなく段階的に facade 化する。

### 6-3. 先にデータ境界を決める
クラス分割より先に、以下を決めることが重要。

- Surface 共通データは何か
- Gerstner 専用データは何か
- FFT 専用データは何か
- RayTracing が必要とする最小 surface 情報は何か

### 6-4. UI は最後に薄くする
UI を先に書き換えると確認窓口が減るため、内部責務をある程度整理した後に依存先を縮小するのが安全である。

---

## 7. 完了条件
この計画の完了条件を以下とする。

- Water 関連コードの正本が `Project` 配下で一意に管理される
- `.cpp` include が廃止されている
- Water 共通定義が Engine 側に集約されている
- `WaterPlaneObject` が描画表現主体として軽量化されている
- Scene setup、runtime sync、simulation、render binding、UI の責務が分かれている
- Engine の Water 機能が Application 側 shader asset に依存しない
- Gerstner / FFT / DXR / Caustics が共通 surface model で説明できる

---

## 8. リスクと保留事項

### 主なリスク
- シェーダ移動時に asset 解決経路が壊れる可能性がある
- `WaterPlaneObject` 分割時に描画バインド漏れが起こりやすい
- FFT と DXR の surface 差異を完全一致させるには追加設計が必要な可能性がある

### 保留事項
- Water を Graphics ドメイン直下に置くか、将来 `SceneFeature/Water` のような別軸へ寄せるか
- RayTracing 側の surface 要件を共通化しきれるか
- 将来 foam / SSR / shoreline を Water ドメインへどう追加するか

---

## 9. 次アクション
この計画書を起点として、実作業では次を順に進める。

- Phase 0 実施用の具体タスクリスト作成
- Water 関連ファイルの移設マップ作成
- `WaterPlaneObject` の責務棚卸し表作成
- shader asset 移動時の参照元一覧作成

必要であれば、この親計画書からさらに `Water_Phase0.md`、`Water_Phase1.md` のような詳細実装手順書へ分割する。