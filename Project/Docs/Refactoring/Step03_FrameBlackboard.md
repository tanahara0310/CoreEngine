# Step 3: FrameBlackboard 導入

## ステータス
- 状態: 完了
- 優先度: 高
- 依存ステップ: Step 1, Step 2
- 完了後に着手しやすい次ステップ: Step 4

## 目的
パス間のデータ受け渡しを Manager 直参照から論理リソース名ベースへ寄せ、`SceneColor` や `SceneDepth` を Scene 側から直接注入しなくても機能追加できる土台を作る。

## このステップの作業範囲
- 新規 `FrameBlackboard` 関連クラス
- `RenderContext` / `RenderPipeline` 周辺の共有リソース参照経路
- `GBufferPass` / `DeferredLightingPass` / `PostEffectPass` などの入出力整理
- 必要に応じて `RenderTargetManager` / `GBufferManager` との橋渡し層

## このステップで扱う責務
- 論理リソース名の定義
- Pass ごとの Read / Write 対応整理
- フレーム内共有リソースの所有権明確化
- Scene 側の一時的な SRV 注入経路の縮小

## 代表的な論理リソース候補
- `SceneColor`
- `SceneDepth`
- `ReflectionColor`
- `ReflectionDepth`
- `GBufferA`
- `GBufferB`
- `GBufferC`
- `Velocity`
- `SSAO`
- `ShadowMask`
- `RTShadowMask`

## 作業項目
- [x] フレーム内で共有したい論理リソース名を一覧化する
- [x] `SceneColor` / `SceneDepth` / `GBuffer*` / `SSAO` / `RTShadowMask` などを定義する
- [x] 各 Pass がどのリソースを読むか書くか整理する
- [x] Manager 直参照を Blackboard 経由参照へ段階的に置き換える
- [x] Water / Reflection / Screen Space 系が Scene 直配線なしで利用できる方向へ整理する
- [x] 既存描画結果が維持されることを確認する

## 実施結果
- `FrameBlackboard` を新規追加し、`SceneColor` / `SceneDepth` / `SSAO` / `RTShadowMask` / `GBuffer*` の論理リソース名を定義した
- `RenderContext` に `frameBlackboard` を追加し、`EngineSystem::ExecuteRenderPipeline()` でフレームごとに Blackboard を構築するようにした
- `SceneDepth` は `DirectXCommon` から初期登録し、深度参照系の正式な入口を用意した
- `GBufferPass` が `GBufferAlbedoAO` / `GBufferNormalRoughness` / `GBufferEmissiveMetallic` / `GBufferWorldPosition` / `GBufferMotionVector` を Blackboard に登録するようにした
- `SSAOPass` が `SSAO` を Blackboard に登録するようにし、`DeferredLightingPass` は Blackboard 優先で AO 入力を解決するようにした
- `RTShadowPass` が `RTShadowMask` を Blackboard に登録するようにし、RT 系結果の論理名経由参照の足場を作った
- `DeferredLightingPass` / `GeometryPass` / `PostEffectPass` が `SceneColor` を更新し、`PostEffectPass` / `BackBufferPass` は Blackboard 優先で最終入力を解決するようにした
- ワークスペース全体のビルド成功を確認した

## この段階での Read / Write 整理

| Pass | Read | Write |
|---|---|---|
| `GBufferPass` | `SceneDepth` | `GBufferAlbedoAO`, `GBufferNormalRoughness`, `GBufferEmissiveMetallic`, `GBufferWorldPosition`, `GBufferMotionVector` |
| `SSAOPass` | `GBuffer*`, `SceneDepth` | `SSAO` |
| `RTShadowPass` | `GBufferWorldPosition`, `GBufferNormalRoughness`, `GBufferMotionVector` | `RTShadowMask` |
| `DeferredLightingPass` | `GBuffer*`, `SSAO`, `RTShadowMask`, `SceneDepth` | `SceneColor` |
| `GeometryPass` | `SceneColor`, `SceneDepth` | `SceneColor` |
| `PostEffectPass` | `SceneColor` | `SceneColor` |
| `BackBufferPass` | `SceneColor` | `BackBuffer` |

## 実装時の観点
- 最初から完全抽象化を目指さず、既存 Manager を内部実装として利用してよい
- 重要なのは『呼び出し側が論理名で考えられること』
- PassOutput の単一出力制約を無理にこの段階で消し切らなくてもよいが、将来の RenderGraph に繋がる設計に寄せる

## 期待する到達状態
- 各 Pass の入出力が論理リソース名で説明できる
- Scene 側が `SceneColor` / `SceneDepth` を直接拾って GameObject に注入する必要が減る
- RenderGraph 導入時の `Read / Write` 宣言へ自然に移行できる

## 完了条件
- `FrameBlackboard` もしくは同等のフレーム共有リソース層が存在する
- 複数の主要 Pass が論理リソース名ベースで入出力を扱える
- Water / Reflection 追加時に Scene 側暫定配線へ戻らない構造の見通しが立っている

## 引き継ぎメモ
- Step 4 ではこの Blackboard を前提に RenderGraph 最小構成へ繋げるため、リソース名と所有権はこの段階で安定させる
- この段階では既存 Manager 直参照を完全には除去しておらず、Blackboard は橋渡し層として導入している。Step 4 では `Read / Write` 宣言と実行順解決を RenderGraph 側へ寄せる
