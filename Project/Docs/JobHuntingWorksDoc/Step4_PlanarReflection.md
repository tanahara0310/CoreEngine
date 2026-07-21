# Step 4 : 反射基盤（Planar Reflection / IBL）

## ステータス
- 状態: 実装完了（検証継続）
- 優先度: 高
- 依存ステップ: Step 1, Step 3
- 現在位置: ReflectionView、反射 RTT、oblique clipping、自己反射防止、グロッシー反射、雲キューブマップ IBL 上書き、サングリッターまで接続済み
- 完了後に着手しやすい次ステップ: Step 5, Step 6, Step 9

## 目的
水面の反射経路を整え、近景反射を Planar Reflection、環境反射を IBL として分担させ、後続の Fresnel 配分と SSR / RT 統合へ接続できる形にする。

## このステップの作業範囲
- 反射 RTT の生成と更新
- 鏡像カメラの組み立て
- クリップ平面による不要反射の抑制
- IBL を使ったフォールバック経路
- 反射ソースの役割分担の整理

## このステップで扱う責務
- 水面近傍の反射取得経路を定義する
- 反射 RTT を水面シェーダーから参照可能にする
- IBL と Planar Reflection の責務境界を決める
- 将来の SSR / RT 反射統合に備えた接続点を作る

## このステップの役割
水面は鏡のような表面反射を持つが、リアルタイムでは反射経路を分担して扱う必要がある。

- **Planar Reflection**: 水面近傍の正確な鏡像反射
- **IBL**: 空や遠景の環境反射
- **SSR / RT**: 後続で追加する補完経路

Step 4 の目的は、これらの基礎となる **Planar Reflection + IBL** を成立させることにある。

## 作業項目
- [x] 水面基準の鏡像カメラを設計する
- [x] Reflection RTT の生成 / 更新方針を決める
- [x] 水面シェーダーから RTT を参照できるようにする
- [x] IBL を反射フォールバックとして位置付ける
- [x] クリップ平面で不要な反射を抑制する

## 実施結果
- `WaterTestScene::BuildRenderViewRequests()` で ReflectionView を engine 側へ要求する構成へ移行した
- `WaterReflectionPass` が反射カメラのセットアップ、clip 設定、カメラ復元を担当している。ミラービューの適用は `ICamera::BeginViewOverride / EndViewOverride`（ビュー行列・視点・射影の一時差し替え API）経由で行い、DebugCamera を含む全カメラ型で機能する
- 水面下ジオメトリの反射除外は clip plane から **oblique near-plane clipping**（`CalcObliqueClippedProjection`。射影行列の near 面を水面平面へ傾ける方式）へ移行した。SkyBox は `z=w` 描画のためクリップの影響を受けない
- **反射ビューでは水面自身を描かない**（`WaterSurfacePass::IsEnabledForView` で GameView 限定）。これを怠ると波で変位した水面がクリップ面をまたいで反射 RTT に焼き付き、「波と一緒に動く巨大な明暗斑」になる（夜間に顕在化した実バグ。露出打ち消し付きの反射テクスチャ可視化で特定した）
- `WaterPlaneObject::ApplyWaterReflectionResult()` が反射 RTT、SceneDepth、SceneColor の SRV を受け取る
- `Water.PS.hlsl` の反射合成は**置き換え**（`reflectColor = 平面反射`）で行う。PBR 出力への加算は空・太陽の二重計上で白飛びするため行わない
- 反射品質のための追加実装:
  - `SampleGlossyReflection()`: 平面反射の複数タップぼかし（かすめ角ほどぼけを強める）
  - `ReflectionGeometricOcclusion()`: Schlick-GGX 視線項でかすめ角の反射スパイクを抑制し、空（明）と水（暗）のハードな明暗差を縮小
  - 反射 UV を波法線の水平成分で歪ませる（フラット鏡像の明滅を防ぎ「砕けた反射」にする）
  - フレネル反射率は鉛直へ弱くブレンドした低周波法線で評価（未解像の細かい斜面でフレネルが暴れて斑になるのを防止）
  - 反射色の高輝度ショルダー圧縮（夜間の自動露出 +8EV による白飛び端点を丸める。昼の空・きらめきは無圧縮域で保存）
- 環境反射は静的 IBL から**動的な空＋雲キューブマップ（スペキュラ IBL）**へ移行した。平面反射に映らない頭上の雲は `lerp(平面反射, 雲キューブマップ, 雲被覆)` で雲の部分だけ上書きし、反射方向が水平線に近い場合はフェードアウトする（球殻雲層を横切る長いレイのため水平線付近は常に被覆 ≈1 になる）。雲のサンプル方向は波法線ではなくフラット法線で計算する（平面反射との幾何整合）
- 反射有効時は鏡像の太陽がぼかしで消えるため、太陽ハイライトは `ComputeSunGlintSpecular`（ディテール法線＋Cook-Torrance、F0=0.02）で解析的に加算する
- ReflectionView は大気散乱の空（昼夜・月・星空）を SkyBoxQueuePass 経由で描くため、空の映り込みは常に本物と一致する

## 物理ベース観点
### 1. 反射は単独で存在しない
水面の見え方は、
- 反射
- 透過
- 吸収
の配分で決まる。

Step 4 ではまだ透過と吸収を本格導入しないが、
**反射が最終的に Fresnel 配分へ組み込まれること** を前提に設計する。

### 2. IBL は空と遠景の近似経路
IBL 自体は重要だが、近景オブジェクトまで IBL のみで済ませるのは不十分である。  
そのため Planar Reflection を主経路とし、IBL は補助に置く。

### 3. Planar Reflection は RT への前段
水面表現では Planar Reflection が非常に有効であり、最終的には RT 反射統合へ発展できる。  
本段階では Planar / IBL の責務分離を優先する。

## 実装要素
| 要素 | 内容 |
|------|------|
| Reflection RTT | 反射結果を保持するオフスクリーンターゲット |
| Mirror camera | 水面基準の鏡像カメラ |
| Clip plane | 水面下の不要反射除去 |
| Fresnel-ready reflection path | 後続の反射配分へ接続可能な経路 |
| IBL fallback | RTT 不在時の環境反射 |

## 実装時の観点
- Reflection RTT は解像度や更新頻度を調整できる構造にする
- Planar Reflection と IBL の寄与を可視化できると後続検証が楽になる
- クリップ平面の扱いは水面高さ基準と一致させる

## 期待する到達状態
- 水面近傍のオブジェクト反射を取得できる
- 反射 RTT が使えない場合でも IBL で環境反射を維持できる
- Step 5 の Fresnel / roughness 整理へ自然に接続できる

## 完了条件
- [x] 水面反射 RTT が生成・更新される
- [x] 水面に近景反射が乗る
- [x] RTT が使えない場合でも IBL で環境反射を補える
- [x] クリップ平面により明らかな破綻を抑制できる
- [x] 後続の Fresnel 配分へ接続できる

## 引き継ぎメモ
- Step 5 ではこの反射経路を PBR 上の反射ソースとして整理する
- Step 9 では SSR を追加し、反射ソースの優先順位を再定義する
- 既知の未解決点: 反射ビューはポストエフェクト無効（生 HDR）で描かれるため、透過側と輝度ドメインが揃っていない。現状はショルダー圧縮による対症で吸収しているが、正道は「反射ビューへ GameView と同じ露出を適用する」こと
- 反射ビューで同一モデルを 2 回描くため、Model の prevWVP がビュー間で汚染される既知の副作用がある（モーションベクター誤り・別タスク管理）

---

*[(← Step 3 Gerstner Wave と解析法線)](Step3_GerstnerWave.md) | [次: Step 5 表面 BRDF/BTDF と材質校正 →](Step5_NormalMap_PBR.md)*