# Step 4 : Planar Reflection + Fresnel（平面反射）

> **状態:** ✅ 完了  
> **目的:** 水面が周囲の景色を映り込む「反射」を実装し、Fresnel による視線角度連動で自然な見え方にする  
> **位置付け:** 水面に「深さ・透明感・映り込み」が生まれる、見た目の最も大きな転換点

---

## 目標

- 水面高さを基準に鏡像カメラを作成し、シーンをオフスクリーン RTT に描画する
- RTT の結果を Pixel Shader でスクリーン UV サンプリングして水面に貼り付ける
- Schlick Fresnel 近似により視線角度が浅いほど反射が強くなるよう制御する
- 反射 RTT 未設定時は IBL（`gPrefilteredMap`）にフォールバックする
- `SV_ClipDistance0` で水面の下側オブジェクトを反射パス中にクリップする

---

## アルゴリズム

### 鏡像カメラの計算

水面（Y = waterHeight）に対して元のカメラを鏡像反転させた「反射カメラ」を作成する。

```
反射行列:
  reflectPlane = float4(0, 1, 0, -waterHeight)  // Y = waterHeight の平面
  reflectMatrix = XMMatrixReflect(reflectPlane)

反射 View 行列:
  reflectedView = originalView * reflectMatrix

反射カメラの位置:
  reflectedPos = reflect(cameraPos, float3(0,1,0)) + float3(0, 2*waterHeight, 0)
```

### Schlick Fresnel 近似

```
// 水の基準反射率（法線入射時）= 0.02
F = F0 + (1 - F0) * (1 - cosθ)^5
cosθ = dot(geometryNormal, viewDir)  // ジオメトリ法線を使う（法線マップは不使用）

※ 法線マップ由来の細かい凹凸で Fresnel が暴れないよう、
  ジオメトリ法線（0, 1, 0）を使うことで安定した反射計算を行う
```

### スクリーン UV サンプリング

```
// 反射テクスチャは平面反射 RTT をスクリーン空間でサンプリングする
float2 screenUV = clipPos.xy / clipPos.w;
screenUV = screenUV * float2(0.5, -0.5) + float2(0.5, 0.5);
screenUV = saturate(screenUV);
float3 reflectColor = gReflectionTexture.Sample(sampler, screenUV).rgb;

// Fresnel ブレンド
// ForwardMain() が ApplyIBL() を含むため、IBL の二重加算を避ける
// 反射 RTT が有効な場合のみ上書きブレンドを行う
output.color.rgb = lerp(output.color.rgb, reflectColor, fresnel);
```

---

## 使用技術

| 技術名 | 役割 |
|--------|------|
| **Planar Reflection（平面反射）** | 水面高さでカメラを鏡像反転させ、別パスでシーンを描画してテクスチャとして使用する手法 |
| **Offscreen Render Target（RTT）** | 反射シーンを画面外のテクスチャに書き込む描画ターゲット |
| **Schlick Fresnel 近似** | 視線と法線の角度から反射率を計算。正面から見ると透過感が強く、浅い角度から見ると鏡のように反射する |
| **SV_ClipDistance0** | HLSL の組み込みセマンティクス。頂点単位でジオメトリをクリップする。反射パス中に水面より下のオブジェクトを除去するために使用 |
| **IBL フォールバック** | 反射 RTT が未設定の場合に `gPrefilteredMap`（スペキュラ IBL）を環境反射として使用する。ForwardMain 内の ApplyIBL と二重加算しないよう注意 |
| **XMMatrixReflect** | DirectXMath の関数。任意の平面に対する反射行列を生成する |

---

## 描画フローの全体像

```
毎フレームの Draw() 処理:

1. クリップ平面を WaterPlaneObject に設定
   SetClipPlane(reflectionPlane, false)

2. 反射パスを実行（WaterReflectionPass::Render）
   a. アクティブカメラを鏡像カメラに差し替える
   b. SV_ClipDistance0 で水面下をクリップ
   c. オフスクリーン RTT に反射シーンを描画
   d. カメラを元に戻す

3. 反射テクスチャを水面オブジェクトに渡す
   SetReflectionTexture(reflectionSRV)

4. 通常描画（BaseScene::Draw）
   Water.PS.hlsl で Fresnel ブレンド
```

---

## D3D12 実装上の注意点

- **サンプラーレジスタの衝突に注意**  
  `Water.PS.hlsl` で使うカスタムサンプラーは `s0 / s1` が他のサンプラー（`gShadowSampler` 等）と衝突する場合がある。  
  使用前に共通 include（`Object3dForward.hlsli`）の登録済みレジスタを確認する。

- **IBL の二重加算問題**  
  `ForwardMain()` 内で `ApplyIBL()` が実行済みのため、PS でさらに `gPrefilteredMap` をサンプリングすると IBL が二重に加算される。  
  フォールバック時は `ForwardMain()` の結果をそのまま使い、反射 RTT がある場合のみ Fresnel でブレンドする。

- **未初期化 SRV のサンプリング禁止**  
  反射テクスチャの有効フラグ（`gReflectionEnabled`）を必ず確認してからサンプリングする。  
  未初期化ハンドルをサンプリングすると GPU クラッシュやノイズの原因になる。

---

## 確認ポイント

- [x] 反射パスが動作し、スカイボックスが水面に映り込んでいる
- [x] 視線角度が浅くなるほど反射が強くなる（Fresnel）
- [x] 反射 RTT 未設定時は IBL フォールバックになる
- [x] 赤いノイズや海テクスチャの二重表示がない
- [x] IBL の二重加算がない（Fresnel ブレンドは RTT 有効時のみ実施）

---

*[← Step 3 Gerstner Wave](Step3_GerstnerWave.md) | [次: Step 5 Normal Map + PBR →](Step5_NormalMap_PBR.md)*
