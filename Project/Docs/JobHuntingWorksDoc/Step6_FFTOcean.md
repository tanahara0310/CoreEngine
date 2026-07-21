# Step 6B : FFT Ocean 分岐

## ステータス
- 状態: 実装完了（検証継続）
- 優先度: 中
- 依存ステップ: Step 3, Step 6
- 現在位置: Phillips スペクトル初期化、時間発展、IFFT、変位・法線テクスチャ生成（choppiness の水平変位勾配込み）、法線ミップチェーン生成、Jacobian 生成・可視化、`WaterPlaneObject` への SRV 接続、海況プリセット付き ImGui 調整 UI、RT 経路との UV 写像共有まで接続済み
- 完了後に着手しやすい次ステップ: Step 7, Step 9, Step 10

## 目的
近景主線とは別に、大規模海面を高忠実度に扱うためのスペクトル波面経路を定義し、主線の反射・透過・泡と接続可能な発展分岐を整える。

## このステップの作業範囲
- スペクトル初期化
- 時間発展
- IFFT による空間波面再構成
- 変位・法線・必要に応じた Jacobian 生成
- 主線シェーディングとの接続点整理

## このステップで扱う責務
- 大規模海面向けの波面生成方式を提供する
- 統計的・分散的な海面表現を扱う
- 砕波や泡の候補量として Jacobian 系情報を供給する
- 主線の光学モデルと競合しない分担を定義する

## このステップの役割
Gerstner Wave は近景水面に強いが、外洋スケールでは表現帯域に限界がある。  
FFT Ocean は、

- 大規模海面
- 分散性
- 風依存スペクトル
- 多数波の統計的表現

を扱うための分岐である。

## 作業項目
- [x] Phillips スペクトルから初期海面を構築する
- [x] Compute Shader で時間発展と IFFT を行う
- [x] 変位、法線テクスチャを生成する
- [x] `WaterPlaneObject` から FFT / Gerstner の描画経路を切り替えられるようにする
- [x] ImGui から `patchLength` `amplitudeScale` `windDirection` `windSpeed` `choppiness` `activeComponentCount` `gravity` を調整できるようにする
- [x] Jacobian / 砕波候補量を生成する（デバッグ可視化まで実装済み。Foam への接続は Step 7）
- [x] 主線の反射・透過系との整合を詰める（泡系は Step 7 未着手のため対象外）
- [ ] 近景主線と FFT 分岐の使い分け基準を文書化し切る

## 現状メモ
- `FFTOceanManager` が `FFTOceanTimeEvolution.CS.hlsl` `FFTOceanIFFT.CS.hlsl` `FFTOceanFinalize.CS.hlsl` を用いて、スペクトルから空間変位・法線テクスチャを生成する
- `FFTOceanPass` から毎フレーム dispatch され、`WaterPlaneObject` は `FFTWater.VS.hlsl` を使う経路へ切り替え可能
- 法線は高さ勾配のみではなく、**choppiness の水平変位勾配を含む接ベクトルの外積**で生成する（`Tx=(1+dDx/dx, dh/dx, dDz/dx)` × `Tz=(dDx/dz, dh/dz, 1+dDz/dz)`。高さ勾配のみだと波頭付近でシェーディング法線が実ジオメトリとズレる）
- 法線マップは `FFTOceanNormalMipGen.CS.hlsl` で毎フレーム**ミップチェーンを生成**する（ミップ無しだと遠方・斜め視点でエイリアシングし、フレネルが画素毎に暴れてスペックル状の白ノイズになる）
- 振幅スケールはスペクトル段階（TimeEvolution）で高さ・変位へ一貫適用する
- メッシュ UV は `PlaneMeshGenerator` の V 反転（`{u, 1-v}`）に合わせて VS / PS / RT で統一済み。RT 側（屈折・コースティクス）とは **ワールド XZ → FFT テクスチャ UV の写像**（`WaterSurfaceData::fftUVScale / fftUVOffset`）を毎フレーム共有し、ラスタと RT が同じ波面を評価する（水面メッシュの回転は非対応・ゼロ前提）
- Jacobian は Finalize で生成され、デバッグ可視化（FFTOceanJacobian モード）で確認できる
- 海況プリセット（FFT Ocean プリセット切替・再適用 UI）を実装済み。プリセット適用中は詳細パラメータをロックし、「カスタム」選択で個別調整できる
- `WaterSurfaceParameterPanel` / `WaterSurfaceDebugPanel` から設定値と接続状態を確認できる
- `FFTWater.VS.hlsl` の頂点法線は常に (0,1,0) でうねりを含まない（シェーディング法線は PS で法線マップから解決する）点に注意

## 現行実装の構成
| レイヤ | 実装 |
|------|------|
| 管理 | `Engine/Src/Graphics/Water/FFTOceanManager.{h,cpp}` |
| リソース生成 | `Engine/Src/Graphics/Water/FFTOceanResourceFactory.{h,cpp}` |
| Dispatch | `Engine/Src/Graphics/Render/Pass/FFTOceanPass.cpp` |
| 時間発展 | `Engine/Assets/Shaders/Water/Simulation/FFTOceanTimeEvolution.CS.hlsl` |
| IFFT | `Engine/Assets/Shaders/Water/Simulation/FFTOceanIFFT.CS.hlsl` |
| 最終化 | `Engine/Assets/Shaders/Water/Simulation/FFTOceanFinalize.CS.hlsl` |
| 法線ミップ生成 | `Engine/Assets/Shaders/Water/Simulation/FFTOceanNormalMipGen.CS.hlsl` |
| 頂点適用 | `Engine/Assets/Shaders/Water/Surface/FFTWater.VS.hlsl` |
| ランタイム調整 | `Application/Src/Scenes/WaterTestScene/WaterSurfaceParameterPanel.cpp` |
| 診断 UI | `Application/Src/Scenes/WaterTestScene/WaterSurfaceDebugPanel.cpp` |

## 波が弱く見えるときの確認順
1. `FFTWater.VS.hlsl` 側で固定クランプしていないか確認する
2. `amplitudeScale` を上げる
3. `windSpeed` を上げる
4. `choppiness` を上げる
5. `patchLength` を下げて空間周波数を上げる
6. `activeComponentCount` を増やして帯域を広げる

### 補足
- 現行実装では `BuildSpectrum()` で Phillips スペクトルを使っている
- `FFTOceanTimeEvolution.CS.hlsl` では `activeComponentCount` に応じた `bandFade` が掛かるため、成分数が少ないと高周波成分が強く抑えられる
- したがって「波っぽくは動くが全体に弱い」場合は、計算停止よりもスペクトル強度または最終適用段の制限を疑う

## 物理ベース観点
### 1. FFT は波面生成の高忠実度化
FFT Ocean は色や反射を直接正しくする技術ではない。  
あくまで波面形状の統計的・物理的妥当性を上げる手段である。

### 2. 表面光学は主線と共有する
- Fresnel
- 吸収
- 透過
- 屈折
は Step 6 主線と同じ考え方で扱う。

### 3. 最終的には RT とも併用可能
高精度波面を RT 反射 / RT 屈折へ接続できれば、最終段の品質上限が大きく上がる。

## 実装要素
| 要素 | 内容 |
|------|------|
| Spectrum initialization | 風依存の初期周波数分布 |
| Time evolution | 時刻 t における海面発展 |
| IFFT | 周波数空間から空間変位への変換 |
| Displacement / normal map | 波面と法線の生成 |
| Jacobian / crest detection | 砕波や泡の発生補助 |

## 実装時の観点
- 近景主線の Gerstner Wave と役割衝突しないよう用途を分ける
- FFT の出力を Step 6 / Step 7 / Step 9 の入力へどう流すかを先に決める
- Compute コストと更新頻度の調整方針を持つ

## 期待する到達状態
- 大規模海面に対して Gerstner より自然な統計波面が得られる
- 主線の反射・透過・泡の設計を流用できる
- RT 反射 / RT 屈折と組み合わせた最終品質強化の足場になる

## 完了条件
- [x] スペクトルベースの大規模波面が生成される
- [x] FFT 出力を使う頂点変位・法線経路へ切り替えられる
- [x] 分散性が見た目に十分現れる（海況プリセットで検証済み）
- [x] 主線の反射・透過系と接続できる（泡系は Step 7 未着手）
- [x] RT 屈折 / RT コースティクスの入力波面として利用できる（UV 写像共有により整合。RT 反射は未着手）

## 引き継ぎメモ
- Step 7 では Jacobian や高圧縮領域を泡候補へ利用できる（Jacobian は生成・可視化済み）
- Step 9 と Step 10 では主線と分岐の品質・コスト比較が重要になる
- 波面・法線・UV 写像を触るときは、ラスタ（VS/PS）と RT（屈折・コースティクス）の両方が同じ波面を評価し続けているかを必ず可視化で確認する（過去に V 反転不一致・写像不一致で「波と無関係な明るいまだら」「屈折交点ズレ」を踏んだ）

---

*[(← Step 6 透過・吸収・屈折の主線)](Step6_SurfaceShading.md) | [次: Step 7 Foam →](Step7_Foam.md)*
