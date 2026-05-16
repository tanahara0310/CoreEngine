# リアルな水面表現 — 実装ロードマップ

> **対象環境:** C++ / DirectX 12  
> **方針:** 動くだけの最小実装から始め、段階的にグラフィック品質を高める  
> **最終目標:** FFT Ocean + SSR + Caustics によるハイクオリティな水面レンダリング

---

## 全体フロー

| Step | タイトル | 状態 | 詳細ドキュメント |
|------|----------|:----:|----------------|
| 1 | グリッドメッシュの生成と表示 | ✅ 完了 | [Step1_GridMesh.md](Step1_GridMesh.md) |
| 2 | UV スクロール（流れるだけの水） | ✅ 完了 | [Step2_UVScroll.md](Step2_UVScroll.md) |
| 3 | Gerstner Wave（頂点変位による波） | ✅ 完了 | [Step3_GerstnerWave.md](Step3_GerstnerWave.md) |
| 4 | Planar Reflection + Fresnel（反射） | ✅ 完了 | [Step4_PlanarReflection.md](Step4_PlanarReflection.md) |
| 5 | Normal Map + PBR ライティング | 🔄 進行中 | [Step5_NormalMap_PBR.md](Step5_NormalMap_PBR.md) |
| 6 | FFT Ocean（本格的な波シミュレーション） | ⬜ 未着手 | [Step6_FFTOcean.md](Step6_FFTOcean.md) |
| 7 | SSR / Caustics / Foam（最高品質の演出） | ⬜ 未着手 | [Step7_SSR_Caustics_Foam.md](Step7_SSR_Caustics_Foam.md) |

---

## 現在地と次にやるべきこと

### 完了済み
- 平面メッシュ（64×64 分割）の生成・表示
- UV スクロール + タイリング
- Gerstner Wave による頂点変位（4 波の重ね合わせ）
- Planar Reflection（平面反射 RTT）+ Schlick Fresnel ブレンド
- Water 専用シェーダー（`Water.VS.hlsl` / `Water.PS.hlsl`）
- IBL フォールバック（反射 RTT 未設定時は `gPrefilteredMap` を使用）
- ImGui からの全パラメータ動的変更（ベースカラー / Roughness / Metallic / IBL / UV / 波 / テクスチャモード）
- テクスチャなし / ノーマルマップのみ / アルベド+ノーマル の3モード切り替え

### Step 5 で行うこと（次のステップ）
Step 4 までで「動いて反射する水」は完成した。  
Step 5 では **見た目の品質** に踏み込む。具体的には以下を整備する：

1. **ノーマルマップの二重スクロール** — 方向・速度の異なる 2 枚のノーマルマップを重ね合わせ、表面の細かい揺らぎをより自然に見せる
2. **Fresnel パラメータの調整** — 視線角度による反射率変化をより物理的に正確にする（F0 の調整、カスタム Fresnel 係数）
3. **深度フェード（Depth Fade）** — 水底に近いほど色を薄くし、岸辺の浅瀬らしさを演出する
4. **水中散乱近似（Sub-surface Scattering 近似）** — 波の頂点付近が透けてターコイズ色に光る現象を簡易的に再現する
5. **PBR パラメータの微調整** — Roughness / Metallic / IBL 強度を IBL 環境マップと連動させて自然な映り込みにする

詳細 → [Step5_NormalMap_PBR.md](Step5_NormalMap_PBR.md)

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
Week 5〜  : Step 6（FFT Ocean）
Week 8〜  : Step 7（SSR / Caustics / Foam）
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
