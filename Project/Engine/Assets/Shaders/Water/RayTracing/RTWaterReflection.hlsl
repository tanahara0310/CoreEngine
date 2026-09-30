// ============================================================
// DXR 水面反射シェーダー
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"
#include "../../Include/RayTracing/RTHitShading.hlsli"

RWTexture2D<float4> gReflectionOutput : register(u0);
// 水面の日向率（0=影 / 1=日向）。水面ではない画素は 1
RWTexture2D<float> gSunVisibilityOutput : register(u1);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gSceneColor : register(t2);
Texture2DArray<float4> gFFTOceanDisplacement : register(t3);
Texture2DArray<float4> gFFTOceanNormal : register(t4);
TextureCube<float4> gSkyEnvironmentMap : register(t5);
// 反射の元画像（gSceneColor）の縮小段。段 0 は半分の解像度（WaterReflectionColorPyramid.CS）。
// rgb = 物の画素の色の平均 × a、a = 物の画素の割合（空と水域の水面より下の画素は物に数えない）。
// 段を作れなかったフレームは gSceneColor そのもの（段数 1）が入る
Texture2D<float4> gSceneColorPyramid : register(t6);
// 同じ縮小段の空の画素の割合。段を作れなかったフレームは読まない
Texture2D<float> gSkyCoveragePyramid : register(t7);

// DXR グローバルルートシグネチャの静的サンプラ（GlobalRootSignatureManager）。
// 空キューブと、再投影先のシーン色の双線形取得に使う。
SamplerState gLinearClamp : register(s0);

cbuffer WaterReflectionConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInvViewProjection; // 深度復元用
    float3 gCameraPosition;
    float gWaterHeight;
    float3 gSunDirection; // メインライトの向き（光源→シーン・正規化済み）
    float gSurfaceBias;
    float gMaxRayDistance;
    float gSkyEnvReflectionEnabled; // 1 = gSkyEnvironmentMap が有効
    float gScreenWidth;
    float gScreenHeight;
    float gMaxReflectionOffsetPixels;
    uint gSunShadowEnabled; // 1 なら水面の点から光源へ影のレイを撃つ
    float gDebugDisplayScale;
    uint gDebugViewMode;
};

/// @brief 反射パスのペイロード（反射レイと影のレイで共通）
struct RTReflectionPayload
{
    float hitT;
    float hitFlag;        // 1 = 当たった / 0 = 抜けた
    uint instanceIndex;   // InstanceID()（ヒットシェーディングの表の行）
    uint primitiveIndex;  // PrimitiveIndex()
    float2 barycentrics;  // 交点の重心座標
};

#ifdef __INTELLISENSE__
#define RAY_FLAG_NONE 0x0
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout RTReflectionPayload payload);
#endif

// 失敗理由コード（kRTReason*）・画面端フェード・波面評価は
// RTWaterSurfaceCommon.hlsli（3 シェーダー共通）。

// ===== 成功アルファ ＝ 反射色の「信頼度」を連続値で運ぶ =====
//   alpha ∈ (0.5, 1.0] … 成功。confidence = (alpha - 0.5) * 2
//   alpha < 0.5        … 失敗（kRTReason* の理由コード）
float MakeSuccessAlpha(float confidence)
{
    return 0.5f + 0.5f * saturate(confidence);
}

float4 MakeFallbackOutput(float reasonCode)
{
    // 反射レイを飛ばす所まで到達できなかった場合だけ使う（水面ではない画素など）。
    // ここに来た画素は Water.PS が反射を必要としないか、空環境マップが無いフレーム。
    return float4(0.0f, 0.0f, 0.0f, reasonCode);
}

// ===== 空環境マップ =====
// ★Water.PS.hlsl の kWaterReflectionMicroRoughness / kEnvMipCount と一致させること★
// （未解像さざ波の実効ラフネスに相当するミップを引く。値がずれると
//   RT が解決した空と Water.PS 側の保険フォールバックで見た目が食い違う）
static const float kSkyEnvMipCount = 5.0f;
static const float kSkyEnvMicroRoughness = 0.20f;

/// @param perceptualRoughness 空をぼかすラフネス（ミップ = ラフネス × (段数 - 1)）
float3 SampleSkyEnvironment(float3 dir, float perceptualRoughness)
{
    const float mip = saturate(perceptualRoughness) * (kSkyEnvMipCount - 1.0f);
    return gSkyEnvironmentMap.SampleLevel(gLinearClamp, dir, mip).rgb;
}

/// @brief トレースしたレイの向きで空を引いた「解決済みの反射色」を返す。
/// @details 空キューブが無いフレームだけ理由コード（alpha<0.5）へ落とす。
///          その場合のみ Water.PS の保険フォールバックが動く。
float4 MakeSkyResolvedOutput(float3 rayDir, float reasonCodeIfNoSky, float perceptualRoughness)
{
    if (gSkyEnvReflectionEnabled < 0.5f)
    {
        return MakeFallbackOutput(reasonCodeIfNoSky);
    }
    return float4(SampleSkyEnvironment(rayDir, perceptualRoughness), MakeSuccessAlpha(1.0f));
}

/// @brief 縮小段の段数（縮小段が無いフレームは 1）
uint GetReflectionPyramidLevels()
{
    uint pyramidWidth = 1;
    uint pyramidHeight = 1;
    uint pyramidLevels = 1;
    gSceneColorPyramid.GetDimensions(0, pyramidWidth, pyramidHeight, pyramidLevels);
    return pyramidLevels;
}

/// @brief ぼかしの幅に合った縮小段の段
/// @param blurPixels ぼかしの幅（フル解像度の画素数。段 0 の 2 画素より細かくはしない）
float ComputeReflectionPyramidLevel(float blurPixels, uint pyramidLevels)
{
    // 段 L の 1 画素はフル解像度の 2^(L+1) 画素
    return clamp(log2(max(blurPixels, 2.0f)) - 1.0f, 0.0f, (float)(pyramidLevels - 1));
}

/// @brief 反射の元画像を縦と横で別の広がりでぼかし、写っている物の色と割合を引く
/// @param uv                    引く位置（スクリーン UV）
/// @param verticalSigmaPixels   縦の広がり（1σ・フル解像度の画素数）
/// @param horizontalSigmaPixels 横の広がり（1σ・フル解像度の画素数）
/// @return rgb = 物の色の平均、a = 水面より上の画素のうち物の割合（残りは空）。
///         範囲の画素がすべて水面より下なら a は負
/// @details 縦に並べたタップで縦の広がりを、縮小段の選び方で横の広がりを作る。
///          段のぼかし幅はタップの間隔以上にする。
///          空の画素は色に使わず、割合だけを返す。水面より下の画素は割合の分母からも外す。
float4 SampleReflectedGeometryStreak(
    float2 uv, float verticalSigmaPixels, float horizontalSigmaPixels, uint pyramidLevels)
{
    // ±2σ の範囲に等間隔でタップを置き、正規分布の重みで平均する
    static const int kTapCount = 8;
    const float tapSpacingPixels = 4.0f * verticalSigmaPixels / (float)kTapCount;
    const float level = ComputeReflectionPyramidLevel(
        max(2.0f * horizontalSigmaPixels, tapSpacingPixels), pyramidLevels);
    float3 geometryColorSum = float3(0.0f, 0.0f, 0.0f);
    float geometrySum = 0.0f;
    float skySum = 0.0f;
    [unroll]
    for (int i = 0; i < kTapCount; ++i)
    {
        const float offsetSigma = ((float(i) + 0.5f) / (float)kTapCount) * 4.0f - 2.0f;
        const float weight = exp(-0.5f * offsetSigma * offsetSigma);
        const float2 tapUV = saturate(uv + float2(0.0f, offsetSigma * verticalSigmaPixels / gScreenHeight));
        const float4 geometry = gSceneColorPyramid.SampleLevel(gLinearClamp, tapUV, level);
        geometryColorSum += geometry.rgb * weight;
        geometrySum += geometry.a * weight;
        skySum += gSkyCoveragePyramid.SampleLevel(gLinearClamp, tapUV, level) * weight;
    }
    const float aboveWaterSum = geometrySum + skySum;
    if (aboveWaterSum <= 1.0e-3f)
    {
        return float4(0.0f, 0.0f, 0.0f, -1.0f);
    }
    return float4(geometryColorSum / max(geometrySum, 1.0e-4f), geometrySum / aboveWaterSum);
}

/// @brief 投影先のまわりの画素から、当たった点と同じ奥行きに写っている物の色を探す
/// @param uv              当たった点を画面へ投影した位置
/// @param hitViewDistance カメラから当たった点までの距離
/// @param tolerance       同じ物とみなす距離の差
/// @param color           見つかった画素の色の平均
/// @return 見つかった度合い（0 = 見つからない、1 = 3 画素以上見つかった）
float FindHitColorNearby(float2 uv, float hitViewDistance, float tolerance, out float3 color)
{
    static const int kSearchTapCount = 12;
    const float2 screenSize = float2(gScreenWidth, gScreenHeight);
    float3 colorSum = float3(0.0f, 0.0f, 0.0f);
    float foundCount = 0.0f;
    [unroll]
    for (int i = 0; i < kSearchTapCount; ++i)
    {
        // 黄金角のらせんで半径 1〜6 画素に散らす
        const float angle = float(i) * 2.39996323f;
        const float radiusPixels = 1.0f + 5.0f * sqrt((float(i) + 0.5f) / (float)kSearchTapCount);
        const float2 tapUV = saturate(uv + float2(cos(angle), sin(angle)) * radiusPixels / screenSize);
        const uint2 tapCoord = min(uint2(tapUV * screenSize), uint2(screenSize - 1.0f));
        const float tapDepth = gSceneDepth.Load(int3(tapCoord, 0));
        if (IsBackgroundDepth(tapDepth))
        {
            continue;
        }
        const float3 tapWorldPos = ReconstructWorldPosition(ScreenUVToNDC(tapUV), tapDepth, gInvViewProjection);
        if (abs(length(tapWorldPos - gCameraPosition) - hitViewDistance) > tolerance)
        {
            continue;
        }
        colorSum += gSceneColor.Load(int3(tapCoord, 0)).rgb;
        foundCount += 1.0f;
    }
    color = (foundCount > 0.0f) ? colorSum / foundCount : float3(0.0f, 0.0f, 0.0f);
    return saturate(foundCount / 3.0f);
}

/// @brief 点から光源が見えるかを返す（1=日向 / 0=遮蔽物の影）
/// @param position 調べる点
/// @param normal   面の法線（自己交差を避けるずらしに使う）
/// @param bias     法線方向へのずらし幅（m）
float TraceSunVisibility(float3 position, float3 normal, float bias)
{
    const float3 toSun = -gSunDirection;
    if (gSunShadowEnabled == 0 || toSun.y <= 0.0f)
    {
        return 1.0f;
    }

    RayDesc ray;
    ray.Origin = position + normal * bias;
    ray.Direction = toSun;
    ray.TMin = 0.001f;
    ray.TMax = gMaxRayDistance;

    // 当たりはヒットシェーダーを通さずに終えるので、当たった扱いで初期化してミスだけが 0 に戻す
    RTReflectionPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 1.0f;
    payload.instanceIndex = 0;
    payload.primitiveIndex = 0;
    payload.barycentrics = float2(0.0f, 0.0f);
    TraceRay(
        gScene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF, 0, 1, 0, ray, payload);
    return 1.0f - payload.hitFlag;
}

// アルファで抜ける所に当たったときに、その先へ撃ち直す回数の上限
static const uint kMaxCutoutRetrace = 3;

/// @brief 反射レイが当たった物を、当たった点の材質と光で照らした色を返す
/// @param ray               反射レイ
/// @param payload           反射レイの結果（当たっていること）
/// @param coneWidthAtOrigin レイの始点での広がりの幅（m）
/// @param coneSpread        レイの広がりの角度（rad。距離に比例して幅が増える）
/// @return rgb = 照らした色、a = 1（抜ける材質の先で何にも当たらなかったときは 0）
/// @details 抜ける材質（アルファ）に当たったときは、その先へ撃ち直す
float4 ShadeReflectionHit(RayDesc ray, RTReflectionPayload payload, float coneWidthAtOrigin, float coneSpread)
{
    [loop]
    for (uint attempt = 0; attempt <= kMaxCutoutRetrace; ++attempt)
    {
        const float coneWidth = coneWidthAtOrigin + coneSpread * payload.hitT;
        const RTHitSurface surface = FetchHitSurface(
            payload.instanceIndex, payload.primitiveIndex, payload.barycentrics, ray.Direction, coneWidth);
        if (!surface.cutout)
        {
            const float bias = max(0.02f, payload.hitT * 1.0e-4f);
            const float visibility = TraceSunVisibility(surface.position, surface.normal, bias);
            return float4(ShadeHitSurface(surface, -ray.Direction, visibility), 1.0f);
        }
        if (attempt == kMaxCutoutRetrace)
        {
            break;
        }

        ray.TMin = payload.hitT + 1.0e-3f;
        payload.hitT = 0.0f;
        payload.hitFlag = 0.0f;
        TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
        if (payload.hitFlag < 0.5f)
        {
            break;
        }
    }
    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

[shader("raygeneration")]
void RTWaterReflectionRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    // 水面ではない画素は日向のまま（水面の点が求まった後で上書きする）
    gSunVisibilityOutput[launchIndex] = 1.0f;
    float ndcDepth = gSceneDepth.Load(int3(launchIndex, 0));
    float2 screenUV = (float2(launchIndex) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);

    // 背景（水面の後ろに不透明ジオメトリが無い外洋）でも反射を計算する。
    // 地平線より上を向く画素は下の tRefined 判定が TraceRay の前に弾く
    const bool hasBackground = IsBackgroundDepth(ndcDepth);

    // 画素を通る視線は深度に依存しない（同じ直線）ので、背景でも向きは求まる。
    float3 rayThroughPixel = ReconstructWorldPosition(
        ScreenUVToNDC(screenUV), hasBackground ? 0.5f : ndcDepth, gInvViewProjection);
    float3 cameraToScene = rayThroughPixel - gCameraPosition;
    float pixelRayLength = length(cameraToScene);
    if (pixelRayLength <= 1.0e-4f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonNearZeroSceneDistance);
        return;
    }

    float3 primaryDir = cameraToScene / pixelRayLength;

    // 「水面が不透明シーン点より手前か」の判定に使う距離。
    // 背景ピクセルには不透明面が無いのでレイ最大距離を上限にする。
    float sceneDistance = hasBackground ? gMaxRayDistance : pixelRayLength;
    float denom = primaryDir.y;
    if (abs(denom) <= 1.0e-5f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonParallelToWater);
        return;
    }

    // フラット平面シード→波反映の固定点反復（RTWaterRefraction と共通の
    // RefineWaterSurfaceIntersection。詳細コメントは RTWaterSurfaceCommon.hlsli）。
    float3 waterPos;
    if (!RefineWaterSurfaceIntersection(
            gFFTOceanDisplacement, gCameraPosition, primaryDir, sceneDistance, waterPos))
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidPlaneIntersection);
        return;
    }

    // 水面が opaque シーン点より手前にあるピクセルだけ反射する（＝水面が見えている領域）。
    float tRefined = dot(waterPos - gCameraPosition, primaryDir);
    if (tRefined <= 1.0e-4f || tRefined >= sceneDistance)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidPlaneIntersection);
        return;
    }

    // 1 ピクセルの視線の広がり（rad）
    const float pixelAngle = ComputePixelPerpendicularWidth(
        screenUV,
        hasBackground ? 0.5f : ndcDepth,
        float2(gScreenWidth, gScreenHeight),
        gInvViewProjection) / pixelRayLength;

    // 1 ピクセルが水面交点で覆う幅。垂直断面幅を水面までの距離へ比例縮小し、
    // 平坦水面（+Y）へ投影する。波法線のカスケード縮小フィルタに渡す
    const float surfaceFootprintMeters = ProjectFootprintOntoSurface(
        pixelAngle * tRefined,
        primaryDir,
        float3(0.0f, 1.0f, 0.0f));

    // 反射レイの向きは「うねりスケールの法線」だけで決める。
    // 反射は再投影のてこ（レイ長）が長く、フットプリント内で解像できている
    // 細かなさざ波の傾きでも、隣接ピクセル間で再投影先が別の物体へ飛ぶ。
    // 画素単位のばらつきは Water.PS の SampleGlossyReflectionRGBA がならす。
    // 1.0m で最細カスケードが消え、中間カスケードは減衰して通る。
    const float kReflectionDirectionMinFootprint = 1.0f;
    const float directionFootprint = max(surfaceFootprintMeters, kReflectionDirectionMinFootprint);

    float3 waterNormal = EvaluateWaterNormal(gFFTOceanNormal, waterPos.xz, directionFootprint);

    // 1 ピクセルより細かくて見えない波の傾きを、反射のぼけとして戻す（画素より大きい波はぼかさない）。
    // 空はそのラフネスに合ったミップで、物は反射の向きのばらつき（1σ・rad）の幅でぼかして引く。
    // 軸ごとの傾きの分散は平均二乗傾斜の半分。視線を含む縦の面では反射の向きが傾きの 2 倍ぶれ、
    // 横へは視線と水面のなす角 γ の sin 倍に縮む
    const float excludedMeanSquareSlope = EvaluateExcludedMeanSquareSlope(waterPos.xz, surfaceFootprintMeters);
    const float skyRoughness = AddSlopeVarianceToRoughness(kSkyEnvMicroRoughness, excludedMeanSquareSlope);
    const float verticalReflectionSpread = 2.0f * sqrt(0.5f * excludedMeanSquareSlope);
    const float horizontalReflectionSpread = verticalReflectionSpread * saturate(-primaryDir.y);

    // この水面の点がヤシや岩の影に入っているか（Water.PS がメインライトの項へ掛ける）
    gSunVisibilityOutput[launchIndex] = TraceSunVisibility(waterPos, waterNormal, gSurfaceBias);

    // 視線（primaryDir）を水面法線で鏡面反射。カメラは水面を上から見下ろすため
    // primaryDir は下向き、reflectedDir は上向き（空・水上ジオメトリ方向）になる。
    float3 reflectedDir = reflect(primaryDir, waterNormal);
    if (dot(reflectedDir, reflectedDir) <= 1.0e-6f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidBounceVector);
        return;
    }
    // 下を向いた反射は隣の波でもう一度はね返るとみなし、水平面で折り返して上へ向ける
    reflectedDir.y = max(abs(reflectedDir.y), 0.01f);

    RayDesc ray;
    // 反射レイは空気側（+waterNormal）へ進むため、自己交差回避のバイアスも +waterNormal。
    ray.Origin = waterPos + waterNormal * gSurfaceBias;
    ray.Direction = normalize(reflectedDir);
    ray.TMin = 0.001f;
    ray.TMax = gMaxRayDistance;

    RTReflectionPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
    payload.instanceIndex = 0;
    payload.primitiveIndex = 0;
    payload.barycentrics = float2(0.0f, 0.0f);

    // 反射は最も近い交差面の色が必要なため最近接ヒット（RAY_FLAG_NONE）。
    TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    if (payload.hitFlag < 0.5f)
    {
        // 反射レイが何にも当たらず空へ抜けた。トレースした向きそのもので空を引く
        gReflectionOutput[launchIndex] = MakeSkyResolvedOutput(ray.Direction, kRTReasonTraceMiss, skyRoughness);
        return;
    }

    // 反射レイの広がり。始点の幅は水面の 1 画素ぶん、角度は視線の 1 画素ぶんと荒さのばらつき
    const float coneWidthAtOrigin = pixelAngle * tRefined;
    const float coneSpread = pixelAngle + verticalReflectionSpread;
    const bool hitShadingEnabled = (gHitShadingEnabled != 0);

    float3 hitWorldPos = ray.Origin + ray.Direction * payload.hitT;
    float4 clip = mul(float4(hitWorldPos, 1.0f), gViewProjection);
    float3 ndc = float3(0.0f, 0.0f, 0.0f);
    float2 uv = float2(0.0f, 0.0f);
    float edgeFade = 0.0f;
    if (clip.w > 1.0e-5f)
    {
        ndc = clip.xyz / clip.w;
        uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
        edgeFade = ComputeRTScreenBoundsFade(uv, float2(gScreenWidth, gScreenHeight));
    }
    if (edgeFade <= 1.0e-4f)
    {
        // 当たった点が画面に写っていない（画面の外・カメラの後ろ）ときは、当たった点を照らして色を決める
        if (hitShadingEnabled)
        {
            const float4 shaded = ShadeReflectionHit(ray, payload, coneWidthAtOrigin, coneSpread);
            if (shaded.a > 0.5f)
            {
                gReflectionOutput[launchIndex] = float4(shaded.rgb, MakeSuccessAlpha(1.0f));
                return;
            }
        }
        gReflectionOutput[launchIndex] = MakeSkyResolvedOutput(ray.Direction, kRTReasonInvalidClip, skyRoughness);
        return;
    }

    float2 clampedUV = saturate(uv);
    float2 reflectedUV = clampedUV;
    if (gMaxReflectionOffsetPixels > 0.0f)
    {
        float2 maxOffset = max(gMaxReflectionOffsetPixels, 0.0f) / float2(gScreenWidth, gScreenHeight);
        float2 uvOffset = clamp(clampedUV - screenUV, -maxOffset, maxOffset);
        reflectedUV = saturate(screenUV + uvOffset);
    }

    uint2 sampleCoord = uint2(reflectedUV * float2(gScreenWidth, gScreenHeight));
    sampleCoord = min(sampleCoord, uint2(gScreenWidth - 1.0f, gScreenHeight - 1.0f));

    // 同じレイ向きで引いた空（当たった点を照らせないときと、ぼかした範囲の空の分に使う）
    const bool hasSkyCube = (gSkyEnvReflectionEnabled >= 0.5f);
    const float3 colorAtTarget = gSceneColor.SampleLevel(gLinearClamp, reflectedUV, 0.0f).rgb;
    const float3 skyAlongRay = hasSkyCube ? SampleSkyEnvironment(ray.Direction, skyRoughness) : colorAtTarget;

    // スクリーン空間オクルージョン判定（SSR の要）。反射ヒット点の深度が、
    // 再投影先ピクセルの SceneDepth と食い違う場合、その反射点は画面上で
    // 別の物体に隠れており色を取得できない → フォールバック。
    const float hitViewDistance = length(hitWorldPos - gCameraPosition);
    const float depthMismatchThreshold = max(0.08f, hitViewDistance * 0.03f);

    // 投影先の画素に当たった物が写っている度合い。背景（空）なら 0、
    // 奥行きが食い違うほどなめらかに下げる
    float mismatchConfidence = 0.0f;
    const float sampledDepth = gSceneDepth.Load(int3(sampleCoord, 0));
    if (!IsBackgroundDepth(sampledDepth))
    {
        const float3 sampledWorldPos =
            ReconstructWorldPosition(ScreenUVToNDC(reflectedUV), sampledDepth, gInvViewProjection);
        const float depthMismatch = abs(length(sampledWorldPos - gCameraPosition) - hitViewDistance);
        mismatchConfidence =
            1.0f - smoothstep(depthMismatchThreshold, depthMismatchThreshold * 4.0f, depthMismatch);
    }

    // 投影先に当たった物が写っていないとき（画素に満たない細い葉・裏向きで描かれない面）は、
    // まわりの画素から同じ奥行きの物を探してその色を使う
    float3 hitColor = colorAtTarget;
    float hitConfidence = mismatchConfidence;
    if (mismatchConfidence < 1.0f)
    {
        float3 nearbyColor;
        const float nearbyConfidence = FindHitColorNearby(
            reflectedUV, hitViewDistance, depthMismatchThreshold * 4.0f, nearbyColor);
        if (nearbyConfidence > 0.0f)
        {
            hitColor = lerp(nearbyColor, colorAtTarget, mismatchConfidence);
            hitConfidence = max(mismatchConfidence, nearbyConfidence);
        }
    }

    // 画面から当たった物の色が取れない分（画面端・遮蔽）は、当たった点を照らした色で埋める。
    // 照らせないときは同じレイ向きの空で埋める
    const float sceneWeight = saturate(edgeFade * hitConfidence);
    float3 tracedColor = skyAlongRay;
    if (hitShadingEnabled && sceneWeight < 0.999f)
    {
        const float4 shaded = ShadeReflectionHit(ray, payload, coneWidthAtOrigin, coneSpread);
        tracedColor = (shaded.a > 0.5f) ? shaded.rgb : skyAlongRay;
    }
    float3 resolvedColor = lerp(tracedColor, hitColor, sceneWeight);

    // 反射の向きのばらつきが 1 画素を超えるときは、ばらつきが当たった物のまわりで覆う範囲に
    // 写っている物の色と割合で置き換え、範囲の残りを同じレイ向きの空（荒さに合ったミップ）で埋める
    const float hitPixelMeters = ComputePixelPerpendicularWidth(
        reflectedUV, saturate(ndc.z), float2(gScreenWidth, gScreenHeight), gInvViewProjection);
    const float pixelsPerRadian = payload.hitT / max(hitPixelMeters, 1.0e-4f);
    const float verticalSigmaPixels = verticalReflectionSpread * pixelsPerRadian;
    const uint pyramidLevels = GetReflectionPyramidLevels();
    // 広がり 0.5〜1 画素の間でぼかさない色からなめらかにつなぐ
    const float blurBlend = saturate(log2(max(2.0f * verticalSigmaPixels, 1.0f)));
    if (blurBlend > 0.0f && pyramidLevels > 1)
    {
        const float4 geometry = SampleReflectedGeometryStreak(
            reflectedUV, verticalSigmaPixels, horizontalReflectionSpread * pixelsPerRadian, pyramidLevels);
        if (geometry.a >= 0.0f)
        {
            const float3 screenBlurredColor = lerp(skyAlongRay, geometry.rgb, geometry.a);
            const float3 blurredColor = lerp(tracedColor, screenBlurredColor, sceneWeight);
            resolvedColor = lerp(resolvedColor, blurredColor, blurBlend);
        }
    }

    // Water.PS へは常に「解決済みの 1 枚」を渡す（alpha は成功固定）。
    gReflectionOutput[launchIndex] = float4(resolvedColor, MakeSuccessAlpha(1.0f));
}

[shader("miss")]
void RTWaterReflectionMiss(inout RTReflectionPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterReflectionClosestHit(
    inout RTReflectionPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
    payload.instanceIndex = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.barycentrics = attr.barycentrics;
}
