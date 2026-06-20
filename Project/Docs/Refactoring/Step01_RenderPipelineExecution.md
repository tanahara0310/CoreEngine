# Step 1: RenderPipeline 実行責務の分離

## ステータス
- 状態: 完了
- 優先度: 最優先
- 依存ステップ: なし
- 完了後に着手しやすい次ステップ: Step 2

## 目的
`EngineSystem::ExecuteRenderPipeline()` に集中しているレンダーパイプライン実行責務を `RenderPipeline` 側へ移し、`EngineSystem` がパス順序を直接知らない構造へ寄せる。

## このステップの作業範囲
- `Engine/Src/EngineSystem/EngineSystem.cpp`
- `Engine/Src/Graphics/Render/Pass/RenderPipeline.h`
- `Engine/Src/Graphics/Render/Pass/RenderPipeline.cpp`
- 必要に応じて `RenderContext` / `RenderPass` 周辺の補助コード

## このステップで扱う責務
- RenderContext 構築とパス実行責務の分離
- `Setup / Execute / Cleanup` 呼び出し主体の移設
- パス順序制御の集約
- 実行前準備 / 実行本体 / 実行後処理の整理

## 作業項目
- [x] `EngineSystem::ExecuteRenderPipeline()` の責務を棚卸しする
- [x] 前準備・パス実行・後処理の境界を整理する
- [x] `executePass` 相当の責務を `RenderPipeline` へ移す
- [x] `RenderPipeline` が複数 Pass を一貫実行できる形へ整える
- [x] `EngineSystem` からパス順序の知識を減らす
- [x] 現行描画結果を崩していないか確認する

## 実施結果
- `RenderPipeline` に `PrepareFrame`、`ExecutePass`、`ResetExecutionState`、`GetPreviousOutput`、`SetPreviousOutput` を追加した
- `GeometryPass` のフレーム準備処理を `EngineSystem` から `RenderPipeline` 側へ移した
- `EngineSystem` の共通パス実行責務を `RenderPipeline` 経由へ移した
- 当時残っていた旧デバッグビュー描画も `RenderPipeline::ExecutePass()` を使う形へ揃えた
- 旧ビュー経路で古い SSAO 入力が残留しないよう、前段出力が無効でも各 Pass に空入力を渡すよう修正した
- ビルド成功を確認した

## 実装時の観点
- この段階では RenderGraph を導入しない
- 既存パスの実装責務は大きく変えず、まず実行器の責務だけを整理する
- 旧ビュー互換や RT などの特例処理は、無理にこの段階で完全解消しなくてよい
- ただし `EngineSystem` 側に新しい特例分岐を増やさない

## 期待する到達状態
- `EngineSystem` は RenderContext 構築と実行依頼に寄る
- `RenderPipeline` が実行順制御の中心になる
- 後続ステップで RT / Composite / Blackboard を追加しやすい土台になる

## 完了条件
- `EngineSystem` が各 Pass の呼び出し順を直接列挙しない、または列挙量が大幅に縮小している
- `RenderPipeline` が実質的なパイプライン実行器として成立している
- 既存フレーム描画が維持されている

## 補足
- 実行順の完全移譲は未完で、RT Shadow 特例や旧ビュー互換は Step 2 以降で継続整理する
- ただし Step 1 の目的だった「共通実行責務の移設」と「実行器としての RenderPipeline 整備」は達成済み

## 引き継ぎメモ
- Step 2 では RTShadow の特例処理を Pass 化するため、この時点で `RenderPipeline` へ Pass を追加しやすい構造になっていることが重要
