# Step 5 : Normal Map + PBR（見た目の品質向上）

> **状態:** 🔄 進行中  
> **目的:** ノーマルマップの高度な活用と PBR パラメータの最適化により、水面の質感を大幅に向上させる  
> **位置付け:** Step 4 までで「動いて反射する水」は完成。Step 5 では「本物らしい水」に仕上げる

---

## このステップで行うこと

1. **ノーマルマップの二重スクロール** — 2 枚のノーマルマップを異なる速度・方向でスクロールして重ね合わせる
2. **Fresnel パラメータの調整** — F0 値と Fresnel 指数を調整して水らしい反射率カーブにする
3. **深度フェード（Depth Fade）** — 水底に近いほど色を薄くして浅瀬らしさを出す
4. **水中散乱近似（Subsurface Scattering 近似）** — 波の頂点付近がターコイズ色に透けて見える現象を再現する
5. **PBR パラメータの微調整** — 湖・海・プールなど用途別に Roughness / Metallic / IBL を調整する

---

## 1. ノーマルマップの二重スクロール

1 枚のノーマルマップだけでは規則的な繰り返しが目立つ。  
**2 枚を異なる方向・速度でスクロールして合成**することで自然な揺らぎが生まれる。

```
// Pixel Shader 内
float2 uv1 = input.texcoord * tiling + scrollSpeed1 * time;
float2 uv2 = input.texcoord * tiling + scrollSpeed2 * time;

float3 n1 = SampleNormalMap(uv1);
float3 n2 = SampleNormalMap(uv2);

// 2 枚の法線を混合（Whiteout Blend が品質高め）
float3 normal = normalize(float3(n1.xy + n2.xy, n1.z * n2.z));
```

### Whiteout Blend vs 単純加算

| 手法 | 特徴 |
|------|------|
| 単純加算 `(n1 + n2) / 2` | 軽量だが法線が平坦になりやすい |
| Whiteout Blend | 法線の合成精度が高い。急な角度変化にも対応できる |

---

## 2. Fresnel パラメータの調整

### 水の物理的な F0 値

```
// 水の屈折率は約 1.33
// スネルの法則から F0 ≈ 0.02 が正確な値
F0 = 0.02f  // 水面の標準値（現在の実装値と一致）
```

### 指数の調整による見た目の変化

Schlick 近似の `pow(1 - cosθ, 5)` の指数 5 を変えると反射の「切れ味」が変わる。  
指数を小さくするとより広い角度範囲で反射が発生する。

---

## 3. 深度フェード（Depth Fade）

水底に近いほど色を薄く（透明に）することで浅瀬の透明感を表現する。

```
アルゴリズム:
  1. シーン深度バッファから現在ピクセルの深度を取得
  2. 水面ピクセルの深度と差分を取る → 水深を算出
  3. 水深に応じて albedo の alpha を変化させる

  waterDepth = sceneDepth - waterSurfaceDepth
  depthFade = saturate(waterDepth / fadeDistance)
  color.a = lerp(shallowAlpha, deepAlpha, depthFade)
```

### D3D12 での深度バッファ読み取り

- 深度バッファを SRV として Water.PS.hlsl にバインドする必要がある
- `DXGI_FORMAT_R32_FLOAT`（読み取り専用ビュー）として別途 SRV を作成する
- 深度値の線形化が必要: `linearDepth = near * far / (far - depth * (far - near))`

---

## 4. 水中散乱近似（Subsurface Scattering 近似）

光が水面内部で複数回散乱することで、波の頂点付近が明るくターコイズ色に光る現象。  
本格的な SSS は重いため、以下の近似で十分な見た目を得られる。

```
アルゴリズム:
  1. 波の頂点高さ（Y 変位量）を計算
  2. 視線と光方向の内積から透過散乱の強さを推定
  3. ターコイズ色を波高に応じて加算する

  // 波頂点の検出
  float waveHeight = saturate(gerstnerY / maxAmplitude)

  // 散乱色の計算
  float sssStrength = pow(saturate(dot(viewDir, -lightDir)), 4.0f)
  float3 sssColor = float3(0.0f, 0.9f, 0.8f)  // ターコイズ
  output.color.rgb += sssColor * sssStrength * waveHeight * sssIntensity
```

---

## 5. PBR パラメータのガイドライン

用途別の推奨値（ImGui で実験して最適値を探すこと）：

| 用途 | Roughness | Metallic | IBL 強度 | 特徴 |
|------|-----------|----------|----------|------|
| 湖（静かな水） | 0.02〜0.05 | 0.0 | 1.0 | 鏡面に近く周囲が映り込む |
| 海（波が立つ水） | 0.05〜0.15 | 0.0 | 1.0 | やや荒れた表面感 |
| 池・プール | 0.01〜0.03 | 0.0 | 1.2 | 非常に鏡面的で透明 |
| 雨水・水たまり | 0.03〜0.08 | 0.0 | 0.8 | やや曇り感のある反射 |

---

## 使用技術

| 技術名 | 役割 |
|--------|------|
| **Normal Map Blending** | 複数ノーマルマップの合成による自然な法線表現 |
| **Whiteout Blend** | 法線マップの合成アルゴリズム。単純加算より品質が高い |
| **Depth Fade** | 深度バッファを参照して水の深さに応じた透明度を計算する |
| **Subsurface Scattering（近似）** | 水の内部散乱を簡易的に再現し、ターコイズ色の輝きを加える |
| **PBR（Physically Based Rendering）** | Metallic / Roughness / AO / IBL による物理ベースの光計算 |

---

## 確認ポイント

- [ ] ノーマルマップが 2 枚合成されて複雑な波紋が見える
- [ ] 視線角度が浅いほど反射が強くなる（Fresnel）
- [ ] 岸辺に近い浅い部分が透けて見える（Depth Fade）
- [ ] 波の頂点付近がターコイズ色に輝いて見える（SSS 近似）
- [ ] ImGui で Roughness を上げると反射がぼやける

---

*[← Step 4 Planar Reflection](Step4_PlanarReflection.md) | [次: Step 6 FFT Ocean →](Step6_FFTOcean.md)*
