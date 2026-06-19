# Step 5: 自動バリア導入

## ステータス
- 状態: 完了
- 優先度: 中
- 依存ステップ: Step 3, Step 4
- 完了後に着手しやすい次ステップ: Step 6

## 目的
RenderGraph と論理リソース管理を前提に、最終的には全パスの状態遷移を自動化し、手動バリア依存を段階的に置き換えるための中核基盤を作る。

## このステップの作業範囲
- `RenderGraph` の実行時状態追跡
- `SceneColor` / `SceneDepth` / `GBuffer*` / `SSAO` / `ShadowMap` / `RTShadowMask` / `BackBuffer` の状態管理
- 既存 `ResourceBarrierHelper` / `DepthStencilManager` / RenderTarget 系との接続
- read-only DSV を含む深度参照経路
- 将来の全パス自動遷移に向けた状態追跡の正本整理

## このステップで扱う責務
- `RTV / DSV / SRV / UAV / Present` の主要状態自動遷移
- 冗長バリアの削減
- 手動バリアから自動遷移への段階的移行方針の確立
- 深度参照パスの標準化

## 作業項目
- [x] 自動化対象のリソース状態を `RTV / DSV / SRV / UAV / Present` に絞る
- [x] `SceneColor` の状態遷移を自動化する
- [x] `GBuffer*` の状態遷移を自動化する
- [x] `SceneDepth` の状態遷移を自動化する
- [x] `BackBuffer` の状態遷移を自動化する
- [x] read-only DSV と SRV 併用の経路を標準化する
- [x] 冗長バリアが減っているか確認する
- [x] 既存手動バリアとの二重管理がないか確認する

## 実施結果
- `FrameBlackboard` に実リソースの `currentState` 参照を保持できるよう拡張した
- `RenderGraphBuilder::Read / Write` が要求リソース状態を宣言できるようになり、`RTV / DSV / SRV / UAV / Present` を主要対象として扱えるようにした
- `RenderGraph` が Blackboard と既存 Manager から `SceneColor` / `SceneDepth` / `GBuffer*` / `SSAO` / `BackBuffer` の実リソースと状態参照を解決するようにした
- `RenderGraph` 実行前に `ResourceBarrierHelper::Transition()` を使って、各パスの要求状態へ自動遷移する処理を追加した
- `SceneDepth` は `DEPTH_READ | PIXEL_SHADER_RESOURCE` を Graph の要求状態として宣言し、read-only DSV と SRV の併用経路を正式な扱いへ寄せた
- `BackBufferRenderTarget` に状態追跡変数を追加し、Present ↔ RenderTarget の遷移を追跡できるようにした
- `GBufferPass` / `SSAOPass` / `DeferredLightingPass` / `GeometryPass` / `PostEffectPass` が Blackboard 登録時に状態参照も渡すようにした
- `ShadowMapPass` を RenderGraph 登録対象へ追加し、`ShadowMapManager` の `currentState` を Blackboard / Graph から共有できるようにした
- `RTShadowPass` が `RayTracingShadowManager` の実リソースと `currentState` を Blackboard へ登録し、Graph から `NON_PIXEL_SHADER_RESOURCE` 状態として追跡できるようにした
- `RayTracingSubsystem` の GBuffer 前後手動バリアと `EngineSystem` の ShadowPass 手動実行を外し、GameView の主要パスを Graph 実行へ寄せた
- ワークスペース全体のビルド成功を確認した

## このステップの位置づけ
- Step 5 は『主要リソースで自動遷移を成立させる基盤作り』であり、今後はここを起点に全パス対応へ広げる
- 一時的な手動バリア停止だけで終わらせず、最終的には全パスの状態遷移を RenderGraph 管理へ寄せる前提とする
- したがって、手動バリア停止は応急処置ではなく、全パス自動遷移へ寄せる途中段階として扱う

## この段階の自動化対象

| 論理リソース | 主な要求状態 | 状態参照元 |
|---|---|---|
| `SceneColor` | `RENDER_TARGET`, `PIXEL_SHADER_RESOURCE` | `OffscreenRenderTarget` |
| `SceneDepth` | `DEPTH_WRITE`, `DEPTH_READ | PIXEL_SHADER_RESOURCE` | `DepthStencilManager` |
| `GBuffer*` | `RENDER_TARGET`, `PIXEL_SHADER_RESOURCE` | `GBufferManager` |
| `SSAO` | `RENDER_TARGET`, `PIXEL_SHADER_RESOURCE` | `OffscreenRenderTarget` |
| `ShadowMap` | `DEPTH_WRITE`, `PIXEL_SHADER_RESOURCE` | `ShadowMapManager` |
| `RTShadowMask` | `NON_PIXEL_SHADER_RESOURCE` | `RayTracingShadowManager` |
| `BackBuffer` | `RENDER_TARGET`, `PRESENT` | `BackBufferRenderTarget` |

## この段階での責務境界
- Graph 側は主要リソースの『要求状態への遷移』を担当する
- 各 RenderTarget / GBuffer / Depth / Shadow / RTShadow 管理クラスは実リソースと現在状態の保持を担当する
- 一部の Begin / End 内部遷移はまだ残るが、`currentState` 追跡を共有することで二重発行を抑える
- ReflectionView / CaptureView など複数 View を跨ぐ完全な状態追跡は Step 6 以降へ持ち越す

## 全パス自動遷移へ向けた引き継ぎ
- 以後の方針は『今だけの対策を足す』ではなく、『全パスのバリア処理とリソースステート遷移を最終的に自動化する』ことを優先する
- Step 6 では Shadow / RT / ReflectionView など特殊経路を自動遷移対象へ広げる
- Step 7 では全パスが RenderGraph 管理下に入った前提で、残存する手動バリアと旧状態管理コードを削除する

## 実装時の観点
- 最初は `SceneColor` 1 本から試し、問題なければ対象を広げる
- 既存の手動バリアを一気に消さず、Graph 管理へ段階的に置き換える
- 水面や Screen Space 系で起きやすい SRV / RTV, SRV / DSV の競合を重点確認する

## 期待する到達状態
- パス追加時に毎回明示的な手動バリア追加を要求しにくくなる
- 深度参照系パスの扱いが標準化される
- 後続の Reflection / MultiView 統合で状態不整合を起こしにくくなる

## 完了条件
- 主要リソースの状態遷移が自動化されている
- 手動バリアと Graph バリアの責務が競合していない
- read-only DSV を必要とするパスが正式経路で動作する

## 引き継ぎメモ
- Step 6 では複数 View と Shadow / RT の統合へ進むため、ここで View ごとのリソース状態を追跡しやすい構造が望ましい
- Step 6 では GameView / ReflectionView / 将来の CaptureView で状態追跡対象をどう分離するか、RT 系出力の状態参照をどう正式化するかを重点確認する
- 全パスの自動遷移を最終目標とし、Step 6 で特殊経路を統合、Step 7 で旧手動バリアを廃止する順序を維持する
