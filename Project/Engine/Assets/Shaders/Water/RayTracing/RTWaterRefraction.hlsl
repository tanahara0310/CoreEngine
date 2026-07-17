// ============================================================
// DXR 水面屈折シェーダー
// フラットな水面平面を近似し、屈折レイの最初のヒット位置を SceneColor に再投影して
// 水面屈折用のカラー出力を生成する。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"

RWTexture2D<float4> gRefractionOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float4> gWorldPosition : register(t1);
Texture2D<float4> gSceneColor : register(t2);
Texture2D<float4> gFFTOceanDisplacement : register(t3);
Texture2D<float4> gFFTOceanNormal : register(t4);

cbuffer WaterRefractionConstants : register(b0)
{
    float4x4 gViewProjection;
    float3 gCameraPosition;
    float gWaterHeight;
    float gSurfaceBias;
    float gMaxRayDistance;
    float gRefractionEta;
    float gAbsorptionCoeff;
    float gScreenWidth;
    float gScreenHeight;
    float gMaxRefractionOffsetPixels;
    uint gFFTOceanEnabled;
    float gFFTOceanPatchLength;
    uint gFFTOceanResolution;
    float gDebugDisplayScale;
    uint gDebugViewMode;
    // ワールドXZ → FFT テクスチャ UV の写像（ラスタ描画 FFTWater.VS と一致させる）
    // float2 が 16 バイト境界を跨がないよう、旧 gPadding0 を置き換えて配置する
    float2 gFFTOceanUVScale;
    float2 gFFTOceanUVOffset;
};

static const uint kRTRefractionDebugNone = 0;
static const uint kRTRefractionDebugUVOffsetPixels = 1;
static const uint kRTRefractionDebugDepthMismatch = 2;
static const uint kRTRefractionDebugWaterNormal = 3;
static const uint kRTRefractionDebugRefractedDirection = 4;

struct RefractionPayload
{
    float hitT;
    float hitFlag;
};

#ifdef __INTELLISENSE__
#define RAY_FLAG_NONE 0x0
#define RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH 0x4
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout RefractionPayload payload);
#endif

static const float kRTReasonNoWorldPosition = 1.0f / 255.0f;
static const float kRTReasonNearZeroSceneDistance = 2.0f / 255.0f;
static const float kRTReasonParallelToWater = 3.0f / 255.0f;
static const float kRTReasonInvalidPlaneIntersection = 4.0f / 255.0f;
static const float kRTReasonInvalidRefractionVector = 5.0f / 255.0f;
static const float kRTReasonTraceMiss = 6.0f / 255.0f;
static const float kRTReasonInvalidClip = 7.0f / 255.0f;
static const float kRTReasonDepthMismatch = 9.0f / 255.0f;

// ------------------------------------------------------------
// 成功時のアルファ値エンコード
// 失敗理由コード（1〜9/255、= [0, 0.5) の範囲）と衝突しないよう、
// 成功時は [0.5, 1.0] の範囲を使い、その中に「屈折レイが水面から
// ヒット点まで実際に進んだ光路長（＝真の水柱厚さ）」を詰め込む。
// Water.PS.hlsl 側の Beer-Lambert 吸収計算はこれまで水面ピクセル
// 直下のスクリーン空間深度（屈折で曲げる前の深度）を使っていたため、
// 実際に表示されている（屈折で曲がった先の）内容と深度が食い違い、
// 「水中オブジェクトが水面にそのまま浮いて見える」原因になっていた。
// この光路長を伝搬させることで、表示内容と吸収量を一致させる。
// kRTMaxOpticalPathMeters は Water.PS.hlsl 側の同名定数と必ず一致させること。
// ------------------------------------------------------------
static const float kRTSuccessRangeMin = 0.5f;
static const float kRTMaxOpticalPathMeters = 64.0f;

float4 MakeFallbackOutput(float3 fallbackColor, float reasonCode)
{
    return float4(fallbackColor, reasonCode);
}

float EncodeSuccessAlpha(float opticalPathLength)
{
    float normalized = saturate(opticalPathLength / kRTMaxOpticalPathMeters);
    return kRTSuccessRangeMin + normalized * (1.0f - kRTSuccessRangeMin);
}

float3 EncodeSignedVector(float3 vectorValue)
{
    return normalize(vectorValue) * 0.5f + 0.5f;
}

float3 VisualizeScalar(float value, float displayScale)
{
    const float scaled = 1.0f - exp(-max(value, 0.0f) * max(displayScale, 1.0e-4f));
    return scaled.xxx;
}

float3 BuildRefractionDebugColor(
    uint debugViewMode,
    float uvOffsetPixels,
    float depthMismatch,
    float3 waterNormal,
    float3 refractedDir)
{
    if (debugViewMode == kRTRefractionDebugUVOffsetPixels)
    {
        return VisualizeScalar(uvOffsetPixels, gDebugDisplayScale);
    }

    if (debugViewMode == kRTRefractionDebugDepthMismatch)
    {
        return VisualizeScalar(depthMismatch, gDebugDisplayScale);
    }

    if (debugViewMode == kRTRefractionDebugWaterNormal)
    {
        return EncodeSignedVector(waterNormal);
    }

    if (debugViewMode == kRTRefractionDebugRefractedDirection)
    {
        return EncodeSignedVector(refractedDir);
    }

    return 0.0f.xxx;
}

float ComputeScreenBoundsFade(float2 uv)
{
    // 画面内では減衰させず、画面外へはみ出した距離に応じて対称にフェードする。
    // 以前の実装は下端だけ広い固定マージンを持っており、uv.y が 1 に近づくだけで
    // 実際には画面内にある屈折まで早期にフォールバックしていた。
    const float2 screenSize = float2(gScreenWidth, gScreenHeight);
    const float2 outsideDistancePixels = abs(uv - saturate(uv)) * screenSize;
    const float outsidePixels = max(outsideDistancePixels.x, outsideDistancePixels.y);
    const float fadeMarginPixels = 32.0f;
    return 1.0f - smoothstep(0.0f, fadeMarginPixels, outsidePixels);
}

bool UseFFTOceanSurface()
{
    return gSurfaceSimulationType == kWaterSurfaceModelTypeFFTOcean
        && gFFTOceanEnabled != 0
        && gFFTOceanResolution > 0
        && gFFTOceanPatchLength > 1.0e-4f;
}

float3 EvaluateRefractionWaterOffset(float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterOffset(worldXZ);
    }

    return SampleFFTOceanBilinear(gFFTOceanDisplacement, worldXZ, gFFTOceanUVScale, gFFTOceanUVOffset, gFFTOceanResolution).xyz;
}

float3 EvaluateRefractionWaterNormal(float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterNormal(worldXZ);
    }

    const float3 encodedNormal = SampleFFTOceanBilinear(gFFTOceanNormal, worldXZ, gFFTOceanUVScale, gFFTOceanUVOffset, gFFTOceanResolution).xyz;
    const float3 decodedNormal = normalize(encodedNormal * 2.0f - 1.0f);
    return decodedNormal.y < 0.0f ? -decodedNormal : decodedNormal;
}

[shader("raygeneration")]
void RTWaterRefractionRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float4 fallbackSample = gSceneColor.Load(int3(launchIndex, 0));
    float4 worldPosSample = gWorldPosition.Load(int3(launchIndex, 0));

    if (worldPosSample.a < 0.5f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonNoWorldPosition);
        return;
    }

    float3 cameraToScene = worldPosSample.xyz - gCameraPosition;
    float sceneDistance = length(cameraToScene);
    if (sceneDistance <= 1.0e-4f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonNearZeroSceneDistance);
        return;
    }

    float3 primaryDir = cameraToScene / sceneDistance;
    float denom = primaryDir.y;
    if (abs(denom) <= 1.0e-5f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonParallelToWater);
        return;
    }

    // フラット平面（波なし）での交点は、あくまで反復解法の初期値（シード）に過ぎない。
    // 実際の水面は波で上下するため、フラット高さでの交点が sceneDistance の近く/外側に
    // あっても、波を反映した本当の交点は有効範囲内に収まることがある。
    // 以前はこの「シード」段階で 0 < t < sceneDistance を厳密に要求していたため、
    // カメラを水面へ近づけるとカメラ直下の近い/浅いジオメトリ（sceneDistance が小さい）
    // でフラット高さの誤差が相対的に大きくなり、本来は有効なはずの交点まで誤って
    // 弾かれ、その領域だけ屈折がフォールバックしていた
    // （＝「カメラを近づけるとカメラ下側のエリアの屈折が消える」不具合の原因）。
    // ここではシード自体の妥当性判定を行わず、波を反映した精密化後に判定し直す。
    float tSurfaceSeed = (gSurfaceWaterHeight - gCameraPosition.y) / denom;
    if (!isfinite(tSurfaceSeed))
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidPlaneIntersection);
        return;
    }
    float tSeedClamped = clamp(tSurfaceSeed, 0.0f, sceneDistance);

    float3 waterPos = gCameraPosition + primaryDir * tSeedClamped;
    float safeDenom = denom;
    if (abs(safeDenom) < 1.0e-4f)
    {
        safeDenom = (safeDenom < 0.0f) ? -1.0e-4f : 1.0e-4f;
    }
    [unroll]
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        float3 waveOffset = EvaluateRefractionWaterOffset(waterPos.xz);
        float surfaceY = gSurfaceWaterHeight + waveOffset.y;
        float deltaY = waterPos.y - surfaceY;
        waterPos -= primaryDir * (deltaY / safeDenom);
    }

    // 波を反映した精密化後の実際の交点までの距離で改めて有効性を判定する。
    float tRefined = dot(waterPos - gCameraPosition, primaryDir);
    if (tRefined <= 1.0e-4f || tRefined >= sceneDistance)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidPlaneIntersection);
        return;
    }

    float3 waterNormal = EvaluateRefractionWaterNormal(waterPos.xz);
    float3 refractedDir = refract(primaryDir, waterNormal, gRefractionEta);
    if (dot(refractedDir, refractedDir) <= 1.0e-6f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidRefractionVector);
        return;
    }

    RayDesc ray;
    // 屈折後のレイは水中（-waterNormal 側）へ進むため、バイアスも同じ -waterNormal 方向へ
    // かける必要がある（RTWaterCaustics.hlsl と同じ規約）。
    // +waterNormal 方向（空気側）へずらしていた場合、レイが水面直下でバイアス距離分
    // 逆走することになり、浅瀬や水面ぎりぎりのジオメトリで自己交差・誤ミスを招く。
    ray.Origin = waterPos - waterNormal * gSurfaceBias;
    ray.Direction = normalize(refractedDir);
    ray.TMin = 0.001f;
    ray.TMax = gMaxRayDistance;

    RefractionPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;

    // 屈折は「屈折レイが最初に交差する最も近い面」の色が必要なため、
    // 最近接ヒット（RAY_FLAG_NONE）でトレースする。
    // 以前は RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH（遮蔽判定用の任意ヒット）を
    // 使っていたため、最近接ではない面がヒットして誤った屈折色や
    // depth mismatch によるフォールバックの原因になっていた。
    TraceRay(
        gScene,
        RAY_FLAG_NONE,
        0xFF,
        0,
        1,
        0,
        ray,
        payload);

    if (payload.hitFlag < 0.5f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonTraceMiss);
        return;
    }

    float3 hitWorldPos = ray.Origin + ray.Direction * payload.hitT;
    float4 clip = mul(float4(hitWorldPos, 1.0f), gViewProjection);
    if (clip.w <= 1.0e-5f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidClip);
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;

    float2 screenUV = (float2(launchIndex) + 0.5f) / float2(gScreenWidth, gScreenHeight);

    // スクリーン空間で SceneColor を再利用する都合上、屈折先が画面外に出た場合は
    // その位置の色を物理的に取得できない（この手法の原理的な制約）。
    // ただし画面端に近いだけの有効な屈折まで減衰させると、特定方向だけ
    // 不自然に途切れて見える。そこでフェードは「画面外へ実際にはみ出した距離」に対してのみ
    // 適用し、画面内に留まっている屈折は上下左右で等しく保持する。
    float edgeFade = ComputeScreenBoundsFade(uv);

    if (edgeFade <= 1.0e-4f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidClip);
        return;
    }

    // サンプル座標の算出には必ず [0,1] にクランプした値を使う。
    // 範囲外の uv をそのまま uint へキャストすると負値が巨大な符号なし値にラップし、
    // 意図せず画面反対側の端をサンプルしてしまうため。
    float2 clampedUV = saturate(uv);

    // レイトレーシングで得た屈折ヒット点の「正確な」スクリーン投影位置をそのまま使う。
    // 以前は gMaxRefractionOffsetPixels でずれ量を固定ピクセルに強制クランプしていたため、
    // 屈折が「水中オブジェクトが少しずれただけ」に見え、さらにクランプ後のサンプル位置と
    // 実ヒット点の深度が食い違い depth mismatch でフォールバックしていた。
    // gMaxRefractionOffsetPixels は 0 で無制限（物理的に正しい RT 屈折）、
    // 正の値のときのみ暴発防止用の安全クランプとして機能する。
    float2 refractedUV = clampedUV;
    if (gMaxRefractionOffsetPixels > 0.0f)
    {
        float2 maxOffset = max(gMaxRefractionOffsetPixels, 0.0f) / float2(gScreenWidth, gScreenHeight);
        float2 uvOffset = clamp(clampedUV - screenUV, -maxOffset, maxOffset);
        refractedUV = saturate(screenUV + uvOffset);
    }

    uint2 sampleCoord = uint2(refractedUV * float2(gScreenWidth, gScreenHeight));
    sampleCoord = min(sampleCoord, uint2(gScreenWidth - 1.0f, gScreenHeight - 1.0f));

    const float uvOffsetPixels = length((refractedUV - screenUV) * float2(gScreenWidth, gScreenHeight));

    float4 sampledWorldPos = gWorldPosition.Load(int3(sampleCoord, 0));
    float depthMismatch = 0.0f;
    float depthMismatchThreshold = 0.0f;
    if (sampledWorldPos.a < 0.5f)
    {
        if (gDebugViewMode != kRTRefractionDebugNone)
        {
            const float3 debugColor = BuildRefractionDebugColor(
                gDebugViewMode,
                uvOffsetPixels,
                0.0f,
                waterNormal,
                refractedDir);
            gRefractionOutput[launchIndex] = float4(debugColor, kRTReasonDepthMismatch);
            return;
        }

        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonDepthMismatch);
        return;
    }

    float sampledViewDistance = length(sampledWorldPos.xyz - gCameraPosition);
    float hitViewDistance = length(hitWorldPos - gCameraPosition);
    depthMismatch = abs(sampledViewDistance - hitViewDistance);
    depthMismatchThreshold = max(0.08f, hitViewDistance * 0.03f);

    // 屈折レイが水面から実際のヒット点まで進んだ距離 = 真の光路長（水柱厚さ）。
    // Water.PS.hlsl の Beer-Lambert 吸収計算に渡し、表示内容と深度を一致させる。
    const float opticalPathLength = length(hitWorldPos - ray.Origin);

    if (gDebugViewMode != kRTRefractionDebugNone)
    {
        const float3 debugColor = BuildRefractionDebugColor(
            gDebugViewMode,
            uvOffsetPixels,
            depthMismatch,
            waterNormal,
            refractedDir);
        const float reasonCode = (depthMismatch > depthMismatchThreshold)
            ? kRTReasonDepthMismatch
            : EncodeSuccessAlpha(opticalPathLength);
        gRefractionOutput[launchIndex] = float4(debugColor, reasonCode);
        return;
    }

    if (depthMismatch > depthMismatchThreshold)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonDepthMismatch);
        return;
    }

    float3 refractedColor = gSceneColor.Load(int3(sampleCoord, 0)).rgb;
    // 画面端フェードを適用し、画面外へ抜ける手前でなだらかにフォールバック色へ収束させる。
    float3 blendedColor = lerp(fallbackSample.rgb, refractedColor, edgeFade);

    gRefractionOutput[launchIndex] = float4(blendedColor, EncodeSuccessAlpha(opticalPathLength));
}

[shader("miss")]
void RTWaterRefractionMiss(inout RefractionPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterRefractionClosestHit(
    inout RefractionPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
}
