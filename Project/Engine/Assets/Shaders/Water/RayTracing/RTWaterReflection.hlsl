// ============================================================
// DXR 水面反射シェーダー
// フラットな水面平面（+FFT波）を近似し、反射レイの最初のヒット位置を
// SceneColor に再投影して水面反射用のカラー出力を生成する。
// RTWaterRefraction.hlsl の対称形（refract→reflect、バイアス方向反転）。
// ★空もこのパスの中で解決する（2026-08-09 変更）★
// 以前はミス／画面外／深度不一致で alpha < 0.5 を返し、Water.PS 側が
// 空環境マップへ落としていた。ところが Water.PS のフォールバックは反射方向を
// **完全な平面法線 float3(0,1,0)** で計算していたため、
//   ・RT 反射 … 波法線に沿って動く像
//   ・空フォールバック … 波に追従しない動かない像
// という幾何的に食い違う 2 枚を lerp する構造になっていた。かすめ角では
// 成否がピクセル単位で入り混じるため、水面際で「反射が二重に重なって見える」
// 現象になっていた（実測で確定）。
// 対策 = 空もこのシェーダーが**実際にトレースしたレイの向き**で引き、
// Water.PS へは常に 1 枚だけ渡す。これで面が 1 つに揃う。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"

RWTexture2D<float4> gReflectionOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gSceneColor : register(t2);
Texture2DArray<float4> gFFTOceanDisplacement : register(t3);
Texture2DArray<float4> gFFTOceanNormal : register(t4);
TextureCube<float4> gSkyEnvironmentMap : register(t5);

// DXR グローバルルートシグネチャの静的サンプラ（GlobalRootSignatureManager）。
// Texture2D 系は Load による手動バイリニアなのでサンプラ不要だが、
// TextureCube は SampleLevel が要るためこれを使う。
SamplerState gLinearClamp : register(s0);

cbuffer WaterReflectionConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInvViewProjection; // 深度復元用
    float3 gCameraPosition;
    float gWaterHeight;
    float gSurfaceBias;
    float gMaxRayDistance;
    float gSkyEnvReflectionEnabled; // 1 = gSkyEnvironmentMap が有効（旧 gUnused0 のスロットを転用）
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

float3 SampleSkyEnvironment(float3 dir)
{
    const float mip = kSkyEnvMicroRoughness * (kSkyEnvMipCount - 1.0f);
    return gSkyEnvironmentMap.SampleLevel(gLinearClamp, dir, mip).rgb;
}

/// @brief トレースしたレイの向きで空を引いた「解決済みの反射色」を返す。
/// @details 空キューブが無いフレームだけ理由コード（alpha<0.5）へ落とす。
///          その場合のみ Water.PS の保険フォールバックが動く。
float4 MakeSkyResolvedOutput(float3 rayDir, float reasonCodeIfNoSky)
{
    if (gSkyEnvReflectionEnabled < 0.5f)
    {
        return MakeFallbackOutput(reasonCodeIfNoSky);
    }
    return float4(SampleSkyEnvironment(rayDir), MakeSuccessAlpha(1.0f));
}

[shader("raygeneration")]
void RTWaterReflectionRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float ndcDepth = gSceneDepth.Load(int3(launchIndex, 0));
    float2 screenUV = (float2(launchIndex) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);

    // ★背景（水面の後ろに不透明ジオメトリが無い＝外洋）でも反射を計算する★
    // 以前はここで早期 return していたため、外洋の水面だけ RT 反射が一切効かず
    // Water.PS の「平面法線で引いた空」しか出ていなかった。岸際ではこの領域と
    // ジオメトリのある領域が波で細かく入り混じるので、波に沿って動く像と
    // 動かない像が重なって「反射が二重」に見えていた。
    // 追加コストは水面の画素だけに限られる: 地平線より上を向く画素は
    // 下の tRefined 判定が TraceRay の前に弾く。
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

    // 1 ピクセルが水面交点で覆う幅。垂直断面幅を水面までの距離へ比例縮小し、
    // 平坦水面（+Y）へ投影する。波法線のカスケード縮小フィルタに渡すと、
    // 隣接ピクセルの反射方向が滑らかにつながり、再投影先の UV が飛ばなくなる。
    const float surfaceFootprintMeters = ProjectFootprintOntoSurface(
        ComputePixelPerpendicularWidth(
            screenUV,
            hasBackground ? 0.5f : ndcDepth,
            float2(gScreenWidth, gScreenHeight),
            gInvViewProjection) * (tRefined / pixelRayLength),
        primaryDir,
        float3(0.0f, 1.0f, 0.0f));

    // 反射レイの向きは「うねりスケールの法線」だけで決める。
    // 反射は再投影のてこ（レイ長）が長く、フットプリント内で解像できている
    // 細かなさざ波の傾きでも、隣接ピクセル間で再投影先が別の物体へ飛ぶ。
    // 解像できない微細斜面は方向ジッタではなくグロッシーぼかし
    // （Water.PS の SampleGlossyReflectionRGBA）が受け持つ。
    // 1.0m で最細カスケードが消え、中間カスケードは減衰して通る。
    const float kReflectionDirectionMinFootprint = 1.0f;
    const float directionFootprint = max(surfaceFootprintMeters, kReflectionDirectionMinFootprint);

    float3 waterNormal = EvaluateWaterNormal(gFFTOceanNormal, waterPos.xz, directionFootprint);
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
        // 反射レイが何にも当たらず空へ抜けた。
        // ★トレースした向きそのもので空を引く（物理的にこれが正しい反射先）★
        gReflectionOutput[launchIndex] = MakeSkyResolvedOutput(ray.Direction, kRTReasonTraceMiss);
        return;
    }

    float3 hitWorldPos = ray.Origin + ray.Direction * payload.hitT;
    float4 clip = mul(float4(hitWorldPos, 1.0f), gViewProjection);
    if (clip.w <= 1.0e-5f)
    {
        gReflectionOutput[launchIndex] = MakeSkyResolvedOutput(ray.Direction, kRTReasonInvalidClip);
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;

    // 画面端フェード。ヒット点の色が取れない側の端点も「同じレイ向きの空」なので、
    // ここで混ぜても幾何的な食い違いは起きない（旧実装は Water.PS 側で平面法線の
    // 空と混ぜていたため二重像になっていた）。
    float edgeFade = ComputeRTScreenBoundsFade(uv, float2(gScreenWidth, gScreenHeight));
    if (edgeFade <= 1.0e-4f)
    {
        gReflectionOutput[launchIndex] = MakeSkyResolvedOutput(ray.Direction, kRTReasonInvalidClip);
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
    // 同じレイ向きで引いた空。以降のフォールバック／混合の端点は必ずこれを使う。
    const bool hasSkyCube = (gSkyEnvReflectionEnabled >= 0.5f);
    const float3 sceneColorAtTarget = gSceneColor.Load(int3(sampleCoord, 0)).rgb;
    const float3 skyAlongRay = hasSkyCube ? SampleSkyEnvironment(ray.Direction) : sceneColorAtTarget;

    float sampledDepth = gSceneDepth.Load(int3(sampleCoord, 0));
    if (IsBackgroundDepth(sampledDepth))
    {
        // 再投影先が空（背景）＝反射先は空。SceneColor の空は雲・太陽まで
        // 描かれているのでキューブより情報が多い。画面端だけキューブへ寄せる。
        gReflectionOutput[launchIndex] =
            float4(lerp(skyAlongRay, sceneColorAtTarget, edgeFade), MakeSuccessAlpha(1.0f));
        return;
    }

    float3 sampledWorldPos = ReconstructWorldPosition(ScreenUVToNDC(reflectedUV), sampledDepth, gInvViewProjection);
    float sampledViewDistance = length(sampledWorldPos - gCameraPosition);
    float hitViewDistance = length(hitWorldPos - gCameraPosition);
    float depthMismatch = abs(sampledViewDistance - hitViewDistance);
    float depthMismatchThreshold = max(0.08f, hitViewDistance * 0.03f);

    // ★2 値棄却をやめて信頼度へ★
    // 旧実装は depthMismatch > 閾値 でハード棄却しており、かすめ角では
    // 成否がピクセル単位で反転してギザギザ・二重像の原因になっていた
    // （水面まわりの「線」が毎回 2 値切替から出ていたのと同じ構図）。
    const float mismatchConfidence =
        1.0f - smoothstep(depthMismatchThreshold, depthMismatchThreshold * 4.0f, depthMismatch);

    // 端点はどちらも「反射レイの向きの色」なので混ぜても面が食い違わない。
    const float sceneWeight = saturate(edgeFade * mismatchConfidence);
    const float3 resolvedColor = lerp(skyAlongRay, sceneColorAtTarget, sceneWeight);

    // Water.PS へは常に「解決済みの 1 枚」を渡す（alpha は成功固定）。
    gReflectionOutput[launchIndex] = float4(resolvedColor, MakeSuccessAlpha(1.0f));
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
