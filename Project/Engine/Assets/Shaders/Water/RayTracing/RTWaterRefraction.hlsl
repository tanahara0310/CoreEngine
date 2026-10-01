// ============================================================
// DXR 水面屈折シェーダー
// フラットな水面平面を近似し、屈折レイの最初のヒット位置を SceneColor に再投影して
// 水面屈折用のカラー出力を生成する。ヒット位置が画面に写っていない（水上の物に隠れている・
// 画面の外・画面に写る別の面の陰）ときは、当たった点を材質と光で照らして色を決める。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"
#include "../../Include/RayTracing/RTHitShading.hlsli"
// 出力アルファのエンコード規約（読み手の Water.PS.hlsl と共有）
#include "../Common/WaterRefractionEncoding.hlsli"
// 水面を通って水中の点へ届く日光（RTWaterCaustics と共有）
#include "RTWaterCausticsCommon.hlsli"

RWTexture2D<float4> gRefractionOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1); // WorldPosition ターゲット廃止に伴い深度から復元する
Texture2D<float4> gSceneColor : register(t2);
Texture2DArray<float4> gFFTOceanDisplacement : register(t3);
Texture2DArray<float4> gFFTOceanNormal : register(t4);

cbuffer WaterRefractionConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInvViewProjection; // WorldPosition ターゲット廃止に伴う深度復元用
    float3 gCameraPosition;
    float gWaterHeight;
    float gSurfaceBias;
    float gMaxRayDistance;
    float gRefractionEta;
    float gScreenWidth;
    float gScreenHeight;
    float gMaxRefractionOffsetPixels;
    float gDebugDisplayScale;
    uint gDebugViewMode;
    // 水中の点を照らす値（DeferredLighting の水中ライティング・RT コースティクスと同じ値）
    float3 gUnderwaterAbsorption;     // 吸収係数 σa [1/m]
    uint gUnderwaterLightingEnabled;  // 1 = 水中の点のメインライトを水面を通った日光で照らす
    float gCausticsIntensityScale;    // RT コースティクスの強さの倍率
    float3 gRefractionPad;
};

static const uint kRTRefractionDebugNone = 0;
static const uint kRTRefractionDebugUVOffsetPixels = 1;
static const uint kRTRefractionDebugDepthMismatch = 2;
static const uint kRTRefractionDebugWaterNormal = 3;
static const uint kRTRefractionDebugRefractedDirection = 4;

/// @brief 屈折パスのペイロード（屈折レイと影のレイで共通）
struct RTRefractionPayload
{
    float hitT;
    float hitFlag;        // 1 = 当たった / 0 = 抜けた
    uint instanceIndex;   // InstanceID()（ヒットシェーディングの表の行）
    uint primitiveIndex;  // PrimitiveIndex()
    float2 barycentrics;  // 交点の重心座標
};

#ifdef __INTELLISENSE__
#define RAY_FLAG_NONE 0x0
#define RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH 0x4
#define RAY_FLAG_SKIP_CLOSEST_HIT_SHADER 0x8
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout RTRefractionPayload payload);
#endif

// ペイロード・失敗理由コード・画面端フェード・波面評価は RTWaterSurfaceCommon.hlsli（共通）。
// アルファのエンコード規約（kRTSuccessRangeMin / kRTMaxOpticalPathMeters /
// kRTColorInvalidOffset / EncodeHitAlpha）は Common/WaterRefractionEncoding.hlsli が
// 唯一の情報源。読み手の Water.PS.hlsl も同じヘッダーを include する。
// 失敗理由コード（1〜9/255 = [0, 0.5) の範囲）は成功レンジと衝突しない。

float4 MakeFallbackOutput(float3 fallbackColor, float reasonCode)
{
    return float4(fallbackColor, reasonCode);
}

/// @brief ヒットはしたが色が取れなかった場合の出力（光路長は必ず伝える）
float4 MakeColorFallbackWithPath(float3 fallbackColor, float opticalPathLength)
{
    return float4(fallbackColor, EncodeHitAlpha(opticalPathLength, false));
}

/// @brief この高さ [m] までの面は水中の面として扱う（波込みの水面との誤差の分）
static const float kUnderwaterMarginMeters = 0.02f;
/// @brief アルファで抜ける所に当たったときに、その先へ撃ち直す回数の上限
static const uint kMaxCutoutRetrace = 3;

/// @brief 何にも当たっていない状態のペイロード
RTRefractionPayload MakeEmptyRefractionPayload()
{
    RTRefractionPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
    payload.instanceIndex = 0;
    payload.primitiveIndex = 0;
    payload.barycentrics = float2(0.0f, 0.0f);
    return payload;
}

/// @brief 点から向きへ遮る物が無いかを返す（1 = 無い / 0 = 遮られる）
float TraceVisibility(float3 origin, float3 direction, float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001f;
    ray.TMax = maxDistance;

    // 当たりはヒットシェーダーを通さずに終えるので、当たった扱いで初期化してミスだけが 0 に戻す
    RTRefractionPayload payload = MakeEmptyRefractionPayload();
    payload.hitFlag = 1.0f;
    TraceRay(
        gScene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF, 0, 1, 0, ray, payload);
    return 1.0f - payload.hitFlag;
}

/// @brief メインライトで水中の点を照らすコースティクスの値（RT コースティクスのパスと同じ値）
WaterCausticsParams MakeRefractionCausticsParams(DirectionalLightData mainLight)
{
    WaterCausticsParams params;
    params.lightDirection = mainLight.direction;
    params.lightColor = mainLight.color.rgb;
    params.lightIntensity = mainLight.intensity;
    params.intensityScale = gCausticsIntensityScale;
    params.refractiveIndex = 1.0f / gRefractionEta;
    params.absorption = gUnderwaterAbsorption;
    params.surfaceBias = gSurfaceBias;
    params.maxTraceDistance = gMaxRayDistance;
    params.regionCenterXZ = gSurfaceRegionCenterXZ;
    params.regionHalfExtentXZ = gSurfaceRegionHalfExtentXZ;
    params.regionValid = gSurfaceRegionValid;
    return params;
}

/// @brief 水中の点へ屈折して届く日光が、平らな水面へ入る点を求める（RTShadow.hlsl の TryFindWaterEntryPoint と同じ）
/// @param toLight 点から光源へ向かう方向（空気中）
/// @return 点が水域の水面より下にあり、光が水面の上から来るとき true
bool TryFindSunEntryPoint(float3 position, float3 toLight, out float3 entryPoint)
{
    entryPoint = position;
    if (toLight.y <= 0.0f || position.y >= gSurfaceWaterHeight)
    {
        return false;
    }
    if (gSurfaceRegionValid != 0 && any(abs(position.xz - gSurfaceRegionCenterXZ) > gSurfaceRegionHalfExtentXZ))
    {
        return false;
    }

    // スネルの法則で水中の光の向きを求める（水平成分が 1/n 倍になる）
    const float2 horizontal = toLight.xz * gRefractionEta;
    const float vertical = sqrt(saturate(1.0f - dot(horizontal, horizontal)));
    const float travel = (gSurfaceWaterHeight - position.y) / max(vertical, 1.0e-4f);
    entryPoint = float3(
        position.x + horizontal.x * travel,
        gSurfaceWaterHeight,
        position.z + horizontal.y * travel);
    return true;
}

/// @brief 屈折レイが当たった点を照らす光を組み立てる（DeferredLighting の水中ライティングと同じ扱い）
/// @param surface             当たった点の面
/// @param footprintMeters     当たった点で屈折レイが面上に覆う幅 [m]（コースティクスの集光率の差分幅）
/// @param mainLightVisibility メインライトの日向率。水中の点は、屈折した日光が水面へ入る点から光源が見えるか
RTHitUnderwaterLighting BuildRefractionHitLighting(
    RTHitSurface surface, float footprintMeters, out float mainLightVisibility)
{
    RTHitUnderwaterLighting lighting = AboveWaterLighting();
    mainLightVisibility = 1.0f;

    if (gDirectionalLightCount == 0)
    {
        return lighting;
    }
    StructuredBuffer<DirectionalLightData> lights = ResourceDescriptorHeap[gDirectionalLightsIndex];
    const DirectionalLightData mainLight = lights[0];
    if (!mainLight.enabled)
    {
        return lighting;
    }

    // 水に覆われている割合と、水面を通って届く日光（RT コースティクスと同じ計算）
    if (gUnderwaterLightingEnabled != 0)
    {
        const WaterCausticsParams params = MakeRefractionCausticsParams(mainLight);
        const WaterCausticsPath path = BeginWaterCaustics(
            params, surface.position, surface.normal, footprintMeters, gFFTOceanDisplacement, gFFTOceanNormal);
        lighting.factor = saturate(path.coverage);
        if (lighting.factor > 0.0f)
        {
            // 空の光と補助ライトは平らな水面からの深さで弱める
            lighting.ambientTransmittance = lerp(
                float3(1.0f, 1.0f, 1.0f),
                exp(-gUnderwaterAbsorption * max(gSurfaceWaterHeight - surface.position.y, 0.0f)),
                lighting.factor);
            lighting.transmittedMainLight = path.baselineRadiance;
            if (path.traceRequired)
            {
                // 日光が水面へ入る点から当たった点まで届くか（水中の区間の遮蔽）と集光率
                RTRefractionPayload payload = MakeEmptyRefractionPayload();
                TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, path.ray, payload);
                lighting.transmittedMainLight = FinishWaterCaustics(
                    path, params, surface.position, surface.normal, footprintMeters,
                    payload.hitFlag >= 0.5f, payload.hitT,
                    gFFTOceanDisplacement, gFFTOceanNormal).radiance;
            }
        }
    }

    // メインライトの影。水中の点は、日光が平らな水面へ入る点から光源へ向けて水より上の遮蔽だけを調べる
    const float3 toLight = -normalize(mainLight.direction);
    if (toLight.y > 0.0f)
    {
        float3 shadowOrigin;
        if (!TryFindSunEntryPoint(surface.position, toLight, shadowOrigin))
        {
            shadowOrigin = surface.position + surface.normal * max(gSurfaceBias, 0.02f);
        }
        mainLightVisibility = TraceVisibility(shadowOrigin, toLight, gMaxRayDistance);
    }
    return lighting;
}

/// @brief 屈折レイが当たった物を、当たった点の材質と光で照らした色を返す
/// @param ray               屈折レイ
/// @param payload           屈折レイの結果（当たっていること）
/// @param coneWidthAtOrigin レイの始点での広がりの幅（m）
/// @param coneSpread        レイの広がりの角度（rad。距離に比例して幅が増える）
/// @return rgb = 照らした色、a = 1（抜ける材質の先で何にも当たらなかったときは 0）
/// @details 抜ける材質（アルファ）に当たったときは、その先へ撃ち直す
float4 ShadeRefractionHit(RayDesc ray, RTRefractionPayload payload, float coneWidthAtOrigin, float coneSpread)
{
    [loop]
    for (uint attempt = 0; attempt <= kMaxCutoutRetrace; ++attempt)
    {
        const float coneWidth = coneWidthAtOrigin + coneSpread * payload.hitT;
        const RTHitSurface surface = FetchHitSurface(
            payload.instanceIndex, payload.primitiveIndex, payload.barycentrics, ray.Direction, coneWidth);
        if (!surface.cutout)
        {
            const float footprintMeters = ProjectFootprintOntoSurface(coneWidth, ray.Direction, surface.normal);
            float mainLightVisibility;
            const RTHitUnderwaterLighting lighting =
                BuildRefractionHitLighting(surface, footprintMeters, mainLightVisibility);
            // 見る向きは画面に写る点と同じくカメラからの直線（屈折レイの向きではない）
            const float3 toCamera = normalize(gCameraPosition - surface.position);
            return float4(ShadeHitSurface(surface, toCamera, mainLightVisibility, lighting), 1.0f);
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

float3 EncodeSignedVector(float3 vectorValue)
{
    return normalize(vectorValue) * 0.5f + 0.5f;
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
        return VisualizeRTScalar(uvOffsetPixels, gDebugDisplayScale);
    }

    if (debugViewMode == kRTRefractionDebugDepthMismatch)
    {
        return VisualizeRTScalar(depthMismatch, gDebugDisplayScale);
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

[shader("raygeneration")]
void RTWaterRefractionRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float4 fallbackSample = gSceneColor.Load(int3(launchIndex, 0));
    float ndcDepth = gSceneDepth.Load(int3(launchIndex, 0));

    if (IsBackgroundDepth(ndcDepth))
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonBackground);
        return;
    }

    float2 screenUV = (float2(launchIndex) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);
    float3 worldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gInvViewProjection);

    float3 cameraToScene = worldPos - gCameraPosition;
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

    // フラット平面シード→波反映の固定点反復（詳細は RefineWaterSurfaceIntersection）。
    // シード段階の 0 < t < sceneDistance 判定は行わず、精密化後に判定し直す
    // （「カメラを近づけるとカメラ下側の屈折が消える」既知バグの恒久対策）。
    float3 waterPos;
    if (!RefineWaterSurfaceIntersection(
            gFFTOceanDisplacement, gCameraPosition, primaryDir, sceneDistance, waterPos))
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidPlaneIntersection);
        return;
    }

    // 波を反映した精密化後の実際の交点までの距離で改めて有効性を判定する。
    const float tRefined = dot(waterPos - gCameraPosition, primaryDir);
    if (tRefined <= 1.0e-4f || tRefined >= sceneDistance)
    {
        // ★波打ち際の細い暗線の真因★（2026-07-26 実測で特定）
        // tRefined >= sceneDistance は「水面交点が不透明面より奥（＝そこに水は無い）」を意味し、
        // 波打ち際ではまさに水柱ゼロの汀線そのものに当たる。ここを reason コード付きの
        // 「光路長も無効」なフォールバックにすると、Water.PS.hlsl が RT 実測光路長から
        // スクリーン空間近似へ 1 ピクセル境界で切り替わり、水柱厚さが段差になって
        // 波打ち際に沿った細い暗線（Beer-Lambert で赤から落ちるので紺色）が出る。
        // 波の上下でピクセル単位に判定が反転するため、破線状に見えるのも説明がつく。
        //
        // 正しい答えは「無効」ではなく「水柱ゼロ」。光路長 0 を返せば透過率 1 になり、
        // 有効側（汀線際の光路長もほぼ 0）と連続につながって段差が原理的に消える。
        // rgb は屈折なしのシーン色そのもので、これも水柱ゼロのときの正解と一致する。
        gRefractionOutput[launchIndex] = MakeColorFallbackWithPath(fallbackSample.rgb, 0.0f);
        return;
    }

    // 1 ピクセルが水面交点で覆う幅。垂直断面幅を水面までの距離へ比例縮小し、
    // 平坦水面（+Y）へ投影する。波法線のカスケード縮小フィルタに渡す。
    const float pixelPerpendicularWidth = ComputePixelPerpendicularWidth(
        screenUV, ndcDepth, float2(gScreenWidth, gScreenHeight), gInvViewProjection);
    const float surfaceFootprintMeters = ProjectFootprintOntoSurface(
        pixelPerpendicularWidth * (tRefined / sceneDistance),
        primaryDir,
        float3(0.0f, 1.0f, 0.0f));

    // 屈折レイの向きは中間スケール以上の波だけで決める。
    // 最細カスケードの傾きは隣接ピクセル間で再投影先の色を飛ばし、
    // 高コントラストな海底の上でピクセル単位の色ノイズになる。
    // 0.5m で最細カスケードが消え、中間カスケードはそのまま通る。
    const float kRefractionDirectionMinFootprint = 0.5f;
    const float directionFootprint = max(surfaceFootprintMeters, kRefractionDirectionMinFootprint);

    float3 waterNormal = EvaluateWaterNormal(gFFTOceanNormal, waterPos.xz, directionFootprint);
    float3 refractedDir = refract(primaryDir, waterNormal, gRefractionEta);
    if (dot(refractedDir, refractedDir) <= 1.0e-6f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidBounceVector);
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

    RTRefractionPayload payload = MakeEmptyRefractionPayload();

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

    // 屈折レイが実際に水中を進んだ距離 = 真の光路長（水柱厚さ）。
    // ここから先の失敗はすべて「スクリーン空間から色を拾えない」というだけで、
    // この光路長は常に正しい。以降のフォールバックでも必ず伝搬させること
    // （捨てると Water.PS.hlsl が別の推定量へ切り替わり、境界が線として見える）。
    const float opticalPathLength = length(hitWorldPos - ray.Origin);

    // ヒット点を画面へ投影する。カメラの後ろは画面の外と同じに扱う
    const float4 clip = mul(float4(hitWorldPos, 1.0f), gViewProjection);
    float2 uv = screenUV;
    float edgeFade = 0.0f;
    if (clip.w > 1.0e-5f)
    {
        const float3 ndc = clip.xyz / clip.w;
        uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
        // 画面の外へはみ出した距離だけ下げる（画面内の屈折先は上下左右とも 1）
        edgeFade = ComputeRTScreenBoundsFade(uv, float2(gScreenWidth, gScreenHeight));
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

    const float sampledDepth = gSceneDepth.Load(int3(sampleCoord, 0));

    // 再投影先の可視サーフェスと実ヒット点のカメラ距離差。
    // 背景（空）が写っている場合は復元位置がファークリップ相当まで飛ぶため、
    // 特別扱いせずともこの差が巨大になり、下の信頼度が自動的に 0 へ落ちる。
    const float3 sampledWorldPos =
        ReconstructWorldPosition(ScreenUVToNDC(refractedUV), sampledDepth, gInvViewProjection);
    const float sampledViewDistance = length(sampledWorldPos - gCameraPosition);
    const float hitViewDistance = length(hitWorldPos - gCameraPosition);
    const float depthMismatch =
        IsBackgroundDepth(sampledDepth) ? 1.0e8f : abs(sampledViewDistance - hitViewDistance);
    const float depthMismatchThreshold = max(0.08f, hitViewDistance * 0.03f);

    // 再投影先に写っている面が当たった点そのものか（距離の差が閾値の 1〜4 倍でなだらかに 0 へ）
    const float depthConfidence =
        1.0f - smoothstep(depthMismatchThreshold, depthMismatchThreshold * 4.0f, depthMismatch);

    // ===== 画面に写っていない屈折先 =====
    // 次の画素では当たった点の色が画面に無いので、当たった点を材質と光で照らした色で埋める
    // （照らせないときは屈折させない色）。
    //   - 再投影先に水面より上（空気中）の面が写っている: 水上の物の陰（水面から 2〜10 cm でなだらかに）
    //   - 再投影先が画面の外
    //   - 再投影先に当たった点とは別の面が写っている: 物の水中の部分などの陰（深度の信頼度）
    const float sampledSurfaceY =
        gSurfaceWaterHeight + EvaluateDrawnSurfaceHeight(gFFTOceanDisplacement, sampledWorldPos.xz);
    const float aboveWater = IsBackgroundDepth(sampledDepth)
        ? 0.0f
        : smoothstep(kUnderwaterMarginMeters, 0.10f, sampledWorldPos.y - sampledSurfaceY);
    const float hiddenWeight = max(max(aboveWater, 1.0f - edgeFade), 1.0f - depthConfidence);

    if (gDebugViewMode != kRTRefractionDebugNone)
    {
        const float3 debugColor = BuildRefractionDebugColor(
            gDebugViewMode,
            uvOffsetPixels,
            depthMismatch,
            waterNormal,
            refractedDir);
        const float debugColorWeight = lerp(1.0f, (gHitShadingEnabled != 0) ? 1.0f : 0.0f, hiddenWeight);
        gRefractionOutput[launchIndex] = float4(
            debugColor,
            EncodeHitAlpha(opticalPathLength, debugColorWeight >= 0.5f));
        return;
    }

    // 画面に写っていない屈折先を埋める色（rgb）と、当たった点を照らせたか（a）
    float4 hiddenColor = float4(fallbackSample.rgb, 0.0f);
    if (hiddenWeight > 0.0f && gHitShadingEnabled != 0)
    {
        // 屈折レイの広がり。始点の幅は水面の 1 画素ぶん、角度は視線の 1 画素ぶん
        const float4 shaded = ShadeRefractionHit(
            ray,
            payload,
            pixelPerpendicularWidth * (tRefined / sceneDistance),
            pixelPerpendicularWidth / sceneDistance);
        if (shaded.a > 0.5f)
        {
            hiddenColor = shaded;
        }
    }

    const float3 blendedColor = lerp(gSceneColor.Load(int3(sampleCoord, 0)).rgb, hiddenColor.rgb, hiddenWeight);
    const float colorWeight = lerp(1.0f, hiddenColor.a, hiddenWeight);

    // アルファの colorValid ビットは診断用（デバッグ表示・統計）に残すが、
    // rgb は既に連続ブレンド済みなので Water.PS.hlsl はこのビットで
    // 色を切り替えてはいけない（切り替えると段差が復活する）。
    gRefractionOutput[launchIndex] = float4(blendedColor, EncodeHitAlpha(opticalPathLength, colorWeight >= 0.5f));
}

[shader("miss")]
void RTWaterRefractionMiss(inout RTRefractionPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterRefractionClosestHit(
    inout RTRefractionPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
    payload.instanceIndex = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.barycentrics = attr.barycentrics;
}
