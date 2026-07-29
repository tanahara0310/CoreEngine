# レイトレーシング基盤 リファクタリングレビュー / 実装計画

- 作成日: 2026-07-28
- 対象ブランチ: `fix/dxr-optimization`
- 対象コード: `Engine/Src/Graphics/RayTracing/`, `Engine/Src/Graphics/Water/RayTracing/`,
  `Engine/Src/EngineSystem/Subsystem/RayTracingSubsystem.*`, `Engine/Assets/Shaders/RayTracing/`

---

## 0. 結論サマリ

| # | 結論 | 根拠 |
|---|---|---|
| A | **「影に16本レイを飛ばしている」は事実ではない。実際は 1 ピクセル 1 本** | `softShadowSamples` の既定値は `1`、かつ UI／設定ファイル／コード上に上書き箇所が存在しない（`16` は `kMaxSamples` のクランプ上限にすぎない） |
| B | **RTシャドウの重さの半分以上は「レイ」ではなく後処理（Temporal + A-Trous 4パス）** | 実測: Trace 1.14ms / Temporal 0.29ms / Denoise 1.08ms。後処理計 1.37ms > Trace 1.14ms |
| C | **A-Trous の 28% は無駄な行列演算**（`invViewProj` による世界座標復元を 1px あたり 40 回） | 復元を線形深度に置換する実験で Denoise 1.081ms → 0.783ms（-27.6%） |
| D | **レイ側の定番最適化はほぼ効かない**（＝BVHトラバースが律速） | `SKIP_CLOSEST_HIT_SHADER`+`FORCE_OPAQUE` で -1%、N·L≤0 早期棄却で -9% |
| E | **RTシャドウは画面外の遮蔽物を正しく拾えている。逆に RT水面反射／屈折は画面外を捨てている** | TLAS はフラスタム非依存。一方 反射のペイロードは `{hitT, hitFlag}` のみでヒット面の色が取れず、スクリーン空間へ再投影して SceneColor を舐めている（4章） |
| F | **基盤は「シャドウ専用の作り」で汎用RTには足りない**（ローカルルートシグネチャ／ヒット時のマテリアル参照／BLAS更新／スキンメッシュが全て不在） | 3章 |
| G | **RT関連のデバッグ表示・設定UIが1つも存在しない**（`RegisterEngineDebugPanel` は定義済みで呼び出し元ゼロ、`SetSettings` も呼び出し元ゼロ＝設定が到達不能） | 3.2 ⑤ |

RT 関連パスの合計は **フレームの約 42%**（4.68ms / 11.02ms）。うち RTシャドウ 3 パスが **2.52ms（22.8%）** で単一機能としては最大。

---

## 1. 実測データ

### 1.1 計測方法

`DebugSubsystem::PostFinalizeFrame` に一時プローブを仕込み、起動 600 フレーム目の
`GpuTimestampProfiler::GetResults()` を全スロットぶんログへ 1 回だけダンプした
（計測後にプローブは削除済み）。

- 構成: Development（最適化有効）
- シーン: `WaterTestScene`（島 × 複数 + FFT 海面）
- カメラ: `Application/Saved/EditorSettings/DebugCamera.json` の保存済み構図
  （`target=(101, 120, -421)`, `distance=20`, `fov=0.45`）
- 有効ディレクショナルライト: **1 灯**（Sun のみ。Moon はオプトインで未生成）

### 1.2 ベースライン（フレーム全体）

```
Frame Total                11.0182 ms
├─ ASBuild                  0.0543   ← DXR BLAS/TLAS 構築
├─ AtmosphereLUT            0.4526
├─ GBuffer                  0.5304
├─ HiZOcclusion             0.0913
├─ RTShadowTrace            1.1427   ← DispatchRays
├─ RTShadowTemporal         0.2929   ← CS + 全画面 CopyResource
├─ RTShadowDenoise          1.0813   ← A-Trous CS × 4
├─ RTWaterCausticsPass      1.0988
├─ DeferredLighting         0.3144
├─ FFTOceanPass             2.9891   ← 実行ごとの振れが大きい（1.20〜2.99 を観測）
├─ AerialPerspective        0.1792
├─ VolumetricCloud          0.1597
├─ GodRay                   0.9544
├─ RTWaterRefractionPass    0.5181
├─ RTWaterReflectionPass    0.4936
├─ WaterSurface             0.1280
├─ LensFlare                0.2355
└─ ToneMapping / BackBuffer 0.13 前後
```

**レイトレ合計 = 4.68 ms（42.5%）**
（内訳: シャドウ 2.52 / コースティクス 1.10 / 屈折 0.52 / 反射 0.49 / ASBuild 0.05）

> 注意: `FFTOceanPass` だけは起動ごとに 1.20〜2.99ms と大きく振れた。
> RT 系 3 パスは 4 回の起動を通じて ±0.01ms 以内で安定しており、以下の A/B 差分は有意。

### 1.3 A/B 実験（出力ディレクトリの HLSL を直接書き換えて再起動。実験後に復元済み）

| 変更内容 | RTShadowTrace | RTShadowDenoise |
|---|---|---|
| ベースライン | 1.1427 ms | 1.0813 ms |
| ① `RAY_FLAG_SKIP_CLOSEST_HIT_SHADER \| RAY_FLAG_FORCE_OPAQUE` 追加 | 1.1315 ms (**-1.0%**) | 1.0813 ms |
| ② ①に加えて `dot(N, rayDir) <= 0` の早期棄却 | 1.0353 ms (**-9.4%**) | 1.0813 ms |
| ③ A-Trous の `ReconstructWorldPosition` を線形深度で代替 | 1.0373 ms | **0.7834 ms (-27.6%)** |

この 3 実験から読み取れること:

- **①がほぼ効かない** → ClosestHit シェーダ呼び出しは律速ではない。
  つまり「遮蔽ヒット時のシェーダ実行コスト」は問題ではなく、**BVH トラバース自体**が支配的。
- **②が 9% どまり** → 現在の構図では可視ピクセルの大半が太陽に向いており、
  裏面棄却の取り分は小さい（別の構図・低い太陽高度では効きが変わる。汎用的には入れる価値あり）。
- **③が 28% 効く** → デノイザは**帯域だけでなく ALU でも詰まっている**。
  1 パスあたり 10 回、4 パスで **1 ピクセルあたり 40 回**も 4×4 行列積を回して世界座標を作り直し、
  さらに `length()` で「カメラからの距離」に落としているだけ。深度の線形化だけで足りる。

---

## 2. RTシャドウが重い理由（優先度順）

### 【最大】2.1 1 サンプルのために後処理 5 パス + 全画面コピーを回している

現在のパイプライン（ライト 1 灯・1920×1080 あたり）:

```
DispatchRays              1回  … 2.07M rays                        1.14 ms
  ↓ denoiseTemp
Temporal CS               1回  … 3×3 空間前処理 + 再投影 + Variance Clamp
CopyResource              1回  … 8.3 MB の全画面コピー（履歴用）      計 0.29 ms
  ↓ texture
A-Trous CS                4回  … 各 3×3 タップ、step 1/2/4/8         1.08 ms
```

- A-Trous は `kSteps[] = {1,2,4,8}` の **4 パス固定**（`kNumPasses が偶数` という
  ping-pong の都合で偶数に縛られている）。1 パスあたり約 **0.27 ms**。
- **step=8（実効 31×31）まで広げる必要は 1spp のシャドウには無い。** 2 パスへ落とすだけで
  約 **0.54 ms** が返ってくる（frame の 4.9%）。
- 履歴更新が `CopyResource` になっており、全画面 read+write（約 16MB のトラフィック）を
  毎フレーム払っている。テクスチャ 2 枚の ping-pong にすればコピーはゼロにできる。

### 【大】2.2 深度→世界座標の復元が全パスで重複している（実測 -28%）

`RTShadow.hlsl` / `RTShadowTemporal.CS.hlsl` / `RTShadowDenoise.hlsl` の 3 つが
それぞれ `gInvViewProj` を受け取り、近傍タップごとに `ReconstructWorldPosition` を呼んでいる。

```
Denoise:   (中心1 + 近傍9) × 4パス = 40 回 / px
Temporal:  (中心1 + 近傍9)         = 10 回 / px
RayGen:    中心 + 量子化誤差測定用  =  2 回 / px
                                     ─────────
                                     52 回 / px / frame
```

しかも用途はほぼ全て `length(worldPos)`（＝カメラ距離）**のみ**。
NDC 深度から線形ビュー深度を出す 2 命令で置き換えられる。

さらに RayGen 側では、セルフシャドウバイアスのために
`ReconstructWorldPosition` を**深度を 1 コードずらして 2 回目**呼んでいる
（`kDepthQuantum` の実測誤差評価）。これは正しさのための実装だが、
Projection 行列の要素から解析的に出せるので毎ピクセルの行列積は不要。

### 【中】2.3 全画面フル解像度・R32_FLOAT で 3 種のテクスチャを持っている

`ShadowView` 1 つあたり **R32_FLOAT × 3 枚**（`texture` / `historyTexture` / `denoiseTemp`）。
1920×1080 なら 8.3MB × 3 = **25MB / ライト / ビュー**。
`views_[2][4]` の配列なので最大 8 スロット = 最悪 200MB。

- シャドウマスクは 0〜1 の 1 チャンネル。**R8_UNORM で足りる**（帯域 1/4）。
- ハーフ解像度トレース + バイラテラルアップサンプルなら Trace が約 1/4（-0.85ms 相当）。

### 【中】2.4 ライト灯数ぶんチェーン全体が線形に増える

`RayTracingSubsystem::DispatchRTShadow{Trace,Temporal,Denoise}` は
`for (li < MAX_DIRECTIONAL_LIGHTS)` で **有効ライトごとにチェーン全体を回す**。

今は Sun 1 灯なので 2.52ms だが、**大気エディタで Moon を有効化した瞬間に 5.0ms になる**
（`AtmosphereEditor::ApplyMoonSettings` が第 2 ディレクショナルライトを生成する）。
月の影は昼間には視覚的にほぼ寄与しないので、
「照度が閾値未満のライトは RTシャドウをスキップする」ガードが必要。

### 【小】2.5 BLAS が常に LOD0（最高密度）

`AccelerationStructureManager::BuildBLASFromModelResource` は
`indexCount_`（= LOD0 の全インデックス）で BLAS を作る。
`ModelResource` は既に LOD1/LOD2 のインデックス範囲を持っている（`SubMeshLodRange`）ので、
**シャドウ専用に LOD1 の BLAS を別途持つ**ことで BVH トラバース段数とメモリを下げられる。
実験①が効かなかった＝トラバースが律速、という結果と整合するので効果が期待できる方向。

### 【小】2.6 TLAS を毎フレーム完全再構築している

`BuildTLAS` は `PREFER_FAST_TRACE` のみで、`ALLOW_UPDATE` / `PERFORM_UPDATE` を使っていない。
インスタンス数が少ない今は `ASBuild 0.054ms` なので無害だが、
インスタンスが増えると素直に効いてくる。
また `GetAllObjects()` を毎フレーム走査して全要素に `dynamic_cast<ModelGameObject*>` している。

---

## 3. 基盤の汎用性レビュー

### 3.1 現状の構造

```
AccelerationStructureManager   … BLAS/TLAS（唯一の共有部品）
GlobalRootSignatureManager     … 宣言的にグローバルRSを構築（共有）
RayTracingPipelineBuilder      … State Object 構築（共有）
ShaderTableBuilder             … シェーダテーブル構築（共有）
RayTracingOutputViewSet        … view毎の UAV/SRV 出力テクスチャ（★水面系のみが利用）

WaterRayTracingPassBase        … 水面3種の共通基盤（出力/定数/ガード/診断）
 ├─ WaterRefractionRayTracingManager
 ├─ WaterReflectionRayTracingManager
 └─ WaterCausticsRayTracingManager

RayTracingShadowManager        … ★基盤を使わず独自に全部持っている（747行）
```

### 3.2 汎用RTに向けて足りないもの（重要度順）

#### ① ヒット点でマテリアル/ジオメトリを引けない（最重要）

- `RayTracingPipelineBuilder` に**ローカルルートシグネチャの subobject が無い**
  （`D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE` /
  `..._SUBOBJECT_TO_EXPORTS_ASSOCIATION` 未対応）。
- `ShaderTableBuilder` は 1 レコード = シェーダ識別子 32B 固定で、
  **ローカルルート引数を書き込む余地が無い**。
- `BuildTLAS` は `InstanceContributionToHitGroupIndex = 0` 固定、`InstanceMask = 0xFF` 固定。
- 結果、反射・屈折はヒット面の色を取れず、スクリーン空間再投影に頼っている（→ 4章）。

推奨は **bindless なジオメトリ記述子テーブル**
（`InstanceID` を鍵に VB/IB/マテリアルを StructuredBuffer で引く）。
既存の `DescriptorManager` と相性が良く、ローカルルートシグネチャは
必要になってから足せばよい。

#### ② `InstanceMask` が使われていない

シャドウキャスター／反射に映るもの／コースティクス受光面を**レイ種別で選り分けられない**。
現在は「半透明は TLAS に入れない」という一律の除外しか手段がない
（`BlendMode != kBlendModeNone` で弾いている）。

#### ③ スキンメッシュ（アニメーション）が一切 RT に載らない

- TLAS 収集は `dynamic_cast<ModelGameObject*>` のみ。`AnimatedModelObject` は
  `ModelGameObject` を継承していないため**キャラクターが影を落とさない**。
- BLAS は「初回 1 回作って `HasBLAS()` で二度と作らない」設計で、
  **頂点が動くメッシュの BLAS 更新パスが存在しない**。

#### ④ `RayTracingShadowManager` が共通基盤を使っていない

| 機能 | Water 側 | Shadow 側 |
|---|---|---|
| 出力テクスチャ管理 | `RayTracingOutputViewSet` | 独自 `ShadowView` 構造体 |
| ディスパッチ前ガード | `BeginDispatch` / `ValidateDispatchPreconditions` | 各所に散らばった `if` |
| 診断情報 | `DispatchDiagnostics` | **無し**（`dispatchLogCount_ < 10` のログのみ） |
| CommandList4 取得 | `QueryCommandList4` | 毎ディスパッチで `QueryInterface` |

→ `WaterRayTracingPassBase` を `Water` から切り離して **`RayTracingPassBase`** に昇格させ、
  シャドウもその上に載せるのが素直。名前に `Water` が付いているのは既に実態と合っていない。

#### ⑤ その他の一貫性・小さな瑕疵

| 箇所 | 内容 |
|---|---|
| シェーダパス指定 | `L"RTShadow.hlsl"`（ファイル名） vs `L"Engine/Assets/Shaders/.../RTWaterRefraction.hlsl"`（フルパス）で不統一 |
| 各マネージャの `Initialize` | それぞれが `ShaderCompiler` をローカル生成・初期化（4 重初期化） |
| `GetRootParameterIndex` | `std::map<std::string, UINT>` 検索を**毎ディスパッチ 6 回**、`std::string` 一時生成つきで呼んでいる |
| `RayTracingSubsystem` の 3 関数 | 同じ前処理（camera 取得・invViewProj 計算・SRV 収集・ライトループ）を丸ごと 3 回コピペ。逆行列計算も 1 フレーム 3 回 |
| 同上 | 全 RT ディスパッチが `GetGameViewCamera3D()` を使う。`ReflectionView` では行列が GameView のものになり結果が壊れる（現在は発火しないが器だけ残っている潜在バグ） |
| `RayTracingShadowManager.h:30` | 「必要なら 2〜4 まで増やす」とあるが**増やす手段が存在しない** |
| `RayTracingShadowSettings` | `SetSettings` の呼び出し元ゼロ。`EditorSettingsSubsystem` にも未登録 |
| `AccelerationStructureManager` | BLAS の破棄・再構築 API が無い（`blasList_` は append only）。モデルをアンロードしても BLAS が残る |

---

## 4. 画面外の遮蔽・反射はどこまで取れているか

パスによって結論が正反対なので分けて記述する。

### 4.1 RTシャドウ → **画面外の遮蔽物も正しく効いている**

TLAS には**フラスタムに関係なくシーン全体**が入る
（`BuildAccelerationStructures` の収集条件は `IsActive()` のみで、
フラスタムカリングも Hi-Z オクルージョンも参照していない）。
レイの**原点**は画面に映った面だが、そこから光源へ飛んだレイは画面外のジオメトリにも当たる。
これがシャドウマップに対する RT の本質的な利点であり、そこは機能している。

ただし穴が 3 つある:

| # | 穴 | 影響 |
|---|---|---|
| 1 | `maxRayDistance = 1000.0f` 固定 | far=100000・島が数千単位に散る構図で遠景の島が影を落とさない。**太陽高度 10° ならレイ1000で稼げる高さは174単位だけ**なので、朝夕の長い影は原理的に出せない |
| 2 | TLAS に入るのは `ModelGameObject` かつ `BlendMode::kBlendModeNone` のみ | スキンメッシュ・FFT海面・パーティクル・アルファブレンドの草木が**影を落とさない** |
| 3 | 受け手は画面内ピクセルのみ | スクリーン空間マスクとして正しい仕様。問題なし |

### 4.2 RT水面反射／屈折 → **レイは当たっているが色を持ち帰れず捨てている**

`RTWaterReflection.hlsl` のペイロードは `{hitT, hitFlag}` だけで、
ClosestHit は `RayTCurrent()` と 1.0 を書くのみ。ヒット面のアルベドも法線も取れない。

```
TraceRay → hitFlag=1（画面外でも当たる）
  ↓ 色が無いので
hitWorldPos をスクリーンへ再投影 → SceneColor を Load
  ├ clip.w <= 0        → kRTReasonInvalidClip で棄却（カメラ後方）
  ├ edgeFade <= 1e-4   → kRTReasonInvalidClip で棄却（★画面外）
  └ 深度不一致          → kRTReasonDepthMismatch で棄却（画面内だが他物体に隠れている）
```

さらに `gMaxReflectionOffsetPixels` で再投影先 UV のズレ量までクランプしている。
棄却されると空の環境マップへフォールバックする。

**つまり RT水面反射は実質 SSR であり、`RAY_FLAG_NONE` の最近接ヒット探索（早期終了なし）に
0.49ms を払いながら SSR と同じ画面内制約を受けている。**
症状としては「反射に映っていた島が画面外に出た瞬間に空へ化ける」。屈折も同構造。

→ 解決は 3.2 ① のジオメトリ記述子テーブル（Stage 5）。

---

## 5. 実装計画（Stage 0 〜 5）

```
Stage 0  可視化と操作性                 ← 何も見えない/触れないので最初
   ↓
Stage 1  シェーダ内で閉じる影の最適化     ← 基盤と衝突しないので先取り (-0.84ms)
   ↓
Stage 2  基盤統一 RayTracingPassBase     ← 分水嶺
   ↓
Stage 3  リソース管理を伴う影の最適化     ← Stage 2 の上で1回だけ書く
   ↓
Stage 4  TLAS/BLAS の汎用化（スキン・InstanceMask）
   ↓
Stage 5  ヒット点シェーディング（反射の画面外対応）
```

### Stage 0 — 可視化と操作性 ✅ 実装済み（2026-07-28）

**目的**: 以降の全 Stage の検証手段を作る。現状 RT には「見る手段」も「触る手段」もない。

**実装結果**

| 追加/変更 | ファイル |
|---|---|
| 共通診断型 | `Engine/Src/Graphics/RayTracing/RayTracingDispatchInfo.{h,cpp}`（新規） |
| デバッグパネル | `Engine/Src/Editor/ImGui/RayTracingDebugPanel.{h,cpp}`（新規） |
| 設定の自動保存 | `Engine/Src/Graphics/RayTracing/RayTracingSettingsSection.{h,cpp}`（新規、`RayTracing.json`） |
| AS 統計の公開 | `AccelerationStructureManager`（Tier / インスタンス数 / 各バッファのバイト数 / 退避数） |
| 影の診断・中間バッファ公開・設定追加 | `RayTracingShadowManager`（`atrousPassCount` / `disableHistory` を実際に効かせた） |
| 共通型への変換 | `WaterRayTracingPassBase::GetDispatchInfo()` |
| パネル登録 | `DebugSubsystem`（`RegisterEngineDebugPanel` の初の利用者） |

実機確認済みの表示内容（`Debug > Ray Tracing`）:

- 加速構造: Raytracing Tier 1.1 / BLAS 5 / TLAS インスタンス 9 /
  BLAS 56.57MB / TLAS 3.5KB / スクラッチ 8.71MB / ASBuild 0.061ms
- RTシャドウ: ステージ別 GPU 時間とフレーム比、ライトごとの status・解像度・レイ本数・
  実効設定（`samples/px=1 atrousPasses=4 lightRadius=0.02 maxRayDistance=1000`）、
  総レイ本数と Grays/s
- 水面 RT: 3 パスの status / 解像度 / レイ本数 / GPU 時間 / waterHeight・activeWaves
- 中間バッファ: Raw（1spp のスペックルノイズ）/ Temporal+A-Trous 後（平滑化済み）/ 履歴 の 3 枚

**実装中に見つかって直した罠**

- **中間バッファの状態不整合**: `denoiseTemp` はパス終了時 `UNORDERED_ACCESS`、
  `historyTexture` は `NON_PIXEL_SHADER_RESOURCE` で残るため、そのまま `ImGui::Image` に
  渡すとピクセルシェーダから不正な状態で読むことになる。
  `RequestDebugViewTransition()` / `PrepareDebugViews()` を追加し、
  **表示を要求したフレームだけ** `PIXEL_SHADER_RESOURCE` へ遷移させる方式にした
  （1 フレーム限りの要求なので、パネルを閉じればバリアも消える）。
- A-Trous パス数は ping-pong の parity に縛られるため、UI は 0 / 2 / 4 の 3 択にした
  （奇数だと最終結果が `denoiseTemp` 側に残る）。この制約は Stage 3 で解消する。
- マスクは R32_FLOAT の 1ch なので ImGui では赤単色で表示される（既知の見た目上の制限）。

**未了（Stage 0 の範囲外と判断）**: ステージ別 ON/OFF の完全なトグルは、
Temporal を切ると `view.texture` が書かれないままになるため、
Stage 3 の履歴 ping-pong 化と同時にやるのが安全。
代わりに `atrousPassCount=0`（デノイズ無効）と `disableHistory`（履歴無効）を用意した。

1. **共通診断構造体 `RayTracingDispatchInfo` を先に切り出す。**
   水面側の `DispatchDiagnostics` を一般化し、影側にも同じものを持たせる。
   パネルが「共通の型」だけを読むようにしておけば、Stage 2 で実装が入れ替わっても
   パネルを書き直さずに済む。派生固有の値は `extras[]`（ラベル＋値）で汎用的に運ぶ。
2. **`RayTracingDebugPanel`** を `RegisterEngineDebugPanel("Ray Tracing", ...)` で Debug メニューへ登録。
   この関数は実装済みで呼び出し元ゼロ＝初の利用者になる。表示内容:
   - **加速構造**: DXR 対応可否 / BLAS 数 / TLAS インスタンス数 / AS・スクラッチのバイト数
   - **RTシャドウ**: ライトごとの status・解像度・レイ本数、
     Trace / Temporal / Denoise の GPU 時間内訳（`GpuTimestampProfiler` から引く）
   - **中間バッファのサムネイル**: 生マスク / Temporal 後 / 履歴
     （`RenderPassDebugPanel::DrawThumbnail` と同じ作法）
   - **水面 RT**: 3 パスの `RayTracingDispatchInfo` を表で（既に情報はあるのに誰も表示していない）
   - **設定**: `RayTracingShadowSettings` 全項目 + A-Trous パス数
3. **設定を到達可能にする**: `SetSettings` をパネルから呼び、
   `EditorSettingsSubsystem` のセクションとして登録して再起動後も残す。
   A-Trous パス数を定数からパラメータへ昇格させる。

**なぜ設定の外出しが先か**: Stage 1 の各項目は効果と同時に見た目の再較正（`phiDepth` 等）が必要。
スライダが無いとビルド→起動の往復になる。

**検証ゲート**: パネル上に Trace 1.14 / Temporal 0.29 / Denoise 1.08ms が表示される。

### Stage 1 — シェーダ内で閉じる影の最適化 ✅ 実装済み（2026-07-28）

**なぜ先取りできるか**: 下記は HLSL 内か C++ の定数で完結し、
**Stage 2 が書き換える領域（出力テクスチャ・状態遷移・履歴）に触らない**。

**実測結果**（1920×991・Sun 1灯・同一構図。RT シャドウ 3 パスは run 間 ±3〜5% で安定）

| ステージ | Before | After | 差 |
|---|---|---|---|
| RTShadowTrace | 0.966 ms | 1.05〜1.14 ms | ±0（下記注） |
| RTShadowTemporal | 0.307 ms | **0.226 ms** | **-26%** |
| RTShadowDenoise | 1.137 ms | **0.390 ms** | **-66%** |
| **合計** | **2.409 ms** | **1.67 ms** | **-0.74 ms（-31%）** |

内訳:
- 深度線形化のみ（A-Trous 4 パスのまま）: Denoise 1.137 → 0.787（**-31%**）、
  Temporal 0.307 → 0.226（**-26%**）＝ 計 **-0.43 ms**
- A-Trous 4 → 2 パス: Denoise 0.787 → 0.390（ちょうど半分）＝ 追加 **-0.40 ms**

**実装内容**

| やったこと | ファイル |
|---|---|
| `LinearizeViewDepth()` を追加（`viewZ = _43 / (ndcZ - _33)`） | `DepthReconstruction.hlsli` |
| デノイザ/テンポラルの深度指標を線形ビュー深度へ。`invViewProj`(16 float) を `float2` へ縮小し、ルート定数 24→8 | `RTShadowDenoise.hlsl` / `RTShadowTemporal.CS.hlsl` |
| `DenoiseConstants` / `TemporalConstants` を C++ 側の名前付き構造体＋`static_assert` に整理 | `RayTracingShadowManager.cpp` |
| `Denoise` / `ApplyTemporal` の引数を `invViewProj` → `projection` に変更 | 同上 + `RayTracingSubsystem.cpp` |
| `atrousPassCount` 既定 4 → **2** | `RayTracingShadowManager.h` |
| `denoisePhiDepth` を設定＋パネル＋自動保存へ追加 | 同上 + パネル + `RayTracingSettingsSection` |
| `scaleRayDistanceBySunElevation`（射程を `1/sin(高度)` 倍・最大 10 倍） | `ResolveEffectiveRayDistance()` |

**副次的な正しさの改善**: 旧実装の深度指標は `length(worldPos)`＝**ワールド原点からの距離**で、
視線方向に沿っていなかった（原点から等距離ならシルエットの両側でエッジを検出できない）。
線形ビュー深度は安いだけでなく指標としても正しい。
そのぶん `phiDepth` の意味が変わるのでスライダ（既定 1.0）で調整できるようにした。

**射程スケーリングのコスト = 0（現状）**: 既定の太陽方向は `(0,-1,0)`＝高度 90° なので
`sin=1` → 倍率 1.0 で従来と同一。ON/OFF で Trace は 1.1428ms で完全に一致した。
**コストが増えるのは実際に太陽を下げたときだけ**で、そのときは正しさのための対価になる。

**Stage 3 へ送った項目とその理由**

- **照度によるライト間引き**: マスクのバインドが per-light で、
  `SetRTShadowHandle({})` は「テーブルを張らない」＝**前フレームの記述子が残る**契約。
  さらに `useRTShadow` の判定が mask0 の寸法だけで行われるため、
  「太陽を間引いて月だけ残す」と月の影まで無効になる。
  マスク管理を直さずに間引くと夜間に不具合を出すので、
  直接光/環境光マスクの分離（Stage 3）と同時にやる。
- **`dot(N,L)<=0` の早期棄却**（-0.10ms）: 同上。マスクが IBL・空・環境光・コースティクスの
  遮蔽にも流用されている（`DeferredLighting.PS.hlsl:534-640`）。

**検証**

- 静止画: 椰子の幹・葉の影が 2 パスでも輪郭を保ち、ノイズ・バンディングなし
- カメラ回転中: ゴースト・尾引き・黒いチカつきなし
- RayGen のセルフシャドウバイアス（過去に「移動中の黒チカチカ」を起こした箇所）は**意図的に未変更**。
  ワールド座標が本当に必要なのは RayGen だけで、かつ 1px あたり 2 回しか呼ばれず
  取り分が小さいため、リスクに見合わない。

### Stage 2 — 基盤統一（分水嶺） ✅ 実装済み（2026-07-28）

**検証ゲート結果: 合格。** 同一構図・同一解像度（1920×991）で全 RT パスの GPU 時間が
Stage 1 と一致した（差は全て 0.006ms 以内＝計測ノイズの範囲）。画像も影が完全に一致。

| パス | Stage 1 | Stage 2 | 差 |
|---|---|---|---|
| RTShadowTrace | 1.1428 ms | 1.1425 ms | -0.0003 |
| RTShadowTemporal | 0.2263 ms | 0.2273 ms | +0.0010 |
| RTShadowDenoise | 0.3901 ms | 0.3901 ms | ±0 |
| RTWaterCausticsPass | 0.8550 ms | 0.8509 ms | -0.0041 |
| RTWaterRefractionPass | 0.5171 ms | 0.5181 ms | +0.0010 |
| RTWaterReflectionPass | 0.4874 ms | 0.4936 ms | +0.0062 |

**実装内容**

| 段階 | やったこと |
|---|---|
| 2a | `WaterRayTracingPassBase` の汎用部分を `Graphics/RayTracing/RayTracingPassBase` へ引き上げ。水面固有（`WaterSurfaceConstants` / `IWaterSurfaceModelProvider`）だけ `WaterRayTracingPassBase : RayTracingPassBase` に残した。あわせて水面独自の `DispatchStatus` / `DispatchDiagnostics` を廃し、Stage 0 で作った `RayTracingDispatchInfo` に一本化 |
| 2b | `RayTracingOutputViewSet` を「view 固定 2 枚」から「平坦なスロット 32 枚 + `TextureOptions`（フォーマット / UAV 要否 / 初期ステート）」へ一般化。スロットの意味は呼び出し元が決める |
| 2c | `RayTracingShadowManager` を `RayTracingPassBase` の派生に。独自 `ShadowView` のテクスチャ 3 枚（Mask / History / DenoiseTemp）を共通基盤へ移し、`MakeSlotIndex(view, light, 用途)` で参照。重複していた 8 メンバ（dxCommon_ / descriptorManager_ / asMgr_ / globalRootSigMgr_ / stateObject_ / stateObjectProperties_ / shaderTableBuilder_ / isInitialized_）を削除 |
| 2d | ルートパラメータ番号を Initialize で 1 回だけ解決してキャッシュ（毎ディスパッチ 6 回の `std::map<std::string>` 検索を撤去）。`RayTracingSubsystem` の重複 3 関数を `BuildShadowStageContext` + `ForEachShadowCastingLight` に集約 |

**踏んだ罠（重要）**

- **基底メンバの隠蔽で影が全消失した。** 2c で `RayTracingShadowManager` を派生クラスにした際、
  派生側に `bool isInitialized_` の宣言が残っていた。`Initialize()` は派生側を `true` にする一方、
  `IsInitialized()`（基底の実装）は基底側の `false` を読み続けるため、
  `RTShadowPass::Execute` が毎フレーム即 return して**影が完全に消えた**。
  コンパイルエラーにも警告にもならない。
  → **派生させるときは、基底へ移したメンバの宣言が派生側に残っていないか必ず確認する。**
  「数値が変わらないこと」という検証ゲートが無ければ見逃していた（実際 RTShadow 3 パスが
  0.001ms 未満になって初めて気づいた）。
- **計測は構図を固定しないと比較にならない。** スクリーンショット用のクリック / ドラッグで
  デバッグカメラが動き、`DebugCamera.json` が自動保存される。
  別構図で測った数値は Stage 1 比で「半分」に見え、一瞬 Stage 2 の効果と誤認しかけた。
  計測スクリプトは起動前に必ず `DebugCamera.json` を既知の構図へ書き戻すこと。
- **前面化は 1 回では足りない。** 待機中に前面を奪われるとフレームが止まるので、
  ポーリングのたびに `SetForegroundWindow` を呼び直す。

**積み残し（Stage 2d の一部）**

- ビューごとのカメラ供給。`RayTracingSubsystem` は実行中のビューに関わらず
  `GetGameViewCamera3D()` を使っている。`RenderManager::GetViewMatrix()` も内部的には
  「アクティブな Camera3D」を返すだけでビュー別ではないため、単純な差し替えでは直らない。
  現状 `BuildRenderViewRequests()` が反射ビューを発行しないため発火しないので、
  コードに TODO を残して据え置いた。`ViewID::ReflectionView` を復活させる場合は
  ビュー別カメラの供給を先に用意すること。
- `ShaderCompiler` の共有（4 重初期化）とシェーダパス指定の統一。

---

### Stage 2 の元の計画（記録用）

**なぜ Stage 3 より前か**: 残った影の最適化は**全部リソース管理そのもの**
（履歴の ping-pong / R8化 / ハーフ解像度）。
今の独自 `ShadowView` に書いてから `RayTracingOutputViewSet` へ移すと**二度書き**になる。
逆順なら 1 回で済み、水面 3 パスも同じ恩恵を受ける。

- **2a. 純粋な移動**: `WaterRayTracingPassBase` → `Graphics/RayTracing/RayTracingPassBase` に
  改名・移動。水面固有（`WaterSurfaceConstants` / `IWaterSurfaceModelProvider`）は
  `WaterRayTracingPassBase : RayTracingPassBase` に残す。**振る舞い変更ゼロ**。
- **2b. 基盤の拡張**: `RayTracingOutputViewSet` を
  「view あたり複数の名前付きテクスチャ + フォーマット指定 + ping-pong ペア」対応にする
  （影は mask / history / denoiseTemp の 3 枚が必要）。
- **2c. 影を載せ替え**: 独自 `ShadowView` を削除。
- **2d. こぼれた掃除**: ルートパラメータインデックスのキャッシュ化、`ShaderCompiler` の共有、
  シェーダパス指定の統一、`RayTracingSubsystem` の重複 3 関数の統合、
  ビューごとのカメラ行列の修正（または `ViewID::ReflectionView` の器ごと削除）。

**検証ゲート**: **数値が全部変わらないこと**。
Trace 1.14 / Denoise 1.08 / Caustics 1.10 / Refraction 0.52 / Reflection 0.49 が維持され
画像が完全一致すれば移動は正しい。ここで数値が動いたら移動にバグがある。

### Stage 3 — リソース管理を伴う影の最適化

- 履歴の ping-pong 化（`CopyResource` 廃止・16MB/frame の帯域回収）
- `R8_UNORM` 化（5 パス全部の帯域が 1/4）
- 裏面早期棄却 ＋ **直接光マスクと環境光マスクの分離判断**
- ハーフ解像度トレース + バイラテラルアップサンプル（Trace -0.8ms 前後。本 Stage 最大の作業）
- 影専用の LOD1 BLAS（実験①よりトラバースが律速と分かっているので効く方向）

**検証ゲート**: 影 1.68ms → **1.0ms 前後**。
浮いた分を `softShadowSamples` 1→2 に投資すれば、
**当初やりたかった「ちゃんとしたソフトシャドウ」が同じ予算で実現できる**。

### Stage 4 — TLAS/BLAS の汎用化

1. **`InstanceMask` にビット定義**（`kShadowCaster` / `kReflectionVisible` / `kCausticsReceiver`）。
   「`BlendMode != None` は TLAS に入れない」の一律除外を置き換える
   → 半透明の草木が影を落とせるようになる（4.1 穴②）。
2. **BLAS のライフサイクル API**: 破棄・再構築（`ALLOW_UPDATE` + `PERFORM_UPDATE`）。
3. **TLAS の差分更新**（静止フレームは `PERFORM_UPDATE`）。現状 0.054ms なので後回しで正しい。
4. **スキンメッシュを TLAS へ**: `AnimatedModelObject` を収集ループに入れ、
   スキニング結果を UAV 頂点バッファへ出して BLAS を更新（2 が前提）。

**検証ゲート**: キャラクター・草木が影を落とす。ASBuild が許容内。

### Stage 5 — ヒット点シェーディング（反射の画面外対応）

最大の作業量で、**唯一見た目が大きく変わる** Stage。
Stage 0 のデバッグビューと Stage 4 の `InstanceMask` が揃っていないと検証も制御もできない。

1. **ジオメトリ記述子テーブル（bindless）**:
   `InstanceID` → `{ vertexBufferSRVIndex, indexBufferSRVIndex, materialIndex, normalMatrix }`
   の StructuredBuffer を TLAS インスタンスと同時に更新。
2. ClosestHit を書き換えてヒット点で直接シェーディング。
   スクリーン空間再投影は**高速パス／フォールバックに降格**
   （画面内で深度が一致するなら SceneColor のほうが安いので、消さずに残すのが得）。
3. 屈折も同様。

**検証ゲート**: 画面外に出た島が反射に映り続ける。
`kRTReasonInvalidClip` の発生率（Stage 0 のパネルに出す）が激減する。

---

## 6. 補足: 「16本レイ」説について

`RTShadow.hlsl:143-144`

```hlsl
static const int kMaxSamples = 16;
int numSamples = clamp(gSoftShadowSamples, 1, kMaxSamples);
```

`gSoftShadowSamples` の供給元は `RayTracingShadowSettings::softShadowSamples` のみで、
リポジトリ全体で**既定値 `1` 以外に代入している箇所は存在しない**
（`grep softShadowSamples` のヒットは宣言・構造体定義・代入の 3 箇所だけ）。

実測の Trace 1.14ms は「2.07M 本 / 1.14ms ≒ 1.8 Grays/s」であり、
16 本飛んでいればこの 10 倍以上の時間になっているはずで、数値としても整合する。

**したがって「レイ本数を削る」方向の改善余地は現状設定ではほぼ無く、
削るべきは後処理チェーンと重複した座標復元。**

---

## 7. 参考: 計測手順（再現用）

```
1. DebugSubsystem::PostFinalizeFrame に 600 フレーム目 1 回だけ
   gpuProfiler_.GetResults() を全スロットログ出力するプローブを追加
2. MSBuild で Development をビルド
3. Invoke-CimMethod Win32_Process Create で起動
4. ★起動後にウィンドウを前面化する（SetForegroundWindow）
5. Cache/logs/Graphics/Graphics_<timestamp>.log を grep
6. 設定値の A/B は Application/Saved/EditorSettings/RayTracing.json を書いてから起動すれば
   再ビルド不要（登録時に Deserialize されるため）
```

**計測時の落とし穴（実際にハマった分）**

- **ウィンドウが前面でないとフレームが進まない。** オクルージョン時にスロットルされるため、
  デタッチ起動しただけだと frame 600 に到達せずプローブが永久に出ない
  （ログが 105 行で止まって見える）。必ず前面化すること。
- **`/t:Rebuild` は使わない。** DirectXTex の `ATGDeleteShaders`(AfterTargets=Clean) が
  `Shaders/Compiled/*.inc` を消し、以後 `CompileShaders` が 9009 で失敗してビルド不能になる。
  構造体サイズを変えたときのクリーンは `generated/CoreEngine/obj/<Config>` の削除だけにする。
- **フレーム全体の数値は run 間で大きく振れる**（Frame Total 8.1〜12.8ms、
  RTWaterCaustics 0.85〜2.36ms、GBuffer 0.50〜0.92ms）。
  一方 **RT シャドウ 3 パスは ±3〜5% で安定**しているので、影の A/B には使える。
  影以外のパスを基準に正規化しようとすると、パスごとにクロック感度が違うため誤った結論になる。
- ログはバッファされるため、最終行だけを待つ判定にすると取りこぼす。

この手順は将来的に Stage 0 のデバッグパネルへ置き換えられる
（パネルは同じ数値をリアルタイム表示する）。
