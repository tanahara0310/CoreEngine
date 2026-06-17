# Step 6: Shadow / RT / 複数 View 統合

## ステータス
- 状態: 未着手
- 優先度: 中
- 依存ステップ: Step 2, Step 3, Step 4, Step 5
- 完了後に着手しやすい次ステップ: 継続改善フェーズ

## 目的
Shadow / RT / 複数 View を同じ基盤へ段階的に統合し、GameView / SceneView / ReflectionView / CaptureView を再利用可能なレンダリング構造へ寄せる。

## このステップの作業範囲
- ShadowMap 系の Graph / Blackboard 統合
- RT 系パスの正式統合
- GameView / SceneView / ReflectionView / CaptureView の抽象化
- View ごとの描画リスト構築と実行フロー整備

## このステップで扱う責務
- 複数 View の統一的な実行モデル
- Shadow / RT の特例削減
- Reflection 用補助 View の正式化
- 将来機能向けの拡張基盤整備

## 作業項目
- [ ] ShadowMap 系を RenderGraph / Blackboard に統合する
- [ ] RT 系パスを Graph 上の正式パスへ寄せる
- [ ] GameView / SceneView の扱いを共通化する
- [ ] `ReflectionView` / `CaptureView` を想定した View 抽象化を行う
- [ ] 複数 View で同じ流れを再利用できるか確認する
- [ ] Water / Reflection / 将来の Capture 系が Scene 特例なしで成立するか確認する

## 実装時の観点
- まずは GameView を基準に整える
- SceneView は特例処理を後から寄せる
- Reflection は Scene が直接反射パスを回すのではなく、View 実行結果として扱う
- Capture 系や将来の RT Reflection を見据えて、View 単位でリソース集合を扱えるようにする

## 期待する到達状態
- 複数 View を同じレンダリング基盤で扱える
- Water Reflection のような補助ビューが Scene 側特例なしで実装できる
- Shadow / RT / Composite / PostProcess の統合拡張がしやすくなる

## 完了条件
- GameView / SceneView / ReflectionView などを同じ枠組みで説明できる
- Shadow / RT 系が特例ではなく通常パス群として扱える
- 新しい View 追加時に Scene 側の暫定処理へ戻らない構造になっている

## 引き継ぎメモ
- ここまで完了すれば、RenderGraph 基盤上で Water / SSR / RT Reflection / Volumetric などを追加しやすい継続改善フェーズへ入れる
