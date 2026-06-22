# Step 9 : SSR と反射統合

## ステータス
- 状態: 未着手
- 優先度: 中
- 依存ステップ: Step 4, Step 6
- 現在位置: 設計のみ。現行コードに screen-space ray marching や hit refinement は未実装
- 完了後に着手しやすい次ステップ: Step 10

## 目的
Planar Reflection と IBL だけでは拾い切れない画面内反射を SSR で補完し、反射経路をハイブリッド化する。

## このステップの作業範囲
- SSR レイ生成
- depth 交差判定
- hit refinement
- 失敗時フォールバック
- Planar / IBL / SSR の優先順位整理

## このステップで扱う責務
- 画面内反射の欠損を補う
- SSR を補助反射として位置付ける
- 既存 Planar Reflection の優位性を保つ
- 将来 RT 反射を追加する際の統合順を整理する

## このステップの役割
水面反射は最終的に複数経路の統合で考える。

- Planar Reflection
- IBL
- SSR
- 将来的な RT Reflection

Step 9 では、RT 導入前に SSR を組み込み、画面内オブジェクトの反射欠損を埋める。

## 作業項目
- [ ] 法線と視線から SSR レイを生成する
- [ ] 深度との交差でヒット点を求める
- [ ] 失敗時は Planar / IBL へフォールバックする
- [ ] 反射ソースの優先順位を整理する
- [ ] 将来 RT 反射を追加した際の配置方針を明文化する

## 現状メモ
- `Water.PS.hlsl` の反射経路は現状 Planar Reflection RTT と IBL の組み合わせのみ
- SSR hit mask、ray marching、画面端フォールバックなどの処理は未実装
- Step 10 の debug view に `Reflection` はあるが、SSR 個別可視化はまだ存在しない

## 物理ベース観点
### 1. SSR は近似であり主役ではない
SSR は画面外情報を持たず、反射の厳密解ではない。  
そのため主反射ではなく補助反射として扱う。

### 2. Planar Reflection を壊さない
水面が平面に近い限り、Planar Reflection の方が正確である。  
SSR は不足分の補完に限定する。

### 3. RT 反射導入後は役割を再定義する
最終段では、SSR は RT の前処理・近距離補完・低コスト fallback に再配置できる。

## 実装要素
| 要素 | 内容 |
|------|------|
| SSR ray marching | 画面空間反射探索 |
| Depth intersection | 深度との交差判定 |
| Hit refinement | 必要に応じた精密化 |
| Reflection source blending | Planar / IBL / SSR 統合 |
| Failure fallback | 欠損時の復帰 |

## 実装時の観点
- SSR の寄与率を debug 表示できると調整しやすい
- Planar Reflection が存在する場合の優先順位を明示的に決める
- 画面端欠損や不連続ノイズをフォールバック側で吸収する

## 期待する到達状態
- 近接オブジェクトの反射欠損が減る
- Planar Reflection を主軸にしつつ画面内情報を補完できる
- RT 反射統合前のハイブリッド反射基盤になる

## 完了条件
- [ ] 近接オブジェクトの反射密度が上がる
- [ ] 画面端や欠損時の破綻が制御されている
- [ ] Planar Reflection の優位性を壊さない
- [ ] 将来的な RT 反射統合へ接続できる

## 引き継ぎメモ
- Step 10 では reflection source の内訳を必ず可視化したい
- 最終的な RT 統合時は、SSR を主反射ではなく補完層として再配置する

---

*[(← Step 8 Caustics / Underwater Lighting)](Step8_Caustics.md) | [次: Step 10 デバッグ / 検証 / RT 最終段 →](Step10_Tuning_Debug.md)*
