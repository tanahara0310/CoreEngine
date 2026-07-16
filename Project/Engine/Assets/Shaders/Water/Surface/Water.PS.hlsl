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
// 頂点解像度に依存しない法線を得るため、ピクセルシェーダーで直接再サンプリングする
Texture2D<float4> gFFTOceanNormal : register(t19);

// ===== 空の放射照度 SH9 係数（SkyIrradianceSH.CS.hlsl 出力）=====
// WaterPlaneObject::BindCustomResources() が t24 にバインドする。
// 係数には太陽色・強度が焼き込み済みで、Σ c_i・Y_i(N) がそのまま
// 「法線 N に対する空からの放射照度 / π」になる（DeferredLighting と同じ規約）。
StructuredBuffer<float4> gWaterSkyIrradianceSH : register(t24);

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
    float gDebugPadding;
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
    float3 ambient = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float downwelling = saturate(-normalize(gDirectionalLights[i].direction).y);
        ambient += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * downwelling / kPi;
    }

    // 天空拡散光
    if (gSkyAmbientEnabled != 0)
    {
        // 大気散乱の空を SH9 で評価（上向き法線＝水面へ降り注ぐ天空放射照度/π）。
        // DeferredLighting と同じスケールでサーフェス光単位へ変換する
        ambient += EvaluateWaterSkyIrradiance(float3(0.0f, 1.0f, 0.0f)) * gSkyAmbientScale;
    }
    else if (gIBLParams.sceneIBLEnabled != 0)
    {
        // Irradiance マップは既に「albedo に掛けるだけ」の放射輝度相当なのでそのまま加算
        ambient += gIrradianceMap.SampleLevel(gSampler, float3(0.0f, 1.0f, 0.0f), 0.0f).rgb
                 * gIBLParams.environmentIntensity;
    }

    return ambient;
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

/// @brief ピクセル単位の法線を解決する
/// @details Gerstner Wave は頂点シェーダーで解析的に計算した法線をそのまま補間して使えるが、
///          FFT Ocean はテクスチャベースの法線マップであるため、頂点解像度で補間すると
///          メッシュの三角形境界に沿った法線のファセット化（IBL 反射のギザギザ）が発生する。
///          FFT Ocean 使用時は texcoord から法線マップを直接再サンプリングし、
///          頂点密度に依存しない滑らかな法線を得る。
float3 ResolveSurfaceNormal(WaterPSInput input)
{
    float3 vertexNormal = normalize(input.normal);
    if (gUseFFTOceanNormalMap == 0)
    {
        return vertexNormal;
    }

    // FFTWater.VS は変位サンプリングと同じ「スケール適用後 UV」を texcoord に渡してくる。
    // パッチ境界を跨ぐ補間を避けるためピクセル単位で frac して [0,1) に折り返す。
    float3 encodedNormal = gFFTOceanNormal.Sample(gLinearClamp, frac(input.texcoord)).xyz;
    float3 localNormal = normalize(encodedNormal * 2.0f - 1.0f);
    float3 tangent = normalize(input.tangent);
    float3 bitangent = normalize(input.bitangent);
    return normalize(localNormal.x * tangent + localNormal.y * vertexNormal + localNormal.z * bitangent);
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
        // 対応する「真の光路長」で上の近似値を上書きする。
        // 上のスクリーン空間近似は水面ピクセル直下の素の深度（屈折前の深度）を使っており、
        // 屈折で表示位置がズレた分だけ吸収量が表示内容と食い違ってしまう
        // （＝水中オブジェクトが水面に浮いて見える一因）。RT側で実測した光路長を
        // 使うことでこの食い違いを解消する。
        float4 rtRefractionSample = SampleRTWaterRefraction(pixelCoord);
        if (IsRTRefractionSuccess(rtRefractionSample.a) > 0.5f)
        {
            hasValidDepthFade = true;
            waterColumn = DecodeRTOpticalPath(rtRefractionSample.a);
        }
    }

    // Beer-Lambert 則（波長別）: exp(-σt·d)
    float3 transmittance = exp(-sigmaT * waterColumn);

    // ---- 4. 視線方向と Fresnel 係数を計算する ----
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
    float3 geomNormal = ResolveSurfaceNormal(input);
    float cosTheta = saturate(dot(geomNormal, viewDir));
    float fresnel = FresnelSchlick(cosTheta, saturate(gFresnelBaseReflectance));
    float reflectanceWeight = saturate(fresnel * gFresnelReflectanceScale);

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
        reflectColor = gReflectionTexture.Sample(gLinearClamp, screenUV).rgb;
    }

    float3 desiredWaterView = lerp(transmissionColor, reflectColor, reflectanceWeight);
    // 水面ピクセルは常に「水面越しに見える像」（屈折 or 反射）そのものであるべきで、
    // 屈折で曲げていない生のスクリーン座標の背景（gSceneColor.Sample(screenUV)）を
    // 混ぜてはいけない。以前はここで backgroundColor（未屈折・未反射の素の背景）と
    // desiredWaterView（正しく屈折/反射された像）を gMaterial.color.a（既定 0.85）で
    // ブレンドしていたため、屈折によるズレが小さかった頃は両者がほぼ同じ位置を指しており
    // 気付かなかったが、屈折のクランプを撤廃して正しくズレるようになった結果、
    // 「同じ地形が少しズレた位置に二重に見える」ゴースト（二重像）として顕在化した。
    // gMaterial.color.a による不透明度は、背景との合成ではなく屈折色自体との
    // ブレンドとして扱うことで、常に「同じ位置を指す像」同士を混ぜるようにする。
    float surfaceCoverage = saturate(baseCoverage);
    float3 finalWaterComposite = lerp(refractionColor, desiredWaterView, surfaceCoverage);

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
            output.color.rgb = gReflectionEnabled ? gReflectionTexture.Sample(gLinearClamp, screenUV).rgb : float3(1.0f, 0.0f, 1.0f);
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

        output.color.rgb = float3(1.0f, 1.0f, 0.0f);
        return output;
    }

    output.color.rgb = finalWaterComposite;

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
