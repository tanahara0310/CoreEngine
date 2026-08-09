// ============================================================
// DXR 水面反射シェーダー
// フラットな水面平面（+FFT波）を近似し、反射レイの最初のヒット位置を
// SceneColor に再投影して水面反射用のカラー出力を生成する。
// RTWaterRefraction.hlsl の対称形（refract→reflect、バイアス方向反転）。
// ミス／画面外／深度不一致のときはアルファ < 0.5 のフォールバック信号を出し、
// Water.PS.hlsl 側が空環境マップ（gSkyEnvironmentMap）へ落とす。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"

RWTexture2D<float4> gReflectionOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gSceneColor : register(t2);
Texture2DArray<float4> gFFTOceanDisplacement : register(t3);
Texture2DArray<float4> gFFTOceanNormal : register(t4);

cbuffer WaterReflectionConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInvViewProjection; // 深度復元用
    float3 gCameraPosition;
    float gWaterHeight;
    float gSurfaceBias;
    float gMaxRayDistance;
    float gUnused0; // （屈折の refractionEta 相当。反射では未使用。レイアウト維持）
    float gUnused1; // （屈折の absorptionCoeff 相当。反射では未使用）
    float gScreenWidth;
    float gScreenHeight;
    float gMaxReflectionOffsetPixels;
    // 旧 FFT 有効情報 3 スロット。実体は b1（RTWaterSurfaceCommon.hlsli）へ一本化済み。
    uint gFFTOceanPad1;
    float gFFTOceanPad0;
    uint gFFTOceanPad2;
    float gDebugDisplayScale;
    uint gDebugViewMode;
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
    inout RTWaterPayload payload);
#endif

// ペイロード・失敗理由コード（kRTReason*）・画面端フェード・波面評価は
// RTWaterSurfaceCommon.hlsli（3 シェーダー共通）。

// ===== 成功アルファ ＝ 反射色の「信頼度」を連続値で運ぶ =====
// ★以前は画面端フェードを色へ乗算していた（旧コメント: 「ここで色に織り込む」）★
// これは「反射情報が無い」を「黒い反射」にすり替える実装で、
// かすめ角ほど反射レイの再投影先が画面外へ出るため、水面すれすれの視点で
// 黒いギザギザの帯として現れていた（2026-08-09 修正）。
// フェードは色ではなく信頼度として渡し、Water.PS 側で空環境マップへ
// 連続ブレンドさせるのが正しい（過去の「線＝2値切替」の教訓と同じ構図）。
//   alpha ∈ (0.5, 1.0] … 成功。confidence = (alpha - 0.5) * 2
//   alpha < 0.5        … 失敗（kRTReason* の理由コード）
float MakeSuccessAlpha(float confidence)
{
    return 0.5f + 0.5f * saturate(confidence);
}

float4 MakeFallbackOutput(float reasonCode)
{
    // 失敗時 RGB は使われない（Water.PS が空環境マップで置き換える）。0 で埋める。
    return float4(0.0f, 0.0f, 0.0f, reasonCode);
}

[shader("raygeneration")]
void RTWaterReflectionRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float ndcDepth = gSceneDepth.Load(int3(launchIndex, 0));

    // 背景（空）ピクセルは水面ではないので反射不要。フォールバック信号を出す。
    if (IsBackgroundDepth(ndcDepth))
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonBackground);
        return;
    }

    float2 screenUV = (float2(launchIndex) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);
    float3 worldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gInvViewProjection);

    float3 cameraToScene = worldPos - gCameraPosition;
    float sceneDistance = length(cameraToScene);
    if (sceneDistance <= 1.0e-4f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonNearZeroSceneDistance);
        return;
    }

    float3 primaryDir = cameraToScene / sceneDistance;
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

    float3 waterNormal = EvaluateWaterNormal(gFFTOceanNormal, waterPos.xz);
    // 視線（primaryDir）を水面法線で鏡面反射。カメラは水面を上から見下ろすため
    // primaryDir は下向き、reflectedDir は上向き（空・水上ジオメトリ方向）になる。
    float3 reflectedDir = reflect(primaryDir, waterNormal);
    if (dot(reflectedDir, reflectedDir) <= 1.0e-6f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidBounceVector);
        return;
    }

    RayDesc ray;
    // 反射レイは空気側（+waterNormal）へ進むため、自己交差回避のバイアスも +waterNormal。
    ray.Origin = waterPos + waterNormal * gSurfaceBias;
    ray.Direction = normalize(reflectedDir);
    ray.TMin = 0.001f;
    ray.TMax = gMaxRayDistance;

    RTWaterPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;

    // 反射は最も近い交差面の色が必要なため最近接ヒット（RAY_FLAG_NONE）。
    TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    if (payload.hitFlag < 0.5f)
    {
        // 反射レイが何にも当たらず空へ抜けた → 空環境マップへフォールバック。
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonTraceMiss);
        return;
    }

    float3 hitWorldPos = ray.Origin + ray.Direction * payload.hitT;
    float4 clip = mul(float4(hitWorldPos, 1.0f), gViewProjection);
    if (clip.w <= 1.0e-5f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidClip);
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;

    float edgeFade = ComputeRTScreenBoundsFade(uv, float2(gScreenWidth, gScreenHeight));
    if (edgeFade <= 1.0e-4f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidClip);
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

    // スクリーン空間オクルージョン判定（SSR の要）。反射ヒット点の深度が、
    // 再投影先ピクセルの SceneDepth と食い違う場合、その反射点は画面上で
    // 別の物体に隠れており色を取得できない → フォールバック。
    float sampledDepth = gSceneDepth.Load(int3(sampleCoord, 0));
    if (IsBackgroundDepth(sampledDepth))
    {
        // 再投影先が空（背景）＝反射先は空。SceneColor の空をそのまま使える。
        float3 skyColor = gSceneColor.Load(int3(sampleCoord, 0)).rgb;
        // 色は素のまま。画面端フェードは信頼度へ（黒く沈めない）
        gReflectionOutput[launchIndex] = float4(skyColor, MakeSuccessAlpha(edgeFade));
        return;
    }

    float3 sampledWorldPos = ReconstructWorldPosition(ScreenUVToNDC(reflectedUV), sampledDepth, gInvViewProjection);
    float sampledViewDistance = length(sampledWorldPos - gCameraPosition);
    float hitViewDistance = length(hitWorldPos - gCameraPosition);
    float depthMismatch = abs(sampledViewDistance - hitViewDistance);
    float depthMismatchThreshold = max(0.08f, hitViewDistance * 0.03f);

    if (depthMismatch > depthMismatchThreshold)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonDepthMismatch);
        return;
    }

    float3 reflectedColor = gSceneColor.Load(int3(sampleCoord, 0)).rgb;
    // 色は素のまま。画面端フェードは信頼度へ（黒く沈めない）
    gReflectionOutput[launchIndex] = float4(reflectedColor, MakeSuccessAlpha(edgeFade));
}

[shader("miss")]
void RTWaterReflectionMiss(inout RTWaterPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterReflectionClosestHit(
    inout RTWaterPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
}
