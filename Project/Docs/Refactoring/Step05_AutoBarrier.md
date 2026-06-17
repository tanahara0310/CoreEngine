# Step 5: 自動バリア導入

## ステータス
- 状態: 未着手
- 優先度: 中
- 依存ステップ: Step 3, Step 4
- 完了後に着手しやすい次ステップ: Step 6

## 目的
RenderGraph と論理リソース管理を前提に、主要リソースの状態遷移を自動化し、パス追加時の人的ミスと暫定バリア実装を減らす。

## このステップの作業範囲
- `RenderGraph` の実行時状態追跡
- `SceneColor` / `SceneDepth` / `GBuffer*` / `SSAO` / `BackBuffer` の状態管理
- 既存 `ResourceBarrierHelper` / `DepthStencilManager` / RenderTarget 系との接続
- read-only DSV を含む深度参照経路

## このステップで扱う責務
- `RTV / DSV / SRV / UAV / Present` の主要状態自動遷移
- 冗長バリアの削減
- 手動バリアとの二重管理防止
- 深度参照パスの標準化

## 作業項目
- [ ] 自動化対象のリソース状態を `RTV / DSV / SRV / UAV / Present` に絞る
- [ ] `SceneColor` の状態遷移を自動化する
- [ ] `GBuffer*` の状態遷移を自動化する
- [ ] `SceneDepth` の状態遷移を自動化する
- [ ] `BackBuffer` の状態遷移を自動化する
- [ ] read-only DSV と SRV 併用の経路を標準化する
- [ ] 冗長バリアが減っているか確認する
- [ ] 既存手動バリアとの二重管理がないか確認する

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
