# 水面関連コード 全体レビュー（2026-07-27）

## この文書について
- 対象: 水面オブジェクトに関わる C++ / HLSL の全処理（Application 層・Engine 層・シェーダ）
- 目的: 「どこに無駄・死コード・対症療法・責務の混線があるか」を実ファイル単位で特定し、リファクタリング順序を決める
- 親計画書: [WaterRefactoringPlan.md](../WaterRefactoringPlan.md)（※後述のとおり内容が現状と乖離している）

---

## 0. 現状の構成マップ

| 層 | ファイル群 | 行数 |
|---|---|---|
| App / GameObject | `Application/Src/GameObjects/Water/*` | 約 950 |
| App / Scene・UI | `Application/Src/Scenes/WaterTestScene/*` | 約 2,050 |
| Engine / Water ドメイン | `Engine/Src/Graphics/Water/**` | 約 3,900 |
| Engine / RenderPass | `Render/Pass/{WaterSurface,WaterCaustics,RTWater*}Pass` | 約 440 |
| Engine / Technique | `RenderingTechnique/Lighting/WaterCausticsTechnique` | 約 370 |
| Shader | `Engine/Assets/Shaders/Water/**` | 約 3,400 |

**合計 約 11,000 行。** うち後述のとおり **削除可能なものが約 800〜1,000 行**、**共通化で圧縮できるものが約 900 行**ある。

親計画書の Phase 0〜6（配置統一・`.cpp` include 廃止・Engine 移管・シェーダ集約・Facade 導入）は**ほぼ完了している**。現在残っている問題は当時の計画とは別種で、「**機能が置き換わったのに旧経路が消えていない**」「**同じ物理量が何箇所にも複製されている**」「**波打ち際バグ対応で積んだ分岐と定数がそのまま残っている**」の 3 つに集約される。

---

## 1. 死んでいるコード（削除候補）

平面反射（鏡像カメラ）→ DXR 反射への置き換え、FFT のカスケード化を経て、**旧経路が丸ごと生き残っている**。

### 1-1. 平面反射（ReflectionView）系が完全に死んでいる

`WaterTestScene::BuildRenderViewRequests()` は `{}` を返すため ReflectionView は 1 つも発行されない（[WaterTestScene.cpp:67](../../../Application/Src/Scenes/WaterTestScene/WaterTestScene.cpp:67)）。その結果、以下が全て到達不能:

| 対象 | 場所 | 状態 |
|---|---|---|
| `WaterReflectionPass` クラス（斜交射影・鏡像行列 214 行） | `Application/Src/Scenes/WaterTestScene/WaterReflectionPass.{h,cpp}` | **生成箇所ゼロ** |
| `ApplyWaterRenderViewResult` → `ApplyWaterReflectionResult` の 3 段チェーン | SceneController / RuntimeController / WaterPlaneObject | **呼び出し元ゼロ** |
| `WaterPlaneObject::SetClipPlane` | `WaterPlaneObject.cpp:278` | **呼び出し元ゼロ** |
| → `frameCB_.clipEnabled` が恒久的に 0 | | |
| → `WaterConstantBufferSet` の 3 本目の CB（`reflectionFrameCB_`） | `WaterConstantBufferSet.cpp:12` | **永久に未使用**（毎フレーム確保・マップ済み） |
| → `GetFrameCBGpuAddress(bool)` の分岐 | `WaterPlaneObject.cpp:119,137` | 常に同じ側 |
| → HLSL `gClipPlane` / `gClipEnabled` | `Water.PS.hlsl:75-76` | 参照なし |

**削除見込み: 約 280 行 + CB 1 本。**

### 1-2. `WaterSurfaceSnapshot` は書き込み専用

`WaterSurfaceSimulator::CaptureSurface()` の第 2 引数として Gerstner / FFT の両実装が律儀に埋めているが、**読む側が存在しない**（`waterSurfaceSnapshot_` は `WaterSurfaceRuntimeController` のメンバに代入されるだけ）。親計画書 Phase 1「共通 surface model」の残骸。

- `WaterSurfaceData.h:21-27`（型定義）
- `WaterSurfaceSimulator.h:41-44`（インターフェース引数）
- `GerstnerWaterSimulator.cpp:46-58`、`FFTOceanSurfaceSimulator.cpp:21-24`（生成）
- `WaterSurfaceRuntimeController.h:66`（保持）

### 1-3. FFT UV マッピングの配管が丸ごと死んでいる

カスケード化（`SampleFFTOceanCascade*` が `worldXZ / kFFTCascadePatch[c]` の固定写像を内部で持つ）により、**外から渡す UV 写像は誰も参照しなくなった**。にもかかわらず配管だけが 5 層にわたって残存:

```
WaterSurfaceRuntimeController.cpp:234-243  fftUVScale/Offset の導出（メッシュ transform から）
  → WaterSurfaceData::fftUVScale / fftUVOffset / fftUVMappingValid
    → FFTOceanRefractionInput / FFTOceanReflectionInput / FFTOceanCausticsInput（同型が3つ）
      → WaterRefraction/Reflection/CausticsConstants::fftOceanUVScale/Offset
        → HLSL cbuffer gFFTOceanUVScale / gFFTOceanUVOffset  ← ★ 3本とも一切参照なし
```

同じ理由で `SampleFFTOceanBilinear()`（`RTWaterSurfaceCommon.hlsli:161-183`、23 行）も**呼び出し元ゼロ**。`gFFTOceanPatchLength` も実質「`> 1e-4` か」の真偽値としてしか使われていない（`RTWaterRefraction.hlsl:195` 他）。

### 1-4. HLSL の到達不能・誤誘導コード

| 対象 | 場所 | 内容 |
|---|---|---|
| `EvaluateWaterOffsetFFTOcean` / `EvaluateWaterNormalFFTOcean` | `RTWaterSurfaceCommon.hlsli:103-113` | **ゼロと真上を返すスタブ**。`EvaluateWaterOffset/Normal` の `simulationType` 分岐は「FFT 時に波の無い平面を返す」罠になっており、呼び出し側 3 本は全てこれを迂回して `SampleFFTOceanCascade*` を直接呼んでいる |
| `ComputeSunDownwellingTransmittance` | `Water.PS.hlsl:344-376` | 2026-07-27 に呼び出しを撤去（二重計上のため）したが関数本体は残存 |
| `SampleGlossyReflection` | `Water.PS.hlsl:618-647` | 平面反射廃止後、本経路から消滅。**デバッグモード 19 からのみ**呼ばれる |
| `ReflectionGeometricOcclusion` | `Water.PS.hlsl:652` | 呼ばれてはいるが、平面反射前提の「かすめ角スパイク抑制」で RT 反射では意味が変質 |
| `kWaterReflectionDistortStrength` | `Water.PS.hlsl:613` | **完全未使用**（鏡像 UV 歪ませ用） |

### 1-5. その他

- `Application/Src/GameObjects/Water/WaterConstantBuffer.h` — include 転送 4 行のみのファイル
- `WaterPlaneObject::UpdateUVScroll()`（`WaterPlaneObject.cpp:420`）— コメント自ら「旧呼び出し経路との互換のため」。現行は `UpdateUVAnimation` + `SetSimulationTime` の分離版が使われている

---

## 2. 同じ物理量・同じ構造の多重定義

「一致必須」というコメントが付いているものは、**仕組みで守られていない不変条件**であり、過去の実バグの温床になっている。

### 2-1. RT 水面マネージャ 3 本の構造重複

`WaterRayTracingPassBase` は存在するが**公開面が共通化されていない**ため、以下が Refraction / Reflection / Caustics で 3 回ずつ書かれている:

| 重複対象 | 備考 |
|---|---|
| `FFTOcean{Refraction,Reflection,Caustics}Input` | **メンバ完全同型**（displacementSRV / normalSRV / resolution / patchLength / enabled / uvScale / uvOffset） |
| `enum class DispatchStatus` | 7 要素とも同一 |
| `struct DispatchDiagnostics` | フィールド名が 1〜2 個違うだけ |
| `ViewID` + `kViewCount` + `static_assert` | 定型 |
| `Get{Refraction,Reflection,Caustics}SRVHandle/Resource/CurrentState` | **基底へ転送するだけの薄いラッパ**（名前だけ違う） |
| `SetSurfaceModelProvider` / `GetSurfaceModelProvider` | 完全な転送 |
| `EnsureOutputTexture` / `EnsureConstantBuffer` | 基底呼び出し 1 行 |
| `Set/GetSettings` / `GetLastDiagnostics` | 定型 |
| `ToString(DispatchStatus)` | switch 7 分岐が 3 コピー |
| Dispatch 前段の `DispatchGuardStatus` → `DispatchStatus` 変換 switch | **20 行の switch が 3 コピー**（`WaterReflectionRayTracingManager.cpp:221-241` 他） |

### 2-2. `RayTracingSubsystem::DispatchWater*` 3 本のコピペ

`DispatchWaterRefraction` / `DispatchWaterReflection` / `DispatchWaterCaustics`（`RayTracingSubsystem.cpp:228-560`）で以下が 3 回:

- SceneColorSnapshot → SceneColor のフォールバック取得
- `GetGameViewCamera3D()` / `viewProjection` / `cameraPosition` / `width`/`height` の取得
- FFT 入力の組み立て（`IsInitialized() && simulationType == FFTOcean` の判定、`invPatch` フォールバック）

しかも **ガードの厳しさとログ量が 3 本でバラバラ**（Refraction は毎フレーム 20 引数の `Infof` を 2 本、Reflection / Caustics は無言）。

### 2-3. 波面評価ロジックの二重実装

| 実装 | 場所 | cbuffer |
|---|---|---|
| Gerstner（RT 用） | `RTWaterSurfaceCommon.hlsli:35-101` | `b1` |
| Gerstner（スクリーンスペース Caustics 用） | `WaterCaustics.PS.hlsl:74-140` | `b2` |

同じ Gerstner 式が別ファイル・別 cbuffer レイアウトで 2 本ある。片方を直してもう片方を忘れると、水面描画と集光模様の波面がズレる（過去の「基準不一致」バグと同じ構図）。

### 2-4. FFT カスケード定数の 4 箇所手コピー

`kFFTCascadeCount` / `kFFTCascadePatch[3]` / `kFFTCascadeRotC` / `kFFTCascadeRotS` / `kFFTWaveGroupStrength` / `ComputeFFTWaveGroupEnvelope()` **の実体**が:

- `Water.PS.hlsl:30-44`
- `FFTWater.VS.hlsl`
- `RTWaterSurfaceCommon.hlsli:191-221`
- `FFTOceanManager`（C++ 側 `kCascadePatchLength` 他）

の 4 箇所に重複。**共有 hlsli 化されていない**（`RTWaterSurfaceCommon.hlsli` は RT 側からしか include されていない）。

### 2-5. RT 屈折アルファのエンコード規約が 2 箇所

`kRTMaxOpticalPathMeters = 64.0f` とアルファのレンジ規約（`[0,0.5)` 失敗 / `[0.5,1]` 色有効 / `[1.5,2]` 色無効）が `Water.PS.hlsl:399-431` と `RTWaterRefraction.hlsl:86-126` に別々に書かれ、コメントで「必ず一致させること」と注記。

### 2-6. C++ ↔ HLSL の cbuffer レイアウト一致がコメント頼み

`WaterConstants` / `WaterFrameConstants`（`WaterSurfaceTypes.h:46,57`）と `Water.PS.hlsl:73-113` の一致を保証しているのは `static_assert(sizeof(WaveParams) == 32)` **のみ**。`WaterFrameConstants` は 20 メンバ以上あるがサイズ / オフセットの検証がない。RT 側は `static_assert(sizeof(WaterReflectionConstants) == 208)` があるので、**同じ配慮が水面本体に無い**のは非対称。

> 参考: 過去に `gInvViewProj` が 16B ずれた RT シャドウのバグと同じクラスの事故が起きうる。

### 2-7. 同じパラメータが複数の設定構造体に

| パラメータ | 保持箇所 | 調停 |
|---|---|---|
| `refractiveIndex` | `WaterCausticsTechnique::Params` と `WaterCausticsRayTracingSettings` | `WaterEditorFacade.cpp:104` で読んだ後 `:115` で RT 側の値に上書き、Apply では両方へ書く |
| 吸収係数 σa | `WaterFrameConstants` と `WaterCausticsRayTracingSettings` | `WaterSurfaceRuntimeController.cpp:149-156` で**毎フレームコピー同期** |
| Backend 選択 | `WaterCausticsTechnique::Backend` enum と UI の `int` | `settings.backend == 1 ? ScreenSpace : RayTracing`（配列順に依存したマジックナンバー） |
| プリセット定義 | `WaterSurfaceTypes.h:117`（水質/Gerstner）と `WaterSurfaceParameterPanel.cpp:490`（FFT 海況）、Jerlov（パネル内） | 分散 |

---

## 3. 責務の混線

### 3-1. `WaterPlaneObject` が実質「水面レンダラ」

Application 層のゲームオブジェクトが、Engine の描画リソースを 12 個の setter で抱えている:

```
SetReflectionTexture / SetSceneDepthSRV / SetSceneColorSRV / SetRefractionColorSRV
SetFFTOceanTextureSRVs / SetAtmosphereAPResources / SetSkyAmbientResources
SetSkyEnvironmentReflection / SetCameraClipPlanes / SetClipPlane
SetWaterOpticalCoefficients / SetFresnelParameters
```

親計画書 Phase 2 で `WaterRenderResources` へ切り出したものの、**setter の窓口は `WaterPlaneObject` に残ったまま**で、責務が減っていない。加えて各 setter に `depthFadeDebugEnabled` ガード付きのログが埋め込まれており（`WaterPlaneObject.cpp` の約 1/4 がログ）、本質的なロジックが埋もれている。

### 3-2. `WaterSurfaceRuntimeController::SyncFrameResources` が結線ハブ化

120 行の中で以下を直接叩いている（`WaterSurfaceRuntimeController.cpp:72-190`）:

`RenderDomainContext` / 3 つの RT マネージャ / `FFTOceanManager` / `RenderTargetManager` / `DirectXCommon` / `SceneManager` / `AtmosphereManager`

**Engine 内部の結線が丸ごと Application 側に露出している。** これは「水面を出す＝この 8 個のサブシステムを正しい順序で繋ぐこと」を意味し、別シーンで水面を使う際に同じコードを写経することになる。

### 3-3. `UpdateWaterRefractionSurfaceData` にジオメトリ知識が漏れている

`WaterSurfaceRuntimeController.cpp:228-254` が、水面メッシュのローカルサイズ・scale・translate から「FFT UV 写像」と「水域矩形 AABB」を導出している。前者は §1-3 のとおり死んでいるが、後者（`regionCenterXZ` / `regionHalfExtentXZ`）は生きており、**「メッシュは回転しない正方形」という前提がコメントでしか担保されていない**。

### 3-4. `waterRefractionSurfaceData` は屈折専用ではない

`RenderContext::waterRefractionSurfaceData`（`RenderPass.h:68`）は実際には:

- RT 屈折 / RT 反射 / RT コースティクスの dispatch データ
- **FFT Ocean の GPU シミュレーション時間**（`FFTOceanPass.cpp:40` が `->time` を読む）
- 水域の有効判定（`regionValid`）

を兼ねる**共有水面データ**。名前が実態と合っておらず、親計画書 1-7 で指摘された「FFT の時間更新が Gerstner 由来の snapshot に引きずられている」問題が**未解決のまま**。

### 3-5. データの所有と生存期間が App 側

`WaterSurfaceData` の型は Engine にあるが、**唯一の実体は Application の `WaterSurfaceRuntimeController` のメンバ**。RT マネージャは `weak_ptr<IWaterSurfaceModelProvider>` 経由で生ポインタを覗く。`SyncFrameResources` は**変化がなくても毎フレーム provider を 3 マネージャへ再接続**している。

### 3-6. UI の窓口が二系統

`WaterEditorFacade` を導入したにもかかわらず、パネルは:

- Facade 経由（FFT / RT 屈折 / コースティクス設定）
- `WaterPlaneObject` の setter 直接叩き（見た目 / 光学係数 / デバッグ表示）

の 2 経路を併用している。`WaterSurfaceDebugPanel::Initialize` も `waterPlane->SetDepthFadeDebug()` を直接呼ぶ。

### 3-7. `WaterSurfaceTypes.h` の名前空間とヘッダ肥大

- `kMaxWaterWaveCount` / `WaveParams` / `WaterConstants` / `WaterFrameConstants` / `WaterPresetType` が**グローバル名前空間**（Engine の公開ヘッダなのに）。一方 `WaterSurfaceData.h` は `CoreEngine` 名前空間。混在。
- `GetWaterPresetData()` が 90 行のインライン関数 + function-static テーブル。多数の TU に展開される。

---

## 4. 対症療法の堆積（`Water.PS.hlsl`）

1,189 行のうち、**バグ対応の履歴コメントと診断コードが約 40%** を占める。

### 4-1. 水柱厚さが 4 系統

同一ピクセルで以下を全部計算し、最後にブレンドしている（`Water.PS.hlsl:774-861`）:

1. `ComputeWaterOpticalPathLength()` × `ComputeRefractedPathScale()`（スクリーン空間近似）
2. `ComputeAnalyticWaterColumn()`（解析的な鉛直深度）
3. `DecodeRTOpticalPath()`（RT 実測）
4. `kInfiniteWaterColumnMeters = 1e4`（背景が far plane）

`3` が有効なら `1` を上書きし、さらに `smoothstep(1m, 4m)` で `2` とブレンド。つまり **`1` の計算結果は浅瀬では `2` に、深部では `3` に潰されて、実質どこでも使われない**可能性が高い。

ブレンド範囲 `kAnalyticColumnFullMeters = 1.0` / `kAnalyticColumnBlendEndMeters = 4.0` は「0.3〜1.5 から拡大した」と注記があり、**線が消えるまで動かした定数**であることがコメント自体に記録されている。

### 4-2. 真因修正後に緩和したまま検証されていないマジックナンバー

| 定数 | 値 | コメントの経緯 |
|---|---|---|
| `kFresnelNormalFlatten` | 0.35 | 0.75 から緩和。「まだらの真因（反射ビューへの水面自己描画）は修正済み」 |
| `kWaterReflectionMicroRoughness` | 0.20 | 0.55 から緩和。「0.55 は嵐の海に相当する過大な値だった」 |
| `kWaterReflectionBlurTexels` | 3.0 | 5 から縮小 |
| `kFresnelNormalMipBias` | 3.0 | **カスケード化後は未使用**（コメントだけが残り、実際は最大パッチのスライス直接サンプルに変わっている） |
| `kReflectionCompressKnee/Max` | 2.0 / 6.0 | 白飛び対策のショルダー圧縮。トーンマッパと責務重複 |

**真因が直った今もこれらが必要かは検証されていない。** コメントに「夜間にまだらが再発しないか要確認」と書かれたまま残っている。

### 4-3. デバッグ表示 22 モードが本体に常駐

`main()` の `gDepthFadeDebugEnabled` ブロック（`:960-1164`）が **205 行**。うちモード 17〜22 は「まだら切り分け用の追加可視化」「reflectColor の構成要素を単独表示」という**一時的な診断のために追加されたもの**で、真因特定後も恒久コードとして残っている。

---

## 5. 毎フレームの無駄処理

### 5-1. GPU: 同一ピクセルで法線を最大 4 回計算

`ResolveSurfaceNormal(input)` は **3 カスケード分のテクスチャサンプル + normalize + 回転** を行う重い関数だが、`main()` の中で:

- `:717`（`WaterForwardMain` 内）
- `:782`（`depthSurfaceNormal`）
- `:868`（`geomNormal`）
- `:1015`（デバッグモード 7）

と最大 4 回呼ばれる。加えて `ResolveFresnelNormal`（`:873`）でさらに 1 サンプル。**1 回計算して使い回すだけで水面 PS のコストが目に見えて落ちる。**

### 5-2. GPU: フォワード PBR の結果が捨てられている

`WaterForwardMain()` が `CalculateAllLighting()` + `ApplyIBL()` をフル実行するが、`gReflectionEnabled != 0` のとき `:901` の `reflectColor = output.color.rgb` は `:926` の `reflectColor = rtHit ? rtReflection.rgb : skyReflectColor` で**必ず上書きされる**。

つまり通常経路（RT 反射有効）では、**全ライト分の Cook-Torrance と IBL キューブマップサンプルが毎フレーム完全に無駄。** 使われるのは `gMaterial.color.a`（＝`baseCoverage`）だけ。

### 5-3. GPU: RT パスのガードが非対称

| パス | 水面不在チェック | technique 有効チェック |
|---|---|---|
| `RTWaterCausticsPass` | ✅ `regionValid == 0` で return | ✅ Backend を見る |
| `RTWaterRefractionPass` | ❌ **なし** | ❌ なし |
| `RTWaterReflectionPass` | ❌ **なし** | ❌ なし |

**水面が非表示・不在のフレームでも屈折と反射の `DispatchRays` がフル解像度で走る。**

### 5-4. CPU: デバッグフラグに守られていない毎フレームログ

| 場所 | 内容 |
|---|---|
| `RTWaterRefractionPass.cpp:59,78,100` | `Infof` × 3（無条件） |
| `RTWaterCausticsPass.cpp:91` | `Infof` × 1（無条件） |
| `RayTracingSubsystem.cpp:288,315` | `Infof` × 2（うち 1 本は**引数 20 個**の波パラメータダンプ） |
| `WaterSurfaceRuntimeController.cpp:273` | 120 フレームに 1 回（マシ） |
| `WaterPlaneObject.cpp` 各 setter | `depthFadeDebugEnabled` ガード付き（正しい） |

**同じドメイン内でログのガード方針が統一されていない。**

### 5-5. CPU: 不要な GPU 転送と再接続

- `SetSimulationTime()` が毎フレーム `WaterConstants` **全体 576 バイト**を memcpy（波が変わらなくても 16 本分の `WaveParams` を毎回転送）
- `SyncFrameResources()` が変化の有無を問わず 3 マネージャへ provider を再 `SetSurfaceModelProvider`
- `UpdateWaterRefractionSurfaceData()` が `Initialize` 内と `WaterSceneController::Update` の両方から呼ばれる

---

## 6. 命名・ドキュメントのドリフト

| 対象 | 現状の記述 | 実態 |
|---|---|---|
| `Water.PS.hlsl:5-6` | 「反射テクスチャ（Planar Reflection RTT）」 | RT 反射（`RTWaterReflectionPass`）の出力 |
| `WaterPlaneObject.h:73` | 「毎フレーム `WaterReflectionPass` から渡す」 | `WaterReflectionPass` は死んでいる |
| `WaterSceneSetup.cpp:14-19,63-66` | 「Planar Reflection（ReflectionView）が大気散乱の空を映し込む」 | ReflectionView は発行されない |
| `WaterSceneSetup.cpp:33-35` | 「カメラ追従（`WaterTestScene::OnUpdate`）でタイルを常にカメラ直下へ置く」 | **カメラ追従は撤去済み**（ギズモ阻害のため） |
| `WaterSurfaceDebugPanel.cpp:155` | 「DXR 屈折データは Gerstner 系 `WaterConstants` を参照しており FFT と一致しない」 | RT は `SampleFFTOceanCascade*` で FFT を直接評価する |
| `RTWaterSurfaceCommon.hlsli:5-7` | 「将来 FFT 版を追加する際は同じシグネチャで差し替えるだけ」 | FFT 版は追加されず、呼び出し側が別関数で迂回した |
| `WaterRefactoringPlan.md` | ステータス「計画中」、対象パスが `Application/Src/Sample/SampleScene/...`、`C:/CoreEngine/Application` との二重配置 | Phase 0〜6 は完了済み、パスは全て旧構成 |

---

## 7. 推奨リファクタリング順序

リスクの低い順・効果の大きい順に並べた。**各フェーズ単体でビルド通過と目視確認ができる粒度**にしてある。

### Phase R0: 死コード削除（低リスク・約 1,000 行減）— **実施済み（2026-07-27）**

> 実施記録は [WaterPhaseR0_DeadCodeRemoval.md](WaterPhaseR0_DeadCodeRemoval.md) を参照。

1. `WaterReflectionPass.{h,cpp}` 削除、`ApplyWaterRenderViewResult` チェーン 3 段削除
2. `SetClipPlane` / `clipEnabled` / `reflectionFrameCB_` / `GetFrameCBGpuAddress(bool)` / HLSL `gClipPlane`・`gClipEnabled` 削除
3. `WaterSurfaceSnapshot` と `CaptureSurface` の第 2 引数を削除
4. fftUV 写像の配管を 5 層まとめて削除（`WaterSurfaceData` / 3 つの Input 構造体 / 3 つの Constants / 3 つの cbuffer / `SampleFFTOceanBilinear`）
5. `EvaluateWaterOffsetFFTOcean` / `EvaluateWaterNormalFFTOcean` と `simulationType` 分岐を削除し、`EvaluateWater*` を `*Gerstner` にリネーム（FFT は呼び出し側の `SampleFFTOceanCascade*` が正）
6. `ComputeSunDownwellingTransmittance` / `kWaterReflectionDistortStrength` / `WaterConstantBuffer.h` / `UpdateUVScroll` 削除

> **検証**: 水面の見た目に変化が出ないこと（全て到達不能コードのため）。Gerstner / FFT 両モードで確認。

### Phase R1: 単一情報源化（中リスク・事故予防）— **実施済み（2026-07-27）**

> 実施記録は [WaterPhaseR1_SingleSourceOfTruth.md](WaterPhaseR1_SingleSourceOfTruth.md) を参照。
> 項目 4（cbuffer の static_assert）は R0 で先行実施済み。

1. FFT カスケード定数と `ComputeFFTWaveGroupEnvelope` を `Shaders/Water/Common/FFTOceanCascade.hlsli` へ集約し、`Water.PS` / `FFTWater.VS` / `RTWaterSurfaceCommon` から include
2. RT 屈折アルファのエンコード / デコードを `Shaders/Water/Common/WaterRefractionEncoding.hlsli` へ集約
3. Gerstner 波評価を `Shaders/Water/Common/GerstnerWave.hlsli` へ集約し、`WaterCaustics.PS.hlsl` の重複実装を置換（cbuffer レイアウトも統一）
4. `WaterFrameConstants` / `WaterConstants` に `static_assert(sizeof(...) == N)` とオフセット検証を追加（RT 側と同水準に）
5. C++ 側のカスケード定数（`FFTOceanManager`）と HLSL を 1 つのヘッダで共有するか、少なくとも `static_assert` で結ぶ

### Phase R2: `Water.PS.hlsl` の整理（見た目に影響しうる・要目視）— **実施済み（2026-07-27）**

> 実施記録は [WaterPhaseR2_WaterSurfaceShader.md](WaterPhaseR2_WaterSurfaceShader.md) を参照。
> 項目 5（マジックナンバーの値の見直し）はチューニング判断のため未実施。
> **§5-2 の「PBR 出力は必ず捨てられる」という記述は誤りだった**（実施記録 §0 参照）。

1. **法線を 1 回だけ計算**して `surfaceNormal` / `fresnelNormal` をローカルに保持（§5-1）
2. `gReflectionEnabled` 時に `WaterForwardMain` の PBR/IBL をスキップ（§5-2）。必要なのは `gMaterial.color.a` のみ
3. 水柱厚さの決定を 1 つの関数 `ResolveWaterColumn()` に閉じ、4 系統の関係を「1 つの連続場 + 深部の RT 補正」として書き直す。到達不能になった系統は削除
4. デバッグ表示 22 モードを `Water.Debug.hlsli` へ分離。診断専用モード（17〜22）は削除するか、`#if WATER_DEBUG_VIEW` でビルド分離
5. 緩和済みマジックナンバー（§4-2）を 1 箇所にまとめ、既定値の根拠をコメントではなく**プリセット / UI 露出**に移す

### Phase R3: RT 水面パス 3 本の共通化（約 500 行減）— **実施済み（2026-07-27）**

> 実施記録は [WaterPhaseR3_RayTracingConsolidation.md](WaterPhaseR3_RayTracingConsolidation.md) を参照。
> 項目 3（`Get*SRVHandle` の一本化）は、呼び出し側の可読性が落ちるため意図的に見送った。

1. `FFTOcean*Input` を `WaterFFTOceanInput` 1 つに統合
2. `DispatchStatus` / `DispatchDiagnostics` / `ViewID` / `ToString` / ガード変換 switch を `WaterRayTracingPassBase` へ引き上げ
3. `Get*SRVHandle/Resource/CurrentState` を基底の `GetOutput*` に一本化（呼び出し側の名前は using かインライン別名で吸収）
4. `RayTracingSubsystem::DispatchWater*` の共通前処理（SceneColorSnapshot 取得 / カメラ / 解像度 / FFT 入力組み立て）を `BuildWaterDispatchContext()` に抽出
5. `RTWaterRefractionPass` / `RTWaterReflectionPass` に `regionValid` と technique 有効のガードを追加（§5-3）
6. 無条件 `Infof` を `debugLogEnabled` ガード配下へ（§5-4）

### Phase R4: 水面リソース結線を Engine へ移す（設計変更）
1. `Engine/Src/Graphics/Water/Render/WaterRenderFeature`（仮）を新設し、`SyncFrameResources` の 120 行（Atmosphere / SceneColor / SceneDepth / RT 出力 / FFT SRV の結線）を移管
2. `WaterPlaneObject` の 12 setter を廃止し、`WaterRenderResources` を Engine 側 Feature が直接組み立てる。`WaterPlaneObject` は **mesh・transform・material・波パラメータ**だけを持つ
3. 各 setter に埋まっているログを Feature 側の 1 箇所の診断関数へ集約
4. `WaterSurfaceData` の実体所有を Engine 側へ移し、`StaticWaterSurfaceModelProvider` の生ポインタ経由を解消
5. `waterRefractionSurfaceData` → `waterSurfaceState`（仮）へリネームし、FFT シミュレーション時間を独立させる（親計画書 Phase 7 の未解決分）

### Phase R5: 設定の単一情報源化
1. `refractiveIndex` / σa を「水の光学プロパティ」1 箇所（`WaterOpticalProperties` を実体化）に集約し、RT コースティクス / 水面 PS / SS コースティクスがそこから読む。毎フレームコピー同期を撤去
2. `Backend` 選択のマジックナンバーを enum ↔ UI インデックスの変換関数へ
3. プリセット（水質 / Gerstner / FFT 海況 / Jerlov）を 1 つの定義ファイルへ集約し、`GetWaterPresetData()` のインライン巨大テーブルを `.cpp` へ移す
4. `WaterSurfaceTypes.h` を `CoreEngine` 名前空間へ統一
5. UI から `WaterPlaneObject` 直叩きを排し、全て `WaterEditorFacade` 経由に統一

### Phase R6: ドキュメント整合
1. §6 の全コメントを実態に合わせて修正（特に「Planar Reflection」「カメラ追従」の記述）
2. `WaterRefactoringPlan.md` を本レビューの結果で更新するか、完了済みとして本文書へ引き継ぐ

---

## 8. 着手推奨

**Phase R0 → R1 → R2 の順を強く推奨する。**

理由:
- R0 は到達不能コードの削除のみで**見た目に一切影響しない**ため、以降の作業で「変えたのはどこか」の切り分けが劇的に楽になる
- R1 の「一致必須コメント」の解消は、過去に何度も起きた**基準不一致バグの再発を仕組みで止める**。R2 以降でシェーダを触る前に済ませておくべき
- R2 の法線 1 回化と PBR スキップは、**見た目を変えずに水面 PS のコストを大きく下げられる**数少ない箇所

R3（RT 共通化）と R4（Engine への結線移管）は規模が大きいので、R0〜R2 が落ち着いてから独立した作業として計画する。
