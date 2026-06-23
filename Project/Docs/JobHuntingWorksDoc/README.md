# 物理ベース水面表現ロードマップ

> **対象環境:** `C++ / DirectX 12`  
> **主目標:** リアルタイムで破綻しにくい物理ベース水面を段階的に構築する  
> **最終到達点:** ラスタライズ中心の水面表現を、最終的にレイトレーシングを含むハイブリッド水面へ発展させる  
> **このファイルの役割:** 水面オブジェクト実装の全体方針、進捗状況、着手順、各ステップ詳細への入口をまとめる

---

## 0. ドキュメント構成

このディレクトリの文書は、全体まとめとステップ別詳細に分けて管理する。

### 0-1. まとめファイル
- `README.md` : 全体方針、共通原則、進捗サマリー、更新ルール、各ステップへの入口

### 0-2. ステップ別詳細ファイル
- [Step1_GridMesh.md](./Step1_GridMesh.md) - 水面メッシュと空間基準の整備
- [Step2_UVScroll.md](./Step2_UVScroll.md) - 時間変化と位相管理
- [Step3_GerstnerWave.md](./Step3_GerstnerWave.md) - Gerstner Wave と解析法線
- [Step4_PlanarReflection.md](./Step4_PlanarReflection.md) - 反射基盤（Planar Reflection / IBL）
- [Step5_NormalMap_PBR.md](./Step5_NormalMap_PBR.md) - 表面 BRDF/BTDF と材質校正
- [Step6_SurfaceShading.md](./Step6_SurfaceShading.md) - 透過・吸収・屈折の主線
- [Step6_FFTOcean.md](./Step6_FFTOcean.md) - FFT Ocean 分岐
- [Step7_Foam.md](./Step7_Foam.md) - Foam
- [Step8_Caustics.md](./Step8_Caustics.md) - Caustics / Underwater Lighting
- [Step9_SSR.md](./Step9_SSR.md) - SSR と反射統合
- [Step10_Tuning_Debug.md](./Step10_Tuning_Debug.md) - デバッグ / 検証 / RT 最終段

### 0-3. 読み方
- 最初にこの `README.md` で全体像と現在位置を確認する
- 実装時は対象ステップの詳細文書を主参照にする
- 進捗更新は、まず各ステップ文書の `ステータス` と `作業項目` を更新し、その後この README のサマリーへ反映する

---

## 1. このロードマップの考え方

このドキュメント群は、単に「見た目がそれっぽい水」を作るためのものではない。  
**反射・透過・吸収・散乱・屈折・泡・水中光** を、リアルタイム表現として段階的に整合させていくための実装計画である。

ここで言う「物理的に正しい」は、オフラインレンダラの厳密解をそのまま再現する意味ではない。  
本プロジェクトでは以下の 3 段階で整理する。

1. **物理に反しない近似**  
   Fresnel、Beer-Lambert、深度差、反射 / 透過のエネルギー配分を崩さない
2. **リアルタイム向けの高忠実度近似**  
   SSR、簡易屈折、泡、コースティクス、FFT Ocean を統合する
3. **最終段の高忠実度実装**  
   レイトレーシングを用いた反射・屈折・水中減衰・水中影を統合する

---

## 2. 全体進捗サマリー

### 2-1. 現在の実装状況
- `WaterPlaneObject` と `WaterTestScene` を中心に、水面専用メッシュ、Gerstner Wave、Planar Reflection、PBR 材質調整、Depth Fade、screen-space 屈折、デバッグ UI の基盤は実装済み
- 水面の主線は `Water.VS.hlsl` / `Water.PS.hlsl` に集約され、ReflectionView / SceneColor / SceneDepth を使った描画経路まで接続済み
- 一方で、FFT Ocean、Foam、Caustics、SSR、RT 最終統合は未着手または設計段階に留まる
- Step ごとの状態は、設計メモではなく **現行コード確認ベース** で更新する

### 2-2. ステップ進捗一覧

| Step | タイトル | 状態 | 依存 | 役割 | 次に見る文書 |
|------|----------|------|------|------|--------------|
| 1 | 水面メッシュと空間基準の整備 | 実装完了（検証継続） | なし | 水面表現の幾何基盤 | [Step1_GridMesh.md](Step1_GridMesh.md) |
| 2 | 時間変化と位相管理 | 実装完了（検証継続） | Step 1 | 波・屈折・泡に共通する時間基盤 | [Step2_UVScroll.md](Step2_UVScroll.md) |
| 3 | Gerstner Wave と解析法線 | 実装完了（検証継続） | Step 1, Step 2 | 近景水面の手続き波面 | [Step3_GerstnerWave.md](Step3_GerstnerWave.md) |
| 4 | 反射基盤（Planar Reflection / IBL） | 実装完了（検証継続） | Step 1, Step 3 | 反射経路の土台 | [Step4_PlanarReflection.md](Step4_PlanarReflection.md) |
| 5 | 表面 BRDF/BTDF と材質校正 | 実装完了（検証継続） | Step 3, Step 4 | 水面を PBR 的に扱う基礎 | [Step5_NormalMap_PBR.md](Step5_NormalMap_PBR.md) |
| 6 | 透過・吸収・屈折の主線 | 実装中 | Step 4, Step 5 | 物理ベース水面の中心（簡易屈折まで実装） | [Step6_SurfaceShading.md](Step6_SurfaceShading.md) |
| 6B | FFT Ocean 分岐 | 未着手 | Step 3, Step 6 | 大規模海面向け発展経路 | [Step6_FFTOcean.md](Step6_FFTOcean.md) |
| 7 | Foam | 未着手 | Step 3, Step 6, Step 6B | 散逸と接触の補強 | [Step7_Foam.md](Step7_Foam.md) |
| 8 | Caustics / Underwater Lighting | 未着手 | Step 6 | 水中への光伝播表現 | [Step8_Caustics.md](Step8_Caustics.md) |
| 9 | SSR と反射統合 | 未着手 | Step 4, Step 6 | 反射品質の補完と統合 | [Step9_SSR.md](Step9_SSR.md) |
| 10 | デバッグ / 検証 / RT 最終段 | 実装中 | Step 1 ～ Step 9 | 品質保証と最終到達点整理 | [Step10_Tuning_Debug.md](Step10_Tuning_Debug.md) |

### 2-3. 推奨着手順
1. **Step 6** の残タスクである高忠実度屈折経路と透過品質の仕上げを進める
2. **Step 10** の debug 可視化と検証項目を拡充し、現行実装の観察性を上げる
3. **Step 7 ～ Step 9** で泡・水中光・SSR を追加して説得力を上げる
4. 海面スケールが必要な場合のみ **Step 6B** を並行検討する
5. 最後に RT 系到達点を段階的に接続する

---

## 3. 目指す見え方

### 3-1. 必須要件
- 真上視点では水中が見えやすい
- 斜め視点では反射が強くなる
- 浅瀬では明るく、深場では吸収が強くなる
- 水面の色が背景を二重に暗くしない
- 岸際・障害物との接点に説得力がある
- 空と近景オブジェクトの反射が分離して考えられる

### 3-2. 最終要件
- 平面反射、SSR、環境反射、必要に応じて RT 反射を統合できる
- 水面下オブジェクトが屈折・減衰・散乱の影響を受けて見える
- 波の崩れや泡、浅瀬のコースティクスまで一貫して説明できる
- 画面上の水面が「色を貼った透明板」ではなく、光学的な境界面として扱われる

---

## 4. 実装方針

### 4-1. 幾何は手続き生成を主軸にする
- 近景水面は **Gerstner Wave** を主線とする
- 海面スケールが必要な場合のみ **FFT Ocean** 分岐へ進む
- 水面専用ノーマルマップには依存しない

### 4-2. 表面と体積を分けて考える
- **表面:** Fresnel、roughness、反射、屈折方向
- **体積:** Beer-Lambert 吸収、散乱、浅瀬 / 深場の色変化

### 4-3. 反射経路は段階的に統合する
- 最初は Planar Reflection + IBL
- 次に SSR を追加して画面内反射を補う
- 最終的に RT 反射 / RT 屈折を到達点とする

### 4-4. 色は線形空間で扱う前提を崩さない
- Fresnel は反射率として扱う
- alpha は UI 都合ではなく、最終ブレンドとの整合で決める
- 背景 `SceneColor` を使う場合も、二重減衰を起こさない構成を優先する

### 4-5. 水面固有効果は発生条件を持たせる
- 泡は岸際、交差部、高エネルギー部を中心に限定する
- コースティクスは浅瀬と入射光条件を満たす場合に限定する
- 常時過剰表示するのではなく、物理寄りの条件に紐付ける

---

## 5. 現時点の優先順位

### 5-1. 第 1 段階: リアルタイムで破綻しない物理ベース基礎
- 手続き波面
- 解析法線
- Planar Reflection
- Fresnel
- Beer-Lambert 吸収
- `SceneColor` を使った透過整合

### 5-2. 第 2 段階: 高忠実度化
- 簡易屈折
- RGB 吸収
- Foam
- Caustics
- SSR
- FFT Ocean 分岐

### 5-3. 第 3 段階: 最終到達点
- RT Reflection
- RT Refraction
- 水中シャドウ / 水中減衰
- 波面と RT 経路の整合
- ハイブリッド反射の品質最適化

---

## 6. ステップ文書の更新ルール

### 6-1. 各ステップに必ず持たせる項目
- `ステータス`
- `目的`
- `このステップの作業範囲`
- `このステップで扱う責務`
- `作業項目`
- `実装時の観点`
- `期待する到達状態`
- `完了条件`
- `引き継ぎメモ`

### 6-2. 状態の使い分け
- `未着手` : 文書のみで、実装着手前
- `設計更新済み` : 実装方針とチェックリストが整理済み
- `実装中` : 一部作業着手済み
- `実装完了（検証継続）` : 主要実装は完了し、確認項目が残る
- `完了` : 目的と完了条件が満たされ、引き継ぎ可能

### 6-3. 進捗更新の流れ
1. 対象ステップ文書の `状態` を更新する
2. `作業項目` のチェックを更新する
3. 必要なら `実施結果` / `残タスク整理` を追記する
4. 最後に README の進捗一覧へ反映する

---

## 7. 非目標

以下は主線より後ろに置く。

- 物理整合を無視した演出優先の色付け
- 水面専用ノーマルマップ前提の表現設計
- 近景品質を犠牲にしたまま大規模海面だけを先行すること

---

## 8. 参考理論

| リソース | 用途 |
|---------|------|
| Real-Time Rendering 4th Edition | BRDF / BTDF / Fresnel / SSR / volume の整理 |
| Physically Based Rendering: From Theory to Implementation | 表面反射・透過・体積の基礎 |
| Tessendorf 2001 | FFT Ocean と海洋スペクトル理論 |
| GPU Gems / GPU Pro 系記事 | リアルタイム水面の実装近似 |
| DirectX 12 公式ドキュメント | 実装基盤 |

---

## 9. 最終到達点

このロードマップの最高地点は、

- **ラスタライズで構築した水面基盤**
- **SSR / Planar / IBL のハイブリッド反射**
- **レイトレーシングによる反射・屈折・水中減衰**

を統合した **ハイブリッド物理ベース水面** である。

レイトレーシングは単なる追加機能ではなく、
**画面外反射・正しい屈折経路・水中シャドウ・複雑な反射経路** を扱うための最終段として位置付ける。

---

*最終更新: 2026年6月*
