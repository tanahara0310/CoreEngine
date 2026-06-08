# リアルな水面表現 — 実装ロードマップ

> **対象環境:** C++ / DirectX 12  
> **方針:** 動くだけの最小実装から始め、段階的に水面品質を高める  
> **目標:** 海の大規模表現ではなく、近景で破綻しにくい水面品質を段階的に仕上げる

---

## 全体フロー

| Step | タイトル | 状態 | 詳細ドキュメント |
|------|----------|:----:|----------------|
| 1 | グリッドメッシュの生成と表示 | ✅ 完了 | [Step1_GridMesh.md](Step1_GridMesh.md) |
| 2 | UV スクロール（流れるだけの水） | ✅ 完了 | [Step2_UVScroll.md](Step2_UVScroll.md) |
| 3 | Gerstner Wave（頂点変位による波） | ✅ 完了 | [Step3_GerstnerWave.md](Step3_GerstnerWave.md) |
| 4 | Planar Reflection + Fresnel（反射） | ✅ 完了 | [Step4_PlanarReflection.md](Step4_PlanarReflection.md) |
| 5 | Normal Map + PBR ライティング | 🔄 進行中 | [Step5_NormalMap_PBR.md](Step5_NormalMap_PBR.md) |
| 6 | 法線再整合 + Fresnel + Depth Fade + 浅瀬色 | ⬜ 未着手 | [Step6_SurfaceShading.md](Step6_SurfaceShading.md) |
| 7 | Foam（岸際の泡） | ⬜ 未着手 | [Step7_Foam.md](Step7_Foam.md) |
| 8 | Caustics（浅瀬の揺れる光） | ⬜ 未着手 | [Step8_Caustics.md](Step8_Caustics.md) |
| 9 | SSR（補助反射） | ⬜ 未着手 | [Step9_SSR.md](Step9_SSR.md) |
| 10 | 調整 UI / デバッグ / プリセット整理 | ⬜ 未着手 | [Step10_Tuning_Debug.md](Step10_Tuning_Debug.md) |

---

## 現在地と次にやるべきこと

### 完了済み
- 平面メッシュ（64×64 分割）の生成・表示
- UV スクロール + タイリング
- Gerstner Wave による頂点変位（多波重ね合わせ）
- Planar Reflection（平面反射 RTT）+ Schlick Fresnel ブレンド
- Water 専用シェーダー（`Water.VS.hlsl` / `Water.PS.hlsl`）
- IBL フォールバック（反射 RTT 未設定時は `gPrefilteredMap` を使用）
- ImGui からの全パラメータ動的変更（ベースカラー / Roughness / Metallic / IBL / UV / 波 / テクスチャモード）
- テクスチャなし / ノーマルマップのみ / アルベド+ノーマル の3モード切り替え
- Depth Fade / 吸収係数 / 浅瀬色 / 深場色の基礎実装

### Step 5 で行うこと（次のステップ）
Step 4 までで「動いて反射する水」は完成した。  
Step 5 では **近景の水面品質** に踏み込む。具体的には以下を整備する：

1. **ノーマルマップの二重スクロール** — 方向・速度の異なる 2 枚のノーマルマップを重ね合わせ、表面の細かい揺らぎをより自然に見せる
2. **PBR パラメータの微調整** — Roughness / Metallic / IBL 強度を用途別に整理し、水らしい反射バランスを作る
3. **ノーマル強度と波表面の密度感調整** — 近景でプラスチック状に見えないように整える
4. **品質確認観点の整理** — 水面品質の確認ポイントを Step ごとに残せる状態にする

詳細 → [Step5_NormalMap_PBR.md](Step5_NormalMap_PBR.md)

### Step 6 以降の方針

- **Step 6** で法線再整合・Fresnel の見直し・Depth Fade・浅瀬/深場色をまとめて整備する
- **Step 7** で岸際フォームを追加し、水際の切れ目感を減らす
- **Step 8** で浅瀬限定の Caustics を追加する
- **Step 9** で SSR を補助反射として追加する
- **Step 10** で ImGui / デバッグ表示 / プリセット整理を行う

### 非目標

今回の主目標には以下を含めない。

- FFT Ocean のような海専用の大規模波シミュレーション
- 外洋の遠景スケール表現
- 荒天時の高波や砕波主体の海演出

---

## アーキテクチャ概要

```
WaterTestScene
  ├─ WaterPlaneObject          // GameObject: 水面メッシュ + 波CB + フレームCB
  │    ├─ Water.VS.hlsl        // 頂点シェーダー: Gerstner Wave 変位 + SV_ClipDistance
  │    └─ Water.PS.hlsl        // ピクセルシェーダー: PBR + Fresnel + 反射/IBL
  └─ WaterReflectionPass       // 反射パス: 鏡像カメラ → オフスクリーン RTT
```

| クラス / ファイル | 役割 |
|-----------------|------|
| `WaterPlaneObject` | 水面専用 GameObject。波・UV・反射・マテリアルを統合管理 |
| `WaterConstantBuffer.h` | `WaveParams` / `WaterConstants` / `WaterFrameConstants` の HLSL 対応 C++ 定義 |
| `WaterReflectionPass` | 平面反射用オフスクリーン RTT の生成・描画を管理 |
| `Water.VS.hlsl` | Gerstner Wave 頂点変位、SV_ClipDistance0 によるクリップ制御 |
| `Water.PS.hlsl` | PBR フォワード + Schlick Fresnel + Planar Reflection / IBL フォールバック |

---

## 実装スケジュール目安

```
Week 1    : Step 1（メッシュ生成）+ Step 2（UV スクロール）  ✅
Week 2    : Step 3（Gerstner Wave）                         ✅
Week 3    : Step 4（Planar Reflection + Fresnel）            ✅
Week 4    : Step 5（Normal Map + PBR 品質向上）              🔄
Week 5    : Step 6（法線再整合 + Depth Fade + 浅瀬色）
Week 6    : Step 7（Foam）
Week 7    : Step 8（Caustics）
Week 8    : Step 9（SSR）
Week 9    : Step 10（UI / デバッグ / プリセット整理）
```

---

## 参考リソース

| リソース | 内容 |
|---------|------|
| Simulating Ocean Water (Tessendorf 2001) | FFT Ocean の数式・理論の出典となる学術文書 |
| GPU Gems 2 - Chapter 18: Water Flow | NVIDIA GPU Gems 2 の水面実装解説 |
| DirectX 12 公式ドキュメント | https://learn.microsoft.com/ja-jp/windows/win32/direct3d12/directx-12-programming-guide |
| DirectX 12 サンプル (Microsoft) | https://github.com/microsoft/DirectX-Graphics-Samples |
| Real-Time Rendering 4th Edition | PBR・SSR・水面の理論的基盤 |
| Book of Shaders | シェーダー数学の基礎: https://thebookofshaders.com |

---

*最終更新: 2026年5月*
