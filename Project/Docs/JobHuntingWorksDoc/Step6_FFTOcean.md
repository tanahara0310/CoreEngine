# Step 6B : FFT Ocean 分岐

## ステータス
- 状態: 実装中（検証継続）
- 優先度: 中
- 依存ステップ: Step 3, Step 6
- 現在位置: Compute Shader ベースの FFT Ocean 経路は実装済み。Phillips スペクトル初期化、時間発展、IFFT、変位・法線テクスチャ生成、`WaterPlaneObject` への SRV 接続、ImGui からの調整 UI まで接続済み。現在は波強度と最終見た目の検証を継続中
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
- [ ] Jacobian / 砕波候補量を生成する
- [ ] 主線の反射・透過・泡系との整合を詰める
- [ ] 近景主線と FFT 分岐の使い分け基準を文書化し切る

## 現状メモ
- `FFTOceanManager` が `FFTOceanTimeEvolution.CS.hlsl` `FFTOceanIFFT.CS.hlsl` `FFTOceanFinalize.CS.hlsl` を用いて、スペクトルから空間変位・法線テクスチャを生成する
- `FFTOceanPass` から毎フレーム dispatch され、`WaterPlaneObject` は `FFTWater.VS.hlsl` を使う経路へ切り替え可能
- `WaterSurfaceParameterPanel` / `WaterSurfaceDebugPanel` から設定値と接続状態を確認できる
- `FFTWater.VS.hlsl` の固定クランプで最終変位が頭打ちになっていたため、縦 `±0.5` / 横 `±0.25` の制限は削除済み
- 現在の見た目上の主要論点は「FFT の出力が無い」ではなく、「スペクトル強度とパラメータ帯域のチューニング」である

## 現行実装の構成
| レイヤ | 実装 |
|------|------|
| 管理 | `Project/Engine/Src/Graphics/Water/FFTOceanManager.{h,cpp}` |
| Dispatch | `Project/Engine/Src/Graphics/Render/Pass/FFTOceanPass.cpp` |
| 時間発展 | `Project/Application/Assets/Shaders/Water/FFTOceanTimeEvolution.CS.hlsl` |
| IFFT | `Project/Application/Assets/Shaders/Water/FFTOceanIFFT.CS.hlsl` |
| 最終化 | `Project/Application/Assets/Shaders/Water/FFTOceanFinalize.CS.hlsl` |
| 頂点適用 | `Project/Application/Assets/Shaders/Water/FFTWater.VS.hlsl` |
| ランタイム調整 | `Application/Src/Sample/SampleScene/WaterTestScene/WaterSurfaceParameterPanel.cpp` |
| 診断 UI | `Application/Src/Sample/SampleScene/WaterTestScene/WaterSurfaceDebugPanel.cpp` |

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
- [ ] 分散性が見た目に十分現れる
- [ ] 主線の反射・透過・泡系と接続できる
- [ ] RT 反射 / RT 屈折の入力波面として利用できる

## 引き継ぎメモ
- Step 7 では Jacobian や高圧縮領域を泡候補へ利用できる
- Step 9 と Step 10 では主線と分岐の品質・コスト比較が重要になる

---

*[(← Step 6 透過・吸収・屈折の主線)](Step6_SurfaceShading.md) | [次: Step 7 Foam →](Step7_Foam.md)*
