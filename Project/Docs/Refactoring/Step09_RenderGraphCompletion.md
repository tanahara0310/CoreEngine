# Step 9: RenderGraph 全面管理化と Deferred + Forward ハイブリッドの再設計

## ステータス
- 状態: **Phase A〜F 実装完了（各フェーズでビルド + 実行画面 + ログ検証済み）**。残タスクは §「今後の改善候補」参照
- 優先度: 高
- 依存ステップ: Step 8（完了済み）

## 進捗ログ（2026-07-08）
- **Phase A 完了**: RenderGraph.h/.cpp — リソースバージョニング、WAR エッジ、循環検出=assert+登録順フォールバック、
  未解決リソースの実行時再解決（Blackboard 遅延登録対応）+ Debug ログ、ResourceBarrierBatch による
  パス単位バリア一括発行、同一リソース Read+Write の単一遷移マージ（Write 優先）、UAV 連続書き込みの自動 UAV バリア、
  最小 index 優先 Kahn による決定的実行順
- **Phase B 完了**: RenderPass に DeclareResources / ConfigureForView / IsEnabledForView を追加し、
  全 14 パスが自己宣言化。RenderPipeline::BuildRenderGraph は汎用ループになり、具象パス include は
  17 → 4（BackBuffer / PostEffect / Geometry / DeferredLighting、各々 Phase E/F で削除予定の暫定依存）。
  ConfigurePassesForView 削除。EngineSystem の AddPass 順を旧 Graph 登録順（データフロー順）に統一
- **Phase C 完了**: RenderPassPhase 列挙 + AddPass(pass, phase, priority) + RemovePass / RemovePassesByOwner、
  IScene::RegisterRenderPasses フック（SceneManager がシーン生成時に所有者タグ付きで登録・破棄時に一括除去）、
  EngineSystem::GetRenderPipeline 公開、パス分離契約を RenderPass.h に明文化。
  ※ sticky グローバル状態（RenderManager::SetCamera 等 / Model::SetCurrentRenderSlot static）の廃止は
  IRenderer プロトコル全体に波及するため Phase F（RenderManager 縮小）へ移動
- **Phase D 完了（増分）**: 静的リソース（GBuffer×5 / SceneDepth / SceneColor / ShadowMap / SceneColorSnapshot）の
  実行中 Blackboard 再登録を削除（パス分離契約 3 を静的リソースに適用）。RTShadowMask の View 特例を
  RenderGraph::ResolveResources から RenderPipeline::RegisterFrameResources へ移設し、RenderGraph から
  Manager 依存 include を全削除。SSAO / RT 系 / PostEffect の実行時決定出力の公開は Registry 化（Phase E）までの暫定として残置

- **Phase E 完了**:
  - `ASBuildPass`（FrameSetup）新設: BLAS/TLAS 構築と RT シャドウ状態リセットを Graph ノード化。
    `RenderContext::frameNumber` ガードで「フレーム内 1 回」を保証（Graph は View ごとに実行されるため）
  - `RTShadowPass` を **Trace / Temporal / Denoise の 3 Graph ノード**に分割
    （`RayTracingSubsystem::DispatchRTShadowTrace/Temporal/Denoise`）。ライト別内部バリアはマネージャ内サブステップとして残置
  - `FFTOceanPass` に frameNumber ガード追加: View ごとに同一時刻の FFT を二重計算していた無駄を排除
  - **AtmosphereLUT クラッシュの根本原因を特定・解消**: LUT 生成 compute が SRV ディスクリプタヒープの
    バインドを先行パスに暗黙依存していた（フレーム先頭に置くとヒープ未バインドで死ぬ）。
    パス自身が `SetDescriptorHeaps` を呼ぶよう修正し、FrameSetup フェーズ（フレーム先頭）へ移動済み
  - WaterRayTracingPassBase の Begin/EndOutputWrite（UAV ラウンドトリップ 3 箇所）は
    整然と封じられたマネージャ内サブステップとして残置（宣言化は Registry 移行後の改善候補）
- **Phase F 完了**:
  - **投入時ルーティング**: `RenderManager::AddRenderItem` が不透明 Model/SkinnedModel を
    `opaqueDrawQueue_`（GBuffer 経路）へ振り分け。`skipOpaqueModelsInForward_` フラグと
    描画時フィルタを削除。`SetDeferredLightingActive(false)` で Forward フォールバック可能（デバッグ用）
  - **大箱 GeometryPass の分割**: `ForwardQueuePassBase` 基底 + `GeometryPass`（メインキュー）/
    `SkyBoxQueuePass` / `TransparentQueuePass` の 3 Graph ノードへ。各パスがカメラ・スロット等を
    自前設定（パス分離契約 2 準拠）
  - BaseScene::Draw → DrawGeometryPass は Framework から呼ばれない旧経路の残骸と確認（将来削除候補）

### 実装中に判明した注意点
1. ~~AtmosphereLUTPass は ShadowMapPass より前に実行するとクラッシュする~~ → **Phase E で根本解決済み**
   （原因: ディスクリプタヒープ未バインド。教訓: compute パスはヒープを自前バインドすること）
2. SSAO / WaterCaustics / DeferredLighting は「テクニックが実行時に選んだ出力 SRV」を Blackboard に公開する
   （SSAO はブラー有無で出力先が変わる）。静的な事前登録に置き換えるには Registry 化（Create 宣言）が前提
3. エディタカメラ状態は起動間で持ち越されるため、実行画面のスクリーンショット比較は構図が変わる。
   検証はログ（エラー 0）+ 描画要素の有無で判断すること
4. Graph は View ごとに構築・実行されるため、「フレーム内 1 回」であるべき GPU 作業
   （AS 構築 / FFT / LUT）は frameNumber ガードか内部ダーティフラグが必須

## 今後の改善候補（Phase G 相当・任意）
- RGResourceRegistry 化（Create/Import 分離・状態唯一所有・ハンドル化）→ SSAO 等の実行時公開を静的宣言へ
- FFTOcean / Atmosphere のマネージャ内ステージ（約 40 バリア箇所）の Graph ノード分解
- ~~sticky グローバル状態（RenderManager::SetCamera 系 / Model::SetCurrentRenderSlot static）の引数渡し化~~
  → **2026-07-13 解決**: `TransformBufferSlot::Scene` および `RenderManager::activeTransformSlot_` の
  getter は実装全体で一度も消費されておらず（常に `Game` のみが設定・使用されていた）、当初想定した
  「IRenderer プロトコル全体への引数追加」は不要と判明。`Model::SetCurrentRenderSlot`/`GetCurrentRenderSlot`・
  `RenderManager::SetActiveTransformSlot`/`GetActiveTransformSlot`・`TransformBufferSlot` enum 自体を削除し、
  3 箇所の呼び出し元（BaseScene::Draw / GeometryPass::Execute / WaterSurfacePass::Execute）から
  対応する呼び出しを除去した（`ModelSystemRefactoringPlan.md` Phase 4 B2）。
  `RenderManager::SetCamera` 系の sticky 状態は本項目の対象外で未解決のまま。
- RenderManager の PassType 優先度ソートは「単一キュー内の異種レンダラー順序制御」として存続が妥当
  （Graph が管理するのはパス間順序であり、キュー内アイテム順序は引き続き RenderManager の責務）
- IBL / 環境設定の RenderManager からの分離、BaseScene::Draw 旧経路の削除
- パスカリング・async compute・トランジェントエイリアシング・Graph 可視化パネル
- 禁止則 assert（Graph 外 Transition 検出）は Registry 化完了後に導入
- 設計方針の決定事項:
  1. **フレーム中の全レンダーパスを RenderGraph の管理下に置く**（一部 Graph / 一部 Graph 外の混在を解消する）
  2. Deferred + Forward ハイブリッド構成は**維持**する（方式変更なし。境界の実装だけ整理する）
  3. ユーザーパスは「RenderPass 派生 1 ファイル + AddPass 1 行」で追加でき、既存描画へ影響しないこと

---

## 0. 大原則: 何を Graph 管理にし、何を外に残すか

### Graph 管理下に置くもの（= フレーム中に GPU コマンドを記録する全処理）
- 既存の全描画パス（ShadowMap / GBuffer / SSAO / Lighting / Forward 系 / PostEffect / BackBuffer）
- **現在 Graph 外に残っている GPU 作業**（本 Step で全て Graph ノード化する）:
  - `RayTracingShadowManager` 内部の Dispatch / Temporal / Denoise / HistoryCopy
  - `FFTOceanManager` / `FFTOceanDispatchHelper` の各 compute ステップ
  - `AtmosphereManager` の LUT 生成ステップ
  - `WaterRayTracingPassBase` 系の内部遷移
  - BLAS / TLAS 構築（`RayTracingSubsystem::BuildAccelerationStructures` → `ASBuildPass` として FrameSetup フェーズへ）

### Graph 外に残すもの（ホワイトリスト・UE も同じ境界）
- Present / queue submit / command allocator reset（executor 責務。`Render::FinalizeFrame()`）
- フレーム外のアセット生成・アップロード（`IBLGenerator`, `TextureGpuUploader`）
- 上記以外で `ResourceBarrierHelper` を直接呼ぶことは**禁止**とする（§4.4 の禁止則）

### 現状の Graph 外バリア発行箇所（実測: 16 ファイル・102 箇所）

| ファイル | 箇所数 | 処分 |
|---|---|---|
| FFTOceanManager.cpp / FFTOceanDispatchHelper.cpp | 21+9 | Graph ノード分解（Phase E） |
| AtmosphereManager.cpp | 19 | Graph ノード分解（Phase E） |
| RayTracingShadowManager.cpp | 11 | Graph ノード分解（Phase E） |
| OffscreenRenderTarget.cpp | 4 | Begin/End をバインド専用化し撤去（Phase D） |
| WaterRayTracingPassBase.cpp | 3 | 宣言ベースへ移行（Phase E） |
| SceneColorCopyPass.cpp | 4 | パス内バリアを宣言へ移行（Phase D） |
| ShadowMapManager.cpp / DepthStencilManager.cpp | 2+1 | 状態所有を Registry へ一元化（Phase D） |
| AccelerationStructureManager.cpp | 2 | ASBuildPass 化（Phase E） |
| BackBufferRenderTarget.cpp | 2 | Present 境界のみ残す（許容） |
| IBLGenerator.cpp / TextureGpuUploader.cpp | 6+1 | フレーム外ユーティリティとして残す（許容） |
| RenderGraph.cpp / ResourceBarrierHelper 本体 | 17 | Graph 本体（正規の発行元） |

---

## 1. なぜ「パス追加で既存モデル描画が壊れる」のか（根本原因）

水パス組み込み時などに従来モデル描画へ影響が出る原因は、以下 4 つの複合。
新設計はこの 4 つをすべて構造的に不可能にすることを完了条件とする。

### 原因 1: 実行順が AddPass 登録順に暗黙依存
- リソースにバージョンが無く、依存は「最終ライター」のみ追跡（RenderGraph.cpp:265-287）
- WAR（Write-After-Read）エッジが無いため、Read するパスと後から Write するパスの順序は
  Kahn 法の FIFO 挙動に**偶然**依存。パスを 1 つ挟むと登録 index がずれ、順序が変わり得る
- → 対策: リソースバージョニング + WAR エッジ（§4.1）

### 原因 2: パスが残留グローバル状態を書き換える
- `GeometryPass` / `WaterSurfacePass` が実行中に
  `RenderManager::SetCamera()` / `SetActiveTransformSlot()` / `SetDebugLineRenderingEnabled()`、
  さらに **static な `Model::SetCurrentRenderSlot()`** を変更する
- 新しいパスがこれらを触ると、以降に実行される既存パスへ状態が漏れる
  （= 水パス追加でモデル描画が壊れる直接メカニズム）
- → 対策: パス分離契約（§4.5）。カメラ・スロット等は RenderContext / ViewSettings から
  パスごとに毎回設定し、sticky なグローバル setter を廃止

### 原因 3: リソース状態の二重管理
- Graph の自動バリアと、Manager / RenderTarget::Begin()/End() 内部の手動バリアが併存
  （Graph 外 102 箇所）。新パスが手動バリアを発行すると Graph 側の状態追跡とズレる
- → 対策: 状態の唯一の所有者を Registry に一元化（§4.4）

### 原因 4: 実行中の Blackboard 書き換え
- `GeometryPass::Execute()` が FrameBlackboard の SceneColor を再登録する副作用を持つ
- Compile 時に解決済みの実体と実行時の実体が食い違う余地がある
- → 対策: Blackboard（→ Registry）への登録は Graph 構築前のみに制限。実行中は読み取り専用

---

## 2. 現状分析: 完全な RenderGraph に足りないもの（全列挙）

1. **パスが自分の Read/Write を宣言しない** — 全宣言が `RenderPipeline::BuildRenderGraph()`
   (RenderPipeline.cpp:312-477) にハードコード。パス追加に最低 3 箇所のエンジン編集が必要
2. **RenderPipeline が全具象パス型を知っている** — 17 個の具象 include + `GetPass<T>()`
   (dynamic_cast) の型別特殊処理。OCP 違反
3. **ユーザーパス注入 API が無い** — `RenderPassType` の 100-999 予約域はフォワードキュー用で
   Graph パスには効かない。アプリの WaterReflectionPass は RenderPass ですらない（カメラ書換ヘルパー）
4. **WAR ハザード未対応・バージョニング無し**（§1 原因 1）
5. **循環依存で無言フォールバック**（RenderGraph.cpp:140-146）、未解決リソースは無言バリアスキップ
6. **リソースを Graph が所有しない** — トランジェント生成 API・プール・エイリアシング無し。
   RenderTarget は一度作ると永久生存
7. **文字列キーのリソース識別** — 毎フレーム map 検索、typo は実行時まで不明
8. **バリアのバッチ化無し、UAV / エイリアシング / スプリットバリア無し、サブリソース粒度無し**
9. **状態追跡の所有権分散**（§1 原因 3）
10. **パスカリング無し** — 水パス群はシーンに水が無くても登録・実行判定される
11. **Graph 外 GPU 作業の混在**（§0 の表）— 「一部 Graph・一部 Graph 外」の主因
12. **抽象へのリーク** — RenderGraph 内の RTShadowMask View 特例（RenderGraph.cpp:186-194）、
    実行中 Blackboard 書換、`DeferredLightingPass` の dynamic_cast によるカメラ CBV 引き抜き、
    20 個超のポインタを持つ RenderContext
13. **Deferred/Forward 境界が場当たり的** — `SetSkipOpaqueMeshInForwardPass()` の毎フレームトグル、
    「暫定の大箱」GeometryPass、RenderManager 内の第 2 スケジューラ（PassType 優先度ソート）
14. **View 抽象が不完全** — カメラ書換 & 復元のステートフルハック、CaptureView 未実装
15. **マルチキュー / 非同期コンピュート無し**
16. **AtmosphereLUTPass / FFTOceanPass は空宣言 `(void)builder` で登録** — Graph 上は
    依存ゼロの浮きノード。内部遷移は自前管理（§0 で解消）

---

## 3. アーキテクチャ評価

### 3.1 Deferred + Forward ハイブリッドは現状のままで良いか
**方式としては現状維持で良い。変更すべきはレンダリング方式ではなく境界の実装。**
- 不透明 = GBuffer + DeferredLighting、透明 / SkyBox / 水 / パーティクル / UI = Forward
  という分割は UE (FDeferredShadingSceneRenderer) と同型で、変更する理由が無い
- Forward+（クラスタードライティング等）への移行も現時点では不要。透明物が大量のライトを
  受ける要件が出てから検討すれば良い
- ただし境界の実装 3 点は要修正（Phase F）:
  1. `skipOpaqueModelsInForward_` フラグ → RenderItem **投入時**にマテリアルドメインで
     GBuffer キュー / Forward キューへ振り分け
  2. 大箱 GeometryPass → SkyBox / Transparent / Debug / Sprite / UI の個別 Graph ノードへ分割
  3. RenderManager の優先度ソート廃止（順序は Graph の責務）

### 3.2 「全パス RenderGraph 管理」は正しいか
**正しい。** UE5 の RDG も同じ到達点にある:
- RT シャドウのデノイズ・テンポラル、水面シミュレーション、LUT 生成、AS 構築を含め、
  フレーム中の GPU 作業は全て RDG パスとして記録される
- Graph 外に残るのは Present / submit / アップロードのみ（本設計の §0 ホワイトリストと一致）
- 全面 Graph 化によって初めて、パスカリング・エイリアシング・async compute・可視化が
  「全 GPU 作業」に対して機能する

### 3.3 Unreal Engine の方式（参考モデル）
- **RDG**: 毎フレーム `FRDGBuilder` で構築し直す（本エンジンの毎フレーム Compile と同方針）
  - リソースは `FRDGTexture` / `FRDGBuffer` **ハンドル**。`CreateTexture()` でトランジェント生成、
    `RegisterExternalTexture()` で永続リソースをインポート
  - パスのアクセスはシェーダーパラメータ構造体で宣言 → 依存・バリア自動導出
  - トランジェントアロケータ + エイリアシング、スプリットバリア、パスカリング、async compute、
    `-rdgdebug` の宣言/実使用不一致検証
- **拡張**: `FSceneViewExtension` の定義済みフック（PreRenderView / PrePostProcess 等）に
  プラグインがパスを注入 → 本設計の RenderPassPhase の手本
- 系譜: Frostbite FrameGraph（GDC 2017, O'Donnell）

---

## 4. 設計案

### 4.1 RenderGraph 本体の完全化
1. **バージョニング**: Write ごとに version++。依存は「その version のライター」へ張る
2. **WAR エッジ**: リソースごとに現 version の読者リストを保持し、次の Write は全読者に依存
3. **循環 = エラー**: フォールバック廃止。Debug は assert + 依存ダンプ、Release はログ + 登録順
4. **未解決リソース = Debug assert**（無言スキップ廃止）
5. **バリアのバッチ化**: パスごとに遷移を集めて 1 回の ResourceBarrier で発行
6. **UAV バリア**: `builder.WriteUAV(h)` を追加（compute / RT パスの連続 UAV 書き込みに必須。
   FFT・デノイズの Graph 化はこれが前提）
7. **Compute / Copy パス種別**: `AddPass(name, pass, PassFlags::Compute | ...)` で種別を宣言し、
   要求状態のデフォルトと検証に使う
8. **パスカリング**: 最終出力（BackBuffer）から逆順到達解析し、到達しないパスをスキップ
   （enabled フラグ手動管理を段階的に置換。水パス群が自動で消える）
9. **トランジェント確保**: Compile 時に Create 宣言をプールへマップ（寿命解析 → 将来エイリアシング）

### 4.2 新しいコア型

```cpp
// リソースはハンドルで参照（文字列は登録・デバッグ用のみ）
struct RGResourceHandle {
    uint32_t index   = kInvalid;
    uint32_t version = 0;        // 書き込みごとにインクリメント
};

class RenderPass {
public:
    // Graph 構築時: Read/Write/Create を自己宣言（従来 RenderPipeline にあったラムダの移設先）
    virtual void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) = 0;
    // View 適合判定（従来の ConfigurePassesForView の型別分岐を置換）
    virtual bool IsEnabledForView(const RenderViewSettings& view) const { return true; }
    virtual void Execute(const RenderContext& context) = 0;
};

class RenderGraphBuilder {
public:
    RGResourceHandle Read(RGResourceHandle h, D3D12_RESOURCE_STATES state);
    RGResourceHandle Write(RGResourceHandle h, D3D12_RESOURCE_STATES state);  // version++
    RGResourceHandle WriteUAV(RGResourceHandle h);                            // UAV バリア対象
    RGResourceHandle Create(const RenderTargetDescriptor& desc);              // トランジェント宣言
    RGResourceHandle Import(const std::string& name);                         // 永続リソース参照
};

// 挿入フェーズ（UE の SceneViewExtension フック相当）
enum class RenderPassPhase : uint32_t {
    FrameSetup,      // ASBuild / AtmosphereLUT / FFTOcean 等の前処理
    Shadow,
    GBuffer,
    PreLighting,     // SSAO / RTShadow / Caustics
    Lighting,
    PostLighting,    // AerialPerspective 等
    Sky,
    Transparent,
    Water,
    PostProcess,
    Overlay,         // Sprite / UI / Debug
    Final,           // BackBuffer
};

void RenderPipeline::AddPass(std::unique_ptr<RenderPass>, RenderPassPhase, int priority = 0);
void RenderPipeline::RemovePass(RenderPass*);  // シーン破棄時のユーザーパス除去用
```

### 4.3 Manager 系の Graph ノード分解（全面 Graph 化の中核）

方針: **Manager は「リソースとテクニック（PSO / ルートシグネチャ / パラメータ）の保持者」に降格し、
GPU コマンド記録とバリア発行は Graph パスだけが行う。**

| 現在の Graph 外実行 | 分解後の Graph ノード（フェーズ） |
|---|---|
| RayTracingSubsystem::BuildAccelerationStructures | `ASBuildPass`（FrameSetup） |
| AtmosphereManager の LUT 生成一式 | `AtmosphereLUTPass` を実宣言化: Transmittance / MultiScattering / SkyView LUT を `Create`+`WriteUAV` 宣言（FrameSetup、ダーティ時のみ enabled） |
| FFTOceanManager の Spectrum→IFFT→Normal 一連 | `FFTOceanSpectrumPass` → `FFTOceanIFFTPass` → `FFTOceanNormalPass`（FrameSetup、UAV チェーン宣言） |
| RayTracingShadowManager の Dispatch / Temporal / Denoise / HistoryCopy | `RTShadowTracePass` → `RTShadowTemporalPass` → `RTShadowDenoisePass` → `RTShadowHistoryCopyPass`（PreLighting）。履歴・中間バッファは View 別サフィックス付き論理名で Registry に正式登録（例: `RTShadowHistory.GameView`）。これにより RenderGraph.cpp の RTShadowMask View 特例も削除できる |
| WaterRayTracingPassBase 内部遷移 | 宣言ベースへ移行し内部 Transition 削除 |

- 分解単位は「バリアが必要な境界」= compute dispatch / copy の切れ目
- ライト別ループ（kMaxDirectionalLights）はパス内部ループのままで良い（UAV バリアのみ宣言）。
  ライト別リソースを Graph で個別追跡する必要が出たら将来分割

### 4.4 状態管理の一元化と禁止則
1. `FrameBlackboard` → **`RGResourceRegistry`** に発展:
   - Import（永続: BackBuffer / ShadowMap / GBuffer / 履歴バッファ）と
     Transient（Create 宣言からプール確保）を区別
   - **リソース状態の唯一の所有者**。各 Manager / RenderTarget の `D3D12_RESOURCE_STATES*`
     分散保持を廃止し、Registry の状態テーブルへ集約
   - 登録は Graph 構築前のみ。**実行中の書き換え禁止**（GeometryPass の再登録副作用を削除）
2. `OffscreenRenderTarget::Begin()/End()` は **バインド専用**
   （OMSetRenderTargets + viewport + clear）に縮小。内部バリア撤去
3. **禁止則**: フレーム描画経路での `ResourceBarrierHelper::Transition()` 直接呼び出しを禁止。
   Debug ビルドでは「Graph 実行スコープ外からの Transition 呼び出し」を assert で検出する
   （RenderGraph 実行中フラグを Helper が参照する程度の軽い仕組みで良い）
4. 例外は §0 ホワイトリスト（Present 境界・フレーム外ユーティリティ）のみ

### 4.5 パス分離契約（新パスが既存描画を壊さないための不変条件）
1. パスは **DeclareResources で宣言したリソース以外に GPU 上で触れない**
2. パスは **残留グローバル状態を変更しない**:
   - `RenderManager::SetCamera / SetActiveTransformSlot / SetDebugLineRenderingEnabled` の
     sticky setter を廃止。カメラ・スロットは `RenderContext`（ViewSettings に camera 参照を追加）
     から**描画呼び出しごとに**引数で渡す
   - static な `Model::SetCurrentRenderSlot()` を廃止し、描画 API の引数へ
3. Blackboard / Registry への登録は Graph 構築前のみ（実行中は読み取り専用）
4. パスの実行順に関する仮定を書かない（順序は宣言した依存からのみ導出される）

この 4 条件を守る限り、パスの追加・削除・入れ替えは他パスに影響しない。
コードレビュー時のチェックリストとしても使う。

### 4.6 Deferred + Forward 境界の整理
1. `GeometryPass`（大箱）を分割: `SkyBoxPass` / `TransparentPass` / `DebugPrimitivePass` /
   `SpritePass` / `UIPass`。各々が RenderManager の 1 キューを消費する Graph ノード
2. RenderItem **投入時**にマテリアルドメイン（Opaque-Deferred / Forward / Transparent / Sky / Water /
   Overlay）でキューへ振り分け。`skipOpaqueModelsInForward_` フラグ削除
3. RenderManager は **RenderQueueSet + RendererRegistry** に縮小。PassType 優先度ソート削除。
   IBL / 環境設定は LightManager か新設 EnvironmentSettings へ移動
4. `DeferredLightingPass` のカメラ CBV dynamic_cast 引き抜きは、カメラ CBV を
   フレーム共通リソースとして Registry（or CameraManager）から取得する形へ

### 4.7 View モデル
- `RenderViewSettings` にカメラ参照を持たせ、パスは context 経由でカメラを取得
  （WaterReflectionPass の「メインカメラを書き換えて戻す」ハックを廃止）
- 当面は View ごとの Graph 構築・実行を維持（現行方式）。
  マルチ View 単一 Graph 化（UE の ViewFamily 方式）は Phase G の任意項目
- CaptureView は本設計確立後に View 種別追加のみで対応可能になる

---

## 5. 移行フェーズ（各フェーズ単体で動作確認可能・推奨着手順）

| Phase | 内容 | 効果 |
|---|---|---|
| **A: 正しさ** | §4.1 の 1-7（バージョニング / WAR / 循環エラー / 未解決 assert / バリアバッチ / UAV バリア / パス種別）。RenderGraph.h/.cpp 内でほぼ完結 | 「不備が目立つ」直接原因と登録順依存を除去。E の前提となる UAV バリアを先行整備 |
| **B: パス自己宣言化** | DeclareResources / IsEnabledForView 導入、BuildRenderGraph の汎用ループ化、GetPass<T> 特殊処理と 17 具象 include 削除。挙動不変 | パス追加時のエンジン編集箇所が 1 箇所（AddPass）になる |
| **C: 注入 API + 分離契約** | RenderPassPhase + AddPass(phase, priority) + RemovePass、`IScene::RegisterRenderPasses()` フック、§4.5 の sticky グローバル状態廃止（SetCamera / TransformSlot / Model static） | **「エンジン非改変・既存描画非破壊のパス追加」目標を達成** |
| **D: 状態一元化** | Registry 化（Import/Transient・状態唯一所有）、RenderTarget Begin/End バインド専用化、実行中 Blackboard 書換削除、禁止則 assert 導入 | 二重バリア管理の根絶 |
| **E: 全面 Graph 化** | §4.3 の Manager 分解（ASBuild / AtmosphereLUT / FFTOcean×3 / RTShadow×4 / WaterRT）、RTShadowMask View 特例削除 | 「一部 Graph・一部 Graph 外」の解消。Graph 外バリア約 70 箇所を撤去 |
| **F: ハイブリッド境界整理** | GeometryPass 分割、投入時キュー振り分け、skip フラグ / 優先度ソート削除、RenderManager 縮小 | Deferred/Forward 境界が Graph 上で可視になる |
| **G: 任意最適化** | ハンドル完全移行、パスカリング本格化、async compute、トランジェントエイリアシング、Graph 可視化パネル、CaptureView、マルチ View 単一 Graph | 性能・開発体験の向上 |

- A → B → C で「安全なパス追加」が完成し、D → E で「全パス Graph 管理」が完成する
- E は A の UAV バリアと D の状態一元化に依存するため、この順序を守ること

## 6. 変更対象ファイル一覧

| ファイル | フェーズ | 変更内容 |
|---|---|---|
| Graphics/Render/RenderGraph.h/.cpp | A,D,G | バージョニング・WAR・バッチ/UAV バリア・検証・カリング・トランジェント。RTShadowMask 特例削除（E） |
| Graphics/Render/Pass/RenderPass.h | B,C | DeclareResources / IsEnabledForView、RenderContext(ViewSettings) へカメラ参照 |
| Graphics/Render/Pass/*.cpp（全パス） | B | 各自の Read/Write 宣言を実装（AtmosphereLUT / FFTOcean の空宣言も実宣言化） |
| Graphics/Render/Pass/RenderPipeline.h/.cpp | B,C | 汎用ループ化、phase 順序管理、具象 include 削除、Remove API |
| Graphics/Render/FrameBlackboard.h/.cpp | D | RGResourceRegistry へ発展（Import/Transient、状態唯一所有、実行中書換禁止） |
| Graphics/Render/RenderTarget/* | D | プール化、Begin/End のバインド専用化 |
| Graphics/Common/ResourceBarrierHelper.* | D | Graph 外呼び出し検出 assert（禁止則） |
| Graphics/RayTracing/RayTracingShadowManager.* | E | 実行部を RTShadow 系 4 パスへ分解、履歴バッファの Registry 正式登録 |
| Graphics/Water/FFTOceanManager.* / FFTOceanDispatchHelper.* | E | 実行部を FFTOcean 系 3 パスへ分解 |
| Graphics/Atmosphere/AtmosphereManager.* | E | LUT 生成を AtmosphereLUTPass の宣言実行へ移管 |
| EngineSystem/Subsystem/RayTracingSubsystem.* | E | AS 構築を ASBuildPass へ移管 |
| Graphics/Render/RenderManager.h/.cpp | C,F | sticky setter 廃止（C）、キュー振り分け・優先度削除・環境設定分離（F） |
| Graphics/Render/Model/Model.*（static スロット） | C | SetCurrentRenderSlot 廃止、引数渡しへ |
| Graphics/Render/Pass/GeometryPass.* | F | Sky/Transparent/Debug/Sprite/UI へ分割 |
| EngineSystem/EngineSystem.cpp | C,E | BuildDefaultRenderPipeline を phase 指定へ、シーンフック、AS 構築呼び出し削除 |
| Scene/IScene.h, SceneManager | C | RegisterRenderPasses フック追加 |

## 7. 完了条件
- フレーム中の GPU コマンド記録・バリア発行が RenderGraph 経由に限定される
  （例外は §0 ホワイトリストのみ。Debug ビルドの禁止則 assert で機械的に検証可能）
- ユーザー定義パスが「RenderPass 派生 1 ファイル + AddPass 1 行」で動作し、
  追加・削除しても既存パスの描画結果が変わらない（§4.5 の分離契約が構造的に保証）
- SceneColor 多段書き込みの順序が AddPass 順ではなく version チェーンで決まる
- 循環依存・未解決リソース・Graph 外バリアが Debug ビルドで即座に検出される
- AtmosphereLUT / FFTOcean / RTShadow / ASBuild が Graph 上のノードとして可視化される
