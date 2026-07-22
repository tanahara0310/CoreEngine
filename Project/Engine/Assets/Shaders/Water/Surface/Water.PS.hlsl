#include "Object3dForward.hlsli"
// 大気散乱の空気遠近感を水面（フォワード半透明）にも適用する（b6 / t22 / t23 を使用）
#include "AtmosphereApply.hlsli"

// ===== 反射テクスチャ（Planar Reflection RTT）=====
Texture2D<float4> gReflectionTexture : register(t14);
SamplerState gLinearClamp : register(s2);

// ===== シーン深度テクスチャ（GBuffer 深度 / Depth Stencil SRV）=====
// WaterPlaneObject::BindCustomResources() が t15 にバインドする
// Depth Fade（Beer-Lambert）で水柱の厚さを計算するために使用する
Texture2D<float> gSceneDepth : register(t15);

// ===== シーンカラーテクスチャ（OffScreen Color SRV）=====
// WaterPlaneObject::BindCustomResources() が t16 にバインドする
// 水面越しに見える背景色の取得に使用する
Texture2D<float4> gSceneColor : register(t16);

// ===== DXR 水面屈折カラー（RTWaterRefractionPass 出力）=====
// WaterPlaneObject::BindCustomResources() が t17 にバインドする
Texture2D<float4> gRTWaterRefractionColor : register(t17);

// ===== FFT Ocean 法線マップ（FFTOceanFinalize.CS.hlsl 出力）=====
// 頂点解像度に依存しない法線を得るため、ピクセルシェーダーで直接再サンプリングする。
// カスケード（マルチスケールFFT）は Texture2DArray のスライスに格納される。
Texture2DArray<float4> gFFTOceanNormal : register(t19);

// マルチスケール・カスケード。FFTOceanManager / FFTWater.VS / RTWaterSurfaceCommon と一致必須
// （パッチ長は互いに素な素数、格子回転 0°/+26°/-49° で周期の整列を破壊）。
static const int kFFTCascadeCount = 3;
static const float kFFTCascadePatch[3] = { 521.0f, 127.0f, 31.0f };
static const float kFFTCascadeRotC[3] = { 1.0f, 0.89879405f, 0.65605903f };
static const float kFFTCascadeRotS[3] = { 0.0f, 0.43837115f, -0.75471006f };

// 波群エンベロープ（タイル周期破壊の空間振幅変調）。
// FFTWater.VS / RTWaterSurfaceCommon と完全一致必須（詳細コメントは FFTWater.VS 参照）。
static const float kFFTWaveGroupStrength = 0.12f;
float ComputeFFTWaveGroupEnvelope(float2 worldXZ)
{
    float g = sin(dot(worldXZ, float2(0.01071f, 0.01353f)) + 0.917f)
            + sin(dot(worldXZ, float2(-0.01409f, 0.00893f)) + 2.618f)
            + sin(dot(worldXZ, float2(0.00531f, -0.00713f)) + 4.523f);
    return 1.0f + kFFTWaveGroupStrength * g;
}

// ===== 空の放射照度 SH9 係数（SkyIrradianceSH.CS.hlsl 出力）=====
// WaterPlaneObject::BindCustomResources() が t24 にバインドする。
// 係数には太陽色・強度が焼き込み済みで、Σ c_i・Y_i(N) がそのまま
// 「法線 N に対する空からの放射照度 / π」になる（DeferredLighting と同じ規約）。
StructuredBuffer<float4> gWaterSkyIrradianceSH : register(t24);

// ===== プリフィルタ済み空スペキュラキューブマップ（空＋雲。Phase 3b）=====
// AtmosphereManager が Sky-View LUT＋雲レイマーチから毎フレーム生成する。
// rgb は空（SkyAtmosphere.PS）と同一の輝度ドメイン（平面反射像と直接ブレンド可能）。
// α には雲の透過率が入っており、平面反射（雲を含まない）へ雲を被せる不透明度に使う。
TextureCube<float4> gSkyEnvironmentMap : register(t25);

struct WaterPSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 jacobianData : TEXCOORD1;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 lightSpacePos : POSITION1;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
};

// ===== フレーム定数バッファ（VS と共有）=====
cbuffer WaterFrameConstants : register(b5)
{
    float4 gClipPlane;
    int gClipEnabled;
    int gReflectionEnabled; // 1 = 反射テクスチャ有効，0 = IBL フォールバック
    float gFresnelReflectanceScale; // Fresnel 反射率スケール
    float gFresnelBaseReflectance; // 正面入射時の反射率（F0）

    // ---- Depth Fade ----
    int gDepthFadeEnabled; // 1 = Depth Fade 有効
    int gDepthFadeDebugEnabled; // 1 = 水深デバッグ表示
    float gDepthFadeDebugScale; // 水深デバッグ表示倍率
    // 空アンビエントの輝度単位 → サーフェス光単位の変換係数（AtmosphereManager::GetSkyAmbientScale と同値）
    float gSkyAmbientScale;

    // ---- 水の光学特性（波長依存 Beer-Lambert）----
    // 水の色は shallow/deep の色指定ではなく、吸収・散乱係数と光源から導出する。
    // 赤 > 緑 > 青 の順に吸収が強いことが「水が青い」物理の本体。
    float3 gAbsorptionCoeff; // 吸収係数 σa [1/m]（RGB 波長別）
    // 1 = 大気散乱の Sky Irradiance SH を天空光として使う（大気アクティブ＋SH生成済みのシーンのみ）
    int gSkyAmbientEnabled;
    float3 gScatteringCoeff; // 散乱係数 σs [1/m]（RGB 波長別）
    float gScatteringPad;

    // ---- デバッグ表示 ----
    uint gDepthDebugViewMode;
    // FFT Ocean 使用時、頂点解像度に依存しないピクセル単位の法線マップ再サンプリングを行うか
    int gUseFFTOceanNormalMap;
    // 大気散乱の空気遠近感を水面へ適用するか（大気アクティブなシーンでのみ 1）
    int gAerialPerspectiveEnabled;
    // 空スペキュラキューブマップで平面反射へ雲を合成するか（大気アクティブ＋生成済みのみ 1）
    int gSkyEnvReflectionEnabled;
};

/// @brief NDC 深度値をビュー空間線形深度（メートル単位）に変換する
/// @param ndcDepth  深度テクスチャから読んだ NDC 深度 [0,1]
/// @param nearZ     ニアクリップ距離
/// @param farZ      ファークリップ距離
float LinearizeDepth(float ndcDepth, float nearZ, float farZ)
{
    // D3D12 のデプスは [0, 1]（0 = near, 1 = far）
    // NDC → ビュー空間深度に変換する
    return (nearZ * farZ) / (farZ - ndcDepth * (farZ - nearZ));
}

/// @brief ビュー空間Z差をスクリーンレイ上の実際の光路長へ変換する
/// @param viewDepthDelta 背景と水面のビュー空間Z差
/// @param waterDepthView 水面のビュー空間Z深度
/// @param worldPos 水面ピクセルのワールド座標
/// @return Beer-Lambert に使う水中光路長
float ComputeWaterOpticalPathLength(float viewDepthDelta, float waterDepthView, float3 worldPos)
{
    float rayDistanceToWater = length(gCamera.worldPosition - worldPos);
    float viewToRayScale = rayDistanceToWater / max(waterDepthView, 1.0e-4f);
    return max(0.0f, viewDepthDelta * viewToRayScale);
}

/// @brief Schlick 近似による Fresnel 係数を計算する
/// @param cosTheta  視線と法線のなす角の余弦（saturate 済み推奨）
/// @param f0        法線入射時の反射率（水面 ≈ 0.02）
float FresnelSchlick(float cosTheta, float f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 VisualizeDepthValue(float value)
{
    return float3(value, value, value);
}

float3 VisualizeJacobian(float4 jacobianData)
{
    const float detJ = jacobianData.x;
    const float breakingCandidate = saturate(jacobianData.y);
    const float compression = saturate(jacobianData.z);
    const float foldover = saturate(jacobianData.w);

    const float detVisualization = saturate((1.0f - detJ) * 0.5f + 0.5f);
    return saturate(float3(detVisualization, breakingCandidate, max(compression, foldover)));
}

// 背景深度が取得できない（水面の背後が far plane = 外洋の水平線など）場合に
// 使う光路長。σt が最小クラス（青 ≈ 0.02/m）でも exp(-σt·d) ≈ 0 になる十分な深さ。
static const float kInfiniteWaterColumnMeters = 1.0e4f;

/// @brief SH9 係数から法線方向の空の放射照度/π を評価する
/// @details DeferredLighting.PS.hlsl の EvaluateSkyIrradiance と同じ評価式。
///          係数（gWaterSkyIrradianceSH）には放射照度畳み込みと太陽色・強度が焼き込み済み。
float3 EvaluateWaterSkyIrradiance(float3 n)
{
    float basis[9];
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * n.y;
    basis[2] = 0.488603f * n.z;
    basis[3] = 0.488603f * n.x;
    basis[4] = 1.092548f * n.x * n.y;
    basis[5] = 1.092548f * n.y * n.z;
    basis[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    basis[7] = 1.092548f * n.x * n.z;
    basis[8] = 0.546274f * (n.x * n.x - n.y * n.y);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        irradiance += gWaterSkyIrradianceSH[i].rgb * basis[i];
    }
    // SH の帯域打ち切りによる負のリンギングを防ぐ
    return max(irradiance, 0.0f);
}

/// @brief 水中インスキャッタリングの環境光（水面に入射する光の放射輝度近似）を求める
/// @details 平行光源（太陽）の下向き放射照度と天空拡散光を合算する。
///          太陽ライトの GPU 転送色には LightManager が大気の Transmittance LUT による
///          減衰（日没の赤方偏移・減光）を乗算済みのため、ここで読むだけで大気に追従する。
///          天空光は大気アクティブ時は Sky Irradiance SH（時刻・太陽高度に連動）、
///          非アクティブ時は静的な拡散 IBL キューブマップへフォールバックする。
float3 ComputeUnderwaterAmbientLight()
{
    const float kPi = 3.14159265f;

    // 太陽など平行光源: 水平な水面へ入る下向き放射照度を Lambert 面の放射輝度へ換算（/π）
    // downwelling は太陽高度に対してほぼ線形に 0 へ落ちる（sin(elevation) 相当）ため、
    // 太陽を下げていくと直射成分だけが急激に暗転する。
    float3 sunAmbient = float3(0.0f, 0.0f, 0.0f);
    float maxDownwelling = 0.0f;
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float downwelling = saturate(-normalize(gDirectionalLights[i].direction).y);
        maxDownwelling = max(maxDownwelling, downwelling);
        sunAmbient += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * downwelling / kPi;
    }

    // 天空拡散光
    float3 skyAmbient = float3(0.0f, 0.0f, 0.0f);
    if (gSkyAmbientEnabled != 0)
    {
        // 大気散乱の空を SH9 で評価（上向き法線＝水面へ降り注ぐ天空放射照度/π）。
        // DeferredLighting と同じスケールでサーフェス光単位へ変換する
        skyAmbient = EvaluateWaterSkyIrradiance(float3(0.0f, 1.0f, 0.0f)) * gSkyAmbientScale;
    }
    else if (gIBLParams.sceneIBLEnabled != 0)
    {
        // Irradiance マップは既に「albedo に掛けるだけ」の放射輝度相当なのでそのまま加算
        skyAmbient = gIrradianceMap.SampleLevel(gSampler, float3(0.0f, 1.0f, 0.0f), 0.0f).rgb
                 * gIBLParams.environmentIntensity;
    }

    // 天空光はブーストせずそのまま使う。
    // 以前ここに「太陽が低いほど天空光を最大2.5倍へ底上げする」ハックがあったが、
    // 夜間に水中インスキャッタ（透過側）だけが空の反射（ほぼ黒）より明るくなり、
    // フレネルの振れに応じて青/黒のうねりスケールのまだらを生む原因になったため撤去。
    // 水中に入る光と水面に映る空は同じ空なので、両者の明るさは常に整合させる。
    return sunAmbient + skyAmbient;
}

/// @brief 海底へ届く太陽光の下り光路の透過率を求める
/// @param viewDir 水面ピクセル → カメラの正規化ベクトル
/// @param surfaceNormal 水面法線
/// @param waterColumn 視線（上り）の水中光路長 [m]
/// @param sigmaT 消散係数 σt [1/m]
/// @details 透過して見える海底の明るさは、視線の上り光路だけでなく
///          太陽から海底までの下り光路でも減衰している。
///          鉛直水深は視線の屈折方向から d・|refr.y| で近似し、
///          太陽の水中天頂角はスネル則で求める（水中では臨界角 ≈48.6° に制限されるため
///          太陽が低くても cos は ≈0.66 以上に留まり、発散しない）。
///          背景色には空光で照らされた成分も含まれるため一様に掛けるのは近似だが、
///          「深い水底ほど太陽が届かず暗い」という支配的な挙動を再現する。
float3 ComputeSunDownwellingTransmittance(
    float3 viewDir, float3 surfaceNormal, float waterColumn, float3 sigmaT)
{
    const float kEtaAirToWater = 1.0f / 1.333f;

    // 太陽 = 最初の有効な平行光源。無ければ減衰なし
    float3 sunTravelDir = float3(0.0f, -1.0f, 0.0f);
    bool sunFound = false;
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled != 0)
        {
            sunTravelDir = normalize(gDirectionalLights[i].direction);
            sunFound = true;
            break;
        }
    }
    // 太陽が無い・地平線下（上向き進行）の場合は下り減衰を追加しない
    if (!sunFound || sunTravelDir.y >= 0.0f)
    {
        return float3(1.0f, 1.0f, 1.0f);
    }

    // 視線を水中へ屈折させ、光路長の鉛直成分から水深を近似する
    float3 refractedView = refract(-viewDir, surfaceNormal, kEtaAirToWater);
    float verticalDepth = waterColumn * saturate(-refractedView.y);

    // 太陽光の水中屈折方向（水平な水面で近似）から下り光路長を求める
    float3 refractedSun = refract(sunTravelDir, float3(0.0f, 1.0f, 0.0f), kEtaAirToWater);
    float sunPathLength = verticalDepth / max(-refractedSun.y, 1.0e-2f);

    return exp(-sigmaT * sunPathLength);
}

/// @brief 水柱を通過した背景色に波長依存の吸収・散乱を適用する
/// @param refractionColor 水面越しに見える背景（海底・水中物体）の色
/// @param transmittance   exp(-σt·d) 視線（上り）光路の波長別透過率
/// @param sunDownTransmittance 太陽 → 海底の下り光路の透過率（ComputeSunDownwellingTransmittance）
/// @param sigmaS          散乱係数 σs [1/m]
/// @param sigmaT          消散係数 σt = σa + σs [1/m]
/// @param ambientLight    水面に入射する環境光（ComputeUnderwaterAmbientLight）
/// @details 透過項 + 均質媒質の単一散乱解析解。
///          浅瀬では transmittance がまだ緑・青を通すため海底アルベド（白砂）が
///          エメラルドに、深瀬では透過が消えて (σs/σt)·L の水固有の青に収束する。
float3 ComputeWaterVolumetricColor(
    float3 refractionColor, float3 transmittance, float3 sunDownTransmittance,
    float3 sigmaS, float3 sigmaT, float3 ambientLight)
{
    // 海底からの光は「太陽の下り + 視線の上り」の両光路で減衰する
    float3 transmitted = refractionColor * transmittance * sunDownTransmittance;
    float3 inscatter = (sigmaS / sigmaT) * ambientLight * (1.0f - transmittance);
    return transmitted + inscatter;
}

// RTWaterRefraction.hlsl 側の成功時アルファエンコードと必ず一致させること。
// 失敗理由コードは [0, 0.5) の範囲（1〜9/255）、成功時は [0.5, 1.0] の範囲に
// 実際の屈折光路長（水柱厚さ、メートル）を詰め込んでいる。
static const float kRTSuccessRangeMin = 0.5f;
static const float kRTMaxOpticalPathMeters = 64.0f;

float IsRTRefractionSuccess(float reasonCode)
{
    return reasonCode >= kRTSuccessRangeMin ? 1.0f : 0.0f;
}

/// @brief RT屈折が成功した場合の、屈折レイが実際に水中を進んだ光路長（メートル）を復元する
/// @details RTWaterRefraction.hlsl の EncodeSuccessAlpha() の逆変換。
///          この値は「表示されている屈折後の内容」に対応する真の水柱厚さであり、
///          スクリーン空間の素の深度（屈折で曲げる前の深度）とは異なる。
float DecodeRTOpticalPath(float reasonCode)
{
    float normalized = saturate((reasonCode - kRTSuccessRangeMin) / (1.0f - kRTSuccessRangeMin));
    return normalized * kRTMaxOpticalPathMeters;
}

float3 ResolveWaterTransmissionColor(uint2 pixelCoord, float2 screenUV)
{
    float4 rtRefraction = gRTWaterRefractionColor.Load(int3(pixelCoord, 0));
    if (IsRTRefractionSuccess(rtRefraction.a) > 0.5f) {
        return rtRefraction.rgb;
    }

    return gSceneColor.Sample(gLinearClamp, screenUV).rgb;
}

float4 SampleRTWaterRefraction(uint2 pixelCoord)
{
    return gRTWaterRefractionColor.Load(int3(pixelCoord, 0));
}

float3 VisualizeRTRefractionReason(float reasonCode)
{
    if (reasonCode >= kRTSuccessRangeMin) return float3(0.0f, 1.0f, 0.0f);

    const float reasonIndex = floor(reasonCode * 255.0f + 0.5f);

    if (reasonIndex == 1.0f) return float3(1.0f, 0.0f, 0.0f);
    if (reasonIndex == 2.0f) return float3(1.0f, 0.5f, 0.0f);
    if (reasonIndex == 3.0f) return float3(1.0f, 1.0f, 0.0f);
    if (reasonIndex == 4.0f) return float3(0.8f, 0.0f, 1.0f);
    if (reasonIndex == 5.0f) return float3(0.0f, 1.0f, 1.0f);
    if (reasonIndex == 6.0f) return float3(0.0f, 0.0f, 1.0f);
    if (reasonIndex == 7.0f) return float3(1.0f, 0.0f, 1.0f);
    if (reasonIndex == 9.0f) return float3(1.0f, 1.0f, 1.0f);

    return float3(0.15f, 0.15f, 0.15f);
}

/// @brief FFT 法線マップのエンコード値をワールド空間法線へ展開する
float3 BuildWorldNormalFromFFTSample(float3 encodedNormal, WaterPSInput input)
{
    float3 localNormal = normalize(encodedNormal * 2.0f - 1.0f);
    float3 vertexNormal = normalize(input.normal);
    float3 tangent = normalize(input.tangent);
    float3 bitangent = normalize(input.bitangent);
    return normalize(localNormal.x * tangent + localNormal.y * vertexNormal + localNormal.z * bitangent);
}

/// @brief ピクセル単位の法線を解決する
/// @details Gerstner Wave は頂点シェーダーで解析的に計算した法線をそのまま補間して使えるが、
///          FFT Ocean はテクスチャベースの法線マップであるため、頂点解像度で補間すると
///          メッシュの三角形境界に沿った法線のファセット化（IBL 反射のギザギザ）が発生する。
///          FFT Ocean 使用時は texcoord から法線マップを直接再サンプリングし、
///          頂点密度に依存しない滑らかな法線を得る。
///          サンプリングは WRAP アドレスの gSampler で行う（frac 不要でタイル境界の
///          微分不連続が出ず、ミップチェーン＋異方性フィルタが自動LODで効くため、
///          遠方・かすめ角で法線がピクセル毎に暴れるエイリアシングを抑制できる）。
/// @brief カスケードごとの法線スライスを合算し、距離フェードでAAした面法線を作る
/// @details 各カスケードの傾き（勾配 = nLocal.xz / nLocal.y）を加算してから鉛直へ再構成する。
///          小さいパッチ（高周波）は遠方でフェードアウトさせ、法線ミップ連鎖の代わりに
///          遠距離・かすめ角のスペックル（フレネルの高周波ノイズ）を抑える。
float3 ResolveSurfaceNormal(WaterPSInput input)
{
    float3 vertexNormal = normalize(input.normal);
    if (gUseFFTOceanNormalMap == 0)
    {
        return vertexNormal;
    }

    float3 tangent = normalize(input.tangent);
    float3 bitangent = normalize(input.bitangent);
    float dist = length(gCamera.worldPosition - input.worldPosition);

    float2 slope = float2(0.0f, 0.0f);
    [unroll]
    for (int ci = 0; ci < kFFTCascadeCount; ++ci)
    {
        const float rc = kFFTCascadeRotC[ci];
        const float rs = kFFTCascadeRotS[ci];
        // ワールドXZ を回転格子系へ（FFTWater.VS と同一の写像）
        float2 cuv = float2(
            rc * input.worldPosition.x - rs * input.worldPosition.z,
            rs * input.worldPosition.x + rc * input.worldPosition.z) / kFFTCascadePatch[ci];
        float3 enc = gFFTOceanNormal.Sample(gSampler, float3(cuv, (float)ci)).xyz;
        float3 nLocal = normalize(enc * 2.0f - 1.0f); // (x=+texU, y=up, z=+texV)
        // 小パッチほど近距離でフェードアウト（パッチ長比例のフェード区間）。
        float fade = 1.0f - smoothstep(kFFTCascadePatch[ci] * 8.0f, kFFTCascadePatch[ci] * 40.0f, dist);
        // テクスチャ格子系の傾きをワールドへ逆回転してから合算する
        float2 slopeTex = nLocal.xz / max(nLocal.y, 1.0e-3f);
        slope += float2(rc * slopeTex.x + rs * slopeTex.y, -rs * slopeTex.x + rc * slopeTex.y) * fade;
    }

    // 波群エンベロープ: 変位（FFTWater.VS）と同じ変調を傾きへ掛け、幾何と法線を一致させる
    slope *= ComputeFFTWaveGroupEnvelope(input.worldPosition.xz);

    float3 combinedLocal = normalize(float3(slope.x, 1.0f, slope.y));
    return normalize(combinedLocal.x * tangent + combinedLocal.y * vertexNormal + combinedLocal.z * bitangent);
}

// フレネル評価用の法線に掛けるミップバイアス。
// 法線マップの短波長成分（メッシュ解像度未満のさざ波）は、(1-cosθ)^5 の強い非線形に
// そのまま食わせると数度の傾きで反射率が激変し、水面に「青と水色の大きなまだら」や
// 高周波スペックルとして浮き出る。マイクロファセット的には未解像の斜面は粗さとして
// 均されるべきなので、フレネルは数テクセル分ぼかした低周波法線（うねりスケール）で
// 評価する。バイアス +3 ≒ 8x8 テクセル（パッチ長 180m / 256px で約 5.6m）の平均。
static const float kFresnelNormalMipBias = 3.0f;

// フレネル評価用法線を鉛直へブレンドする強さ（0=波法線そのまま, 1=完全に平坦）。
// ★まだらの根本対策★
// フレネル混合比を「うねり（低周波の大波）の傾き」で評価すると、うねりがカメラを
// 向く面＝低フレネル＝暗い透過、うねりが寝る面＝高フレネル＝明るい空反射、となり
// うねりスケールの大きな明暗の塊（青/黒のまだら）が出る。これは細かいさざ波ではなく
// うねり＝低周波成分が原因なので、ミップバイアス（高周波ぼかし）では消せない
// （うねりは全ミップに存在するため。過去に平坦化を試して効果が無かった真因）。
// フレネル法線を鉛直へ強くブレンドし、うねりの傾き自体を減衰させることで、
// 反射/透過の混合比が「視線角度に応じた滑らかなグラデーション」になり塊が消える。
// 反射像の歪み（geomNormal 使用）や鏡面ハイライトはフル法線のままなので、
// 波のディテールは失わない。
// 0.75 → 0.35: 平坦化が強すぎると波ごとの反射/透過の切り替わり（水面らしい
// きらめきのコントラスト）まで消えて一様な膜に見えるため緩和。
// まだらの真因（反射ビューへの水面自己描画）は修正済みなので、うねり由来の
// フレネル変化はある程度残してよい。夜間にまだらが再発しないか要確認。
static const float kFresnelNormalFlatten = 0.35f;

/// @brief フレネル（反射/透過の混合比）評価に使う低周波の面法線を解決する
float3 ResolveFresnelNormal(WaterPSInput input)
{
    float3 waveNormal;
    if (gUseFFTOceanNormalMap != 0)
    {
        // フレネルは「うねりスケールの低周波法線」で評価する。カスケード化により、
        // 最大パッチ（低周波の大波）のスライスを単独でサンプルするだけで、旧来の
        // ミップバイアスぼかしと同じ「うねりスケールの滑らかな法線」が得られる
        // （小さいパッチ＝さざ波は混ぜない）。カスケード0は回転恒等なので uv 回転は不要。
        float2 cuv = input.worldPosition.xz / kFFTCascadePatch[0];
        float3 encodedNormal = gFFTOceanNormal.Sample(gSampler, float3(cuv, 0.0f)).xyz;
        // 波群エンベロープを傾きへ掛け、実ジオメトリ（変位×エンベロープ）と整合させる
        float3 nLocal = normalize(encodedNormal * 2.0f - 1.0f);
        float2 slopeTex = (nLocal.xz / max(nLocal.y, 1.0e-3f))
            * ComputeFFTWaveGroupEnvelope(input.worldPosition.xz);
        float3 envLocal = normalize(float3(slopeTex.x, 1.0f, slopeTex.y));
        waveNormal = BuildWorldNormalFromFFTSample(envLocal * 0.5f + 0.5f, input);
    }
    else
    {
        // Gerstner Wave など法線マップが無い経路では、変位適用後のワールド座標の
        // 画面微分から面法線を再構成する（頂点法線は常に真上でうねりを含まないため）。
        float3 faceNormal = normalize(cross(ddy(input.worldPosition), ddx(input.worldPosition)));
        waveNormal = (faceNormal.y < 0.0f) ? -faceNormal : faceNormal;
    }

    // うねりの傾きを鉛直へブレンドして減衰させる（まだらの根本対策・上記コメント参照）。
    float3 flattened = normalize(lerp(waveNormal, float3(0.0f, 1.0f, 0.0f), kFresnelNormalFlatten));
    return flattened;
}

// ===== 反射のグロッシー化（ラフネスを考慮した反射）=====
// 平面反射(gReflectionTexture)は完全な鏡像であり、明るい空がそのままフレネルで
// 波面へ塗られるため、うねりの向きに沿ってハードな明暗の斑（水色/青のまだら）が出る。
// 実際の水面は未解像の微細なさざ波が「ラフネス」として働き、
//  (1) 反射をにじませ（グロッシー反射）、
//  (2) かすめ角では微細斜面同士の幾何遮蔽で反射スパイクを抑える。
// この 2 つを再現して、穏やかな海が均一に見えるようにする。
// 0.55 → 0.20: 実際の穏やかな水面の実効マイクロラフネスは 0.02〜0.1 程度で、
// 0.55 は嵐の海に相当する過大な値だった。かすめ角の「鏡のような空の映り込み」が
// 幾何遮蔽で半減し、水面の輝き・透明感を大きく損なっていたため緩和
// （まだらの真因＝反射ビューへの水面自己描画は修正済み）。
static const float kWaterReflectionMicroRoughness = 0.20f; // 未解像さざ波の実効ラフネス
static const float kWaterReflectionBlurTexels = 3.0f; // 反射のにじみ半径（テクセル基準。5→3: 反射のシャープさを回復）
// 反射UVを波法線で歪ませる強さ（スクリーンUV単位）。平面反射は平らな鏡として
// 描かれているため、波法線でサンプル位置をずらして「波に沿って砕けた反射」に見せる。
// これが無いと平坦な鏡像がフレネルの波形状で明滅し、大きなまだらになる。
static const float kWaterReflectionDistortStrength = 0.03f;

/// @brief 平面反射をラフネス相当でにじませてサンプリングする（グロッシー反射）
/// @param screenUV スクリーンUV
/// @param grazing  かすめ具合 = 1 - cosθ（大きいほど反射が伸び・ぼける）
float3 SampleGlossyReflection(float2 screenUV, float grazing)
{
    uint reflWidth = 1;
    uint reflHeight = 1;
    gReflectionTexture.GetDimensions(reflWidth, reflHeight);
    const float2 texel = 1.0f / float2(reflWidth, reflHeight);

    // 反射像は面が寝るほど鉛直方向へ伸びるため縦を強めに、かすめ角ほど広くぼかす。
    const float2 radius = kWaterReflectionBlurTexels * texel * float2(1.0f, 2.0f) * (1.0f + grazing * 2.0f);

    const float2 kOffsets[9] = {
        float2( 0.0f,  0.0f),
        float2(-1.0f, -1.0f), float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
        float2( 0.0f, -1.0f), float2( 0.0f,  1.0f),
        float2(-1.0f,  0.0f), float2( 1.0f,  0.0f)
    };
    const float kWeights[9] = { 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 2.0f };

    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        const float2 uv = saturate(screenUV + kOffsets[i] * radius);
        sum += gReflectionTexture.Sample(gLinearClamp, uv).rgb * kWeights[i];
        weightSum += kWeights[i];
    }
    return sum / weightSum;
}

/// @brief かすめ角の反射スパイクを微細さざ波の幾何遮蔽で抑える係数
/// @details Schlick-GGX の視線側幾何項に相当。cosθ→0（かすめ角）で 0 に近づき、
///          明るい空の反射が波の裏面へハードに乗るのを弱める。cosθ→1 では 1（無影響）。
float ReflectionGeometricOcclusion(float cosTheta)
{
    const float k = kWaterReflectionMicroRoughness;
    return cosTheta / max(cosTheta * (1.0f - k) + k, 1.0e-4f);
}

// ===== 太陽のスペキュラグリッター（解析的 GGX）=====
// 反射有効時、PBR フォワード出力（太陽の解析的スペキュラを含む）は平面反射像で
// 「置き換え」られるため、太陽ハイライトは鏡像の太陽ディスク頼みになる。
// だが鏡像の太陽はグロッシーぼかし＋輝度圧縮でほぼ消えてしまい、
// 参照画像のような波のきらめき（サングリッター）が全く出ない。
// ここでは太陽の鏡面反射だけをディテール法線＋GGX で解析的に計算し、
// フレネル合成後に加算で復元する。空・環境光は平面反射側にのみ含まれるので
// エネルギーの二重計上はない（太陽ディスク分の平面反射側エネルギーは
// ぼかし・圧縮で実質失われているため、加算しても過大にならない）。
float3 ComputeSunGlintSpecular(float3 normal, float3 viewDir)
{
    // 水の垂直入射反射率 F0 ≈ 0.02
    const float3 kWaterF0 = float3(0.02f, 0.02f, 0.02f);
    // グリッターの広がり。マテリアルのラフネスと連動させる
    // （小さいほど鋭く狭いきらめき、大きいほど広く柔らかい光の帯）
    const float glintRoughness = max(gMaterial.roughness, 0.04f);

    float3 totalGlint = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float3 lightVec = -normalize(gDirectionalLights[i].direction);
        float ndotl = saturate(dot(normal, lightVec));
        if (ndotl <= 0.0f)
        {
            continue;
        }
        float3 brdf = CookTorranceBRDF(normal, viewDir, lightVec, glintRoughness, kWaterF0);
        totalGlint += brdf * gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * ndotl;
    }
    return totalGlint;
}

/// @brief 水面専用フォワードパス処理（ForwardMain の discard 処理を削除したバージョン）
/// 水面は常に描画され、alpha 値で透明度を制御する
VertexShaderOutput ToVertexShaderOutput(WaterPSInput input)
{
    VertexShaderOutput output;
    output.position = input.position;
    output.texcoord = input.texcoord;
    output.normal = input.normal;
    output.worldPosition = input.worldPosition;
    output.lightSpacePos = input.lightSpacePos;
    output.tangent = input.tangent;
    output.bitangent = input.bitangent;
    output.clipPosCurrent = input.clipPosCurrent;
    output.clipPosPrev = input.clipPosPrev;
    return output;
}

PixelShaderOutput WaterForwardMain(WaterPSInput input)
{
    VertexShaderOutput forwardInput = ToVertexShaderOutput(input);
    PixelShaderOutput output;

    // FFT Ocean 使用時はピクセル単位で法線マップを再サンプリングし、Gerstner Wave 使用時は頂点法線をそのまま使う
    forwardInput.normal = ResolveSurfaceNormal(input);

    // 視線方向と alpha
    float3 toEye = normalize(gCamera.worldPosition - forwardInput.worldPosition);
    float finalAlpha = gMaterial.color.a;
    
    // アンリット
    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color;
        return output;
    }

    // 水面はテクスチャを使わず、マテリアル値のみで PBR パラメータを構成する
    float metallic = gMaterial.metallic;
    float roughness = max(gMaterial.roughness, 0.01f);
    float ao = 1.0f; // 水面は AO マップを持たないため遮蔽なし固定

    float3 albedo = gMaterial.color.rgb;

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(forwardInput, albedo, metallic, roughness, ao, toEye, float4(1.0f, 1.0f, 1.0f, 1.0f));
    output.color.a = finalAlpha;

    // IBL
    output.color.rgb += ApplyIBL(forwardInput, albedo, metallic, roughness, ao, toEye);

    return output;
}

PixelShaderOutput main(WaterPSInput input)
{
    // ---- 1. 水面専用 PBR フォワード出力をベースにする（discard なし）----
    PixelShaderOutput output = WaterForwardMain(input);
    float baseCoverage = saturate(output.color.a);

    // ---- 2. スクリーン UV を計算する ----
    uint sceneDepthWidth = 1;
    uint sceneDepthHeight = 1;
    gSceneDepth.GetDimensions(sceneDepthWidth, sceneDepthHeight);
    float2 screenUV = input.position.xy / float2(sceneDepthWidth, sceneDepthHeight);
    screenUV = saturate(screenUV);
    uint2 pixelCoord = min(uint2(input.position.xy), uint2(sceneDepthWidth - 1, sceneDepthHeight - 1));

    // ---- 3. 波長依存 Beer-Lambert による透過率の計算 ----
    // 必要なのは「水中を通った光路長」なので、視線上の線形深度差を使う。
    // σ が RGB で異なるため、同じ光路長でも赤→緑→青の順に減衰し、
    // 浅瀬エメラルド→深瀬青の色相遷移が指数則から自動的に生じる。
    float3 sigmaA = max(gAbsorptionCoeff, 0.0f);
    float3 sigmaS = max(gScatteringCoeff, 0.0f);
    float3 sigmaT = max(sigmaA + sigmaS, 1.0e-4f);
    float sceneDepthNDC = 1.0f;
    float waterDepthNDC = saturate(input.position.z);
    float sceneDepthView = 0.0f;
    float waterDepthView = 0.0f;
    float waterColumn = 0.0f;
    bool hasValidDepthFade = false;
    if (gDepthFadeEnabled)
    {
        // near/far クリップ（カメラデフォルト値）
        const float kNear = 0.1f;
        const float kFar = 1000.0f;

        // シーン深度を NDC → ビュー空間線形深度（m）に変換する
        sceneDepthNDC = gSceneDepth.Sample(gLinearClamp, screenUV).r;
        waterDepthView = LinearizeDepth(waterDepthNDC, kNear, kFar);

        if (sceneDepthNDC < 0.99999f)
        {
            sceneDepthView = LinearizeDepth(sceneDepthNDC, kNear, kFar);

            if (sceneDepthView > waterDepthView + 1.0e-4f)
            {
                hasValidDepthFade = true;

                // 水面自身の線形深度を求め、背景との深度差を水中の光路長として扱う
                waterColumn = ComputeWaterOpticalPathLength(
                    sceneDepthView - waterDepthView,
                    waterDepthView,
                    input.worldPosition);
            }
        }
        else
        {
            // 背景が far plane（＝水面の先に何もない外洋・水平線）の場合は
            // 水柱が実質無限に続くとみなし、透過ゼロ＝インスキャッタのみの
            // 「水固有の色」へ収束させる。
            hasValidDepthFade = true;
            waterColumn = kInfiniteWaterColumnMeters;
        }

        // RT屈折が成功している場合、実際に画面へ表示している内容（屈折で曲がった先）に
        // 対応する「真の光路長」で上の近似値を置き換える。
        // 上のスクリーン空間近似は水面ピクセル直下の素の深度（屈折前の深度）を使っており、
        // 屈折で表示位置がズレた分だけ吸収量が表示内容と食い違ってしまう
        // （＝水中オブジェクトが水面に浮いて見える一因）。
        //
        // ただし RT の成功/失敗はピクセル単位の2値で、波の揺らぎに応じて成功領域が
        // パッチ状に変化する（DepthMismatch・画面外クリップ等）。成功側=実測光路長と
        // 失敗側=スクリーン空間近似が不連続に切り替わると、その境界が透過率の段差
        // （色の輪郭）としてそのまま見えてしまうため、近傍タップの成功率で
        // 光路長をフェザリングして境界を空間的になじませる。
        const int2 kRTNeighborOffsets[4] = { int2(-3, -3), int2(3, -3), int2(-3, 3), int2(3, 3) };
        float rtOpticalPathSum = 0.0f;
        float rtSuccessCount = 0.0f;
        float4 rtRefractionSample = SampleRTWaterRefraction(pixelCoord);
        if (IsRTRefractionSuccess(rtRefractionSample.a) > 0.5f)
        {
            rtOpticalPathSum += DecodeRTOpticalPath(rtRefractionSample.a);
            rtSuccessCount += 1.0f;
        }
        [unroll]
        for (int tapIndex = 0; tapIndex < 4; ++tapIndex)
        {
            int2 tapCoord = clamp(
                int2(pixelCoord) + kRTNeighborOffsets[tapIndex],
                int2(0, 0),
                int2(sceneDepthWidth - 1, sceneDepthHeight - 1));
            float tapAlpha = gRTWaterRefractionColor.Load(int3(tapCoord, 0)).a;
            if (IsRTRefractionSuccess(tapAlpha) > 0.5f)
            {
                rtOpticalPathSum += DecodeRTOpticalPath(tapAlpha);
                rtSuccessCount += 1.0f;
            }
        }
        if (rtSuccessCount > 0.0f)
        {
            const float rtColumn = rtOpticalPathSum / rtSuccessCount;
            const float rtSuccessWeight = rtSuccessCount / 5.0f;
            // フォールバック推定が無効なピクセルでは RT 実測値をそのまま使う
            waterColumn = hasValidDepthFade ? lerp(waterColumn, rtColumn, rtSuccessWeight) : rtColumn;
            hasValidDepthFade = true;
        }
    }

    // Beer-Lambert 則（波長別）: exp(-σt·d)
    float3 transmittance = exp(-sigmaT * waterColumn);

    // ---- 4. 視線方向と Fresnel 係数を計算する ----
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
    float3 geomNormal = ResolveSurfaceNormal(input);

    // フレネルは「うねりスケールの低周波法線」で評価する（ResolveFresnelNormal 参照）。
    // 短波長のさざ波斜面を (1-cosθ)^5 に直接食わせるとまだら・スペックルになるため、
    // ミップバイアス付き法線マップで未解像斜面を平均してから角度応答を計算する。
    float3 fresnelNormal = ResolveFresnelNormal(input);
    float cosTheta = saturate(dot(fresnelNormal, viewDir));
    float fresnel = FresnelSchlick(cosTheta, saturate(gFresnelBaseReflectance));
    // 微細さざ波の幾何遮蔽でかすめ角の反射スパイクを抑え、うねりに沿った
    // ハードな明暗の斑（水色/青のまだら）を和らげる。
    float reflectanceWeight = saturate(fresnel * ReflectionGeometricOcclusion(cosTheta) * gFresnelReflectanceScale);

    float3 refractionColor = ResolveWaterTransmissionColor(pixelCoord, screenUV);
    float3 underwaterAmbient = ComputeUnderwaterAmbientLight();
    float3 sunDownTransmittance = ComputeSunDownwellingTransmittance(
        viewDir, geomNormal, waterColumn, sigmaT);
    float3 transmissionColor = ComputeWaterVolumetricColor(
        refractionColor,
        transmittance,
        sunDownTransmittance,
        sigmaS,
        sigmaT,
        underwaterAmbient);

    // 反射有効時、環境反射は平面反射像で「置き換える」（加算しない）。
    // 平面反射像には空・雲・太陽そのものが含まれるため、PBR 出力（太陽スペキュラ＋
    // 拡散＋環境光）へさらに加算するとエネルギーが二重計上され、フレネルが立つ
    // 波面で空の輝度が飽和して白飛びの原因になる。
    float3 reflectColor = output.color.rgb;
    if (gReflectionEnabled)
    {
        // ===== DXR 水面反射（鏡像カメラ平面反射の置き換え）=====
        // gReflectionTexture は RTWaterReflectionPass の出力（スクリーン空間・
        // 水面ピクセルごとの反射シーン色）。RT レイが既に波法線で反射方向を
        // 計算済みなので、鏡像方式のような screenUV 歪みは不要。自分の screenUV で
        // そのまま引く。alpha >= 0.5 が成功（反射シーン色）、< 0.5 はミス
        // （反射レイが空へ抜けた／画面外／遮蔽）で、空環境マップへフォールバックする。
        float4 rtReflection = gReflectionTexture.SampleLevel(gLinearClamp, screenUV, 0);
        bool rtHit = rtReflection.a >= 0.5f;

        // 空環境マップによる反射フォールバック（空＋雲を含む TextureCube）。
        // 反射方向は波法線ではなくフラット面法線で計算し、波の斜面ごとの
        // まだら混入を避ける（鏡像時代の知見を踏襲）。
        float3 skyReflectColor = reflectColor;
        if (gSkyEnvReflectionEnabled != 0)
        {
            float3 envReflectDir = reflect(-viewDir, float3(0.0f, 1.0f, 0.0f));
            const float kEnvMipCount = 5.0f;
            const float kEnvMip = kWaterReflectionMicroRoughness * (kEnvMipCount - 1.0f);
            skyReflectColor = gSkyEnvironmentMap.SampleLevel(gLinearClamp, envReflectDir, kEnvMip).rgb;
        }

        // RT ヒット時は反射シーン色、ミス時は空環境マップ。
        reflectColor = rtHit ? rtReflection.rgb : skyReflectColor;

        // ---- 反射の輝度圧縮（白飛び端点の除去）----
        // 反射ソース（SceneColorSnapshot）は水面合成前のライティング済み HDR 色。
        // 露出増幅で極端に明るい輝度が lerp(暗い透過, 明るい反射, フレネル) の
        // 白黒まだらを生むのを防ぐため、膝を超えた輝度だけショルダー圧縮する（色相保持）。
        const float kReflectionCompressKnee = 2.0f;
        const float kReflectionCompressMax = 6.0f;
        float reflLuma = dot(reflectColor, float3(0.2126f, 0.7152f, 0.0722f));
        if (reflLuma > kReflectionCompressKnee)
        {
            float excessLuma = reflLuma - kReflectionCompressKnee;
            float compressedLuma = kReflectionCompressKnee
                + excessLuma / (1.0f + excessLuma / kReflectionCompressMax);
            reflectColor *= compressedLuma / max(reflLuma, 1.0e-5f);
        }
    }

    float3 desiredWaterView = lerp(transmissionColor, reflectColor, reflectanceWeight);
    // 水面ピクセルは常に「水面越しに見える像」（屈折 or 反射）そのものであるべきで、
    // 屈折で曲げていない生のスクリーン座標の背景（gSceneColor.Sample(screenUV)）を
    // 混ぜてはいけない（以前はそれが原因で二重像ゴーストが出ていた）。
    // さらに、透明側の端点には「生の屈折色（refractionColor）」ではなく
    // 「吸収・散乱を通した透過色（transmissionColor）」を使う。
    // 以前は lerp(refractionColor, desiredWaterView, α=0.85) だったため、
    // Beer-Lambert もフレネルも迂回した水底の色が常に約15%そのまま混入し、
    // 深い水でも水底（市松床＋コースティクスのセル模様）が薄い水色の斑として
    // 浮き出ていた（穏やかな海ほどセルが大きく明瞭になり顕著）。
    // transmissionColor は refractionColor から導出されるため「同じ位置を指す像」
    // 同士のブレンドであることは変わらず、ゴーストは発生しない。
    // これにより gMaterial.color.a は実質「フレネル反射成分の不透明度」として働く。
    float surfaceCoverage = saturate(baseCoverage);
    float3 finalWaterComposite = lerp(transmissionColor, desiredWaterView, surfaceCoverage);

    if (gDepthFadeDebugEnabled != 0)
    {
        output.color.a = 1.0f;

        if (gDepthDebugViewMode == 1)
        {
            float rawDelta = saturate((sceneDepthNDC - waterDepthNDC) * max(gDepthFadeDebugScale, 1.0f));
            output.color.rgb = float3(sceneDepthNDC, waterDepthNDC, rawDelta);
            if (sceneDepthNDC <= waterDepthNDC + 1.0e-5f)
            {
                output.color.rgb = float3(1.0f, 0.0f, 0.0f);
            }
            return output;
        }

        if (gDepthDebugViewMode == 2)
        {
            float sceneLinear = saturate(sceneDepthView / max(gDepthFadeDebugScale, 1.0e-4f));
            float waterLinear = saturate(waterDepthView / max(gDepthFadeDebugScale, 1.0e-4f));
            output.color.rgb = float3(sceneLinear, waterLinear, saturate((sceneDepthView - waterDepthView) / max(gDepthFadeDebugScale, 1.0e-4f)));
            return output;
        }

        if (gDepthDebugViewMode == 3)
        {
            float debugValue = 1.0f - exp(-waterColumn * max(gDepthFadeDebugScale, 1.0e-4f));
            output.color.rgb = VisualizeDepthValue(debugValue);
            return output;
        }

        if (gDepthDebugViewMode == 4)
        {
            output.color.rgb = float3(screenUV, 0.0f);
            return output;
        }

        if (gDepthDebugViewMode == 5)
        {
            output.color.rgb = ResolveWaterTransmissionColor(pixelCoord, screenUV);
            return output;
        }

        if (gDepthDebugViewMode == 6)
        {
            // 生のテクスチャではなく、グロッシー化＋雲キューブマップ合成まで済んだ
            // 「実際に水面合成へ使われる反射色」を表示する。
            // （以前は gReflectionTexture を直接表示していたため、雲の上書き合成が
            //   原因の明暗まだらがこの可視化に映らず、切り分けを誤った）
            output.color.rgb = gReflectionEnabled ? reflectColor : float3(1.0f, 0.0f, 1.0f);
            return output;
        }

        if (gDepthDebugViewMode == 7)
        {
            float3 debugViewDir = normalize(gCamera.worldPosition - input.worldPosition);
            float3 debugGeomNormal = ResolveSurfaceNormal(input);
            float debugCosTheta = saturate(dot(debugGeomNormal, debugViewDir));
            float debugFresnel = FresnelSchlick(debugCosTheta, saturate(gFresnelBaseReflectance));
            output.color.rgb = VisualizeDepthValue(debugFresnel);
            return output;
        }

        if (gDepthDebugViewMode == 8)
        {
            output.color.rgb = SampleRTWaterRefraction(pixelCoord).rgb;
            return output;
        }

        if (gDepthDebugViewMode == 9)
        {
            output.color.rgb = VisualizeRTRefractionReason(SampleRTWaterRefraction(pixelCoord).a);
            return output;
        }

        if (gDepthDebugViewMode == 10)
        {
            float4 rtRefraction = SampleRTWaterRefraction(pixelCoord);
            float3 sceneColor = gSceneColor.Sample(gLinearClamp, screenUV).rgb;
            float rtSuccess = IsRTRefractionSuccess(rtRefraction.a);
            output.color.rgb = rtSuccess > 0.5f
                ? abs(rtRefraction.rgb - sceneColor) * 4.0f
                : VisualizeRTRefractionReason(rtRefraction.a);
            return output;
        }

        if (gDepthDebugViewMode == 11)
        {
            output.color.rgb = transmissionColor;
            return output;
        }

        if (gDepthDebugViewMode == 12)
        {
            // 波長別透過率をそのまま表示する（無効時はマゼンタ）
            output.color.rgb = hasValidDepthFade ? saturate(transmittance) : float3(1.0f, 0.0f, 1.0f);
            return output;
        }

        if (gDepthDebugViewMode == 13)
        {
            output.color.rgb = VisualizeDepthValue(reflectanceWeight);
            return output;
        }

        if (gDepthDebugViewMode == 14)
        {
            output.color.rgb = finalWaterComposite;
            return output;
        }

        if (gDepthDebugViewMode == 15)
        {
            float rtSuccess = IsRTRefractionSuccess(SampleRTWaterRefraction(pixelCoord).a);
            output.color.rgb = lerp(float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), rtSuccess);
            return output;
        }

        if (gDepthDebugViewMode == 16)
        {
            output.color.rgb = VisualizeJacobian(input.jacobianData);
            return output;
        }

        // ---- まだら切り分け用の追加可視化（reflectColor の構成要素を単独表示）----
        if (gDepthDebugViewMode == 17)
        {
            // 生の平面反射テクスチャ（グロッシー化・雲合成の一切なし）。
            // ここにブロブが出るなら平面反射（ミラー描画）自体が原因。
            output.color.rgb = gReflectionEnabled
                ? gReflectionTexture.Sample(gLinearClamp, screenUV).rgb
                : float3(1.0f, 0.0f, 1.0f);
            return output;
        }

        if (gDepthDebugViewMode == 18)
        {
            // 雲キューブマップの色（反射方向サンプル）。無効時はマゼンタ。
            // ここにブロブが出るなら雲キューブマップの内容が原因。
            if (gSkyEnvReflectionEnabled != 0)
            {
                float3 dbgReflectDir = reflect(-viewDir, float3(0.0f, 1.0f, 0.0f));
                float4 dbgSkyEnv = gSkyEnvironmentMap.SampleLevel(gLinearClamp, dbgReflectDir, 0.0f);
                output.color.rgb = dbgSkyEnv.rgb;
            }
            else
            {
                output.color.rgb = float3(1.0f, 0.0f, 1.0f);
            }
            return output;
        }

        if (gDepthDebugViewMode == 19)
        {
            // 雲上書きの実効強度: R=cloudOpacity（雲の被覆）、G=輝度比減衰後の実効上書き量。
            // R が強く G が弱ければ「暗雲抑制は効いているが被覆自体が広い」と分かる。
            if (gSkyEnvReflectionEnabled != 0)
            {
                float3 dbgReflectDir = reflect(-viewDir, float3(0.0f, 1.0f, 0.0f));
                const float kDbgEnvMipCount = 5.0f;
                const float kDbgEnvMip = kWaterReflectionMicroRoughness * (kDbgEnvMipCount - 1.0f);
                float4 dbgSkyEnv = gSkyEnvironmentMap.SampleLevel(gLinearClamp, dbgReflectDir, kDbgEnvMip);
                float dbgCloudOpacity = saturate(1.0f - dbgSkyEnv.a);

                const float3 kDbgLuma = float3(0.2126f, 0.7152f, 0.0722f);
                float dbgSkyLuma = dot(SampleGlossyReflection(screenUV, 1.0f - cosTheta), kDbgLuma);
                float dbgCloudLuma = dot(dbgSkyEnv.rgb, kDbgLuma);
                float dbgDarkRatio = saturate(dbgCloudLuma / max(dbgSkyLuma, 1.0e-5f));
                float dbgOverlayScale = lerp(0.25f, 1.0f, dbgDarkRatio);
                float dbgHorizonFade = smoothstep(0.08f, 0.30f, dbgReflectDir.y);

                output.color.rgb = float3(dbgCloudOpacity, dbgCloudOpacity * dbgOverlayScale * dbgHorizonFade, 0.0f);
            }
            else
            {
                output.color.rgb = float3(1.0f, 0.0f, 1.0f);
            }
            return output;
        }

        // ---- まだら診断: フレネル混合を強制して端点を単独表示 ----
        // 20/21 のどちらに斑が出るかで、透過側と反射側のどちらが犯人かを一意に確定する。
        if (gDepthDebugViewMode == 20)
        {
            // reflectanceWeight = 0（純透過）の最終合成。ここに斑が出れば透過側が犯人。
            output.color.rgb = lerp(transmissionColor, transmissionColor, surfaceCoverage);
            return output;
        }

        if (gDepthDebugViewMode == 21)
        {
            // reflectanceWeight = 1（純反射）の最終合成。ここに斑が出れば反射側が犯人。
            output.color.rgb = lerp(transmissionColor, reflectColor, surfaceCoverage);
            return output;
        }

        if (gDepthDebugViewMode == 22)
        {
            // 反射と透過の輝度差。明るいほど、そこでフレネルが振れると斑が見える。
            output.color.rgb = abs(reflectColor - transmissionColor) * 3.0f;
            return output;
        }

        output.color.rgb = float3(1.0f, 1.0f, 0.0f);
        return output;
    }

    output.color.rgb = finalWaterComposite;

    // 太陽グリッター（きらめき）を加算で復元する（ComputeSunGlintSpecular 参照）。
    // 反射無効時は WaterForwardMain の PBR 出力（太陽スペキュラ入り）が reflectColor
    // 端点として生きているため、加算すると二重計上になる。反射有効時のみ。
    if (gReflectionEnabled)
    {
        output.color.rgb += ComputeSunGlintSpecular(geomNormal, viewDir);
    }

    // ---- 5. 空気遠近感（Aerial Perspective）----
    // 不透明パスへの合成（AerialPerspective.CS）は水面より前に終わっているため、
    // 水面自身の距離で同じ霞をここで適用する。屈折成分（背景）には背景自身の距離の
    // 霞が既に乗っているが、水面までの区間が重複適用されるのは半透明の慣例的な近似として許容する
    if (gAerialPerspectiveEnabled != 0)
    {
        output.color.rgb = ApplyAerialPerspective(
            output.color.rgb, input.worldPosition, screenUV, gLinearClamp);
    }

    output.color.a = 1.0f;

    return output;
}
