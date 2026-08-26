// ============================================================
// DXR 水面屈折シェーダー
// フラットな水面平面を近似し、屈折レイの最初のヒット位置を SceneColor に再投影して
// 水面屈折用のカラー出力を生成する。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"
// 出力アルファのエンコード規約（読み手の Water.PS.hlsl と共有）
#include "../Common/WaterRefractionEncoding.hlsli"

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
    float gAbsorptionCoeff;
    float gScreenWidth;
    float gScreenHeight;
    float gMaxRefractionOffsetPixels;
    // 旧 FFT 有効情報 3 スロット。実体は b1（RTWaterSurfaceCommon.hlsli）へ一本化済み。
    uint gFFTOceanPad1;
    float gFFTOceanPad0;
    uint gFFTOceanPad2;
    float gDebugDisplayScale;
    uint gDebugViewMode;
};

static const uint kRTRefractionDebugNone = 0;
static const uint kRTRefractionDebugUVOffsetPixels = 1;
static const uint kRTRefractionDebugDepthMismatch = 2;
static const uint kRTRefractionDebugWaterNormal = 3;
static const uint kRTRefractionDebugRefractedDirection = 4;

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
    inout RTWaterPayload payload);
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
    const float surfaceFootprintMeters = ProjectFootprintOntoSurface(
        ComputePixelPerpendicularWidth(
            screenUV, ndcDepth, float2(gScreenWidth, gScreenHeight), gInvViewProjection)
            * (tRefined / sceneDistance),
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

    RTWaterPayload payload;
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

    // 屈折レイが実際に水中を進んだ距離 = 真の光路長（水柱厚さ）。
    // ここから先の失敗はすべて「スクリーン空間から色を拾えない」というだけで、
    // この光路長は常に正しい。以降のフォールバックでも必ず伝搬させること
    // （捨てると Water.PS.hlsl が別の推定量へ切り替わり、境界が線として見える）。
    const float opticalPathLength = length(hitWorldPos - ray.Origin);

    float4 clip = mul(float4(hitWorldPos, 1.0f), gViewProjection);
    if (clip.w <= 1.0e-5f)
    {
        gRefractionOutput[launchIndex] = MakeColorFallbackWithPath(fallbackSample.rgb, opticalPathLength);
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;

    // スクリーン空間で SceneColor を再利用する都合上、屈折先が画面外に出た場合は
    // その位置の色を物理的に取得できない（この手法の原理的な制約）。
    // ただし画面端に近いだけの有効な屈折まで減衰させると、特定方向だけ
    // 不自然に途切れて見える。そこでフェードは「画面外へ実際にはみ出した距離」に対してのみ
    // 適用し、画面内に留まっている屈折は上下左右で等しく保持する。
    float edgeFade = ComputeRTScreenBoundsFade(uv, float2(gScreenWidth, gScreenHeight));

    if (edgeFade <= 1.0e-4f)
    {
        gRefractionOutput[launchIndex] = MakeColorFallbackWithPath(fallbackSample.rgb, opticalPathLength);
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

    // ===== 深度不一致は「2値の棄却」ではなく「連続の信頼度」で扱う =====
    // ★波打ち際の細い暗線の真因★（2026-07-26 実測で特定）
    // 旧実装は depthMismatch > 閾値 で即フォールバックしていた。フォールバック色は
    // 「屈折させていない自分のピクセルのシーン色」で、成功側は「屈折先のシーン色」。
    // この 2 つは屈折オフセット（岸際では十数ピクセル）分だけ別の場所の色なので、
    // 成功/失敗が入れ替わる 1 ピクセル境界がそのまま色の段差になる。
    // 岸際は浅い水底を屈折レイが大きく横へ進み、再投影が自己遮蔽で外れるため
    // 失敗が帯状に発生する（可視化で確認済み）→ 帯の縁が波打ち際に沿った細い暗線になる。
    //
    // 光路長のときと同じ処方: 2値切替を消して連続量にする。閾値の 1〜4 倍で
    // なだらかにフォールバック色へ寄せれば、段差そのものが原理的に生じない。
    const float depthConfidence =
        1.0f - smoothstep(depthMismatchThreshold, depthMismatchThreshold * 4.0f, depthMismatch);

    // 画面端フェードと合成した「屈折色をどれだけ信用するか」の重み
    const float colorWeight = saturate(edgeFade * depthConfidence);

    if (gDebugViewMode != kRTRefractionDebugNone)
    {
        const float3 debugColor = BuildRefractionDebugColor(
            gDebugViewMode,
            uvOffsetPixels,
            depthMismatch,
            waterNormal,
            refractedDir);
        gRefractionOutput[launchIndex] = float4(
            debugColor,
            EncodeHitAlpha(opticalPathLength, colorWeight >= 0.5f));
        return;
    }

    const float3 refractedColor = gSceneColor.Load(int3(sampleCoord, 0)).rgb;
    const float3 blendedColor = lerp(fallbackSample.rgb, refractedColor, colorWeight);

    // アルファの colorValid ビットは診断用（デバッグ表示・統計）に残すが、
    // rgb は既に連続ブレンド済みなので Water.PS.hlsl はこのビットで
    // 色を切り替えてはいけない（切り替えると段差が復活する）。
    gRefractionOutput[launchIndex] = float4(blendedColor, EncodeHitAlpha(opticalPathLength, colorWeight >= 0.5f));
}

[shader("miss")]
void RTWaterRefractionMiss(inout RTWaterPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterRefractionClosestHit(
    inout RTWaterPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
}
