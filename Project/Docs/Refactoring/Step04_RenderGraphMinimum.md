# Step 4: 最小 RenderGraph 導入

## ステータス
- 状態: 完了
- 優先度: 中〜高
- 依存ステップ: Step 1, Step 2, Step 3
- 完了後に着手しやすい次ステップ: Step 5

## 目的
`FrameBlackboard` 上で整理された論理リソースを前提に、最小限の RenderGraph を導入し、主要 5 パスを Graph 上で順序解決できる構造を作る。

## このステップの作業範囲
- 新規 `RenderGraph` 関連クラス
- `RenderGraphPass`
- `RenderGraphResource`
- `RenderGraphBuilder`
- `RenderGraphContext`
- `GBufferPass` / `SSAOPass` / `DeferredLightingPass` / `PostEffectPass` / `BackBufferPass` の Graph 登録

## このステップで扱う責務
- Graph への Pass 登録
- Read / Write 宣言
- 依存関係に基づく実行順決定
- Blackboard と Graph の橋渡し

## 作業項目
- [x] `RenderGraph` の最小責務を定義する
- [x] `RenderGraphPass` / `RenderGraphResource` / `RenderGraphBuilder` を設計する
- [x] `GBufferPass` を Graph 登録対象にする
- [x] `SSAOPass` を Graph 登録対象にする
- [x] `DeferredLightingPass` を Graph 登録対象にする
- [x] `PostEffectPass` を Graph 登録対象にする
- [x] `BackBufferPass` を Graph 登録対象にする
- [x] `GBuffer → SSAO → DeferredLighting → PostEffect → Present` が Graph 上で通ることを確認する

## 実施結果
- `RenderGraph` / `RenderGraphPass` / `RenderGraphResource` / `RenderGraphBuilder` / `RenderGraphContext` を新規追加した
- `Read / Write` 宣言から依存関係を記録し、最小のトポロジカルソートで実行順を決定する初版を実装した
- `RenderPipeline` に `BuildRenderGraph()` と `ExecuteRenderGraph()` を追加し、フレーム準備時に Graph を構築するようにした
- `GBufferPass` / `SSAOPass` / `RTShadowPass` / `DeferredLightingPass` / `GeometryPass` / `PostEffectPass` / `BackBufferPass` を Graph ノードとして登録するようにした
- GameView のメイン描画経路は `ShadowMapPass` 実行後、`RenderGraph` 経由で主要パス群を実行する形へ切り替えた
- `SceneView` のデバッグ描画経路はこの段階では既存 `ExecutePass()` ベースを維持し、完全移行は後続ステップへ送った
- ワークスペース全体のビルド成功を確認した

## この段階の Graph ノード構成

| Pass | Read | Write |
|---|---|---|
| `GBufferPass` | `SceneDepth` | `GBufferAlbedoAO`, `GBufferNormalRoughness`, `GBufferEmissiveMetallic`, `GBufferWorldPosition`, `GBufferMotionVector` |
| `SSAOPass` | `SceneDepth`, `GBufferAlbedoAO`, `GBufferNormalRoughness`, `GBufferWorldPosition` | `SSAO` |
| `RTShadowPass` | `GBufferWorldPosition`, `GBufferNormalRoughness`, `GBufferMotionVector` | `RTShadowMask` |
| `DeferredLightingPass` | `SceneDepth`, `GBufferAlbedoAO`, `GBufferNormalRoughness`, `GBufferEmissiveMetallic`, `GBufferWorldPosition`, `SSAO`, `RTShadowMask` | `SceneColor` |
| `GeometryPass` | `SceneColor`, `SceneDepth` | `SceneColor` |
| `PostEffectPass` | `SceneColor` | `SceneColor` |
| `BackBufferPass` | `SceneColor` | `BackBuffer` |

## この段階で残すもの
- `ShadowMapPass` はまだ Graph 外の前段処理として残している
- `SceneView` は既存パス直実行経路を使っている
- `PassOutput` と Graph 実行経路は一時的に併存している
- 旧手動実行経路の完全削除は次の専用ステップで扱う

## 実装時の観点
- 最初から全パスを Graph 化しない
- Forward Composite や複数 View は後続ステップへ回してよい
- まずは依存関係が比較的素直な 5 パスで成立させる
- Graph の初版では手動順序に近い実装でもよいが、Read / Write の宣言面は先に作る

## 期待する到達状態
- RenderGraph が最小限の実行単位として存在する
- 主要パスが Graph へ登録される
- 後続の自動バリア導入へ繋がる入出力宣言が整う

## 完了条件
- 5 パスが Graph 経由で実行できる
- Graph 上で論理リソース依存が表現されている
- 既存描画結果を大きく崩していない

## 引き継ぎメモ
- Step 5 では Graph の Read / Write 宣言を使って自動バリアを導入するため、ここで状態追跡に必要な情報を保持できる形にしておく
- 旧 `ExecutePass()` ベースの個別実行ロジックはまだ補助経路として残っているため、RenderGraph 完全移行と旧処理削除は別ステップで明示的に進める
