# Step 6: Shadow / RT / 複数 View 統合

## ステータス
- 状態: 実装完了（検証継続）
- 優先度: 中
- 依存ステップ: Step 2, Step 3, Step 4, Step 5
- 完了後に着手しやすい次ステップ: 継続改善フェーズ

## 目的
Shadow / RT / 複数 View を同じ基盤へ段階的に統合し、GameView / SceneView / ReflectionView / CaptureView を再利用可能なレンダリング構造へ寄せる。あわせて Step 5 で作った自動遷移基盤を特殊経路と全 View へ広げる。

## このステップの作業範囲
- ShadowMap 系の Graph / Blackboard 統合
- RT 系パスの正式統合
- GameView / SceneView / ReflectionView / CaptureView の抽象化
- View ごとの描画リスト構築と実行フロー整備
- Shadow / RT / 複数 View ごとのリソース状態追跡統合

## このステップで扱う責務
- 複数 View の統一的な実行モデル
- Shadow / RT の特例削減
- Reflection 用補助 View の正式化
- 将来機能向けの拡張基盤整備
- 全パス自動遷移を特殊経路へ広げる

## 作業項目
- [x] ShadowMap 系を RenderGraph / Blackboard に統合する
- [x] RT 系パスを Graph 上の正式パスへ寄せる
- [x] GameView / SceneView の扱いを共通化する
- [x] `ReflectionView` / `CaptureView` を想定した View 抽象化を行う
- [x] 複数 View で同じ流れを再利用できるか確認する
- [x] Water / Reflection / 将来の Capture 系が Scene 特例なしで成立するか確認する
- [ ] Shadow / RT / 複数 View でも自動バリアと状態追跡が破綻しないか確認する

## 残タスク整理
- `CaptureView` を追加する場合に必要な出力先指定、カメラ切り替え、リソース命名規則をここで共通化する
- `SceneView` で現在無効化している `SSAO` / `RTShadow` / `PostEffect` を、負荷と品質を見ながらどこまで共通構成へ戻すか評価する
- `ShadowMap` / `RTShadowMask` / `SceneColor` / `ReflectionColor` の View ごとの状態正本が混線しないか、Debug Layer とバリアログで継続確認する

## 実施結果
- `RenderContext` に `RenderViewType` / `RenderViewSettings` を追加し、View ごとに有効パスと出力先ターゲットを切り替えられるようにした
- `RenderPipeline` に View 設定ベースのパス構成切り替えを追加し、`SceneView` では `SSAO` / `RTShadow` / `PostEffect` / `BackBuffer` を無効化した軽量 Graph を組めるようにした
- `DeferredLightingPass` / `GeometryPass` の出力先を `viewSettings.sceneColorTargetName` で切り替えられるようにし、`SceneView` ターゲットへ同じパス群を流せるようにした
- `DebugSubsystem::RenderSceneView()` を旧 `ExecutePass()` 直列実行から外し、SceneView 用 RenderGraph を再構築して実行する経路へ切り替えた
- `ReflectionView` を Engine 側の `PrepareFrame() -> BuildRenderGraph() -> ExecuteRenderGraph()` へ統合し、`WaterTestScene` が反射 RTT を直接実行しない構成へ移行した
- `ReflectionColor` を Blackboard の正式論理リソースとして登録し、Water は Scene 注入ではなく ReflectionView 実行結果として参照するようにした
- `WaterReflectionPass` は RenderTarget 制御を持つ Scene 特例から、反射カメラ切り替えと clip plane 組み立てを担う helper へ責務を縮小した
- `OffscreenRenderTarget` / `OffScreenRenderTargetManager` の `currentState` 正本を共有化し、RenderGraph の自動遷移と `Begin()` / `End()` / `BeginCS()` / `EndCS()` が同じ状態参照を更新するようにした
- `RenderGraph::ResolveResources()` は `SceneColor` を Blackboard 登録値より `viewSettings.sceneColorTargetName` から優先解決するようにし、SceneView 実行後の Blackboard が GameView の遷移先を汚染しないよう修正した
- ワークスペース全体のビルド成功を確認した

## 実装時の観点
- まずは GameView を基準に整える
- SceneView は特例処理を後から寄せる
- Reflection は Scene が直接反射パスを回すのではなく、View 実行結果として扱う
- Capture 系や将来の RT Reflection を見据えて、View 単位でリソース集合を扱えるようにする
- Step 5 の主要リソース自動遷移を、ここで Shadow / RT / SceneView 系へ拡張する

## 期待する到達状態
- 複数 View を同じレンダリング基盤で扱える
- Water Reflection のような補助ビューが Scene 側特例なしで実装できる
- Shadow / RT / Composite / PostProcess の統合拡張がしやすくなる

## この段階での到達点
- GameView と SceneView は同じ `RenderPipeline::PrepareFrame()` / `BuildRenderGraph()` / `ExecuteRenderGraph()` の流れで説明できるようになった
- View ごとの差分は `RenderContext::viewSettings` に寄せ、SceneView 特例を Graph 外の直列実行から Graph 構成差分へ移し始めた
- SceneView 導入時に発生した `SceneColor` の遷移衝突は、View 別ターゲット解決と Offscreen 状態正本の共有化で吸収する方向へ整理した
- ReflectionView も同じ Graph 実行モデルへ入り、Water Reflection の Scene 特例実行は除去できた
- Step07 へ進む前提は揃ったため、残る主目的は複数 View 実行時の実行時検証と、CaptureView など将来 View の拡張余地確認に絞られた

## 完了条件
- GameView / SceneView / ReflectionView などを同じ枠組みで説明できる
- Shadow / RT 系が特例ではなく通常パス群として扱える
- 新しい View 追加時に Scene 側の暫定処理へ戻らない構造になっている

## Step07 へ進んでよい条件
- `WaterTestScene` が Reflection RTT を直接実行せず、ReflectionView を engine 側へ要求するだけになっている
- `ReflectionColor` が Blackboard 経由で扱われ、Water がその結果を受け取る構造になっている
- ReflectionView 実行後に GameView / SceneView の RenderGraph 構成へ戻してもビルド上破綻しない
- 残る論点が『旧経路削除』と『手動バリア除去』中心になっている

## 引き継ぎメモ
- 次段では ReflectionView 統合済みの前提で、`ExecutePass()` 依存・旧手動バリア・互換配線を削除していく
- SceneView で省略している `SSAO` / `RTShadow` / `PostEffect` をどこまで共通化するか、品質と負荷のバランスを見ながら再評価する
- ここまで完了すれば、RenderGraph 基盤上で Water / SSR / RT Reflection / Volumetric などを追加しやすい継続改善フェーズへ入れる
- Step 7 では、この段階で統合済みの全パスを前提に、残っている手動バリアと旧状態管理コードを削除する
