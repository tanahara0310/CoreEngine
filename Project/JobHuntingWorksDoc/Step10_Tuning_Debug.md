# Step 10 : 調整 UI / デバッグ / プリセット整理

> **状態:** ⬜ 未着手  
> **目的:** 水面表現を調整しやすくし、品質確認と再利用をしやすい形に整える  
> **位置付け:** 仕上げステップ。見た目を壊さず改善できる運用基盤を整える

---

## このステップで行うこと

1. **ImGui 項目整理** — 似たパラメータをまとめる
2. **デバッグ表示追加** — 波高・法線・Fresnel・Depth Fade・Foam・Caustics・SSR を見える化する
3. **プリセット整理** — Lake / Pool / Rain などの役割差を明確にする
4. **チェックリスト整備** — 品質確認の観点をドキュメント化する

---

## 1. ImGui 項目整理

例:

- Surface
  - Roughness
  - BaseColor
  - Normal Strength
- Reflection
  - Fresnel
  - Planar Reflection Intensity
  - SSR Enable
- Depth
  - Absorption
  - Shallow / Deep Color
- Effects
  - Foam
  - Caustics

---

## 2. デバッグ表示

最低限ほしい表示は以下。

- 波高
- 法線
- Fresnel 係数
- Depth Fade
- 反射テクスチャ
- Foam マスク
- Caustics マスク
- SSR ヒット表示

---

## 3. プリセット整理

水面品質向けの代表プリセット。

| プリセット | 特徴 |
|-----------|------|
| Lake | 静かで反射が立つ |
| Pool | 透明感が強い |
| Rain | 小面積で濡れ感重視 |
| Pond | やや濁りがあり浅瀬が見える |

---

## 確認ポイント

- [ ] 誰が見ても調整場所が分かる UI になっている
- [ ] 各エフェクトを個別に可視化できる
- [ ] プリセットごとの差が説明できる
- [ ] 品質確認チェックリストが揃っている

---

*[← Step 9 SSR](Step9_SSR.md) | [README に戻る →](README.md)*
