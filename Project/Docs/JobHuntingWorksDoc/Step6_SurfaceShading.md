# Step 6 : 透過・吸収・屈折の主線

## ステータス
- 状態: 実装完了（検証継続）
- 優先度: 最優先
- 依存ステップ: Step 4, Step 5
- 現在位置: 水色は shallow / deep の手動色指定を全廃し、波長依存 σa / σs（RGB）の Beer-Lambert 透過＋単一散乱による物理ベース水色へ移行済み。インスキャッタ光は太陽（大気透過率連動）＋天空光（Sky SH）から導出し、Jerlov 水質プリセット・濁度 UI まで接続済み。DXR 屈折は実測光路長の吸収利用とフォールバック境界のフェザリングまで実装済み
- 完了後に着手しやすい次ステップ: Step 7, Step 9

## 目的
水面を表面反射だけでなく、透過・吸収・屈折を持つ光学境界面として扱い、水中の見え方を物理寄りに整える。

## このステップの作業範囲
- SceneColor / 背景光の透過
- Beer-Lambert による吸収
- 浅瀬 / 深場の色遷移
- Fresnel による反射・透過配分
- DXR ベース屈折経路
- DXR 屈折経路の設計と接続点整理
- 最終ブレンドとの整合

## このステップで扱う責務
- 水中がどう見えるかを定義する
- 反射と透過のエネルギー配分を整理する
- 深さ依存の吸収と色変化を扱う
- レイを飛ばして屈折先を求める経路を定義する
- DXR 屈折を既存ラスタライズ水面へ統合できる構造を用意する

## このステップの役割
Step 5 までで水面の表面反射基盤は整う。  
Step 6 では、**水中がどう見えるか** を決める。

対象は以下。

- SceneColor / 背景光の透過
- Beer-Lambert による吸収
- 浅瀬 / 深場の色変化
- Fresnel による反射・透過の配分
- 屈折レイ方向とレイ交差先の取得
- alpha と最終ブレンドの整合

## 作業項目
- [x] Fresnel を反射率として扱う
- [x] Beer-Lambert 則で透過光の減衰を扱う
- [x] 水深差から浅瀬 / 深場の色遷移を決める
- [x] 背景を二重減衰させない透過合成にする
- [x] Snell の法則に基づく屈折レイ生成を定義する
- [x] DXR の RayGen / ClosestHit / Miss で屈折経路を設計する
- [x] TLAS 交差結果から水中背景取得の最小経路を行う
- [x] 既存 `Water.PS.hlsl` と RT 屈折結果の合成責務を分離する
- [x] 吸収を RGB（波長依存 σa）へ拡張する
- [x] 単一散乱インスキャッタ（σs）を解析解で導入し、水色を光学特性×光源から導出する
- [x] インスキャッタ光源を太陽（大気透過率連動）＋天空光（Sky SH）へ接続する
- [x] Jerlov 水型プリセットと濁度による水質調整 UI を用意する

## 実施結果
- `Water.PS.hlsl` が `gSceneColor` と `gSceneDepth` を参照し、水面越しの透過と深度差を扱う構成になった
- Beer-Lambert に基づく `transmittance` / `absorption` を計算し、浅瀬 / 深場の色補間へ接続している
- Fresnel は Schlick 近似で実装され、反射寄与と透過寄与の配分に利用されている
- 標準 alpha ブレンドで背景の二重減衰を避ける合成が組まれている
- `RTWaterRefractionPass` と `WaterRefractionRayTracingManager` がフレーム実行系へ組み込まれ、DXR 出力は `FrameBlackboard::RTWaterRefractionColor` 経由で水面描画へ供給される
- `RTWaterRefraction.hlsl` では波面オフセットと解析法線を再評価し、`refract(primaryDir, waterNormal, gRefractionEta)` で屈折方向を求めて TLAS へ `TraceRay` している
- RT ヒット結果は再投影して `SceneColor` をサンプリングする最小構成で実装され、成功時は `Water.PS.hlsl` の透過色として採用される
- RT 失敗時は理由コード付きで `SceneColor` フォールバックへ戻す構成になっており、`Water.PS.hlsl` と ImGui から理由別の可視化が可能になっている
- Depth Fade と RT 屈折のデバッグ表示は ImGui から切り替え可能で、RT 屈折色、理由、比較、透過光、波長別透過率、反射率、成功マスクまで確認できる

### 物理ベース水色（2026-07 移行）
- `WaterFrameConstants` を shallowColor / deepColor から **float3 σa（absorptionCoeff）＋ float3 σs（scatteringCoeff）** へ変更した。API も `SetWaterColors` → `SetWaterOpticalCoefficients(σa, σs)` へ改名
- `Water.PS.hlsl` の `ComputeWaterVolumetricColor()` が、波長別 `exp(-σt·d)` の透過と、均質媒質の単一散乱解析解 `(σs/σt)·L·(1-exp(-σt·d))` を合成する。浅瀬エメラルド → 深場青の遷移は係数から自然に出る
- インスキャッタ環境光 `ComputeUnderwaterAmbientLight()` は、太陽の下向き放射照度（大気の Transmittance がライト色へ乗算済み）＋ 天空光 SH（`SkyIrradianceSH.CS` 出力を t24 でバインド。大気非アクティブ時は IBL キューブマップへフォールバック）で構成する。日没の赤方偏移へ自動追従する
- 太陽光の水中への下り減衰は `ComputeSunDownwellingTransmittance()`。太陽の水中天頂角はスネル則（臨界角 48.6° 制限）で求め、鉛直水深 × 経路補正で透過項のみに乗算する
- 背景が far plane の場合は光路長 1e4 m（無限水柱）として水固有色へ収束させる
- 水質 UI: Jerlov 水型プリセット 7 種（外洋 I〜III / 沿岸 1C・5C・9C）＋濁度スライダー 0..1（実効 σ = ベース + 濁度 × ゲイン。吸収ゲインは CDOM の青吸収、散乱ゲインは懸濁粒子）。検証用の「海底を白砂色にする」チェックもある
- 透明側の合成端点は生の屈折色ではなく Beer-Lambert・フレネルを通した `transmissionColor` に統一した（生シーン色の常時混入で水底が斑に透けるバグを修正済み）

## 残タスク整理
- 屈折出力は現状 `float4(color, reasonCode)` の最小構成であり、hit distance、透過率、水柱長などの補助情報は別出力化されていない
- RT ヒット後の背景取得は `SceneColor` 再投影ベースで、ヒット材質そのものの評価や多段媒質通過は未対応
- DXR miss / clip fail / 平面交差失敗の理由別フォールバックはあるが、統計的な成功率や品質計測はまだ不足している
- `Water.PS.hlsl` の near / far は 0.1 / 1000 のハードコードのまま（既知の課題）
- 落とし穴: `WaterFrameConstants` の cbuffer レイアウトは **Water.PS.hlsl / Water.VS.hlsl / FFTWater.VS.hlsl / WaterSurfaceTypes.h の 4 箇所**で一致必須。変更時は全部更新する

## DXR 屈折の方針
### 1. 水面シェーディングと交差探索を分離する
- `Water.PS.hlsl` は最終的な Fresnel 配分・吸収・合成の責務を持つ
- 交差探索そのものは DXR パスへ分離し、`RefractionColor` / `RefractionTransmittance` / `RefractionHitDistance` のような結果テクスチャとして返す

### 2. 入射視線と法線から屈折方向を決める
- 視線ベクトル `V` と水面法線 `N`、屈折率比 `eta = etaAir / etaWater` を使う
- HLSL では `refract(-V, N, eta)` 相当で水中方向を求める
- 全反射が起きる場合は屈折 0 とし、反射側へエネルギーを寄せる

### 3. DXR で画面外を含む背景交差を求める
- 水面ピクセルのワールド位置をレイ原点とし、法線オフセット後に屈折方向へ `TraceRay` する
- TLAS と交差した最初の不透明面を水中可視背景として扱う
- 交差距離から水柱長を求め、Beer-Lambert 吸収へ接続する

### 4. ClosestHit では「何を返すか」を最小化する
- 第一段階では ClosestHit で完全な材質評価を行わず、ヒット位置・法線・距離・可視フラグ中心のペイロードを返す
- 返却後に既存の GBuffer / SceneColor / 簡易水中色評価と合成する構成にすると、既存パイプラインへの侵襲を抑えやすい
- 将来段階で必要なら RT 側で直接背景材質評価へ拡張する

### 5. Miss と無効ヒットは安全側へ倒す
- レイが何にも当たらない場合は `SceneColor` 非屈折透過、環境色、または深場色寄りフォールバックへ戻す
- DXR 無効環境では既存透過のみで成立するようにする

## DXR 実装アルゴリズム
### 1. 入力の準備
- GBuffer または水面描画入力から `worldPos`、`surfaceNormal`、`viewDir`、`linearDepth` を得る
- 水の屈折率は固定値 `etaWater ≈ 1.333` から開始する
- 波面法線は Gerstner Wave 解析法線を優先し、法線マップ依存にしない

### 2. レイ生成
- RayGen で水面ピクセルごとに 1 ray 発行する
- `rayOrigin = worldPos + surfaceNormal * epsilon`
- `rayDirection = refract(-viewDir, surfaceNormal, etaAir / etaWater)`
- `TMin` は自己交差回避用の微小値、`TMax` は水中可視距離またはシーン far に合わせる

### 3. 交差判定
- TLAS には少なくとも不透明ジオメトリを載せる
- 水面自身は自己ヒット回避のため instance mask 分離または hit group 側で除外する
- 必要に応じて透明オブジェクトは初期段階では対象外にする

### 4. ヒット後評価
- 現行実装では `hitWorldPos` をスクリーン再投影し、`SceneColor` を読むハイブリッド構成を採用している
- `gMaxRefractionOffsetPixels` でスクリーン上の UV オフセット量を制限し、極端な跳びを抑えている
- 将来的な拡張として、`hitDistance` 由来の水柱長やヒット材質そのものの評価を追加する余地を残している

### 5. 水面最終合成
- `F = FresnelSchlick(...)`
- `finalColor = reflectionColor * F + refractionColor * (1 - F)`
- 屈折色側に shallow / deep tint と吸収を乗せ、既存の水面色設計を維持する

## エンジンへの組み込み手順
### 1. 専用 DXR マネージャを追加する
- 現行コードでは `WaterRefractionRayTracingManager` と `RTWaterRefractionPass` が追加済みで、シャドウ経路とは分離して管理されている
- 理由は、入力も出力もシャドウと責務が異なり、将来的なデノイズや履歴管理も別設計になるため

### 2. DXR パイプラインを構築する
- 既存 `RayTracingPipelineBuilder` を使って RayGen / Miss / ClosestHit を持つ state object を作る
- 既存 `GlobalRootSignatureManager` の拡張、または水面屈折専用 root signature を用意する

### 3. 入出力リソースを定義する
- 現行入力は TLAS、GBuffer の world position、`SceneColor`、水面波データ、カメラ位置、view-projection 行列で構成している
- 現行出力は `RTWaterRefractionColor` 1 枚で、RGB に屈折色、A に理由コードを格納する
- 将来的には `RefractionInfo` を追加し、hit distance、透過率、ヒット有無などを分離して保持できる構成へ拡張する

### 4. フレーム実行順を決める
- 現行コードでは DXR 屈折パスの出力を `FrameBlackboard::RTWaterRefractionColor` へ格納し、後段の `Water.PS.hlsl` が参照する
- その後 `Water.PS.hlsl` で RT 出力を既存の反射・透過経路と合成する

### 5. デバッグ可視化を追加する
- hit / miss mask
- hit distance
- transmittance
- DXR refraction color
- fallback rate

## SSR を主線にしない理由
- SSR は画面外情報を持たないため、水面屈折では岸や大型オブジェクトの欠損が顕著になる
- 屈折は反射以上に画面外背景へ依存しやすく、screen-space 前提だと破綻が目立つ
- そのため本件では SSR を補助にも使わず、DXR を主線として設計する

## 物理ベース観点
### 1. 水面は透明板ではなく媒質境界
見るべきなのは「青い半透明板」ではない。  
空気と水の境界で起こる
- 反射
- 透過
- 吸収
を分けて扱う必要がある。

### 2. Fresnel は alpha の代用品ではない
Fresnel は本来、反射率と透過率の配分に効く。  
そのため、単純に alpha を増減させる用途へ流用しない。

### 3. Beer-Lambert は体積吸収の近似
- 水柱長が長いほど透過光が減衰する
- 深い場所ほど青・緑寄りに残る
- 現段階ではまずスカラー吸収、その後 RGB 吸収へ拡張する

### 4. 屈折は次段の高忠実度化への橋渡し
この段階では **DXR による屈折レイ追跡** を採用する。  
画面内情報だけに依存せず、TLAS 交差から水中背景を求める構造を主線とする。

## 実装要素
| 要素 | 内容 |
|------|------|
| SceneColor transmission | 背景透過の基礎 |
| Beer-Lambert absorption | 水柱長による光減衰 |
| Shallow / deep water tint | 深さ依存の色遷移 |
| Fresnel energy split | 反射 / 透過配分 |
| Refraction ray | Snell の法則から求める屈折レイ |
| DXR hit test | TLAS と交差させて屈折先を求める主経路 |
| Refraction outputs | Water.PS へ返す屈折色・距離・有効フラグ |
| Blend consistency | 最終ブレンドとの整合 |

## この段階で避けるべき破綻
- SceneColor の二重合成
- Fresnel を alpha へ直接流用すること
- 背景が角度で不自然に暗くなりすぎること
- 深さ表現が単なる色 lerp に退化すること
- 水面自身への自己ヒットで白飛びやノイズが出ること
- DXR miss 時に真っ黒へ落ちること

## 実装時の観点
- SceneColor の読み取りと最終合成順を明確にする
- 深度差の取り方が水面高さ基準と一致しているか確認する
- レイ進行方向は水面法線と視線の両方に依存するため、法線再構成精度が重要になる
- DXR レイは自己交差、instance mask、payload サイズ、出力 UAV 帯域を初期段階から意識する
- 反射成分と透過成分の寄与を可視化できるようにすると後続検証が楽になる

## 期待する到達状態
- 真上では水中が見えやすく、斜めでは反射が強くなる
- 深場ほど透過が弱くなり、浅瀬では底や地形が読みやすい
- 将来的な RGB 吸収や水中散乱へ無理なく発展できる
- 屈折が screen-space UV ずらしではなく、DXR 交差に基づく背景取得として説明できる

## 完了条件
- [x] 真上視点では水中が見えやすい
- [x] 斜め視点では反射が優勢になる
- [x] 深場ほど透過が弱くなる
- [x] 背景が二重減衰しない
- [x] DXR で屈折交差先を取得できる
- [x] RGB 吸収 / 水中散乱（単一散乱）が実装され、水色が光学特性×光源から導出される

## 引き継ぎメモ
- Step 7 ではこの透過・反射基盤を壊さずに泡を重ねる必要がある
- Step 8 ではここで整えた吸収と水深差を水中光へ接続する（接続済み。RT コースティクスは同じ太陽情報源を参照）
- Step 9 では反射経路側を補完して反射品質を高める
- DXR 屈折の次段は hit 後情報の増量、品質計測、debug view の定量化。RT の実測光路長（`DecodeRTOpticalPath`、メートル単位）はそのまま σ 系へ流用できる

---

*[(← Step 5 表面 BRDF/BTDF と材質校正)](Step5_NormalMap_PBR.md) | [次: Step 6B FFT Ocean 分岐 →](Step6_FFTOcean.md)*
