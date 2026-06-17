# Step 1: RenderPipeline 実行責務の分離

## ステータス
- 状態: 未着手
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
- [ ] `EngineSystem::ExecuteRenderPipeline()` の責務を棚卸しする
- [ ] 前準備・パス実行・後処理の境界を整理する
- [ ] `executePass` 相当の責務を `RenderPipeline` へ移す
- [ ] `RenderPipeline` が複数 Pass を一貫実行できる形へ整える
- [ ] `EngineSystem` からパス順序の知識を減らす
- [ ] 現行描画結果を崩していないか確認する

## 実装時の観点
- この段階では RenderGraph を導入しない
- 既存パスの実装責務は大きく変えず、まず実行器の責務だけを整理する
- SceneView や RT などの特例処理は、無理にこの段階で完全解消しなくてよい
- ただし `EngineSystem` 側に新しい特例分岐を増やさない

## 期待する到達状態
- `EngineSystem` は RenderContext 構築と実行依頼に寄る
- `RenderPipeline` が実行順制御の中心になる
- 後続ステップで RT / Composite / Blackboard を追加しやすい土台になる

## 完了条件
- `EngineSystem` が各 Pass の呼び出し順を直接列挙しない、または列挙量が大幅に縮小している
- `RenderPipeline` が実質的なパイプライン実行器として成立している
- 既存フレーム描画が維持されている

## 引き継ぎメモ
- Step 2 では RTShadow の特例処理を Pass 化するため、この時点で `RenderPipeline` へ Pass を追加しやすい構造になっていることが重要
