# Step 10 : デバッグ / 検証 / RT 最終段

## ステータス
- 状態: 実装中
- 優先度: 高
- 依存ステップ: Step 1 ～ Step 9
- 現在位置: ImGui による水面調整、22 種の可視化モード（深度系 / 反射系 / 透過系 / RT 屈折系 / 合成診断系）、コースティクス debug view は実装済みで、アーティファクト調査に実運用済み。今後は品質チェックリストの文書化、寄与内訳の定量化、RT 最終段の整理強化が必要
- 完了後に着手しやすい次ステップ: 継続改善フェーズ

## 目的
物理ベース水面の品質確認手段を揃え、反射・透過・吸収・屈折・泡・コースティクスを個別に検証できるようにしたうえで、最終到達点としてレイトレーシング統合を定義する。

## このステップの作業範囲
- Debug UI / ImGui 整理
- 波高、法線、Fresnel、透過率、吸収量、屈折レイ、泡、コースティクス、SSR の可視化
- 品質確認チェックリストの整備
- RT 反射、RT 屈折、水中減衰、水中影の位置付け整理

## このステップで扱う責務
- 水面品質を見た目だけでなく内訳で確認できるようにする
- 調整用パラメータを整理する
- 検証項目を文書化する
- RT 最終段の目標と必要要素を定義する

## このステップの役割
水面表現は見た目だけでは評価できない。  
反射・透過・吸収・屈折・泡・コースティクスが正しく働いているかを確認するための
**可視化・調整・検証基盤** が必要になる。

さらにこのステップでは、ロードマップの最高地点として
**レイトレーシング統合** を定義する。

## 作業項目
- [x] ImGui / Debug UI を物理パラメータ単位で整理する
- [x] Fresnel、透過光、波長別透過率、反射率、反射ソース各段、RT 屈折、Jacobian、コースティクスを可視化する（泡・SSR は未実装のため対象外）
- [x] 反射ソースと透過ソースの寄与を画面切替で確認できるようにする（合成診断系モード。定量集計は未整備）
- [ ] 品質確認チェックリストを整備する
- [ ] RT 反射、RT 屈折、水中減衰、水中影を最終段として整理する

## 実施結果
- 水面の調整 UI は `WaterSurfaceParameterPanel`（見た目調整・Jerlov 水質 / 濁度・海況プリセット）と `WaterSurfaceDebugPanel`（診断・可視化切替）へ整理された。Hierarchy > Environment > Water → Inspector の「デバッグ/診断」からアクセスする
- 可視化モードは `WaterDebugViewMode`（enum 23 値）として整理され、`Water.PS.hlsl` が実装する:
  - 深度系: Raw Depth / Linear Depth / Depth Delta / Screen UV / Scene Color
  - 反射系: Reflection（雲合成込み）/ Fresnel / Reflectance / **PlanarReflectionRaw（生の平面反射）/ SkyEnvCloudColor（雲キューブマップ色）/ CloudOverlayWeight（雲上書き強度）**
  - 透過系: Transmission（透過光）/ Transmittance（波長別透過率）
  - RT 屈折系: RTRefraction / RTRefractionReason（緑=成功 / 白=DepthMismatch / マゼンタ=クリップ外）/ RTRefractionVsScene / RTRefractionSuccessMask
  - FFT 系: FFTOceanJacobian
  - 合成診断系: WaterComposite / **CompositeTransmissionOnly（フレネル=0 固定）/ CompositeReflectionOnly（フレネル=1 固定）/ ReflectionMinusTransmission（両端点の輝度差 ×3）**
- 合成診断系は「透過のみ / 反射のみのどちらに模様が出るか」で不具合の犯人を一意に切り分けるためのモードで、実際に夜間の巨大な明暗斑の真因（反射ビューへの水面自己描画）特定に使用した
- Reflection RTT 接続状態、推奨波本数、現在波本数などの実行時確認項目が ImGui から見える
- 個別波パラメータの直接編集やプリセット再生成も可能になっている
- `WaterCausticsTechnique` / `DeferredLighting.PS.hlsl` 側ではコースティクスの Raw RGB / グレースケール debug view と、強度・深度減衰・曲率・表示倍率などの調整 UI が利用可能になっている

## 運用上の教訓（アーティファクト調査で確立）
- **可視化モードもポストエフェクト後段の自動露出を通る**。夜は +8EV で全モードが白飛び（または黒潰れ）して構造が読めない。夜間の診断はデバッグ出力へ 1/256 を掛けて露出を打ち消すこと（シェーダーはランタイムコンパイルのため C++ リビルド不要）
- 切り分けの定石: 疑わしい要素をインスペクタで OFF にする A/B（例: フレネル反射スケール 0、雲スペキュラ IBL OFF）→ 合成診断系モードで透過 / 反射のどちら側かを確定 → 生テクスチャ系モード（PlanarReflectionRaw 等）で中身を直接見る
- 対症療法の定数（フレネル法線平坦化・グロッシー反射・ショルダー圧縮など）が入っているため、斑が再発した場合はまずこれらを疑うのではなく、**反射テクスチャの中身を露出打ち消しで見る**こと

## 残タスク整理
- Foam、SSR は未実装のため、それぞれ専用 debug view も未整備
- 屈折はレイベース方式の理由別可視化までは整ったが、ヒット距離、成功率の定量表示はまだ不足している
- 反射 / 透過 / フォールバック内訳の定量確認（数値集計）はまだ不足している
- コースティクスは debug view と UI 調整・水域マスクがある一方、RT/近似ソース切り替えの比較や時系列安定性の可視化は未整備
- RT 反射と水中影は未着手で、RT 屈折は最小実装はあるが最終段の整理と検証は未完了
- 正式な品質確認チェックリストはまだ文書化されていない

## デバッグで見るべきもの
### 表面系
- 解析法線
- roughness
- Fresnel 係数
- 反射ソース内訳（Planar / IBL / SSR / RT）

### 透過系
- transmittance
- absorption
- shallow / deep color mask
- refraction ray direction
- refraction hit UV / hit depth
- refraction fallback mask

### 効果系
- foam mask
- caustics intensity
- caustics raw RGB / grayscale
- SSR hit mask
- reflection fallback mask

## レイトレーシングを最高地点に置く理由
ラスタライズだけでは次に限界がある。

- 画面外反射
- 正しい屈折経路
- 水中シャドウ
- 複雑な自己反射
- 反射・透過の長経路

これらを高忠実度で扱う最終段がレイトレーシングである。

## 最終段で目指すもの
| RT 要素 | 目的 |
|--------|------|
| RT Reflection | 画面外も含めた正確な反射 |
| RT Refraction | 屈折方向に沿った背景取得 |
| Underwater shadowing | 水中へ届く光の減衰と影 |
| Hybrid reflection composition | Planar / SSR / IBL / RT の最適統合 |
| Validation tooling | 各経路の寄与確認 |

## 実装時の観点
- 調整 UI は単なる値列挙ではなく、物理グループごとに分ける
- 各マスクや寄与率を画面切替で可視化できるようにする
- レイベース屈折では、ヒット成功率と画面端欠損率を観測できるようにする
- RT 経路を導入した際も既存ラスタ経路との比較ができる構造にする

## 期待する到達状態
- 水面の主要パラメータを UI から確認・調整できる
- 反射・透過・吸収・泡・コースティクスを個別可視化できる
- RT 統合前後の品質差や寄与差を説明できる

## 完了条件
- [x] 主要な水面パラメータを UI から確認・調整できる
- [ ] 反射・透過・吸収・泡・コースティクスを個別可視化できる
- [ ] 品質確認項目がドキュメント化されている
- [ ] RT 反射 / RT 屈折 / 水中影を最終到達点として整理できている

## 引き継ぎメモ
- ここまで揃えば、水面機能の追加時も何を検証すべきかを README と各 Step から追える
- RT を導入する際は、Step 4・Step 6・Step 9 の既存経路との役割比較を必ず残す

---

*[(← Step 9 SSR と反射統合)](Step9_SSR.md) | [README に戻る →](README.md)*
