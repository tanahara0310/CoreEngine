# PBR レンダリング ロードマップ

> **対象ファイル:** `Engine/Assets/Shaders/` 以下の全シェーダー  
> **目的:** UE 相当の物理ベースレンダリング（PBR）を目指すための現状分析・優先実装ガイド

---

## 📋 目次

1. [現在の実装状況](#1-現在の実装状況)
2. [各機能の見た目への影響](#2-各機能の見た目への影響)
3. [不足機能と実装ロードマップ](#3-不足機能と実装ロードマップ)
4. [Phase 別 実装詳細](#4-phase-別-実装詳細)
5. [現行シェーダー構成図](#5-現行シェーダー構成図)

---

## 1. 現在の実装状況

| カテゴリ | 機能 | 状態 | 関連ファイル |
|---|---|---|---|
| **BRDF** | Cook-Torrance (GGX/Smith/Schlick) | ✅ 実装済み | `Common/PBR.hlsli` |
| **レンダリング方式** | Deferred Rendering (GBuffer 5枚) | ✅ 実装済み | `Object/GBuffer.PS.hlsl` |
| **レンダリング方式** | Forward Rendering（半透明・SceneView）| ✅ 実装済み | `Object/Object3d.PS.hlsl` |
| **IBL** | Irradiance Map（拡散 IBL）| ✅ 実装済み | `IBL/IrradianceConvolution.CS.hlsl` |
| **IBL** | Prefiltered Environment Map（鏡面 IBL）| ✅ 実装済み | `IBL/PrefilterEnvironment.CS.hlsl` |
| **IBL** | BRDF LUT（Split-Sum 近似）| ✅ 実装済み | `IBL/BRDFLUT.CS.hlsl` |
| **テクスチャマップ** | Normal / Metallic / Roughness / AO | ✅ 実装済み | `Object/GBuffer.PS.hlsl` |
| **シャドウ** | PCF シャドウマップ (3x3 / 5x5 / 7x7) | ✅ 実装済み | `Common/ShadowCalculation.hlsli` |
| **シャドウ** | DXR レイトレーシングシャドウ | ✅ 実装済み | `RayTracing/RTShadow.hlsl` |
| **シャドウ** | RT シャドウ テンポラル蓄積・デノイズ | ✅ 実装済み | `RayTracing/RTShadowTemporal.CS.hlsl` |
| **ライト** | Directional / Point / Spot / Area | ✅ 実装済み | `Common/LightStructures.hlsli` |
| **シェーディングモード** | PBR / PBR+IBL / Lambert / HalfLambert | ✅ 実装済み | `Object/Object3d.PS.hlsl` |
| **エミッシブ** | GBuffer エミッシブチャンネル | ✅ 実装済み | `Object/GBuffer.PS.hlsl` |
| **モーションベクター** | GBuffer Target4 に書き込み | ✅ 実装済み | `Object/GBuffer.PS.hlsl` |
| **トーンマッピング** | ACES Film トーンマッピング | ✅ 実装済み | `PostProcess/ToneMapping.PS.hlsl` |
| **ポストエフェクト** | Bloom / Blur / Vignette | ✅ 実装済み | `PostProcess/` |
| **ポストエフェクト** | Color Grading (HSV / SMH / 色温度) | ✅ 実装済み | `PostProcess/ColorGrading.PS.hlsl` |
| **ポストエフェクト** | SSAO（Screen Space Ambient Occlusion）| ✅ 実装済み | `PostProcess/SSAO.PS.hlsl`, `PostProcess/SSAOBlur.PS.hlsl` |
| **ポストエフェクト** | DoF / SSR | ❌ **未実装** | — |
| **AA** | TAA (Temporal Anti-Aliasing) | ❌ **未実装** | — |

---

## 2. 各機能の見た目への影響

> 「この機能がなかったらどう見える？」を中心に解説します。

---

### 2-1. Cook-Torrance BRDF ✅

**見た目への影響：最大**

| あり | なし |
|---|---|
| 金属・プラスチック・岩など素材ごとに反射の形状と広がりが変わる | 全素材が同じのっぺりした反射になる |
| 光源に正対したときにのみ強いハイライトが出る | どの角度でも同じ強さでテカる（古典的 Phong） |
| 粗い素材は広くぼやけたハイライト、滑らかな素材は鋭いハイライト | roughness を変えても見た目が変わらない |

**内部パラメータの見た目効果：**

| パラメータ | 低い値（0.0） | 高い値（1.0） |
|---|---|---|
| `roughness` | 鏡面のような鋭いハイライト・高い反射率 | つや消し・ハイライトが広くぼやける |
| `metallic` | 非金属（プラスチック風）・白いハイライト | 金属光沢・アルベド色のハイライト |
| `ao` | 遮蔽なし（明るい） | 隅・くぼみが暗くなる |

---

### 2-2. Deferred Rendering（GBuffer）✅

**見た目への影響：間接的**

- 大量ライトを配置してもパフォーマンスが落ちない
- 不透明オブジェクトに対してのみ有効（半透明は Forward）
- 不透明パスの品質を担保する基盤

| GBuffer チャンネル | 格納内容 | 用途 |
|---|---|---|
| `gAlbedoAO` | rgb=アルベド, a=AO | 基本色と環境遮蔽 |
| `gNormalRoughness` | rgb=法線, a=Roughness | 法線情報とPBRパラメータ |
| `gEmissiveMetallic` | rgb=エミッシブ, a=Metallic | 発光色とPBRパラメータ |
| `gWorldPosition` | rgb=ワールド座標, a=フラグ | ライティング位置計算 |
| `gMotionVector` | rg=NDC差分 | TAA・モーションブラー用 |

---

### 2-3. IBL（Image-Based Lighting）✅

**見た目への影響：非常に大きい**

IBL は環境光の質を左右する最重要機能の一つです。

#### Irradiance Map（拡散 IBL）

| あり | なし |
|---|---|
| 空の色や環境の色が物体の暗部・影部に柔らかく反映される | 影になった部分が真っ黒・不自然に暗い |
| 曇り空なら全体的に均一な灰色環境光、青空なら青みがかった環境光 | ライトのないところは環境光 0 で真黒 |

#### Prefiltered Environment Map + BRDF LUT（鏡面 IBL）

| あり | なし |
|---|---|
| 金属や光沢素材が周囲の環境を映り込む | 金属が黒くなる・環境反射がゼロ |
| roughness に応じて映り込みのシャープ/ぼかしが変化 | roughness を変えても金属の見た目が変わらない |
| BRDF LUT でエネルギー保存則が正確になる | 白飛び・エネルギー過剰になりやすい |

---

### 2-4. Normal Map ✅

**見た目への影響：大きい**

| あり | なし |
|---|---|
| 低ポリゴンメッシュでも細かいデコボコ感が出る | 光が当たる境界がポリゴンエッジで直線的になる |
| 岩の表面・布のシワ・金属の傷が表現できる | 非常にのっぺりした見た目になる |

---

### 2-5. AO Map（Ambient Occlusion Map） ✅

**見た目への影響：中程度**

| あり | なし |
|---|---|
| 隙間・溝・くぼみに自然な陰影が乗る | モデルの接触部分・細部がぼんやりする |
| IBL と組み合わさることで間接光の遮蔽が正確になる | 間接光が平均的にすべてに当たる |

> ✅ **SSAO 実装済み:** `SSAOPass` がランタイムで動的なコンタクトシャドウを生成し、デファードライティングのアンビエント項に乗算されます。

---

### 2-6. PCF シャドウマップ ✅

**見た目への影響：非常に大きい**

| カーネルサイズ | サンプル数 | 影の品質 | GPU 負荷 |
|---|---|---|---|
| 3×3（`PCF_KERNEL_SIZE 0`） | 9 | 影のエッジがやや荒い | 低 |
| 5×5（`PCF_KERNEL_SIZE 1`、**現デフォルト**）| 25 | バランスが良い | 中 |
| 7×7（`PCF_KERNEL_SIZE 2`）| 49 | 影のエッジが滑らか | 高 |

- **シャドウマップ解像度**（`SHADOW_MAP_SIZE`）は現在 4096 px
  - 解像度を下げると→影のエッジがジャギー（ギザギザ）になる
  - 解像度を上げると→より細かい影が出るがVRAM消費増

---

### 2-7. DXR レイトレーシングシャドウ ✅

**見た目への影響：非常に大きい（PCF 比較）**

| PCF シャドウ | RT シャドウ |
|---|---|
| カーネルサイズで固定幅のソフトシャドウ | 光源の半径（`gLightRadius`）に応じて物理的ペナンブラが変化 |
| 遠距離でも近距離でも同じ硬さ | 近くは硬く遠くは柔らかい（現実の影に近い） |
| テンポラル蓄積なし | テンポラル蓄積でノイズを時間方向に平滑化 |

---

### 2-8. ACES トーンマッピング ✅

**見た目への影響：非常に大きい**

| あり | なし |
|---|---|
| HDR の高輝度部分が自然に圧縮され、白飛びしない | 直射日光などの高強度ライトで白飛びが発生 |
| 映画的な色調（低輝度は彩度が上がり、高輝度は黄みがかる）| フラットな直線的輝度変換 |
| 暗部が完全な黒にならず、微細な情報が残る | 暗部が潰れやすい |

> 現在のパイプライン: HDR レンダリング → トーンマッピング（ACES）→ Color Grading → 表示

---

### 2-9. Color Grading ✅

**見た目への影響：大きい（最終仕上げ）**

| パラメータ | 効果 |
|---|---|
| `exposure` | 全体的な明るさ（EV 値、`pow(2, exposure)` で乗算）|
| `temperature` | 正→暖色（夕焼け・炎）、負→寒色（夜・氷）|
| `tint` | マゼンタ↔グリーン方向の色補正 |
| `saturation` | 0.0=グレースケール, 1.0=標準, 2.0=過飽和 |
| `contrast` | 明暗差の強調・弱化 |
| `gamma` | 中間調の明るさ調整（モニタのガンマ調整に相当）|
| `shadowLift` | シャドウ部の色補正（RGB 個別）|
| `midtoneGamma` | 中間調の色補正（RGB 個別）|
| `highlightGain` | ハイライト部の色補正（RGB 個別）|

---

### 2-10. Bloom ✅

**見た目への影響：中程度〜大きい**

| パラメータ | 効果 |
|---|---|
| `threshold` | Bloom が光り始める輝度の閾値。低いと全体が光り、高いと強い光源のみ光る |
| `softKnee` | 閾値の境界をソフトにする（0=ハードカット、1=滑らか）|
| `intensity` | Bloom の強さ |
| `blurRadius` | Bloom の広がり半径 |

> ⚠️ **現状の注意点:** エミッシブ素材の光が Bloom に正しく寄与しているか要確認。  
> エミッシブチャンネルが HDR バッファに加算されていれば自動的に Bloom に乗る。

---

### 2-11. シェーディングモード選択 ✅

`Material.shadingMode` の値によって計算方法が変わる：

| 値 | モード | 見た目 | 用途 |
|---|---|---|---|
| `0` | **PBR** | 物理的に正確な反射・拡散 | 標準的な3Dオブジェクト |
| `1` | **PBR + IBL** | PBR + 環境反射（最もリアル）| 金属・宝石・光沢素材 |
| `2` | **Lambert** | 滑らかな拡散のみ・ハイライトなし | トゥーン・スタイライズ |
| `3` | **HalfLambert** | 影部も明るく柔らかい拡散 | キャラクター・アニメ調 |

---

## 3. 不足機能と実装ロードマップ

### 優先度・難易度マトリクス

```
難易度
  高 │  [Volumetric Fog]  [Lumen GI]
     │  [SSR]             [SSS]
     │  [TAA]             [RTAO]
  中 │  [SSAO]            [DoF]
     │  [POM]             [Clearcoat BRDF]
  低 │  [Emissive→Bloom]  [sRGB 出力確認]
     └──────────────────────────────── 優先度
        高                低
```

| # | 機能 | 優先度 | 難易度 | 見た目効果 | Phase |
|---|---|---|---|---|---|
| 1 | **sRGB 出力確認・修正** | ✅ 完了 | ★☆☆ 低 | 色精度の正確化 | 1 |
| 2 | **エミッシブ → Bloom 連携** | ✅ 完了 | ★☆☆ 低 | 発光素材の光り方 | 1 |
| 3 | **SSAO** | ✅ 完了 | ★★☆ 中 | 接触部分の陰影・奥行き感 | 1 |
| 4 | **TAA** | 🟡 中 | ★★★ 高 | エッジのジャギー除去・全体的なシャープ感 | 2 |
| 5 | **SSR** | 🟡 中 | ★★★ 高 | 床・水面・光沢面の正確な反射 | 2 |
| 6 | **Depth of Field** | 🟡 中 | ★★☆ 中 | 被写界深度・映画的な奥行き | 2 |
| 7 | **Volumetric Fog** | 🟢 低 | ★★★ 高 | 霧・霞・ゴッドレイ | 3 |
| 8 | **Subsurface Scattering** | 🟢 低 | ★★★ 高 | 肌・ろうそく・葉の透過感 | 3 |
| 9 | **Clearcoat / Sheen BRDF** | 🟢 低 | ★★☆ 中 | 車塗装・布素材の表現力向上 | 3 |
| 10 | **Parallax Occlusion Mapping** | 🟢 低 | ★★☆ 中 | ノーマルマップより立体的な表面 | 3 |

---

## 4. Phase 別 実装詳細

---

### Phase 1 ── 即効果が高い修正（✅ 完了）

---

#### 4-1. sRGB 出力確認・修正

**なぜ重要か:**  
リニア色空間で計算した結果をそのまま出力すると、モニタのガンマ特性により全体的に暗く見える。  
正確には「リニア → sRGB 変換」が最終ステップに必要。

**現状:**  
- `ToneMapping.PS.hlsl` で ACES を適用後、sRGB 変換が明示されていない
- `ColorGrading.PS.hlsl` の `gamma` パラメータで代替している可能性あり
- レンダーターゲットのフォーマットが `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` であれば自動変換されるが要確認

**確認・修正方法:**
```hlsl
// ToneMapping.PS.hlsl の出力前に追加（レンダーターゲットが非sRGBフォーマットの場合）
float3 LinearToSRGB(float3 c)
{
    return pow(c, 1.0f / 2.2f);  // 簡易版
    // または正確な IEC 61966-2-1 変換を使用
}
```

---

#### 4-2. エミッシブ → Bloom 連携

**なぜ重要か:**  
エミッシブ素材（発光体）が Bloom を通じて周囲を照らしているように見えることで、「光っている感」が劇的に向上する。

**現状:**
- GBuffer の `emissiveMetallic.rgb` にエミッシブ値を格納済み
- `DeferredLighting.PS.hlsl` で HDR バッファに加算されているか確認が必要

**期待する見た目:**

| エミッシブ + Bloom なし | エミッシブ + Bloom あり |
|---|---|
| 発光素材が明るいだけ | 発光素材の周囲がハレーションを起こし光り輝く |
| ネオン管が単色の白 | ネオン管の色が周囲ににじむ |

**実装確認ポイント:**
```hlsl
// DeferredLighting.PS.hlsl の最終出力で emissive を加算しているか確認
output.color.rgb += emissive;  // ← これが HDR バッファに書かれていれば Bloom に自動的に乗る
```

---

#### 4-3. SSAO（Screen Space Ambient Occlusion）✅ 実装済み

**なぜ重要か:**  
静的 AO マップでは対処できない「キャラクターが地面に接する部分」「引き出しの隙間」などの動的な接触影を生成する。  
接触影があるだけでシーンの「重さ」「リアリティ」が大幅に向上する。

**見た目の比較:**

| SSAO なし | SSAO あり |
|---|---|
| 物体が地面に「浮いている」ように見える | 物体の足元・隅に自然な暗がりが生まれる |
| 隙間・くぼみが一様に明るい | 隅が暗くなり奥行き感が増す |
| キャラクターの首周りが均一に明るい | 首の付け根・脇の下が暗くなりリアルに見える |

**実装概要:**

```
1. GBuffer の深度・法線テクスチャを入力
2. 各ピクセルの周囲をスクリーン空間でサンプリング（半球状）
3. 平均的な深度差から遮蔽率（0-1）を計算
4. ブラーをかけてノイズを除去（Bilateral Blur）
5. DeferredLighting でアンビエント項に乗算
```

**新規シェーダーファイル:**
```
Engine/Assets/Shaders/PostProcess/SSAO.CS.hlsl     ← AO 計算
Engine/Assets/Shaders/PostProcess/SSAOBlur.PS.hlsl ← ブラー
```

**入力テクスチャ:**
```hlsl
Texture2D<float4> gNormalRoughness : register(t0); // GBuffer 既存
Texture2D<float4> gWorldPosition   : register(t1); // GBuffer 既存
Texture2D<float>  gDepth           : register(t2); // 深度バッファ
Texture2D<float4> gNoiseTex        : register(t3); // 4x4 ランダムノイズ
```

**定数バッファ:**
```hlsl
cbuffer SSAOParams : register(b0)
{
    float radius;        // サンプリング半径（ワールド空間）推奨: 0.5
    float bias;          // セルフ遮蔽防止バイアス 推奨: 0.025
    float intensity;     // AO の強さ 推奨: 1.0
    int   sampleCount;   // サンプル数（8 〜 64）推奨: 32
    float4x4 projection; // プロジェクション行列
    float4x4 view;       // ビュー行列
};
```

---

### Phase 2 ── 品質の大幅向上（1〜2 ヶ月）

---

#### 4-4. TAA（Temporal Anti-Aliasing）

**なぜ重要か:**  
ジャギー（エッジのギザギザ）除去だけでなく、SSAO・影・SSR のノイズをフレームをまたいで平滑化し全体的な品質が向上する。  
モーションベクターは既に GBuffer に書き込まれているので、土台は整っている。

**見た目の比較:**

| TAA なし | TAA あり |
|---|---|
| エッジがギザギザ（特に斜め線） | エッジが滑らかになる |
| カメラを動かすとジッター（ちらつき）が目立つ | 動体でも安定した滑らかな映像 |
| SSAO のノイズが 1 フレームごとに変化して見える | ノイズが時間方向に蓄積されて消える |

**実装概要:**
```
1. レンダリング時に Halton 列でサブピクセルジッターをプロジェクション行列に加える
2. 現フレームの色を前フレームとモーションベクターで再投影して合成
3. ゴーストを防ぐためクランプ（Variance Clipping または AABB Clamp）を適用
4. 合成後のフレームを蓄積バッファに保存
```

**新規シェーダーファイル:**
```
Engine/Assets/Shaders/PostProcess/TAA.PS.hlsl
```

**入力テクスチャ:**
```hlsl
Texture2D<float4> gCurrentColor  : register(t0); // 現フレーム HDR カラー
Texture2D<float4> gHistoryColor  : register(t1); // 前フレーム蓄積結果
Texture2D<float2> gMotionVector  : register(t2); // GBuffer モーションベクター（既存）
Texture2D<float>  gDepth         : register(t3); // 深度バッファ
```

---

#### 4-5. SSR（Screen Space Reflections）

**なぜ重要か:**  
IBL だけでは「床に柱が映り込む」「水面に空が映る」などのローカルな反射が表現できない。  
SSR は GBuffer の情報だけでリアルタイムにローカル反射を近似する。

**見た目の比較:**

| IBL のみ | IBL + SSR |
|---|---|
| 金属の床が環境マップを反射（遠景は正確、近景はぼける）| 近くにある柱・壁・オブジェクトが正確に映り込む |
| 水面が空の色のみ | 水面に周囲の建物・キャラクターが映る |
| Roughness=0 の金属でも静的なキューブマップしか見えない | roughness が低ければ鏡面のようにシーンが映る |

**実装概要:**
```
1. 反射ベクトル R = reflect(-V, N) を計算
2. スクリーン空間でレイマーチング（深度バッファとの衝突判定）
3. ヒットしたスクリーン座標の色を反射色として採用
4. roughness が高いほど SSR をブラー or IBL にフォールバック
5. TAA で時間方向に安定化（TAA と組み合わせることで真価を発揮）
```

**新規シェーダーファイル:**
```
Engine/Assets/Shaders/PostProcess/SSR.PS.hlsl
```

---

#### 4-6. Depth of Field（被写界深度）

**なぜ重要か:**  
カメラの焦点距離・絞りに基づいて、フォーカス外の物体をぼかす。  
映画的な「主役を際立たせる」演出に非常に効果的。

**見た目の比較:**

| DoF なし | DoF あり |
|---|---|
| 近景〜遠景がすべてシャープに見える | フォーカス外が自然にぼける（映画的） |
| ゲームっぽいシャープな見た目 | 写真・映画に近い映像品質 |

**実装概要:**
```
1. 深度バッファから CoC（Circle of Confusion）半径を計算
   CoC = abs(depth - focalDistance) / focalDistance * aperture
2. CoC の大きさに応じて Bokeh ブラーをかける
3. 近景ボケと遠景ボケを分けて処理（近景は半透明なオブジェクトをブリード）
```

**新規シェーダーファイル:**
```
Engine/Assets/Shaders/PostProcess/DepthOfField.PS.hlsl
```

**定数バッファ:**
```hlsl
cbuffer DoFParams : register(b0)
{
    float focalDistance; // フォーカス距離（ワールド単位）
    float focalRange;    // フォーカスが合う範囲
    float aperture;      // 絞り値（大きいほどボケが強い）
    float maxCoC;        // CoC の最大半径（ピクセル）
};
```

---

### Phase 3 ── 上級機能（2〜3 ヶ月以上）

---

#### 4-7. Volumetric Fog

**見た目への影響:**

| なし | あり |
|---|---|
| 霧・霞が表現できない | 光が空気中を通る際に散乱して「ゴッドレイ」「薄霧」が表現できる |
| ライトのボリューム感がない | スポットライトの光の柱が視覚的に見える |
| 深度感が薄い | 遠景が霧でぼやけ自然な大気感が出る |

**実装難易度:** ★★★ 高（レイマーチング + ノイズテクスチャ）

---

#### 4-8. Subsurface Scattering (SSS)

**見た目への影響:**

| なし | あり |
|---|---|
| 肌が金属・プラスチックと同じ BRDF で計算される | 肌の奥で光が散乱し、「透き通った」柔らかい質感になる |
| 耳・鼻・指に光が透過しない | 逆光時に耳・鼻が赤くなる（現実の皮膚の現象）|
| ろうそく・牛乳・葉が不透明な外観 | 透過光による柔らかいグロー感 |

**実装難易度:** ★★★ 高（Split-Sum SSS や Screen-Space SSS）

---

#### 4-9. Clearcoat / Sheen BRDF

**見た目への影響:**

| 用途 | 効果 |
|---|---|
| Clearcoat（車の塗装・漆）| ベース層の上にもう 1 層のクリア反射が乗る。塗装感・ラッカー感 |
| Sheen（布・ベルベット）| 布特有の「縁が明るい」シルエット光沢 |

**実装難易度:** ★★☆ 中（BRDF 関数の追加のみ）

---

#### 4-10. Parallax Occlusion Mapping (POM)

**見た目への影響:**

| Normal Map | POM |
|---|---|
| 表面のデコボコは「視差なし」（どこから見ても輪郭はフラット）| 視点に応じてテクスチャがずれ「本当に盛り上がっている」ように見える |
| 石畳の縁が視点によってフラットに見える | 石畳を斜めから見るとレンガの側面が見える |

**実装難易度:** ★★☆ 中（レイマーチング + ハイトマップ）

---

## 5. 現行シェーダー構成図

```
【不透明オブジェクト】
  Object3d.VS.hlsl ──→ GBuffer.PS.hlsl
                             │ MRT 書き込み (Albedo/Normal/Emissive/WorldPos/MotionVec)
                             ▼
  [DXR] RTShadow.hlsl ──→ RTShadowTemporal.CS.hlsl (テンポラル蓄積)
                             │ RTShadowMask
                             ▼
                   DeferredLighting.PS.hlsl ──→ [HDR バッファ]
                   (PBR + IBL + Shadow)

【半透明 / SceneView オブジェクト】
  Object3d.VS.hlsl ──→ Object3d.PS.hlsl ──→ [HDR バッファ]
                   (Forward PBR + IBL + PCF Shadow)

【IBL プリコンピュート（起動時 or 非同期）】
  IrradianceConvolution.CS.hlsl ──→ gIrradianceMap
  PrefilterEnvironment.CS.hlsl  ──→ gPrefilteredMap
  BRDFLUT.CS.hlsl               ──→ gBRDFLUT

【ポストエフェクトチェーン（HDR バッファ → 表示）】
  [HDR バッファ]
       │
       ├──→ Bloom.PS.hlsl (輝度抽出 + ガウシアンブラー)
       │
       ├──→ ToneMapping.PS.hlsl (ACES HDR → LDR)
       │
       ├──→ ColorGrading.PS.hlsl (露出 / HSV / SMH / 色温度)
       │
       ├──→ ChromaticAberration.PS.hlsl
       ├──→ Vignette.PS.hlsl
       ├──→ RadialBlur.PS.hlsl
       └──→ [表示]

【未実装 (Phase 別)】
  Phase 1: SSAO.CS.hlsl
  Phase 2: TAA.PS.hlsl, SSR.PS.hlsl, DepthOfField.PS.hlsl
  Phase 3: VolumetricFog.PS.hlsl, SSS 対応, POM 対応
```

---

## 関連ドキュメント

- [`Docs/Graphics/Lighting.md`](../Lighting.md) — ライトの API 使い方
- [`Engine/Assets/Shaders/Common/PBR.hlsli`](../../../../Engine/Assets/Shaders/Common/PBR.hlsli) — BRDF 実装
- [`Engine/Assets/Shaders/Common/ShadowCalculation.hlsli`](../../../../Engine/Assets/Shaders/Common/ShadowCalculation.hlsli) — シャドウ実装
- [`Engine/Assets/Shaders/PostProcess/DeferredLighting.PS.hlsl`](../../../../Engine/Assets/Shaders/PostProcess/DeferredLighting.PS.hlsl) — ディファードライティング

---

*最終更新: 2025年*
