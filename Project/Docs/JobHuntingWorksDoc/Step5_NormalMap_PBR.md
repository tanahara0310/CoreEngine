# Step 5 : 表面 BRDF/BTDF と材質校正

## ステータス
- 状態: 実装完了（検証継続）
- 優先度: 高
- 依存ステップ: Step 3, Step 4
- 現在位置: PBR 材質設定、F0、roughness、IBL、プリセット調整まで実装済み
- 完了後に着手しやすい次ステップ: Step 6, Step 9, Step 10

## 目的
水面を PBR 材質として扱うための表面反射モデルを整え、F0、roughness、反射ソースの扱いを整理したうえで、透過・吸収・屈折を導入する前提を固める。

## このステップの作業範囲
- 水面用 BRDF / BTDF の責務整理
- F0 の基準値設定
- roughness の意味付けと調整方針
- metallic の固定方針
- 解析法線を PBR 計算へ接続するルート

## このステップで扱う責務
- 水面表面の材質モデルを定義する
- Fresnel の前提となる F0 を整理する
- roughness が反射ローブへどう効くかを決める
- Planar / IBL / 将来 SSR の反射ソース整理を行う

## このステップの役割
ファイル名は `NormalMap_PBR` だが、このステップの主役はノーマルマップではない。  
ここでは **水面を PBR 的にどう扱うか** を整理する。

対象は以下。

- roughness の意味付け
- F0 の基準値
- IBL と Planar Reflection の役割整理
- 解析法線を BRDF にどう接続するか
- alpha を「見た目の透明度」に流用しない設計

## 作業項目
- [x] 水面の F0 を空気 / 水境界前提で定義する
- [x] roughness を反射ローブ制御量として整理する
- [x] metallic を 0 基本で固定する方針を明記する
- [x] 解析法線を PBR 入力へ接続する
- [x] 反射ソースの責務分離を Step 6 以降へ引き継げる形にする

## 実施結果
- `Water.PS.hlsl` に Schlick 近似ベースの Fresnel が実装され、`gFresnelBaseReflectance` と `gFresnelReflectanceScale` を利用している
- `WaterTestScene` には F0 と反射率スケールを調整する ImGui がある
- `WaterPlaneObject` は baseColor、roughness、metallic、IBL 有効フラグをマテリアルへ反映できる
- `WaterConstantBuffer.h` には湖、海、プール、雨水の PBR プリセットが定義されている
- 水面は `metallic = 0` 前提の誘電体運用になっている

## 物理ベース観点
### 1. 水は金属ではない
- `metallic = 0` を基準とする
- 金属的な見え方ではなく、誘電体として Fresnel で反射が増える

### 2. F0 は屈折率から決まる
- 水の屈折率は約 1.33
- 空気との境界では F0 はおよそ 0.02 前後になる
- ここを意図なく大きくすると常時ぎらついた不自然な水面になる

### 3. roughness は表面微細粗さの近似
- roughness が低いと反射は鋭くなる
- roughness を上げると反射ローブは広がる
- 近景で不自然なプラスチック感を出さない範囲を探る

### 4. ノーマルマップ前提にしない
- 近景水面の主法線源は Step 3 の解析法線
- ノーマルマップが必要なら後で補助として検討するが、主線には置かない

## 実装要素
| 要素 | 内容 |
|------|------|
| F0 calibration | 水の基準反射率の設定 |
| Roughness tuning | 反射ローブの広がり調整 |
| Metallic discipline | 誘電体前提の固定 |
| Analytic normal to PBR | 波面法線を BRDF に接続 |
| Reflection source separation | IBL / Planar / 将来 SSR の役割整理 |

## 実装時の観点
- F0 や roughness は UI 調整可能にして Step 10 の検証へ繋げる
- 解析法線の変動が PBR の反射挙動へ素直に反映されるか確認する
- alpha は透過量そのものではなく、最終合成設計と切り分けて扱う

## 期待する到達状態
- 水面材質が PBR 上で一貫して説明できる
- F0 と roughness の意味が明確になる
- Step 6 の透過・吸収・屈折へ役割を受け渡せる

## 完了条件
- [x] 水面材質を誘電体として扱えている
- [x] F0 と roughness の意味が整理されている
- [x] 解析法線が PBR 計算に接続されている
- [x] 反射経路の責務分離が整理されている
- [x] Step 6 の透過・吸収・屈折へ説明を分離できている

## 引き継ぎメモ
- Step 6 では SceneColor、Beer-Lambert、屈折を追加して表面だけでない水の見え方を完成させる
- Step 9 では SSR を追加し、ここで定めた反射ソース整理を実運用へ進める

---

*[(← Step 4 反射基盤)](Step4_PlanarReflection.md) | [次: Step 6 透過・吸収・屈折の主線 →](Step6_SurfaceShading.md)*