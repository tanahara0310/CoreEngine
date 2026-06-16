# Step 9 : SSR と反射統合

> **状態:** 設計更新済み  
> **目的:** Planar Reflection と IBL だけでは拾い切れない画面内反射を補完し、反射経路をハイブリッド化する  
> **位置付け:** RT 導入前の最終反射強化ステップ

---

## このステップの役割

水面反射は最終的に複数経路の統合で考える。

- Planar Reflection
- IBL
- SSR
- 将来的な RT Reflection

Step 9 では、RT 導入前に SSR を組み込み、画面内オブジェクトの反射欠損を埋める。

---

## 目標

- 法線と視線から SSR レイを生成する
- 深度との交差でヒット点を求める
- 失敗時は Planar / IBL へフォールバックする
- 反射ソースの優先順位を整理する

---

## 物理ベース観点

### 1. SSR は近似であり主役ではない
SSR は画面外情報を持たず、反射の厳密解ではない。  
そのため主反射ではなく補助反射として扱う。

### 2. Planar Reflection を壊さない
水面が平面に近い限り、Planar Reflection の方が正確である。  
SSR は不足分の補完に限定する。

### 3. RT 反射導入後は役割を再定義する
最終段では、SSR は RT の前処理・近距離補完・低コスト fallback に再配置できる。

---

## 実装要素

| 要素 | 内容 |
|------|------|
| SSR ray marching | 画面空間反射探索 |
| Depth intersection | 深度との交差判定 |
| Hit refinement | 必要に応じた精密化 |
| Reflection source blending | Planar / IBL / SSR 統合 |
| Failure fallback | 欠損時の復帰 |

---

## 完了条件

- [ ] 近接オブジェクトの反射密度が上がる
- [ ] 画面端や欠損時の破綻が制御されている
- [ ] Planar Reflection の優位性を壊さない
- [ ] 将来的な RT 反射統合へ接続できる

---

*[(← Step 8 Caustics / Underwater Lighting)](Step8_Caustics.md) | [次: Step 10 デバッグ / 検証 / RT 最終段 →](Step10_Tuning_Debug.md)*
