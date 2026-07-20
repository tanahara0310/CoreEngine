# Step 8 : Caustics / Underwater Lighting

## ステータス
- 状態: 実装完了（検証継続）
- 優先度: 中
- 依存ステップ: Step 6
- 現在位置: DXR 版（`RTWaterCausticsPass` / `RTWaterCaustics.hlsl`）と後処理合成版（`WaterCausticsTechnique` / `WaterCaustics.PS.hlsl`）の 2 系統が接続済み。水域矩形マスク・大気太陽との情報源統一まで実装済み。残課題は Underwater Lighting 方向への拡張（水中散乱・水中影）
- 完了後に着手しやすい次ステップ: Step 9, Step 10

## 目的
水面を通過した光が水中や水底へどう届くかを表現し、浅瀬の説得力を上げる。反射・透過だけでは不足する『水中の光』を補う。

## このステップの作業範囲
- 浅瀬のコースティクス
- 水深に応じた減衰
- 水底への投影
- 入射光方向に応じた重み付け
- 水中光量のデバッグ表示

## このステップで扱う責務
- 水中へ入った光の見せ方を定義する
- コースティクスを浅瀬中心に限定する
- 水深と入射条件で強度を制御する
- 将来の RT 水中ライティングへ接続できる整理を行う

## このステップの役割
水面の物理ベース化では、表面だけでなく **水中へ入った光** も重要になる。  
ここでは次を扱う。

- 浅瀬のコースティクス
- 水中への光減衰
- 必要に応じた水底照明の補正
- 将来的な RT 水中ライティングへの橋渡し

## 作業項目
- [x] 浅瀬限定でコースティクスを出す
- [x] 水深と入射光条件で強度を制御する
- [x] 水底への投影として扱う
- [x] 水面そのものより目立ちすぎないようにする
- [x] 水中光量を可視化できる最小限の debug 表示を用意する

## 現状メモ
- `RTWaterCaustics.hlsl` は GBuffer の `WorldPosition` / `NormalRoughness` と水面波情報を入力に、受光点ごとの簡易集光量を RT 出力へ書き出す
- `RTWaterCausticsPass` は `FrameBlackboard::RTWaterCaustics` を更新し、`DeferredLightingPass` 側は RT 出力があれば近似パスより優先して使用する
- `DeferredLighting.PS.hlsl` は `gWaterCaustics` を最終ライティングへ加算し、debug mode では Raw RGB / グレースケール表示へ切り替えられる
- `WaterSurfaceDebugPanel` と `WaterCausticsTechnique` から強度、深度減衰、曲率、屈折率、表示倍率、表示モードなどを調整できる
- コースティクスは解析的な無限水面として評価されるため、放置すると「水面高さより低い場所すべて」（水域外の床）に集光模様が漏れる。**水面メッシュ直下の矩形範囲マスク**（`WaterSurfaceData::regionCenterXZ / regionHalfExtentXZ`、境界 2m フェードアウト）で水域内へ限定している。水面メッシュの回転は非対応（FFT UV 写像と同じゼロ前提）
- 太陽の方向・色・強度は大気散乱の太陽ライト（`isAtmosphereSun`）と同一情報源を参照する。CPU 直読みは透過率変調済みの `GetEffectiveLightColorRGB()` を使うため、夕方はコースティクスも赤方偏移し、太陽の見た目・反射ハイライト方向と自動的に一致する
- Step 6 で整備された水面高さ・波データ・吸収設計は、今後の水中光拡張へ流用できる

## 物理ベース観点
### 1. コースティクスは浅瀬中心
深い水では散乱と吸収で弱くなる。  
したがって、浅瀬限定適用が前提である。

### 2. 表面と水中は別レイヤー
コースティクスは水面テクスチャではなく、水底へ届いた光の揺らぎとして扱う。

### 3. 最終的な RT 水中光の近似段階
このステップは最終的に RT 経路でより正しく扱う前の、リアルタイム近似段階と位置付ける。

## 実装要素
| 要素 | 内容 |
|------|------|
| Caustics pattern | 浅瀬光条の基礎 |
| Depth attenuation | 水深に応じた減衰 |
| Bottom projection | 水底への投影 |
| Light-direction weighting | 入射光条件の反映 |
| Underwater debug views | 水中光量の可視化 |

## 実装時の観点
- 水面表面を白く塗るのではなく、水底または水中散乱側へ効果を置く
- Step 6 の吸収と深度差計算を前提にする
- Step 7 の泡と競合して白飛びしないよう強度上限を持たせる

## 実施結果
- `WaterCausticsRayTracingManager` が `RTWaterCaustics.hlsl` を用いた DXR パイプラインを構築し、view ごとの UAV / SRV を確保している
- RayGen では受光点から水面位置と解析法線を再構成し、主光源方向を `refract` した方向へレイを飛ばして受光点との一致度を評価している
- 強度計算は浅瀬フェード、受光法線、入射方向、受光点との ray match、指数減衰を組み合わせた簡易モデルになっている
- `DeferredLightingPass` は `RTWaterCaustics` が利用可能ならそれを優先し、無ければ `WaterCausticsTechnique` 側の出力を受ける構成になっている
- `DeferredLighting.PS.hlsl` では albedo / AO / metallic に応じてコースティクス寄与を抑制し、シーン全体に過剰な白飛びが出にくい合成を行っている
- debug view は deferred lighting 側で Raw RGB とグレースケール表示に対応し、ImGui 側では関連パラメータの調整と診断情報表示が可能になっている

## 残タスク整理
- 現状の RT 出力は受光点ごとの単一強度が中心で、屈折集光の時間的安定化や高周波パターン生成は未着手
- Underwater Lighting 全体として見ると、水中散乱、体積減衰、光路の複数回屈折、影との統合は未対応
- コースティクス用の定量的な成功率・カバレッジ計測は不足しており、debug view も 2 モード中心の最小構成に留まる
- Foam、SSR、将来の水中影と同時に有効化した際の視覚競合は今後の検証対象

## 期待する到達状態
- 浅瀬でのみ光の揺らぎが成立する
- 深場では自然に弱まり、うるさくならない
- 将来的な RT 水中光の簡略近似として説明できる

## 完了条件
- [x] 浅瀬でのみ光の揺らぎが成立する
- [x] 深場では自然に弱まる
- [x] 地面オブジェクトへの投影として扱える
- [ ] 将来的な RT 水中照明へ接続できる

## 引き継ぎメモ
- Step 9 では SSR と組み合わせた際に表面反射側と視覚競合しないかを見る
- Step 10 では depth attenuation、投影マスク、RT/近似ソースの使い分け、時系列安定性を可視化できると調整しやすい
- 落とし穴: コースティクスの定数（水域矩形を含む）は **WaterCaustics.PS.hlsl ↔ WaterCausticsTechnique.h（WaterSurfaceConstants）、RTWaterCaustics.hlsl ↔ WaterCausticsRayTracingManager.cpp（WaterCausticsConstants）** で cbuffer レイアウトの一致が必須（static_assert あり）。変更時は両系統まとめて更新する

---

*[(← Step 7 Foam)](Step7_Foam.md) | [次: Step 9 SSR と反射統合 →](Step9_SSR.md)*
