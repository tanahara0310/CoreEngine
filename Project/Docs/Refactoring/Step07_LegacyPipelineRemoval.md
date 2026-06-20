# Step 7: 旧実行経路削除と RenderGraph 完全移行

## ステータス
- 状態: 完了
- 優先度: 中〜高
- 依存ステップ: Step 4, Step 5, Step 6
- 完了後に着手しやすい次ステップ: なし

## 開始判定
- Step06 で GameView / ReflectionView / 将来の CaptureView が同じ RenderGraph 実行モデルへ統合可能な構造になっている
- Water Reflection が Scene 特例の RTT 実行ではなく、Engine 側 ReflectionView の結果として扱われている
- `ReflectionColor` を含む主要共有リソースが Blackboard 経由で参照できる
- 以降の主作業が『旧経路削除』『`PassOutput` 整理』『手動バリア除去』に絞られている

## 目的
`ExecutePass()` ベースの旧個別実行経路、暫定的に残した互換処理、Graph 外の手動接続、手動バリア処理を段階的に削除し、RenderGraph をフレーム実行と状態遷移の唯一の正式経路にする。

## このステップの作業範囲
- `EngineSystem` 側の旧手動パス実行ロジック削除
- `RenderPipeline` 内の旧互換実行経路整理
- `GameView` / `ReflectionView` / 将来の `CaptureView` の Graph 実行統一
- `PassOutput` 依存の縮小または廃止
- Graph 外の暫定 Blackboard / Manager 直参照整理
- 旧手動バリアと重複する状態追跡コードの削除

## このステップで扱う責務
- RenderGraph を唯一の実行器にする
- 旧経路の削除順序管理
- View ごとの Graph 実行モデル統一と廃止済み旧 View 残骸の除去
- 互換コードの廃止判断
- 手動バリア廃止と状態正本の一本化

## 作業項目
- [x] `EngineSystem::ExecuteRenderPipeline()` に残る旧手動実行ロジックを除去する
- [x] `RenderPipeline::ExecutePass()` への直接依存箇所を洗い出す
- [x] 廃止済み旧 View 系の残存コードを削除する
- [x] `PassOutput` の必要性を再評価し、不要な受け渡しを削除する
- [x] Graph 外での論理リソース解決や Manager 直参照を縮小する
- [x] RenderGraph が GameView / ReflectionView の正式実行経路として成立することを確認する
- [x] 全パスが自動遷移へ移行済みであることを確認し、残存する手動バリアを削除する

## 進捗メモ
- `PassOutput` と `ExecutePass()` ベースの旧チェーン実行は削除済みで、RenderGraph は RenderPass を直接 `Setup / Execute / Cleanup` する構成へ移行した
- `SceneView` 由来の旧 View 残骸は RenderViewType / RTShadow ViewID / 不要 RenderTarget / デバッグ表示から整理済み
- `RenderPipeline` に `ExecuteView()` を追加し、GameView / ReflectionView の `PrepareFrame() -> ExecuteRenderGraph()` を View 単位 API へ集約した
- `RenderPipeline` に `ExecuteReflectionView()` を追加し、ReflectionColor の Blackboard 公開と `ReflectionViewResult` 組み立てを `EngineSystem` から移譲した
- `EngineSystem` に残っていた GameView 向けの事前 `PrepareFrame()` 呼び出しを削除し、GameView も `ExecuteView()` を唯一の正式入口とする構成へ揃えた
- `RenderPipeline::PrepareFrame()` で `SceneDepth / SceneColor / BackBuffer / ShadowMap / GBuffer*` を Blackboard へ事前登録するようにし、主要リソースの正本を Blackboard 側へ寄せ始めた
- `RenderGraph::ResolveResources()` は Blackboard 正本優先に切り替え、Manager 直参照 fallback は View 依存の `RTShadowMask` のみに縮小した
- `EngineSystem::ExecuteRenderPipeline()` に残っていた ReflectionView の結果組み立てと Blackboard 注入は削減済みで、現状は実行要求取得と `SceneManager::ApplyReflectionViewResult()` 呼び出しが主な責務になった
- `DepthStencilManager::ScopedDepthReadSRV` と `BeginDepthReadSRV / EndDepthReadSRV` を削除し、GeometryPass の深度 read 互換経路を RenderGraph 前提へ揃えた
- `GBufferManager::BeginGeometryPass/EndGeometryPass` に残っていた GBuffer / SceneDepth の手動遷移を削除し、描画セットアップ責務だけを残した
- `RenderTarget::TransitionBarrier` と対応 `.cpp` を削除し、`OffscreenRenderTarget` / `BackBufferRenderTarget` は `ResourceBarrierHelper` を直接使う構成へ整理した
- `ResourceBarrierHelper` 自体は RenderGraph の自動遷移実装に加え、`OffscreenRenderTarget::BeginCS/EndCS`、`BackBufferRenderTarget` の Present 境界、`RayTracingShadowManager` の compute / copy 系遷移でも引き続き必要であり、現段階では削除対象ではない
- 上記整理後、`BackBufferRenderTarget::Begin()` に残っていた DSV バインドと `ClearDepthStencilView` が `SceneDepth` の `DEPTH_READ | PIXEL_SHADER_RESOURCE` 状態と衝突してクラッシュしたため、最終合成では深度を使わない前提に合わせて BackBuffer の深度依存を削除した

## 実装時の観点
- 一気に全削除せず、View 単位・Pass 群単位で移行する
- デバッグ用途の特例ほど最後に整理する
- 旧経路削除前に Graph 側で同等の責務を保持できていることを確認する
- 完全移行後は『Graph に登録されない描画機能は正式機能ではない』状態を目指す
- 手動バリアの削除は、Step 6 までで全パスの自動遷移対応が揃ってから行う

## 期待する到達状態
- RenderGraph がフレーム実行の唯一の正式経路として機能する
- 旧個別実行コードが保守負債として残らない
- 後続機能追加が Graph ノード追加だけで進めやすくなる

## 完了条件
- GameView / ReflectionView の主要描画が RenderGraph 経由で統一されている
- 旧個別パス実行コードが削除または明確に限定されている
- `PassOutput` と旧互換配線の整理方針がコード上で反映されている
- RenderGraph と重複する互換バリア補助が削除され、残存する `ResourceBarrierHelper` 利用箇所の役割が明確化されている

## 引き継ぎメモ
- Step 4〜6 で作った Graph / Blackboard / バリア基盤を前提に、Graph 外の compute / copy / Present 境界だけを `ResourceBarrierHelper` が担う構成として維持する
