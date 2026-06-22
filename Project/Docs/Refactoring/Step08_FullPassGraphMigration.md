# Step 8: 全パス RenderGraph 化と Graph 外 GPU 経路整理

## ステータス
- 状態: 完了
- 優先度: 中〜高
- 依存ステップ: Step 7
- 完了後に着手しやすい次ステップ: Step 9 相当の最適化・可視化フェーズ

## 目的
Step 7 までで主要描画パスの RenderGraph 化と旧実行経路削除は概ね完了した。
Step 8 では、まだ Graph 外に残っている GPU 作業・一時リソース管理・命令型 ping-pong 実行を整理し、**主要描画だけでなく周辺 GPU 処理も含めて『全パスが RenderGraph 的に説明できる状態』**へ寄せる。

最終的には、
- どの処理がどのリソースを Read / Write するか
- どの View で動くか
- どこで状態遷移するか
- どこが compute / copy / present 境界なのか
をコードとドキュメントの両方で明示できる状態を目指す。

## このステップの作業範囲
- PostEffect ping-pong 経路の Graph 化と旧実行経路の削除
- Graph 外に残る compute / copy / present 境界の明文化
- 中間リソースの論理名整理と Blackboard / Graph 収容
- View ごとの最終表示入力と ReflectionView 結果共有の整理
- 旧 offscreen 命名・旧 offscreen 配列基盤の撤去

## このステップで扱う責務
- RenderGraph 外周に残る GPU 作業の洗い出し
- RenderTarget ベース命令列から Graph ノード列への移行方針定義
- compute / graphics / copy 混在経路の分割方針整理
- `ResourceBarrierHelper` の最終責務境界整理
- 将来の全 GPU パス可視化に向けた論理リソース命名整理

## Step 7 完了時点で RenderGraph 化済みの領域
- `ShadowMapPass`
- `GBufferPass`
- `SSAOPass`
- `RTShadowPass`（Graph 上の正式パスとして登録済み）
- `DeferredLightingPass`
- `GeometryPass`
- `PostEffectPass`
- `BackBufferPass`
- `GameView` / `ReflectionView` の主要描画実行

つまり、**フレーム主要描画の実行順自体は RenderGraph へ統一済み**である。
Step 8 の主対象は、その周囲に残っている「Graph ノードとしてはまだ分解されていない GPU 作業」である。

## 現在 Graph 外に残っている主な経路

### 1. PostEffect の ping-pong 実行
対象:
- `Engine/Src/Graphics/PostEffect/Effect/PostEffectManager.cpp`
- `Engine/Src/Graphics/PostEffect/Effect/PostEffectBase.h` 周辺
- `Engine/Src/Graphics/Render/Pass/RenderPipeline.cpp`

現状:
- `PostEffectManager` から有効エフェクト列を取得し、`RenderPipeline` が effect ごとの Graph ノードを動的追加する構成へ移行済み
- `PostEffectPass` は effect 単位実行に拡張済みで、graphics / compute の両方を Blackboard 論理名経由で扱える
- `BackBufferPass` は固定 `SceneColor` 読み込みではなく、`RenderPipeline` が決めた最終論理入力を読む
- 旧 `PingPongBuffer` と `PostEffectManager::ExecuteEffectChain()` は削除済み
- `PostEffectPing` / `PostEffectPong` は `PostEffectIntermediateN` と `PostEffectFinal` へ置き換え済み
- `Offscreen0/1` 依存も撤去され、`SceneColor` / `PostEffectIntermediateN` / `PostEffectFinal` の論理名ベースへ統一済み

RenderGraph 化の到達点:
- 各ポストエフェクトは 1 ノード単位で Graph へ登録される
- BackBuffer は最終論理入力を Read する形で接続される
- 命令型 ping-pong 実行そのものは撤去済み

優先度:
- 完了

---

### 2. RT Shadow 内部の Dispatch / Temporal / Denoise / Copy
対象:
- `Engine/Src/Graphics/RayTracing/RayTracingShadowManager.h`
- `Engine/Src/Graphics/RayTracing/RayTracingShadowManager.cpp`
- `Engine/Src/EngineSystem/Subsystem/RayTracingSubsystem.cpp`

現状:
- `RTShadowPass` 自体は Graph に乗っている
- ただし pass の中では `RayTracingShadowManager` が
  - `Dispatch`
  - `ApplyTemporal`
  - `Denoise`
  - 履歴コピー
  を内部命令列として処理している
- ここでは `ResourceBarrierBatch` / `ResourceBarrierHelper::UAV()` / copy 遷移が残っている

未 Graph 化の理由:
- 1 パスの内部に複数の compute / UAV / copy ステップが閉じ込められている
- 履歴テクスチャやデノイズ中間バッファが FrameBlackboard の正式論理名になっていない
- ビュー別・ライト別の配列構造を持つため、Graph から直接扱いにくい

RenderGraph 化する対象:
- RayGen 出力
- Temporal 入出力
- Denoise ping-pong
- History Copy
- ライト別 / View 別の論理リソース命名規則

Step 8 完了時点の整理結果:
- `RTShadowPass` は RenderGraph 上の正式パスとして統合済み
- その内部の dispatch / temporal / denoise / copy は「RenderGraph 外に残す manager 内部 GPU サブステップ」として責務を明文化した
- 将来さらに細分化する場合は Step 9 以降の改善対象とする

優先度:
- 完了（内部サブステップ分解は将来改善候補）

---

### 3. Present 境界と BackBuffer 最終処理
対象:
- `Engine/Src/Graphics/Render/Render.cpp`
- `Engine/Src/Graphics/Render/RenderTarget/BackBufferRenderTarget.cpp`

現状:
- `BackBufferPass` は Graph 化済み
- ただし `Present` 直前の `BackBufferRenderTarget::End()` と `Render::FinalizeFrame()` は Graph 外
- これは swapchain / command queue / allocator reset を含むため、完全な Graph 内包は困難

未 Graph 化の理由:
- D3D12 フレーム境界と Present は RenderGraph の責務というよりフレーム実行器の責務
- queue submit / present / allocator reset はレンダーパスではない

RenderGraph 化ではなく整理すべき点:
- Graph の責務は `BackBuffer = RenderTarget` まで
- `Present` 境界は executor 層の責務と明文化する
- `ResourceBarrierHelper` がここで必要なことを明示する

優先度:
- 完了
- 理由: Present / queue submit / allocator reset は RenderGraph 外 executor 責務として位置付けを確定した

---

### 4. OffscreenRenderTarget の graphics / compute ヘルパー
対象:
- `Engine/Src/Graphics/Render/RenderTarget/OffscreenRenderTarget.cpp`
- `Engine/Src/Graphics/Render/RenderTarget/OffscreenRenderTarget.h`

現状:
- `Begin()` / `End()` / `BeginCS()` / `EndCS()` は残っている
- ただし実体は旧 `DirectXCommon` の offscreen 配列 API ではなく、`OffscreenRenderTarget` 自身がリソース・RTV/SRV/UAV・状態を保持する構造へ移行済み
- 旧 `OffScreenRenderTargetManager` と `DirectXCommon::GetOffScreen*` 群は撤去済み

未 Graph 化の理由:
- RenderTarget 自体が「実行ヘルパー」として使われており、Graph ノードの実体ではない
- ただし一時リソース利用は `SceneColor` / `PostEffectIntermediateN` / `PostEffectFinal` の論理名へ整理済みで、固定ターゲット依存は解消済み

RenderGraph 化する対象:
- graphics 用一時レンダーターゲット
- compute 用 UAV 一時ターゲット
- Begin/End API を使う場所を段階的に削減
- 実リソースクラスは保持しつつ、実行の主導権は Graph 側へ移す

優先度:
- 完了
- 理由: 旧 offscreen 配列依存を撤去し、RenderTarget 基盤を現行 Graph 構成に合う形へ整理できたため

---

### 5. IBL 生成や特殊 GPU ユーティリティ
対象候補:
- `Engine/Src/Graphics/IBL/IBLGenerator.cpp`
- `Engine/Src/Graphics/Texture/Gpu/TextureGpuUploader.cpp`

現状:
- `ResourceBarrierHelper::Transition()` を直接使う GPU 補助処理が残っている
- ただしこれらは「毎フレームの主要描画パス」ではなく、生成・アップロード・前処理寄り

未 Graph 化の理由:
- フレーム RenderGraph と、アセット生成 / 初期化 / 非同期アップロードの責務が異なる
- 毎フレーム実行でない処理まで同一 Graph に入れるべきかは設計判断が必要

整理方針:
- フレーム内主要描画 Graph とは分離する
- 必要なら将来 `BuildGraph` / `ComputeGraph` / `UploadGraph` のような別概念を検討する
- Step 8 の段階では「RenderGraph に入れない理由」を明文化する方が重要

優先度:
- 低〜中
- 理由: 毎フレーム主要描画の Graph 化を阻害する本命ではないため

---

## `ResourceBarrierHelper` の今後の位置付け
Step 7 時点で、`ResourceBarrierHelper` は削除対象ではない。
残す理由は以下。

### RenderGraph 内で必要な理由
- `RenderGraph::ApplyTransitionsForPass()` の実装本体として使っている
- Graph が要求状態へ遷移する処理の基盤である

### Graph 外でまだ必要な理由
- `OffscreenRenderTarget::BeginCS()/EndCS()` の compute 境界
- `BackBufferRenderTarget` の Present 境界
- `RayTracingShadowManager` の UAV / copy / temporal / denoise 系遷移
- GPU アップロードや IBL 生成など、フレーム Graph 外の GPU ユーティリティ

### Step 8 でやるべきこと
- `ResourceBarrierHelper` を消すことではなく、
  **「どこまでが Graph 管理、どこからが Graph 外 executor / utility 管理か」を固定すること**
- その上で、Graph 外にあるべきでない用途だけを減らしていく

## 作業項目
- [x] PostEffect ping-pong 経路を Graph ノード列へ分解し、旧実行経路を削除する
- [x] View 別 / 中間リソース命名を `SceneColor` / `ReflectionColor` / `PostEffectIntermediateN` / `PostEffectFinal` ベースへ整理する
- [x] Graph 外に残す GPU 境界（Present / Upload / 生成系）を明文化する
- [x] `ResourceBarrierHelper` の最終責務をドキュメント化する
- [x] 旧 offscreen 配列基盤と固定 `Offscreen0/1` 命名依存を撤去する
- [ ] RT Shadow の内部サブステップを Graph から直接見える単位へ分解する（将来改善）
- [ ] 将来の `CaptureView` を現行 View モデルへ乗せる（将来改善）

## Step 8 完了時点の最終整理
- `RenderPipeline` がフレーム開始時に `FrameBlackboard` へ `SceneDepth` / `SceneColor` / `ShadowMap` / `GBuffer*` / `SSAO` / `PostEffectFinal` / `BackBuffer` などの実体を登録する
- `RenderGraph` は各パスの Read / Write 宣言から依存と実行順を決定し、`ResourceBarrierHelper` を通じて自動状態遷移を適用する
- `FrameBlackboard` は「論理名 → 実リソース / SRV / 現在状態参照」の正本として振る舞い、RenderGraph の実体解決元になる
- `GameView` / `ReflectionView` の主要描画は同一の RenderGraph 実行モデルへ統一済み
- `SceneColor` / `PostEffectIntermediateN` / `PostEffectFinal` の論理名で最終表示経路を追跡できる
- Graph 外に残る責務は、Present 境界、アップロード/生成系 GPU 補助処理、RT Shadow manager 内部サブステップのような executor / utility 領域へ限定された

## 実装時の観点
- いきなりすべてを 1 ノード 1 エフェクトへ分解しない
- まずは「Graph 外に残っているまとまり」を見える化する
- compute / graphics / copy が混在する領域ほど、段階的に切り分ける
- 一時ターゲットの固定名依存を減らし、論理リソース名ベースへ寄せる
- 毎フレーム実行される処理と、生成・アップロード系を同じ責務で混ぜない

## 期待する到達状態
- 主要描画だけでなく周辺 GPU 作業も「どこで何をしているか」を Graph 観点で説明できる
- 新規エフェクトや RT 機能追加時に、Graph 外の命令型処理を増やしにくくなる
- `ResourceBarrierHelper` の存在理由が限定され、設計上の誤解を生みにくくなる

## 完了条件
- Graph 外に残る GPU 作業の一覧と分類ができている
- RenderGraph 化すべき経路と、Graph 外責務として残す経路が分離されている
- 主要描画パス、PostEffect 経路、View 切り替え、BackBuffer 入力が RenderGraph / FrameBlackboard を軸に説明できる
- 旧 offscreen 命名・旧 offscreen 配列基盤・命令型 ping-pong 実行が撤去されている

## 引き継ぎメモ
- Step 8 は完了。次に着手するなら Step 9 相当の最適化・可視化フェーズとする
- `RayTracingShadowManager` の内部サブステップ分解は将来の改善候補
- `Present`、アップロード、アセット生成は RenderGraph 外 executor / utility 責務として扱う
- 将来 `CaptureView` を追加する際は、このステップで整理した View / 中間リソースモデルを前提に設計する
