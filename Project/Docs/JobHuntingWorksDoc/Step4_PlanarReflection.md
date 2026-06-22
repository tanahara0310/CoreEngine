# Step 4 : 反射基盤（Planar Reflection / IBL）

## ステータス
- 状態: 実装完了（検証継続）
- 優先度: 高
- 依存ステップ: Step 1, Step 3
- 現在位置: ReflectionView、反射 RTT、clip plane、IBL フォールバックまで接続済み
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
- `WaterReflectionPass` が反射カメラのセットアップ、clip plane 設定、カメラ復元を担当している
- `WaterPlaneObject::ApplyWaterReflectionResult()` が反射 RTT、SceneDepth、SceneColor の SRV を受け取る
- `Water.PS.hlsl` は反射 RTT があるとき `gReflectionTexture` を加算し、無いときは IBL のみで成立する構造になっている

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

---

*[(← Step 3 Gerstner Wave と解析法線)](Step3_GerstnerWave.md) | [次: Step 5 表面 BRDF/BTDF と材質校正 →](Step5_NormalMap_PBR.md)*