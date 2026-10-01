// ============================================================
// DXR 水面コースティクスシェーダー
// 第2段階ではまず受光点ごとの簡易集光量を専用RT出力へ書き出す。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
// 水面を通って受光点へ届く日光の計算（RTWaterRefraction と共有）
#include "RTWaterCausticsCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"
#include "ColorSpace.hlsli" // Luminance

RWTexture2D<float4> gCausticsOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1); // WorldPosition ターゲット廃止に伴い深度から復元する
Texture2D<float4> gNormalRoughness : register(t2);
Texture2DArray<float4> gFFTOceanDisplacement : register(t3);
Texture2DArray<float4> gFFTOceanNormal : register(t4);

cbuffer WaterCausticsConstants : register(b0)
{
    float gMaxTraceDistance;
    float gSurfaceBias;
    float gIntensityScale;
    // 旧 gWaterHeight。水面高さは b1 の gSurfaceWaterHeight に一本化され未使用。
    // スロットを消すと後続の float3 が 16B 境界をまたいで C++ 側とずれるため
    // パディングとして残す（RTシャドウの cbuffer 配列ずれ事故と同型の罠）。
    float gPadding0;
    float3 gLightDirection;
    float gScreenWidth;
    float gScreenHeight;
    // 旧 FFT 有効情報 3 スロット。実体は b1（RTWaterSurfaceCommon.hlsli）へ一本化済み。
    uint gFFTOceanPad1;
    float gFFTOceanPad0;
    uint gFFTOceanPad2;
    float gRefractiveIndex;
    float gDebugDisplayScale;
    uint gDebugViewMode;
    // シーンの実際のディレクショナルライトが無効な場合はコースティクスも出さない。
    // 以前はここが未使用の padding で、ライトを消してもコースティクスが消えなかった。
    uint gLightEnabled;
    float3 gLightColor;
    float gLightIntensity;
    // 水面メッシュのワールドXZ範囲（WaterCausticsConstants と一致させること）。
    // コースティクスは解析的な無限水面として評価されるため、この矩形でマスクしないと
    // 水域の外（無限床など「水面高さより低い場所すべて」）にも集光模様が漏れる。
    float2 gRegionCenterXZ;
    float2 gRegionHalfExtentXZ;
    uint gRegionValid;
    // 波長依存の吸収係数 σa [1/m]（RGB）。水面描画（WaterFrameConstants.absorptionCoeff、
    // Jerlov プリセット/濁度 UI）と同じ値が毎フレーム同期される。赤 > 緑 > 青。
    float3 gAbsorptionCoeff;
    float4x4 gInvViewProj; // WorldPosition ターゲット廃止に伴う深度復元用
};

static const uint kRTCausticsDebugNone = 0;
static const uint kRTCausticsDebugShallowFade = 1;
static const uint kRTCausticsDebugMatchFactor = 2;
static const uint kRTCausticsDebugAttenuation = 3;
static const uint kRTCausticsDebugReceiverFacing = 4;
static const uint kRTCausticsDebugFinalIntensity = 5;
static const uint kRTCausticsDebugConcentration = 6;

// ペイロード（RTWaterPayload）・失敗理由コード・波面評価は RTWaterSurfaceCommon.hlsli（共通）。

#ifdef __INTELLISENSE__
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout RTWaterPayload payload);
#endif

float3 VisualizeScalar(float value)
{
    return VisualizeRTScalar(value, gDebugDisplayScale);
}

float3 BuildCausticsDebugColor(
    uint debugViewMode,
    float shallowFade,
    float matchFactor,
    float transmittanceLuma, // Beer–Lambert 透過率の輝度（旧 attenuation スロット）
    float receiverFacingFactor,
    float intensity,
    float concentration)
{
    if (debugViewMode == kRTCausticsDebugConcentration)
    {
        return VisualizeScalar(concentration);
    }

    if (debugViewMode == kRTCausticsDebugShallowFade)
    {
        return VisualizeScalar(shallowFade);
    }

    if (debugViewMode == kRTCausticsDebugMatchFactor)
    {
        return VisualizeScalar(matchFactor);
    }

    if (debugViewMode == kRTCausticsDebugAttenuation)
    {
        return VisualizeScalar(transmittanceLuma);
    }

    if (debugViewMode == kRTCausticsDebugReceiverFacing)
    {
        return VisualizeScalar(receiverFacingFactor);
    }

    if (debugViewMode == kRTCausticsDebugFinalIntensity)
    {
        return VisualizeScalar(intensity);
    }

    return 0.0f.xxx;
}

/// @brief 受光点で 1 ピクセルが覆うワールド空間の幅 [m] を返す
/// @param screenUV        受光ピクセル中心の UV
/// @param ndcDepth        受光ピクセルの NDC 深度
/// @param receiverWorldPos 復元済みの受光点ワールド座標
/// @param receiverNormal   受光面のワールド法線
/// @details 同一深度の隣接ピクセルを復元して視線に垂直な断面でのピクセル幅を求め、
///          視線と受光面法線のなす角の余弦で割って受光面に沿った幅へ換算する。
///          かすめ角ほど 1 ピクセルが覆う受光面の範囲は広がる。
float ComputeReceiverFootprintMeters(
    float2 screenUV, float ndcDepth, float3 receiverWorldPos, float3 receiverNormal)
{
    const float perpendicularWidth = ComputePixelPerpendicularWidth(
        screenUV, ndcDepth, float2(gScreenWidth, gScreenHeight), gInvViewProj);

    // ニア平面上の同 UV の点から受光点への向きが、このピクセルの視線方向。
    const float3 nearPoint = ReconstructWorldPosition(ScreenUVToNDC(screenUV), 0.0f, gInvViewProj);
    const float3 toReceiver = receiverWorldPos - nearPoint;
    const float3 viewDir = toReceiver / max(length(toReceiver), 1.0e-4f);

    return ProjectFootprintOntoSurface(perpendicularWidth, viewDir, receiverNormal);
}

/// @brief このシェーダーの定数からコースティクスの計算に使う値を組み立てる
WaterCausticsParams MakeCausticsParams()
{
    WaterCausticsParams params;
    params.lightDirection = gLightDirection;
    params.lightColor = gLightColor;
    params.lightIntensity = gLightIntensity;
    params.intensityScale = gIntensityScale;
    params.refractiveIndex = gRefractiveIndex;
    params.absorption = gAbsorptionCoeff;
    params.surfaceBias = gSurfaceBias;
    params.maxTraceDistance = gMaxTraceDistance;
    params.regionCenterXZ = gRegionCenterXZ;
    params.regionHalfExtentXZ = gRegionHalfExtentXZ;
    params.regionValid = gRegionValid;
    return params;
}

[shader("raygeneration")]
void RTWaterCausticsRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;

    // ライトが無効（消灯）な場合はコースティクスも出さない。
    // 非RT版 WaterCaustics.PS.hlsl の gMainLightEnabled チェックと同じ扱い。
    if (gLightEnabled == 0)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float ndcDepth = gSceneDepth.Load(int3(launchIndex, 0));
    if (IsBackgroundDepth(ndcDepth))
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float2 screenUV = (float2(launchIndex) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);
    float3 receiverWorldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gInvViewProj);
    float3 receiverNormal = normalize(gNormalRoughness.Load(int3(launchIndex, 0)).xyz * 2.0f - 1.0f);

    // 集光率のヤコビアン差分幅に使う、このピクセルが受光面で覆う幅
    const float receiverFootprintMeters = ComputeReceiverFootprintMeters(
        screenUV, ndcDepth, receiverWorldPos, receiverNormal);

    // 水深・被覆率・平らな水面の透過光と、入射点から受光点へのレイ（RTWaterCausticsCommon.hlsli）
    const WaterCausticsParams params = MakeCausticsParams();
    const WaterCausticsPath path = BeginWaterCaustics(
        params, receiverWorldPos, receiverNormal, receiverFootprintMeters,
        gFFTOceanDisplacement, gFFTOceanNormal);
    if (!path.traceRequired)
    {
        gCausticsOutput[launchIndex] = float4(path.baselineRadiance, path.coverage);
        return;
    }

    RTWaterPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;

    TraceRay(
        gScene,
        0,
        0xFF,
        0,
        1,
        0,
        path.ray,
        payload);

    const WaterCausticsResult result = FinishWaterCaustics(
        path, params, receiverWorldPos, receiverNormal, receiverFootprintMeters,
        payload.hitFlag >= 0.5f, payload.hitT,
        gFFTOceanDisplacement, gFFTOceanNormal);

    if (gDebugViewMode != kRTCausticsDebugNone && result.patternEvaluated)
    {
        const float transmittanceLuma = Luminance(result.transmittance);
        const float intensityLuma = Luminance(result.radiance);
        const float3 debugColor = BuildCausticsDebugColor(
            gDebugViewMode,
            1.0f,
            result.matchFactor,
            transmittanceLuma,
            result.geometryFactor,
            intensityLuma,
            result.concentration);
        gCausticsOutput[launchIndex] = float4(debugColor, path.coverage);
        return;
    }

    // α は波込みの水中被覆率（DeferredLighting の直接光置換率）
    gCausticsOutput[launchIndex] = float4(result.radiance, path.coverage);
}

[shader("miss")]
void RTWaterCausticsMiss(inout RTWaterPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterCausticsClosestHit(
    inout RTWaterPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
}
