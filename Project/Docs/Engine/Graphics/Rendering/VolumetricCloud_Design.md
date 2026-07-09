# Volumetric Cloud 実装設計書

アンリアルエンジンの Volumetric Cloud（Schneider "Nubis" 方式 + Hillaire の大気統合）を参考に、
CoreEngine へボリューメトリック雲レンダリングを実装するための完全設計書。

**この文書単体で実装を完遂できること**を目標に書かれている。実装 AI（または人間）は、
本文書の「実装フェーズ計画」に従い、**フェーズごとにビルド → 起動 → 検証**を行ってから
次のフェーズへ進むこと。

- 作成日: 2026-07-08
- 対象ブランチ: `future/Atmospheric-scattering`（大気散乱実装済みが前提）
- 最終ゴール: **大気散乱 SkyBox を使う全シーンで、既定背景として雲が描画される**こと

---

## 目次

1. [ゴールと要求](#1-ゴールと要求)
2. [参考文献](#2-参考文献)
3. [前提となる既存エンジン基盤（必読ファイル）](#3-前提となる既存エンジン基盤必読ファイル)
4. [アーキテクチャ全体像](#4-アーキテクチャ全体像)
5. [アルゴリズム仕様](#5-アルゴリズム仕様)
6. [実装仕様（C++）](#6-実装仕様c)
7. [シェーダーファイル仕様（HLSL）](#7-シェーダーファイル仕様hlsl)
8. [足りないエンジン機能と対応方針](#8-足りないエンジン機能と対応方針)
9. [実装フェーズ計画](#9-実装フェーズ計画)
10. [ビルド・実行・検証ワークフロー（共通手順）](#10-ビルド実行検証ワークフロー共通手順)
11. [既知の落とし穴チェックリスト](#11-既知の落とし穴チェックリスト)
12. [パラメータ既定値一覧](#12-パラメータ既定値一覧)
13. [スコープ外・将来拡張](#13-スコープ外将来拡張)

---

## 1. ゴールと要求

### 機能要求

- UE の Volumetric Cloud 相当のレイマーチング雲を GameView に描画する
- 大気散乱システム（SkyAtmosphere）と物理的に整合する:
  - 太陽の色・強度は `AtmosphereManager` の太陽情報と Transmittance LUT から取得
  - 雲のアンビエントは Sky-View LUT からサンプリング
  - 雲は太陽ディスクを透過率で自然に遮蔽する（SkyBox の後に合成するため自動的に成立）
- 不透明ジオメトリによる遮蔽（SceneDepth 参照）に対応する
- 風による移流アニメーション、カバレッジ（雲量）等を ImGui でリアルタイム編集できる
- **大気散乱モードの SkyBox を使う全シーンで既定有効**（= デフォルト背景）。
  ただしキューブマップ空のシーン・非対応シーンには一切影響を与えない
  （`AtmosphereManager::IsAtmosphereActive()` と同じ「フレーム有効化」パターンで保証する）

### 非機能要求

- 半解像度レイマーチング + フル解像度合成で、1080p / ミドルレンジ GPU で数 ms 以内
- 新規パスは既存描画へ影響しない（`RenderPass.h` のパス分離契約 1〜4 を厳守）
- 雲を使わないフレームでは GPU コストほぼゼロ（パス先頭で早期 return）

---

## 2. 参考文献

実装アルゴリズムは以下に基づく（UE の VolumetricCloud コンポーネントの実装系譜そのもの）:

- A. Schneider, "The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn" (SIGGRAPH 2015)
- A. Schneider, "Nubis: Authoring Real-Time Volumetric Cloudscapes with the Decima Engine" (SIGGRAPH 2017)
- S. Hillaire, "Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite" (SIGGRAPH 2016)
  — エネルギー保存散乱積分・大気との統合はこれに従う
- S. Hillaire, "TileableVolumeNoise" (GitHub) — Perlin-Worley ノイズ生成のリファレンス
- GPU Pro 7, "Real-Time Volumetric Cloudscapes" — remap / 高度勾配 / Beer-Powder の定式化

---

## 3. 前提となる既存エンジン基盤（必読ファイル）

実装前に以下を読むこと。**雲実装は大気散乱実装のパターンをほぼ完全に踏襲する**。

| ファイル | 何を真似るか |
|---|---|
| `Engine/Src/Graphics/Atmosphere/AtmosphereManager.h/.cpp` | Manager 設計の雛形。CB 永続マップ・`static_assert` によるレイアウト検証・LUT/3Dテクスチャ生成（`CreateLUTResources`）・`CustomShaderPipeline` による CS 構築・ダーティフラグ・**フレーム有効化パターン**（`atmosphereActive_` / `ResetFrameActivation`）・`ApplyAerialPerspective` の SceneColor 合成手順（SRV読み → 中間UAV書き → CopyResource 書き戻し → 状態復帰） |
| `Engine/Src/Graphics/Render/Pass/AtmosphereLUTPass.cpp` | compute 専用パスの雛形。`IsAtmosphereActive()` ガード・**パス自身による SRV ヒープバインド**（これを忘れるとクラッシュ） |
| `Engine/Src/Graphics/Render/Pass/AerialPerspectivePass.cpp` | SceneColor へ合成するパスの雛形。`DeclareResources` の宣言内容・GameView 限定ガード・`OffscreenRenderTarget` からのリソース取得 |
| `Engine/Src/Graphics/Render/Pass/RenderPass.h` | `RenderContext` / `RenderPassPhase` / パス分離契約 |
| `Engine/Src/Graphics/Render/RenderGraph.h` | Read/Write 宣言とバリア自動化の仕組み |
| `Engine/Src/Graphics/Render/RenderDomainContext.h/.cpp` | Manager の所有・初期化位置 |
| `Engine/Src/EngineSystem/EngineSystem.cpp` | `RenderContext` への Manager 配線（254行付近）・パス登録（398〜445行付近）・`ResetFrameActivation()` 呼び出し（327行付近） |
| `Engine/Src/Scene/BaseScene.cpp` `UpdateAtmosphere()`（324行付近） | シーン → Manager へのフレーム更新の流し方（雲はここに 1 呼び出しを追加する） |
| `Engine/Src/Graphics/Pipeline/CustomShaderPipeline.h` | ランタイム DXC コンパイル + リフレクション由来ルートシグネチャ。`GetComputeRootParamIndex("リソース名")` でスロット解決 |
| `Engine/Src/Graphics/Shader/ICustomShaderProvider.h` | シェーダーは**ファイル名のみ**指定（AssetDatabase がディレクトリ自動解決 → **プロジェクト全体でファイル名の一意性が必要**） |
| `Engine/Assets/Shaders/Atmosphere/Common/AtmosphereCommon.hlsli` | `AtmosphereConstants`（192バイト）・`SkyViewParamsToUv` ・`SampleTransmittanceToSun` — 雲シェーダーから再利用する |
| `Engine/Assets/Shaders/Atmosphere/AerialPerspective.CS.hlsl` | 深度からのワールド座標復元・SceneColor 合成 CS の実例 |

### 既に存在する（＝新規開発不要な）基盤

- **3D テクスチャ生成**: `AtmosphereManager.cpp` 132〜167 行（Camera Volume LUT）が
  `D3D12_RESOURCE_DIMENSION_TEXTURE3D` + SRV/UAV 作成の完全な実例
- **ランタイム CS コンパイル**: `CustomShaderPipeline::Build`（.hlsl 編集はリビルド不要）
- **リソース状態遷移**: `ResourceBarrierHelper::Transition / UAV`
- **静的サンプラー**: シェーダーで `SamplerState` を宣言するとリフレクションが既定設定
  （LINEAR / **WRAP**。`RootSignatureConfig.h:25`）の static sampler を自動生成する。
  タイリングノイズには WRAP がそのまま使える。CLAMP が必要な箇所は UV を手動クランプで代替する
- **HDR パイプライン**: SceneColor は `R16G16B16A16_FLOAT`。雲の輝度は空と同じドメイン
  （`luminance * sunColor * sunIntensity`）で出力すればトーンマップまで一貫する

---

## 4. アーキテクチャ全体像

### データフロー

```
[初回のみ / ノイズパラメータ変更時]
  CloudBaseShapeNoise.CS ──▶ BaseShapeNoise (Texture3D 128^3 RGBA8, tileable)
  CloudDetailNoise.CS    ──▶ DetailNoise    (Texture3D  32^3 RGBA8, tileable)
  CloudWeatherMap.CS     ──▶ WeatherMap     (Texture2D 512^2 RGBA8, tileable)

[毎フレーム（雲が有効なシーンのみ）]
  BaseScene::UpdateAtmosphere()
    └─ AtmosphereManager::Update(...)            … 既存
    └─ VolumetricCloudManager::Update(...)       … 追加（cloudsActive_ を立てる）

  RenderGraph (GameView):
    FrameSetup:  AtmosphereLUTPass(既存, prio10) → VolumetricCloudNoisePass(新規, prio20)
    ...
    Sky:         GeometryPass(prio0) → SkyBoxQueuePass(prio10)
                 → VolumetricCloudPass(新規, prio20)
                     ├─ CloudRayMarch.CS : 半解像度で雲をレイマーチ
                     │    in : SceneDepth, ノイズ3種, 大気CB+LUT(Transmittance/SkyView), 雲CB
                     │    out: CloudBuffer (半解像度 R16G16B16A16, rgb=前乗算輝度, a=透過率)
                     └─ CloudComposite.CS: SceneColor と合成
                          in : SceneColor(SRV), CloudBuffer, SceneDepth
                          out: 中間UAV → CopyResource で SceneColor へ書き戻し
    Transparent: 以降は既存のまま
```

### パス配置の理由

- **VolumetricCloudNoisePass = FrameSetup, priority 20**:
  AtmosphereLUTPass(10) の後。ノイズはダーティ時のみ生成（通常フレームは即 return）
- **VolumetricCloudPass = Sky, priority 20**:
  SkyBoxQueuePass(10) が大気の空（太陽ディスク含む）を SceneColor に描いた**直後**。
  雲は `SceneColor = cloudColor + SceneColor * transmittance` で上に合成するため、
  太陽ディスクの遮蔽・空との馴染みが自動的に正しくなる。
  Transparent フェーズより前なので、半透明オブジェクトは雲の手前に描かれる（正しい）

### 有効化制御（3段ガード）

1. **シーン単位**: `BaseScene::UpdateAtmosphere()` は SkyBox が大気散乱モードのときだけ
   `VolumetricCloudManager::Update()` を呼ぶ → `cloudsActive_` が立つ（毎フレーム リセット）
2. **機能単位**: `VolumetricCloudManager::SetEnabled(bool)`（既定 true。シーンから雲だけ OFF にできる）
3. **View 単位**: 両パスとも `viewType != GameView` は即 return（ReflectionView へは描かない）

---

## 5. アルゴリズム仕様

### 5.1 座標系と雲層モデル

- ワールドは **1 unit = 1 m**。大気散乱と同じく「ワールドを惑星スケールへ変換しない」方針
- 惑星中心はカメラ基準で `float3(cameraPos.x, groundLevelY - planetRadius, cameraPos.z)`
  とする（大気が「カメラ Y のみを惑星半径へ変換する」のと同じ思想。
  地平線方向で雲層が正しく湾曲して見える）
- 雲層 = 惑星中心を共有する球殻:
  - 内殻半径 `Rin  = planetRadius + layerBottomAltitudeM`（既定 1,500 m）
  - 外殻半径 `Rout = Rin + layerThicknessM`（既定 4,000 m）
- 高さ率 `heightFraction h∈[0,1] = (distance(pos, planetCenter) - Rin) / layerThicknessM`

**レイと球殻の交差**（カメラが層より下にある通常ケース）:

```
tEntry = raySphereIntersectFar側(Rin)   … 内殻との遠い方の交点（=層の入口）
tExit  = raySphereIntersectFar側(Rout)  … 外殻との遠い方の交点（=層の出口）
```

カメラが層内・層上にある場合も破綻しないよう、一般化した区間計算を実装すること
（`raySphereIntersect` が返す [t0,t1] 2 区間の組合せで `[marchStart, marchEnd]` を決める）。
`marchEnd` はさらに `min(marchEnd, maxMarchDistanceM, 不透明ジオメトリまでの距離)` でクランプ。
`marchStart >= marchEnd` なら `output = float4(0,0,0,1)`（雲なし・透過率1）で終了。

### 5.2 ノイズテクスチャ（全て CS で手続き生成・タイル可能）

「タイル可能」は**ラップした整数格子**で Perlin 勾配 / Worley セル点を取ることで実現する
（周波数 = 格子分割数を整数にし、格子座標を `mod(x, freq)` でラップ）。

#### (a) BaseShapeNoise — `Texture3D 128×128×128 RGBA8_UNORM`

Hillaire の TileableVolumeNoise と同じ構成:

- **R**: Perlin-Worley。`perlinFBM`（基本周波数 4、5 オクターブ、lacunarity 2, gain 0.5 を [0,1] 正規化）
  と `worleyFBM_low`（周波数 4 起点の 3 オクターブ合成: `w(4)*0.625 + w(8)*0.25 + w(16)*0.125`）を
  `R = remap(perlinFBM, 0.0, 1.0, worleyFBM_low, 1.0)` で合成
- **G**: `worleyFBM(freq 8)`、**B**: `worleyFBM(freq 16)`、**A**: `worleyFBM(freq 32)`
  （各 `worleyFBM(f) = w(f)*0.625 + w(2f)*0.25 + w(4f)*0.125`）
- Worley 値は「1 - 正規化最近傍距離」（＝セル中心が明るい、雲の綿的な形）

#### (b) DetailNoise — `Texture3D 32×32×32 RGBA8_UNORM`

- **R**: `worleyFBM(freq 2)`、**G**: `worleyFBM(freq 4)`、**B**: `worleyFBM(freq 8)`、**A**: 未使用(1.0)

#### (c) WeatherMap — `Texture2D 512×512 RGBA8_UNORM`

手続き生成（将来テクスチャ差し替え可能な設計にする）:

- **R = カバレッジ**: `saturate(perlinFBM2D(freq 4, 5oct) * 1.2 - 0.1)` 程度。
  広域の雲の有無を決める
- **G = 雲タイプ**: `perlinFBM2D(freq 2, 3oct)`（0=層雲 〜 1=積乱雲）
- **B = 降水/密度倍率**: 当面 1.0 固定
- **A**: 未使用(1.0)

#### 共通ユーティリティ（`CloudNoiseCommon.hlsli` に置く）

- 整数ハッシュ（例: PCG3D または iq の `hash33`。**再現性のため時刻非依存**）
- `perlinNoise3D(p, freq)`（勾配ノイズ、fade = 6t^5-15t^4+10t^3、格子ラップ）
- `worleyNoise3D(p, freq)`（3×3×3 近傍セル走査、格子ラップ、`1 - normalizedDist`）
- `remap(v, l0, h0, l1, h1) = l1 + (v - l0) * (h1 - l1) / (h0 - l0)`（雲実装全域で使用）

### 5.3 密度関数 `SampleCloudDensity(worldPos, heightFraction, cheap)`

GPU Pro 7 / Schneider 方式:

```hlsl
// 1) 風による移流（ワールド XZ 平面）
float3 wind = float3(windDirX, 0, windDirZ) * windSpeedMPerS * timeSec;
float3 sampleWS = worldPos + wind;
// 形状の時間変化を出すため、高度に応じて風上へせり出すスキュー（Nubis）
sampleWS += heightFraction * float3(windDirX, 0, windDirZ) * 500.0f;

// 2) ベース形状
float4 base = gBaseShapeNoise.SampleLevel(gSamplerLinearWrap, sampleWS / baseNoiseScaleM, 0);
float lowFreqFBM = base.g * 0.625f + base.b * 0.25f + base.a * 0.125f;
float baseCloud = remap(base.r, -(1.0f - lowFreqFBM), 1.0f, 0.0f, 1.0f);

// 3) 高度勾配（weather.g の雲タイプで 3 曲線をブレンド）
float gStratus  = remap(h, 0.00f, 0.10f, 0, 1) * remap(h, 0.20f, 0.30f, 1, 0);
float gCumulus  = remap(h, 0.00f, 0.25f, 0, 1) * remap(h, 0.30f, 0.65f, 1, 0);
float gCumulonimbus = remap(h, 0.00f, 0.10f, 0, 1) * remap(h, 0.70f, 1.00f, 1, 0);
// type<0.5: stratus↔cumulus, type>0.5: cumulus↔cumulonimbus を lerp
baseCloud *= heightGradient;

// 4) カバレッジ適用（weather map は worldPos.xz / weatherMapScaleM でサンプル）
float coverage = weather.r * globalCoverage;
float cloudWithCoverage = remap(baseCloud, 1.0f - coverage, 1.0f, 0.0f, 1.0f);
cloudWithCoverage *= coverage;   // 縁を柔らかくアンビル状を防ぐ（GPU Pro 7）

// 5) ディテール侵食（cheap == true のライトマーチ時はスキップして高速化）
float4 detail = gDetailNoise.SampleLevel(gSamplerLinearWrap, sampleWS / detailNoiseScaleM, 0);
float highFreqFBM = detail.r * 0.625f + detail.g * 0.25f + detail.b * 0.125f;
// 層底では billowy、上部では wispy に：高さで反転
float highFreqModifier = lerp(highFreqFBM, 1.0f - highFreqFBM, saturate(h * 10.0f));
float finalCloud = remap(cloudWithCoverage,
                         highFreqModifier * detailErosionStrength, 1.0f, 0.0f, 1.0f);

return saturate(finalCloud);   // [0,1] の無次元密度
```

remap の分母 0 と負値に注意し、結果は必ず `saturate` すること。

### 5.4 レイマーチング（`CloudRayMarch.CS.hlsl`）

半解像度（`ceil(W/2) × ceil(H/2)`）で 1 ピクセル 1 レイ。

1. **レイ生成**: ピクセル中心 → NDC → `invViewProj` でワールド方向へ
   （`AerialPerspective.CS.hlsl` の復元コードを踏襲）
2. **深度による遮蔽**: 対応するフル解像度 SceneDepth を `Load`（左上 1 点で可）。
   `depth < 0.9999999`（※大気と同じしきい値。far=50km 対策で 9 が 7 個）なら不透明あり:
   NDC+depth を `invViewProj` で復元し `distToOpaque = length(worldPos - cameraPos)` を計算、
   `marchEnd = min(marchEnd, distToOpaque)`
3. **ステップ**: 区間長 `L = marchEnd - marchStart` を `N = maxSteps`（既定 64）で等分割
   `dt = L / N`。開始位置はバンディング防止のため IGN
   （Interleaved Gradient Noise: `frac(52.9829189 * frac(0.06711056*x + 0.00583715*y))`）で
   `t += dt * ign` ジッタ
4. **積分ループ**（エネルギー保存・Frostbite 式）:

```hlsl
float3 color = 0; float T = 1.0;
for (i = 0; i < N; ++i) {
    float3 pos = cameraPos + rayDir * t;
    float h = heightFractionAt(pos);
    float density = SampleCloudDensity(pos, h, false);
    if (density > 0.0f) {
        float sigmaS = density * densityScale;          // [1/m] 散乱係数（albedo≒1）
        float sigmaE = max(sigmaS, 1e-7f);              // 消散係数
        float3 sunLum = SunLuminanceAt(pos, h, density);   // 5.5 参照
        float3 ambient = AmbientLuminance(h);              // 5.5 参照
        float3 S = sigmaS * (sunLum + ambient);
        float3 Sint = (S - S * exp(-sigmaE * dt)) / sigmaE;
        color += T * Sint;
        T *= exp(-sigmaE * dt);
        if (T < earlyExitTransmittance) { T = 0; break; }  // 早期終了
    }
    t += dt;
}
// 遠距離フェード（層が地平線で無限に伸びないように）
float fade = saturate(1.0 - (marchStart - startFadeDistanceM未満は0) ... );  // Phase 5 で調整
gCloudOutput[pix] = float4(color, T);
```

出力は**前乗算アルファ**形式（rgb = すでに T を織り込んだ輝度、a = 透過率）。

### 5.5 ライティング

#### 太陽光 `SunLuminanceAt(pos, h, density)`

```hlsl
// (1) 雲自身によるセルフシャドウ: 太陽方向へ 6 サンプルの粗いマーチ
float densitySum = 0;
float3 toSun = -gCloud.sunDirection;                    // 大気と同じ規約（sunDirection=光の進行方向）
[unroll] for (j = 1; j <= 5; ++j) {
    float3 sp = pos + toSun * (lightMarchStepM * j);
    densitySum += SampleCloudDensity(sp, heightAt(sp), true) * lightMarchStepM;
}
// 遠距離サンプル 1 本（濃い雲塊の影を拾う。Nubis の long-range sample）
float3 spFar = pos + toSun * (lightMarchStepM * 15.0f);
densitySum += SampleCloudDensity(spFar, heightAt(spFar), true) * lightMarchStepM * 3.0f;

float tauSun = densitySum * densityScale;
float cosTheta = dot(rayDir, toSun);

// (2) 多重散乱の近似（Hillaire / Frostbite の N オクターブ法）＋ 各オクターブの Beer-Powder
// ※ 実装時に判明: 単一散乱のみだと tauSun が 16〜80 に達し exp(-tauSun)≈0 で
//    雲の内部が真っ黒になる（実際の雲が白いのは多重散乱のため）。必須。
float3 energy = 0;
float attenuation = 1, contribution = 1, eccentricity = 1;
[unroll] for (int o = 0; o < 3; ++o) {
    float phase = lerp(HG(phaseG0 * eccentricity, cosTheta),
                       HG(phaseG1 * eccentricity, cosTheta), phaseBlend);
    float tau = tauSun * attenuation;
    float beer = exp(-tau);
    float powder = 1.0f - exp(-tau * 2.0f);
    float lightEnergy = lerp(beer, beer * powder * 2.0f, beerPowderStrength * 0.5f);
    energy += contribution * phase * lightEnergy;
    attenuation *= 0.25f; contribution *= 0.5f; eccentricity *= 0.5f;
}
// 位相関数は 1/4π 正規化（球面積分=1）。sunIntensity は放射照度スケールなので
// 4π を掛けて放射輝度スケールへ戻す（等方散乱で phase*4π = 1）。これが無いと雲が空より暗くなる。
energy *= 4.0f * PI;

// (3) 大気透過率（夕暮れに雲が赤くなる要）: AtmosphereCommon.hlsli の関数を再利用
//     位置は大気座標系（カメラ=(0, cameraRadiusKm, 0)、km 単位）へ: 高度のみ反映で十分
float3 posAtmo = float3(0, gAtmosphere.cameraRadiusKm + (pos.y - cameraPos.y) * 0.001f, 0);
float3 sunTrans = SampleTransmittanceToSun(gTransmittanceLUT, gLUTSampler, posAtmo, toSun, gAtmosphere);

return sunTrans * energy * gCloud.sunColor * gCloud.sunIntensity;
```

> **実装記録（重要）**: 当初の単一散乱＋Powder のみの式では、`densityScale=0.05 [1/m]` ×
> 光路 1600m で `tauSun` が 16〜80 に達し、`exp(-tauSun)=0` により濃い雲の内部が
> 真っ黒になった（雲が空より暗い青灰色の染みとして描画される）。上記の
> 多重散乱オクターブ近似と 4π 補正の 2 点を追加して解決した。

`HG(c, g) = (1 - g*g) / (4π * pow(1 + g*g - 2*g*c, 1.5))`

#### アンビエント `AmbientLuminance(h)`

Sky-View LUT の天頂付近 1 サンプルを半球平均の代用とする:

```hlsl
float2 uvZenith = SkyViewParamsToUv(false, 0.7f /*やや上向き*/, 0.0f,
                                    radiusKm, gAtmosphere.planetRadiusKm);
float3 skyLum = gSkyViewLUT.SampleLevel(gLUTSampler, uvZenith, 0).rgb
              * gAtmosphere.sunColor * gAtmosphere.sunIntensity;   // 空と同じ輝度ドメインへ
// 層の底ほど暗く（遮蔽近似）
return skyLum * ambientIntensity * lerp(0.5f, 1.0f, h);
```

> 輝度ドメインの整合が最重要:
> `SkyAtmosphere.PS.hlsl` は最終色を `luminance * sunColor * sunIntensity` で出力している。
> 雲も**必ず同じ係数を掛けて**出力すること。さもないと空と雲の明るさが乖離する。

### 5.6 合成・アップサンプル（`CloudComposite.CS.hlsl`）

フル解像度 1 ピクセルごと:

```hlsl
float4 scene = gSceneColor.Load(pix);
float  depth = gSceneDepth.Load(pix);
float4 cloud = gCloudBuffer.SampleLevel(gSamplerLinearWrap, clampUv(uv), 0);  // 半解像度をバイリニア

// 不透明ジオメトリが雲層入口より手前 → 雲を描かない（半解像度縁アーティファクト対策）
if (depth < 0.9999999f && distToOpaque(depth, pix) < cloudNearestPossibleDistance) {
    gOutput[pix] = scene;
} else {
    gOutput[pix] = float4(cloud.rgb + scene.rgb * cloud.a, scene.a);
}
```

- `clampUv` = 半テクセル内側へクランプ（WRAP サンプラーで画面端が反対側と混ざるのを防ぐ）
- `cloudNearestPossibleDistance` はレイマーチ CS が使った `marchStart` の下限
  （カメラ高度から解析計算できる。実装簡略化として「`depth < しきい値` かつ
  `distToOpaque < layerBottomAltitudeM` なら passthrough」で十分）
- 書き込み先は SceneColor と同サイズの中間 UAV。`ApplyAerialPerspective`
  （`AtmosphereManager.cpp:435-503`）と同一の手順で
  遷移 → Dispatch → UAV バリア → CopyResource → `PIXEL_SHADER_RESOURCE` へ復帰

---

## 6. 実装仕様（C++）

### 6.1 新規ファイル一覧と vcxproj 登録

**C++（全て CoreEngine.vcxproj と CoreEngine.vcxproj.filters の両方へ手動登録が必要）**:

| ファイル | 内容 |
|---|---|
| `Engine/Src/Graphics/Cloud/VolumetricCloudManager.h/.cpp` | 雲システム本体 |
| `Engine/Src/Graphics/Render/Pass/VolumetricCloudNoisePass.h/.cpp` | ノイズ生成パス |
| `Engine/Src/Graphics/Render/Pass/VolumetricCloudPass.h/.cpp` | レイマーチ+合成パス |
| `Application/Src/Sample/SampleScene/VolumetricCloudTestScene/VolumetricCloudTestScene.h/.cpp` | 検証シーン |
| `Application/Src/Sample/SampleScene/VolumetricCloudTestScene/CloudEditorFacade.h/.cpp` | ImGui 編集パネル（AtmosphereEditorFacade を踏襲） |
| `Application/Src/Sample/SampleScene/VolumetricCloudTestScene/README.md` | シーン設計記録（AtmosphereTestScene/README.md を踏襲） |

**HLSL（vcxproj 登録不要。ランタイム DXC コンパイル・編集はリビルド不要）**:

| ファイル | 種別 |
|---|---|
| `Engine/Assets/Shaders/Cloud/Common/CloudNoiseCommon.hlsli` | ハッシュ / Perlin / Worley / remap |
| `Engine/Assets/Shaders/Cloud/Common/CloudCommon.hlsli` | `CloudConstants`・密度関数・位相関数 |
| `Engine/Assets/Shaders/Cloud/CloudBaseShapeNoise.CS.hlsl` | 128³ ノイズ生成 |
| `Engine/Assets/Shaders/Cloud/CloudDetailNoise.CS.hlsl` | 32³ ノイズ生成 |
| `Engine/Assets/Shaders/Cloud/CloudWeatherMap.CS.hlsl` | 512² 天候マップ生成 |
| `Engine/Assets/Shaders/Cloud/CloudRayMarch.CS.hlsl` | 雲レイマーチ |
| `Engine/Assets/Shaders/Cloud/CloudComposite.CS.hlsl` | SceneColor 合成 |

> - `.meta` は AssetDatabase が初回起動時に自動生成する（既存 hlsl と同様）
> - シェーダーファイル名はプロジェクト全体で一意にすること（AssetDatabase がファイル名だけで解決するため）
> - hlsli の include は `#include "Common/CloudNoiseCommon.hlsli"` のように
>   Atmosphere シェーダーと同じ相対形式に従う。大気の共通ヘッダーを使う場合は
>   `#include "../Atmosphere/Common/AtmosphereCommon.hlsli"`（DXC の include 解決が
>   相対パスで通ることは AtmosphereCommon 実績から既知。**通らない場合は
>   必要な関数・構造体を CloudCommon.hlsli へ複製し、複製元をコメントで明記する**）

### 6.2 `VolumetricCloudManager` 詳細設計

`AtmosphereManager` と同じ形の Manager。ヘッダー骨子（実装時はこの API を厳守）:

```cpp
#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "Math/MathCore.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

namespace CoreEngine
{
    class AtmosphereManager;
    class DescriptorManager;

    /// @brief 雲シェーダーへ渡す定数バッファ（6.3 のレイアウトと一致・208 バイト）
    struct VolumetricCloudShaderConstants { /* 6.3 参照 */ };

    /// @brief 雲の見た目パラメータ（単位はメートル・秒）
    struct VolumetricCloudParameters { /* 12 章の既定値一覧に準拠 */ };

    class VolumetricCloudManager {
    public:
        static constexpr uint32_t kBaseShapeNoiseSize = 128;
        static constexpr uint32_t kDetailNoiseSize    = 32;
        static constexpr uint32_t kWeatherMapSize     = 512;

        void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager);

        /// @brief フレーム更新（雲を使うシーンの BaseScene::UpdateAtmosphere から呼ばれる）
        /// @details cloudsActive_ を立て、カメラ・時刻・太陽情報を CB へ反映する。
        ///          太陽情報は atmosphereManager から取得（単一情報源）。
        void Update(const Vector3& cameraWorldPosition,
                    const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
                    const AtmosphereManager* atmosphereManager,
                    float timeSec);

        // パラメータ（変更してもノイズ再生成は不要。ノイズ系パラメータのみ MarkNoiseDirty）
        const VolumetricCloudParameters& GetParameters() const;
        VolumetricCloudParameters& GetParametersMutable();
        void MarkNoiseDirty();                 // ノイズ形状パラメータ変更時のみ

        void SetEnabled(bool e);               // 機能単位トグル（既定 true）
        bool IsEnabled() const;

        // フレーム有効化（AtmosphereManager と同一パターン）
        bool AreCloudsActive() const;          // Update() が呼ばれ、かつ enabled_
        void ResetFrameActivation();

        // VolumetricCloudNoisePass から毎フレーム呼ばれる（ダーティ時のみ再生成）
        void GenerateNoiseTexturesIfNeeded(ID3D12GraphicsCommandList* cmdList);
        bool AreNoiseTexturesReady() const;

        // VolumetricCloudPass から呼ばれるフレーム本処理
        /// @param sceneColor / sceneColorState / sceneColorSrvHandle : SceneColor 一式
        /// @param depthSrvHandle : SceneDepth の SRV
        /// @param atmosphereManager : LUT SRV と大気 CB アドレスの取得元
        void RenderClouds(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* sceneColor,
            D3D12_RESOURCE_STATES& sceneColorState,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
            const AtmosphereManager* atmosphereManager);

    private:
        void UploadConstants();
        bool CreateNoiseResources(ID3D12Device*, DescriptorManager*);
        bool CreatePipelines(ID3D12Device*);
        bool EnsureCloudTargets(ID3D12Resource* sceneColor); // 半解像度バッファ+中間UAV を SceneColor サイズ追従で確保

        // ICustomShaderProvider 実装（AtmosphereManager の Provider 構造体群を踏襲）
        // BaseShape / Detail / Weather / RayMarch / Composite の 5 つ
        ...
    };
}
```

実装上の規定:

- **CB は永続マップ**（`ResourceFactory::CreateBufferResource` + `Map`）。
  `static_assert(sizeof(VolumetricCloudShaderConstants) == 208, "...")` を必ず入れる
- ノイズ 3 テクスチャは `ResourceFactory::CreateTextureResource` +
  `DescriptorManager::CreateSRV/CreateUAV`（`AtmosphereManager::CreateLUTResources` と同型。
  デバッグ名は `"CloudBaseShapeSRV"` 等）。フォーマットは `DXGI_FORMAT_R8G8B8A8_UNORM`
- 半解像度 CloudBuffer と合成用中間テクスチャは `EnsureAerialPerspectiveTarget` と同じ
  「SceneColor サイズ追従・遅延確保・リサイズ時再確保」方式。
  CloudBuffer は `R16G16B16A16_FLOAT`
- `RenderClouds` 内のバリア手順は `ApplyAerialPerspective` を一字レベルで踏襲する
  （SceneColor: → NON_PIXEL_SHADER_RESOURCE で読み → 合成後 COPY_DEST → CopyResource →
  PIXEL_SHADER_RESOURCE へ復帰。CloudBuffer: UAV→NON_PIXEL→UAV）
- ルートパラメータは `GetComputeRootParamIndex("gXxx")` で名前解決し、`>= 0` チェックの上バインド
- 初期化完了・ノイズ生成完了・パイプライン構築失敗は
  `Logger`（**spdlog 形式 `{}` プレースホルダ**）で必ずログする

### 6.3 定数バッファレイアウト（C++ / HLSL 完全一致・208 バイト）

```
offset  C++ (VolumetricCloudShaderConstants)          HLSL (CloudConstants, b0)
  0     Matrix4x4 invViewProj;                        float4x4 invViewProj;
 64     Vector3 cameraWorldPos;  float timeSec;       float3 cameraWorldPos;  float timeSec;
 80     Vector3 sunDirection;    float sunIntensity;  float3 sunDirection;    float sunIntensity;
 96     Vector3 sunColor;        float planetRadiusM; float3 sunColor;        float planetRadiusM;
112     float layerBottomAltitudeM; float layerThicknessM;
        float groundLevelY;         float globalCoverage;
128     float baseNoiseScaleM;   float detailNoiseScaleM;
        float detailErosionStrength; float densityScale;
144     float windDirX; float windDirZ;
        float windSpeedMPerS;    float weatherMapScaleM;
160     float phaseG0; float phaseG1; float phaseBlend; float ambientIntensity;
176     float beerPowderStrength; float lightMarchStepM;
        float earlyExitTransmittance; float maxMarchDistanceM;
192     uint32_t maxSteps; uint32_t outputWidth;
        uint32_t outputHeight; uint32_t frameIndex;
208     = sizeof
```

- `sunDirection` は**光の進行方向**（大気と同じ規約）。太陽の方向は `-sunDirection`
- `planetRadiusM` はメートル（レイ・球殻交差用）。大気 CB 側は km なので混同しないこと
- `outputWidth/Height` は**半解像度バッファの**サイズ。合成 CS はフル解像度サイズを
  別途 `SceneColor` の Desc から取得せず、`outputWidth*2` 前提にしない —
  合成 CS 用には `frameIndex` 行を流用せず、Dispatch を SceneColor サイズから計算し
  シェーダーでは `gOutput` の `GetDimensions` を使う（サイズ二重管理を避ける）

### 6.4 `VolumetricCloudNoisePass`

```cpp
class VolumetricCloudNoisePass : public RenderPass {
    const char* GetName() const override { return "VolumetricCloudNoisePass"; }
    // DeclareResources: 宣言なし（自己完結リソースのみ。AtmosphereLUTPass と同じ）
    void Execute(const RenderContext& context) override;
};
```

`Execute` の内容（`AtmosphereLUTPass::Execute` を踏襲）:

1. `context.dxCommon` / `context.volumetricCloudManager` null チェック
2. `!AreCloudsActive()` なら return（雲非対応シーンで一切動かない）
3. **`SetDescriptorHeaps` をパス自身で行う**（忘れるとフレーム先頭実行時にクラッシュ）
4. `GenerateNoiseTexturesIfNeeded(cmdList)`（内部でダーティフラグ判定。
   生成後は各テクスチャを `NON_PIXEL_SHADER_RESOURCE` へ遷移して保持）

### 6.5 `VolumetricCloudPass`

```cpp
class VolumetricCloudPass : public RenderPass {
    const char* GetName() const override { return "VolumetricCloudPass"; }
    void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override {
        // AerialPerspectivePass と同一の宣言（SceneColor 読み書き + SceneDepth 読み）
        builder.Read(FrameBlackboard::SceneDepth,
            D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    void Execute(const RenderContext& context) override;
};
```

`Execute`（`AerialPerspectivePass::Execute` を踏襲）:

1. `viewType != GameView` → return
2. null チェック（dxCommon / volumetricCloudManager / atmosphereManager / renderTargetManager）
3. `!AreCloudsActive()` → return、`!atmosphereManager->AreLUTsReady()` → return
4. `SetDescriptorHeaps`
5. SceneColor を `viewSettings.sceneColorTargetName` から `OffscreenRenderTarget*` 取得
6. `volumetricCloudManager->RenderClouds(...)` 呼び出し

### 6.6 エンジン既存コードへの変更点（全変更を列挙）

| ファイル | 変更 |
|---|---|
| `Engine/Src/Graphics/Render/Pass/RenderPass.h` | 前方宣言 `class VolumetricCloudManager;` 追加、`RenderContext` に `VolumetricCloudManager* volumetricCloudManager = nullptr;` 追加 |
| `Engine/Src/Graphics/Render/RenderDomainContext.h/.cpp` | `std::unique_ptr<VolumetricCloudManager>` 所有・`GetVolumetricCloudManager()`・`Initialize()` 内で AtmosphereManager の直後に初期化・`Shutdown()` で最初に reset |
| `Engine/Src/EngineSystem/EngineSystem.cpp` | (a) 254 行付近: `context.volumetricCloudManager = renderDomainContext_ ? renderDomainContext_->GetVolumetricCloudManager() : nullptr;` (b) 402 行付近: `AddPass(std::make_unique<VolumetricCloudNoisePass>(), RenderPassPhase::FrameSetup, 20);` (c) 431 行付近: `AddPass(std::make_unique<VolumetricCloudPass>(), RenderPassPhase::Sky, 20);` (d) 327 行付近の `ResetFrameActivation()` の隣で雲側もリセット |
| `Engine/Src/Scene/BaseScene.cpp` `UpdateAtmosphere()` | 既存の `atmosphereManager->Update(...)` の直後に、`domainContext->GetVolumetricCloudManager()` を取得し `Update(cameraPosition, viewMatrix, projMatrix, atmosphereManager, 累積時間)` を呼ぶ（大気モード時のみ、という既存ガードの内側なので追加ガード不要）。累積時間はシーン側で `deltaTime` を足し込むか、エンジンの時間取得 API があればそれを使う |
| `Application/Src/MyGame.cpp` | `RegisterScene<VolumetricCloudTestScene>("VolumetricCloudTestScene");` 追加 |

**変更しないもの**: `SkyBoxObject` / `SkyBoxRenderer` / `Skybox.PS.hlsl` /
`SkyAtmosphere.PS.hlsl` / `FrameBlackboard`（新しい論理名は不要。雲の中間リソースは
Manager 所有で AP パターンに倣うため）/ 既存パスすべて。

### 6.7 検証シーン `VolumetricCloudTestScene`

`AtmosphereTestScene` のコピーが出発点:

- `OnInitialize()` で `SetSceneName("VolumetricCloudTestScene")`、床 Plane 1 枚
- **太陽の明示上書き**（必須。忘れると空が暗い紺になる既知不具合）:
  `AtmosphereEditorSunSettings` 既定（intensity=20）と同様に
  `directionalLight_->direction / intensity` を `OnInitialize()` で設定
- SkyBox は自前生成もテクスチャ設定もしない（`SetupDefaultSky()` が大気モードで自動生成）
- `CloudEditorFacade`: ImGui でカバレッジ・密度・風・位相 g・ステップ数等を編集。
  ノイズ形状系（scale 等）の変更時は `MarkNoiseDirty()` を呼ぶ

---

## 7. シェーダーファイル仕様（HLSL）

全 CS 共通: リソース名はここに書いた名前を**そのまま**使う
（C++ 側が `GetComputeRootParamIndex("gCloud")` 等の名前で解決するため）。

### 7.1 `CloudBaseShapeNoise.CS.hlsl`
- `RWTexture3D<float4> gOutput : register(u0);`
- `[numthreads(4,4,4)]`、Dispatch = (32,32,32)
- uvw = `(dtid + 0.5) / 128.0` で 5.2(a) を生成

### 7.2 `CloudDetailNoise.CS.hlsl`
- `RWTexture3D<float4> gOutput : register(u0);`
- `[numthreads(4,4,4)]`、Dispatch = (8,8,8)

### 7.3 `CloudWeatherMap.CS.hlsl`
- `RWTexture2D<float4> gOutput : register(u0);`
- `[numthreads(8,8,1)]`、Dispatch = (64,64,1)

### 7.4 `CloudRayMarch.CS.hlsl`
```hlsl
ConstantBuffer<CloudConstants>      gCloud      : register(b0);
ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b1);
Texture3D<float4> gBaseShapeNoise   : register(t0);
Texture3D<float4> gDetailNoise      : register(t1);
Texture2D<float4> gWeatherMap       : register(t2);
Texture2D<float>  gSceneDepth       : register(t3);
Texture2D<float4> gTransmittanceLUT : register(t4);
Texture2D<float4> gSkyViewLUT       : register(t5);
SamplerState gSamplerLinearWrap     : register(s0);   // 既定 static sampler = LINEAR/WRAP
RWTexture2D<float4> gCloudOutput    : register(u0);   // 半解像度
[numthreads(8,8,1)]
```
Dispatch = `((halfW+7)/8, (halfH+7)/8, 1)`。
深度は `gSceneDepth.Load(int3(pix*2, 0))`（対応フル解像度座標）。

### 7.5 `CloudComposite.CS.hlsl`
```hlsl
ConstantBuffer<CloudConstants> gCloud : register(b0);
Texture2D<float4> gSceneColor  : register(t0);
Texture2D<float4> gCloudBuffer : register(t1);
Texture2D<float>  gSceneDepth  : register(t2);
SamplerState gSamplerLinearWrap : register(s0);
RWTexture2D<float4> gOutput    : register(u0);   // フル解像度中間テクスチャ
[numthreads(8,8,1)]
```
Dispatch = `((W+7)/8, (H+7)/8, 1)`。5.6 の合成式。

### 7.6 C++ 側 ShaderProvider

`AtmosphereManager` 内の Provider 構造体群と同形式で 5 つ定義:

```cpp
struct CloudRayMarchShaderProvider final : ICustomShaderProvider {
    std::wstring GetComputeShaderPath() const override { return L"CloudRayMarch.CS.hlsl"; }
};
// 他 4 つも同様（ファイル名のみ。AssetDatabase がディレクトリを解決）
```

---

## 8. 足りないエンジン機能と対応方針

調査の結果、**ブロッカーとなる欠落機能は無い**。必要なものは全て既存パターンの拡張で賄える。

| # | 欠落 | 影響 | 対応 |
|---|---|---|---|
| 1 | `RenderContext` / `RenderDomainContext` に雲 Manager が無い | 雲パスが Manager へ到達できない | 6.6 の通りメンバー追加（大気と同型。Phase 0 で実施） |
| 2 | `Texture3D` のミップチェーン生成機能が無い | ノイズの距離 LOD が mip0 固定 | **今回は mip なしで実装**（サンプルは常に `SampleLevel(0)`）。遠景の高周波ちらつきは detailErosion の距離フェード（Phase 5）で軽減。ミップ対応は将来拡張（13 章） |
| 3 | 2D/3D テクスチャアセット（外部ファイル）の 3D テクスチャロードが無い | 天候マップ・ノイズを外部アセット化できない | 全て手続き生成 CS で賄う（本設計の方針）。外部化は将来拡張 |
| 4 | 時間積算 API がシーン層に無い可能性 | 風アニメーションの time 供給 | `BaseScene::UpdateAtmosphere` 拡張時にシーン側の deltaTime 積算 or 既存の時間ユーティリティ（`Framework` / `EngineSystem` 周辺を実装時に確認）を使用。無ければ `VolumetricCloudManager` 内部で `Update()` 呼び出し毎に固定 1/60 を積算しても実用上問題ない（可変FPSでも見た目の破綻はない） |
| 5 | 履歴バッファ / TAA 的テンポラル再投影基盤 | 品質向上（テンポラル再構成）ができない | 初期実装はスコープ外（Phase 7・任意）。MotionVector GBuffer は既存なので将来利用可能 |
| 6 | CLAMP 静的サンプラーの簡易指定 | 画面端サンプリング | UV 手動クランプで代替（5.6）。エンジン改修不要 |
| 7 | 雲 → 地面への影（Cloud Shadow Map） | 地表に雲影が落ちない | スコープ外（13 章）。ShadowMask 系への統合は大掛かりなため独立課題とする |

---

## 9. 実装フェーズ計画

**各フェーズの終わりに必ず: ビルド → 起動 → ログ確認 →（Phase 2 以降）スクリーンショット確認。
合格条件を満たすまで次フェーズへ進まない。** ビルド・起動・検証の具体的手順は 10 章。

### Phase 0: 骨格と配線（描画なし）

- 作業:
  - `VolumetricCloudManager` を新規作成（パラメータ構造体・CB 作成/永続マップ/`static_assert`・
    `Update`/`AreCloudsActive`/`ResetFrameActivation`・初期化ログまで。ノイズ/パイプラインは空実装）
  - `VolumetricCloudNoisePass` / `VolumetricCloudPass` を新規作成（ガードと null チェックのみで
    実質 no-op。`DeclareResources` は 6.5 の宣言を最初から入れる）
  - 6.6 のエンジン変更を全て実施（RenderContext / RenderDomainContext / EngineSystem / BaseScene）
  - **vcxproj / filters へ新規 .cpp/.h を登録**
- 合格条件:
  - ビルドエラー 0
  - 起動してクラッシュなし・エラーログ 0
  - ログに `VolumetricCloudManager: 初期化完了` が出る
  - WaterTestScene（初期シーン）の見た目が変わっていない
- ロールバック: このフェーズの変更は加算のみなので revert 容易

### Phase 1: ノイズ生成

- 作業:
  - `CloudNoiseCommon.hlsli`（ハッシュ/Perlin/Worley/remap/FBM）
  - `CloudBaseShapeNoise.CS` / `CloudDetailNoise.CS` / `CloudWeatherMap.CS`
  - Manager: ノイズ 3 テクスチャの生成（`CreateNoiseResources`）・CS パイプライン 3 本
    （`CreatePipelines`）・`GenerateNoiseTexturesIfNeeded`（ダーティ時のみ Dispatch →
    `NON_PIXEL_SHADER_RESOURCE` へ遷移・`noiseGenerated_` ログ）
  - NoisePass から呼ぶ
- 合格条件:
  - 起動時（初期シーン=大気シーン）にログ `VolumetricCloud: ノイズテクスチャ生成完了` が 1 回だけ出る
  - クラッシュなし・D3D12 エラーログなし
  - 非大気シーンではノイズ生成が走らないこと（ガードの確認はログで）

### Phase 2: レイマーチ最小実装（見える化）

- 作業:
  - `CloudRayMarch.CS`: レイ生成・球殻交差・深度遮蔽・固定ステップマーチまで実装。
    密度は**ベース形状ノイズ×高度勾配のみ**（weather/detail/ライティングなし）、
    ライティングは `色 = 一定アルベド × T 積分`（5.4 の積分骨格に S = sigmaS * 1.0 を入れる）
  - `CloudComposite.CS` と Manager の `RenderClouds`（バリア手順含む）・CloudBuffer/中間UAV 確保
  - `VolumetricCloudPass::Execute` を本実装
- 合格条件:
  - AtmosphereTestScene（または新設前の暫定確認として WaterTestScene）のスクリーンショットで
    **空に白〜灰色の雲状の塊が見える**
  - 地平線下・不透明オブジェクトの手前に雲が描かれていない
  - クラッシュ・エラーログなし。フレームが極端に重くない（起動して操作可能）
- 備考: この段階の見た目は「もやっとした白い塊」で良い

### Phase 3: 雲の形（密度モデリング完成）

- 作業: 5.3 を完全実装（weather map カバレッジ・雲タイプ別高度勾配・ディテール侵食・風移流+スキュー）
- 合格条件:
  - スクリーンショットで積雲らしい輪郭（底が平ら気味・上部がもこもこ）が確認できる
  - 時間経過で雲がゆっくり流れる
  - `globalCoverage` を 0 にすると雲が消え、1 で空が覆われる（暫定確認は CB 既定値の書き換えでも、
    Phase 6 の ImGui でも可）

### Phase 4: ライティング

- 作業: 5.5 を完全実装（サンライトマーチ 5+1 サンプル・Beer-Powder・二重 HG・
  Transmittance LUT による太陽色・Sky-View LUT アンビエント・輝度ドメイン統一）
- 合格条件:
  - 太陽方向の雲の縁が明るい（silver lining）
  - 太陽を背にした雲は暗く沈み、空のアンビエントで青みがかる
  - ImGui（または既定値変更）で太陽高度を下げると雲が夕焼け色になる
  - 太陽ディスクが厚い雲に隠れる

### Phase 5: 品質・パフォーマンス仕上げ

- 作業:
  - IGN ジッタ（バンディング除去）
  - 早期終了（T < 0.005）・空でない領域のみの適応ステップ
    （粗いステップで密度 0 が続く間は大股、密度に当たったら 1 歩戻って細かく — Nubis 方式。
    実装が複雑なら固定 64 步 + 早期終了のみでも合格ラインは満たせる）
  - 遠距離フェード（`maxMarchDistanceM` 付近で透過率→1 へブレンド）と
    地平線付近のディテール抑制
  - 合成の深度エッジ対策（5.6 の passthrough 条件）
  - GPU 負荷確認（ステップ数・ライトマーチ回数の調整）
- 合格条件:
  - 雲の縞状バンディングが目立たない
  - 建物・地形エッジ周りに半解像度由来のハロが無い
  - 操作していて明確なカクつきが無い（厳密なフレーム計測は任意）

### Phase 6: デフォルト背景統合・エディタ・検証シーン

- 作業:
  - `VolumetricCloudTestScene` + `CloudEditorFacade`（ImGui: カバレッジ/密度/風向・風速/
    層高度・厚み/位相 g・ブレンド/アンビエント強度/ステップ数/有効トグル。
    ノイズスケール系変更時は `MarkNoiseDirty()`）+ README.md
  - `MyGame.cpp` へシーン登録（vcxproj 登録も忘れず）
  - 既定有効の最終確認: `SetEnabled` 既定 true のまま、
    WaterTestScene / AtmosphereTestScene で雲が出ることを確認
- 合格条件:
  - VolumetricCloudTestScene で ImGui からリアルタイムに雲を編集できる
  - **WaterTestScene**: 雲が出る・水面や既存表現（反射・コースティクス）が壊れていない
  - **PrimitiveTestScene 等キューブマップ空のシーン**: 雲もノイズ生成も一切動かない
    （見た目不変・ログにも雲関連が出ない）
  - `Docs/Engine/Graphics/Rendering/VolumetricCloud_Design.md`（本書）の
    実装状況チェックリスト更新（本章末尾に [x] を付ける）

### Phase 7（任意・将来）: テンポラル再投影ほか

13 章参照。ここまでのフェーズが完了していれば本タスクのゴールは達成。

### フェーズ進行チェックリスト（実装時に更新すること）

- [x] Phase 0: 骨格と配線（2026-07-08 完了。ビルドエラー0・クラッシュなし・初期化ログ確認・WaterTestScene 見た目不変）
- [x] Phase 1: ノイズ生成（2026-07-08 完了。ビルドエラー0・クラッシュなし・「ノイズテクスチャ生成完了」ログ1回・D3D12エラーなし・既存描画健全）
- [x] Phase 2: レイマーチ最小実装（2026-07-08 完了。ビルドエラー0・クラッシュ/D3D12エラー0・空に雲が描画され地平線/不透明遮蔽が正しく機能・ノイズの空間変化を診断で実証。カメラ水平のため見た目は帯状のもやだが Phase 3 のディテール侵食で塊化する）
- [x] Phase 3: 密度モデリング（2026-07-08 完了。青空の隙間を持つもこもこした積雲を確認・weather map カバレッジとディテール侵食が機能・空領域の10秒差分MAD=19.1で移流を実証・エラー0）
- [x] Phase 4: ライティング（2026-07-09 完了。太陽高度2°で雲が黄金色＝Transmittance LUT が効くことを実証・日陰の雲は Sky-View アンビエントで青紫・エラー0）
- [x] Phase 5: 品質・パフォーマンス（2026-07-10 完了。合成の深度エッジ対策を追加＝不透明物が雲層の最近距離より手前の画素では雲を合成しない。A/B比較で悪化なしを確認。GPU使用率41%・エラー0。IGNジッタ／早期終了・適応ステップ／遠距離フェードは Phase 2-4 で実装済みだったため今回の作業は深度エッジ対策のみ）
- [ ] Phase 6: デフォルト背景統合・エディタ

---

## 10. ビルド・実行・検証ワークフロー（共通手順）

（`game-verification-workflow` メモリより。詳細はそちらを参照）

1. **ビルド**: `MSBuild.exe C:\CoreEngine\Project\CoreEngine.vcxproj /p:Configuration=Debug /p:Platform=x64`
   （MSBuild は VS18 Community。パスは vswhere で取得。.sln は無い）
2. **起動**: exe は `C:\CoreEngine\generated\CoreEngine\outputs\Debug\CoreEngine.exe`。
   **作業ディレクトリは `C:\CoreEngine\Project` 必須**（AssetDatabase の起点）。
   シェル直起動はプロセスツリー掃除で殺されるため
   **WMI でデタッチ起動**: `Invoke-CimMethod -ClassName Win32_Process -MethodName Create`
3. 起動完了まで **25〜30 秒**待つ
4. **ログ確認**: `C:\CoreEngine\Project\Cache\logs\`（カテゴリ別）。エラー 0 を確認。
   Logger は spdlog 形式（`{}`。printf 形式は展開されない）
5. **スクリーンショット**: GetWindowRect + CopyFromScreen で**ウィンドウ矩形のみ**
   （全画面キャプチャ禁止）。エディタカメラは起動間で持ち越されるため
   **構図の比較はできない**。「雲が写っているか」等の要素有無で判定する
6. シーン切替はユーザーが起動中ウィンドウを直接操作することがある。
   スクショが想定と違うシーンでも即異常と判断しない
7. **クラッシュ時**: `C:\CoreEngine\Project\Dumps\` にミニダンプ。cdb 無し環境のため
   原因特定は「変更の二分探索」（直前フェーズとの差分を半分ずつ無効化）が実用的。
   初期シーンは WaterTestScene（大気シーン）なので、雲パスの不具合は起動直後に顕在化する

---

## 11. 既知の落とし穴チェックリスト

実装中に一つずつ確認すること（すべて本エンジンでの実績がある事故）:

- [ ] **新規 .cpp/.h は CoreEngine.vcxproj と .filters の両方へ手動追加**（.hlsl は不要）
- [ ] compute 専用パスは **`SetDescriptorHeaps` をパス自身で呼ぶ**
      （AtmosphereLUTPass.cpp:31 のコメント参照。忘れるとクラッシュ）
- [ ] CB の C++/HLSL レイアウト一致は `static_assert(sizeof==208)` で機械検証
- [ ] 深度のスカイ判定しきい値は **0.9999999**（far=50km では 2km 先でも 0.9999 超）
- [ ] フレーム有効化パターン: `Update()` を呼んだシーンだけ有効・
      `EngineSystem` が全 View 描画後にリセット。**これを守らないと
      非対応シーンの SceneColor を汚染する**（大気実装で実際に踏んだ）
- [ ] シーンが `SkyBoxObject::SetTexture()` を呼ぶと大気（＝雲も）は非アクティブになる。
      検証シーンでは SkyBox に触らない
- [ ] **ステップ幅を「マーチ区間長 / 固定ステップ数」で決めない**。カメラが動くと dt が変化して
      サンプル位置が滑り、雲底の見かけの高さが上下に振動する。また地平線方向で dt が数百 m に
      膨らみ、厚さ数千 m の層を数サンプルしか貫かず「平らな板」に見える。
      **ステップ幅は雲層の厚み基準（layerThicknessM/128）で決め、距離に応じて伸ばす**
- [ ] **IGN ジッタ必須**。無いと等距離サンプル面（カメラ中心の球殻）が層と交わり、
      放射状の規則的な縞・穴として見える
- [ ] **ノイズの格子ハッシュに float の frac 系ハッシュ（Dave Hoskins hash33 等）を使わない**。
      0〜数十の小さな整数格子座標に対して出力が相関し、Worley のセル中心・Perlin の勾配が
      規則配列になって雲に格子状の穴が出る。**整数専用のビット混合（PCG3D/PCG2D）を使う**
- [ ] **`detailErosionStrength` は `globalCoverage` より十分小さくする**。密度は
      `cloudWithCoverage *= coverage` で coverage（≈0.3）に上限クランプされるため、
      侵食しきい値がこれを超えると縁だけでなく雲の内部にまで穴が開き全体が斑点状になる
- [ ] `CloudLayerInterval` は**カメラが層の下／中／上のすべて**を扱うこと（雲に近づくと破綻する）
- [ ] `baseNoiseScaleM` がマーチ距離に対して小さすぎると同じ雲塊が繰り返し現れる（既定 16km / マーチ 30km）
- [ ] 天候マップの Perlin FBM 出力は 0.5 付近に集中する。`Remap` でコントラストを広げないと
      カバレッジが一様になり、空全体が均質な板状の雲で覆われる
- [ ] `resolutionDivisor`（1=フル / 2=半解像度）: 実測ではフル解像度にしても見た目は改善せず
      （雲底のぼやけは密度勾配由来）、GPU 使用率が 56% → 86% へ増える。既定 2 のままでよい
- [ ] 雲層は薄い（数千 m）ので、ベースノイズを**等方サンプルすると縦方向の変化が乏しく平板に見える**。
      縦方向だけ小さいスケール（`baseNoiseScaleM * 0.5`）でサンプルする
- [ ] **雲塊のアスペクト比 = ノイズ水平波長 vs 高度勾配の縦スパン**。`baseNoiseScaleM` が層厚に
      対して大きすぎる（例 16km vs 縦 2.6km）と横長のパンケーキ雲になる。ノイズ波長は層厚の
      2〜2.5 倍、積雲の高度勾配は層の 8 割程度（rise 0→0.2 / fall 0.4→0.85）まで使う
- [ ] **サンライトマーチ（セルフシャドウ）は指数ステップの固定オフセットで行い、
      画面空間ジッタ（IGN）を使わない**。累積距離 200/600/1400/3000/6200/12600m の
      指数配置なら近傍密・遠方粗で滑らかな遮蔽が得られ、ジッタ不要。
      **IGN ジッタをセルフシャドウに使うと、密度変動が exp() の急峻さで増幅され、
      IGN 特有の斜めドット格子（ギザギザ模様）が雲面全体に焼き付く**（TAA が無いと除去不能）。
      ※ 固定「等間隔」ステップだと放射状の縞が出るが、それは指数ステップで解決する。
      切り分け: fp16 化で消えない・ディテール無効化で薄まる・`ign=0.5` 固定でも残る
      （shadowJitter が別 IGN タップだから）→ セルフシャドウのジッタを疑う
- [ ] **アンビエントに空色（Sky-View LUT）をそのまま使わない**。太陽光の届かない厚い部分が
      青黒い染みになる。実際の雲内部は多重散乱で無彩色化するため、6 割ほど灰色へ寄せる
- [ ] **Worley FBM をノイズ生成に使うときは各オクターブ周波数をテクスチャ解像度の
      1/4 でクランプする**（`WorleyFBM3D_Safe`）。`WorleyFBM3D(uvw, f)` は内部で f/2f/4f を
      合成するため、最高オクターブが解像度の 1/2（ナイキスト限界）に達すると生成時点で
      エイリアシングした格子が焼き込まれる。ただし今回のギザギザの主因は生成側ではなく
      上記のセルフシャドウ IGN だった（クランプだけでは消えなかった）
- [ ] **カバレッジが高い（0.55 等）と空全体が雲デッキで覆われ、視線が雲を長く貫くぶん
      自己遮蔽が累積して「陰影が強い・重い」印象になる**。青空の隙間を残す 0.48 前後にし、
      暗部が沈むぶんは `ambientIntensity`（1.1〜1.2）で持ち上げると軽い積雲空になる
- [ ] **`densityScale` が低いと雲が半透明のもやになり立体感を失う**。密度は coverage で
      0.3〜0.5 に上限クランプされるため、実効消散は名目の半分以下。0.12 [1/m] 程度で
      固い輪郭とカリフラワー状の陰影が出る（0.05 では縁が 100〜300m のもやに広がる）。
      上げた分だけ太陽光の透過が減るので `ambientIntensity` / `msContribution` を連動して上げる
- [ ] **二重 HG 位相関数は側方散乱（太陽と視線が 90° 前後）で等方値の半分程度まで落ち、
      順光の雲面が灰色に沈む**。`sunLightScale` は側方散乱の見た目（順光の雲が白いこと）で
      決める（既定 3.5）。silver lining（太陽方向）が明るく飛ぶのは実写どおりで正常
- [ ] **Powder 項は照射面の外殻（光学的深さ小）を一律に暗くする**。`beerPowderStrength=1.0`
      だと順光の雲面全体が一段暗く沈む。0.5 程度が自然
- [ ] **`energy *= 4π`（位相関数の 1/4π 打ち消し）はやり過ぎ**。雲の HDR 値がトーンマッパーの
      線形域（空の輝度 ≈1.5）の 4〜13 倍まで飛び出し、明部も暗部も同じ白に潰れる。
      実測: 雲ピクセルの p10=239 / p50=245 / 26% が 255 飽和。調整可能な `sunLightScale`（既定 2.5）にすること
- [ ] **多重散乱オクターブの減衰（`msAttenuation` / `msContribution`）が緩いと最終オクターブが
      「深さに依らない明るい床」になり、全エネルギーの約 80% を占めて雲底が一様な白い板になる**。
      `0.5 / 0.5` → `0.6 / 0.4` で陰影レンジが 16 階調 → 54 階調へ改善（白飛び 32.6% → 0%）。
      切り分け方: `SunLuminanceAt` を 0 倍して描くと、雲がまだ明るければアンビエント過多、
      暗くなれば太陽項が支配（＝オクターブの床を疑う）
- [ ] 大気シーンの太陽は `OnInitialize()` で direction/intensity（≒20）を明示上書き
      （既定の intensity=1 のままだと空が暗い紺色 = Water シーンで踏んだ不具合）
- [ ] `Logger` は `{}` プレースホルダ（spdlog）。printf 形式は展開されない
- [ ] シェーダーファイル名はプロジェクト全体で一意（AssetDatabase がファイル名解決のため）
- [ ] SceneColor へ書いた後は必ず `PIXEL_SHADER_RESOURCE` へ戻す（後続パスの前提状態）
- [ ] `remap()` は分母 0 に注意し結果を `saturate`
- [ ] `sunDirection` は「光の進行方向」。太陽へ向かうベクトルは `-sunDirection`
- [ ] リフレクション静的サンプラーは LINEAR/**WRAP** 既定。CLAMP が要る所は UV 手動クランプ

---

## 12. パラメータ既定値一覧

`VolumetricCloudParameters` の既定値（単位は m / 秒 / 無次元）:

実装・チューニングを経た現在の既定値（`VolumetricCloudManager.h` が正）:

| パラメータ | 既定値 | 意味・根拠 |
|---|---|---|
| `layerBottomAltitudeM` | 1500.0 | 雲底高度（積雲の典型 1〜2km） |
| `layerThicknessM` | 4000.0 | 層厚 |
| `globalCoverage` | 0.48 | 全体カバレッジ倍率 [0,1]。高いと重い曇り空になる |
| `densityScale` | 0.12 | 密度→消散係数 [1/m]。低いと縁がもや状になり立体感を失う |
| `baseNoiseScaleM` | 9000.0 | ベースノイズ 1 タイルの実寸。層厚の 2〜2.5 倍が積雲らしいアスペクト比 |
| `detailNoiseScaleM` | 600.0 | ディテールノイズ 1 タイルの実寸 |
| `detailErosionStrength` | 0.28 | 縁の侵食強度。カリフラワー状の凹凸（coverage より小さくすること） |
| `weatherMapScaleM` | 60000.0 | 天候マップ 1 タイル = 60km（カバレッジ FBM は開始周波数 6） |
| `windDirection` | (1, 0) | XZ 正規化 |
| `windSpeedMPerS` | 8.0 | 中層雲の典型風速 |
| `phaseG0` / `phaseG1` / `phaseBlend` | 0.8 / -0.3 / 0.5 | 前方散乱主体 + 弱い後方散乱 |
| `ambientIntensity` | 1.15 | Sky-View アンビエント倍率（densityScale・coverage と連動して調整） |
| `beerPowderStrength` | 0.5 | Powder 効果。1.0 だと順光面全体が暗く沈む |
| `lightMarchStepM` | 200.0 | サンライトマーチの基準ステップ（指数配置 6 サンプル・ジッタ無し） |
| `earlyExitTransmittance` | 0.005 | 早期終了しきい値 |
| `maxMarchDistanceM` | 30000.0 | マーチ最大距離（地平線対策） |
| `maxSteps` | 128 | 反復回数の予算（ステップ幅は層厚/128 基準・距離で伸長） |
| `sunLightScale` | 3.5 | 太陽散乱スケール（順光の雲が白く見える値で決める） |
| `msAttenuation` / `msContribution` / `msEccentricity` | 0.6 / 0.5 / 0.5 | 多重散乱オクターブの減衰 |
| `resolutionDivisor` | 2 | レイマーチ解像度分割（1=フル。見た目差なし・GPU+30%） |
| `enabled` | true | 機能トグル（大気シーンで既定有効） |

---

## 13. スコープ外・将来拡張

- **テンポラル再投影 / 再構成**（品質・負荷の本命改善。MotionVector GBuffer は既存。
  履歴バッファと前フレーム viewProj の保持を Manager に追加すれば実装可能）
- **雲影（Cloud Shadow Map）**: 太陽方向から雲の透過率マップを生成し
  DeferredLighting の直接光へ乗算。ShadowMask 系への統合設計が別途必要
- **ReflectionView への雲**: 水面反射に雲を映す（大気の ReflectionView 対応と同時に検討）
- **雲の IBL 寄与**（環境光へ雲の遮蔽を反映）
- **Texture3D ミップチェーン生成ユーティリティ**（距離 LOD）
- **天候マップの外部アセット化・ペイント**（局所的な雲配置の演出制御）
- **カールノイズによるディテール歪み**（wispy な縁の強化）
- **複数雲層**（巻雲レイヤーの追加は 2D テクスチャベースの安価な手法が向く）
