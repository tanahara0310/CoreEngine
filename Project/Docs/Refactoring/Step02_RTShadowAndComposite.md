# Step 2: RT Shadow Pass 化と Composite 系整理

## ステータス
- 状態: 未着手
- 優先度: 高
- 依存ステップ: Step 1
- 完了後に着手しやすい次ステップ: Step 3

## 目的
RT Shadow を特例呼び出しから通常の RenderPass に寄せ、併せて `GeometryPass` に集中している Forward Composite の責務分離方針を固める。

## このステップの作業範囲
- `Engine/Src/EngineSystem/Subsystem/RayTracingSubsystem.cpp`
- 新規 `RTShadowPass`
- `Engine/Src/Graphics/Render/Pass/GeometryPass.*`
- `Engine/Src/Graphics/Render/RenderManager.*`
- 必要に応じて `RenderPipeline` への Pass 登録処理

## このステップで扱う責務
- `DispatchRTShadow()` の RenderPass 化
- GBuffer 後 / DeferredLighting 前への自然な差し込み
- `GeometryPass` の責務棚卸し
- Composite 系パス分割方針の明文化

## 作業項目
- [ ] `DispatchRTShadow()` の入力依存を洗い出す
- [ ] `RTShadowPass` を新規作成する
- [ ] `GBufferPass` の後、`DeferredLightingPass` の前に `RTShadowPass` を入れる
- [ ] `EngineSystem` から RT Shadow の特例呼び出しを削除する
- [ ] `GeometryPass` の責務を整理する
- [ ] `TransparentPass` / `SkyPass` / `ParticlePass` / `UIPass` 分割方針を確定する

## 実装時の観点
- 最初の `RTShadowPass` は薄いラッパーでもよい
- この段階では Composite 系パスを完全分離しなくてもよいが、責務境界は文章とコードの両方で見えるようにする
- Water や Reflection を追加するための『特例で Scene から呼ぶ』経路はここで増やさない

## 期待する到達状態
- RT 技術を通常 Pass として差し込める
- `GeometryPass` が暫定の大箱責務であることが明文化される
- 後続の Blackboard 導入時に、各 Composite パスの入出力整理へ進みやすくなる

## 完了条件
- RT Shadow が RenderPass 経由で実行される
- `EngineSystem` に RT Shadow 専用の特例呼び出しが残っていない、または大きく縮小している
- Composite 系パス分割の方針が文書とコード構造の両方で追える

## 引き継ぎメモ
- Step 3 では論理リソース名で入出力を整理するため、ここで `SceneColor` / `ShadowMask` / `RTShadowMask` の受け渡し地点を把握しておく
