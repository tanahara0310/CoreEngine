# Step 7: 旧実行経路削除と RenderGraph 完全移行

## ステータス
- 状態: 未着手
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
- View ごとの Graph 実行モデル統一と廃止済み SceneView 残骸の除去
- 互換コードの廃止判断
- 手動バリア廃止と状態正本の一本化

## 作業項目
- [ ] `EngineSystem::ExecuteRenderPipeline()` に残る旧手動実行ロジックを除去する
- [ ] `RenderPipeline::ExecutePass()` への直接依存箇所を洗い出す
- [ ] 廃止済み `SceneView` の残存コードを削除する
- [ ] `PassOutput` の必要性を再評価し、不要な受け渡しを削除する
- [ ] Graph 外での論理リソース解決や Manager 直参照を縮小する
- [ ] RenderGraph が GameView / ReflectionView の正式実行経路として成立することを確認する
- [ ] 全パスが自動遷移へ移行済みであることを確認し、残存する手動バリアを削除する

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

## 引き継ぎメモ
- Step 4〜6 で作った Graph / Blackboard / バリア基盤を前提に、最終的な責務の一本化を行う
