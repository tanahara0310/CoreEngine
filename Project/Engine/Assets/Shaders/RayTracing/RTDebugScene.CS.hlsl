// ============================================================
// レイトレーシングのデバッグ表示：RT から見たシーン
// 画素ごとにカメラからレイを撃ち、当たった物・サブメッシュ・三角形・距離・面の向き・材質の色・
// インスタンスの印・動き・ラスタの深度との差・候補の数のどれか 1 つを書き出す。
// 追跡のときは 1 画素だけ撃ち、カメラのレイと当たった点を記録する
// （効果が撃ったレイは、記録版のシェーダが RTRayRecord.hlsli の記録先へ書く）。
// ============================================================

#include "../Include/Common/DepthReconstruction.hlsli"
#include "../Include/RayTracing/RTTrace.hlsli"

cbuffer RTDebugSceneConstants : register(b0)
{
    float4x4 gInvViewProj;      // View * Projection の逆行列（行ベクトル規約）
    float gCameraX;             // カメラの位置
    float gCameraY;
    float gCameraZ;
    uint gMode;                 // 書き出す値（RTDebugSceneMode）
    uint gWidth;                // 画面の幅
    uint gHeight;               // 画面の高さ
    uint gInstanceMask;         // カメラのレイが当てる印
    uint gAlphaTest;            // 1 ならアルファで抜ける所を通す
    uint gInstanceTableIndex;   // 材質のインスタンス表（kRTHitNoTexture = 表なし）
    uint gSubMeshTableIndex;    // 材質のサブメッシュ表
    uint gRowCount;             // 行の情報の数
    uint gTraceOnly;            // 1 なら gTraceX, gTraceY の 1 画素だけ撃って記録する
    uint gTraceX;
    uint gTraceY;
    float gConeSpread;          // 1 画素の広がりの角度（rad）
    float gPad0;
};

RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1);
StructuredBuffer<uint4> gRowInfo : register(t2); // x = 印, y = 行の印（kRowFlag*）
RWTexture2D<float4> gSceneOutput : register(u0);
RWByteAddressBuffer gDebugData : register(u1);

#define kSceneModeInstance 0
#define kSceneModeSubMesh 1
#define kSceneModeTriangle 2
#define kSceneModeDistance 3
#define kSceneModeNormal 4
#define kSceneModeBaseColor 5
#define kSceneModeMask 6
#define kSceneModeFrontFace 7
#define kSceneModeMotion 8
#define kSceneModeDepthDifference 9
#define kSceneModeCandidates 10
#define kSceneModeAlphaTests 11

// 行の印（C++ 側 RTDebugSystem の kRowFlag* と同じ）
static const uint kRowFlagAlphaTested = 1u << 0;
static const uint kRowFlagDynamicShape = 1u << 1;
static const uint kRowFlagMoving = 1u << 2;

// 書き出す場所（C++ 側 RTDebugSystem の kDataScene* / kDataTrace* と同じ）
static const uint kDataSceneTotals = 128u;
static const uint kDataTraceHeader = 192u;
static const uint kDataTraceRays = 256u;

/// @brief 片方だけ当たったときに書く距離の差
static const float kDepthDifferenceOneSided = 1000.0f;

/// @brief 撃ったレイの結果と数えた値
struct DebugTraceResult
{
    RTTraceHit hit;
    uint candidates;  // シェーダーへ来た候補の数
    uint alphaTests;  // アルファを調べた数
};

/// @brief 行の情報（行の数を超えたら 0）
uint4 LoadRowInfo(uint row)
{
    return (row < gRowCount) ? gRowInfo[row] : uint4(0, 0, 0, 0);
}

/// @brief レイを撃ち、候補とアルファを調べた数を数える
/// @param countAllCandidates true なら不透明な物も候補としてシェーダーへ来させて数える
DebugTraceResult TraceDebugRay(RTSceneTables tables, RayDesc ray, uint instanceMask, bool countAllCandidates, float coneWidthAtOrigin)
{
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    uint rayFlags = tables.valid ? RAY_FLAG_NONE : RAY_FLAG_FORCE_OPAQUE;
    if (countAllCandidates)
    {
        rayFlags = RAY_FLAG_FORCE_NON_OPAQUE;
    }
    query.TraceRayInline(gScene, rayFlags, instanceMask, ray);

    DebugTraceResult result;
    result.candidates = 0;
    result.alphaTests = 0;
    [loop]
    while (query.Proceed())
    {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            continue;
        }
        ++result.candidates;
        RTHitPrimitive candidate;
        candidate.instanceIndex = query.CandidateInstanceID();
        candidate.geometryIndex = query.CandidateGeometryIndex();
        candidate.primitiveIndex = query.CandidatePrimitiveIndex();
        candidate.barycentrics = query.CandidateTriangleBarycentrics();
        bool cutout = false;
        if (gAlphaTest != 0 && tables.valid && (LoadRowInfo(candidate.instanceIndex).y & kRowFlagAlphaTested) != 0)
        {
            ++result.alphaTests;
            const float coneWidth = coneWidthAtOrigin + gConeSpread * query.CandidateTriangleRayT();
            cutout = IsRTHitCutout(tables, candidate, ray.Direction, coneWidth);
        }
        if (!cutout)
        {
            query.CommitNonOpaqueTriangleHit();
        }
    }

    result.hit = NoRTTraceHit();
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        result.hit.hit = true;
        result.hit.hitT = query.CommittedRayT();
        result.hit.frontFace = query.CommittedTriangleFrontFace();
        result.hit.primitive.instanceIndex = query.CommittedInstanceID();
        result.hit.primitive.geometryIndex = query.CommittedGeometryIndex();
        result.hit.primitive.primitiveIndex = query.CommittedPrimitiveIndex();
        result.hit.primitive.barycentrics = query.CommittedTriangleBarycentrics();
    }
    return result;
}

/// @brief 画素の中心を通るカメラのレイ
RayDesc MakeCameraRay(uint2 pixel)
{
    const float2 uv = (float2(pixel) + 0.5f) / float2(gWidth, gHeight);
    const float2 ndc = ScreenUVToNDC(uv);
    const float3 camera = float3(gCameraX, gCameraY, gCameraZ);
    const float3 nearPosition = ReconstructWorldPosition(ndc, 0.0f, gInvViewProj);
    const float3 farPosition = ReconstructWorldPosition(ndc, 1.0f, gInvViewProj);
    const float3 toFar = farPosition - camera;
    const float farDistance = length(toFar);

    RayDesc ray;
    ray.Origin = camera;
    ray.Direction = (farDistance > 1.0e-6f) ? toFar / farDistance : float3(0.0f, 0.0f, 1.0f);
    ray.TMin = max(dot(nearPosition - camera, ray.Direction), 0.0f);
    ray.TMax = max(farDistance, ray.TMin + 1.0e-3f);
    return ray;
}

/// @brief 当たった面の法線（レイが来た側を向く。表なしなら -レイの向き）
float3 HitFaceNormal(RTSceneTables tables, RTTraceHit hit, float3 direction)
{
    if (!tables.valid)
    {
        return -direction;
    }
    return LoadRTHitTriangle(tables, hit.primitive, direction).faceNormal;
}

/// @brief カメラのレイを記録する
void StoreCameraRay(RayDesc ray, uint instanceMask, DebugTraceResult traced)
{
    const uint offset = kDataTraceRays;
    gDebugData.Store4(offset, uint4(asuint(ray.Origin), asuint(ray.TMax)));
    gDebugData.Store4(offset + 16u, uint4(asuint(ray.Direction), asuint(traced.hit.hit ? traced.hit.hitT : -1.0f)));
    const uint flags = (traced.hit.hit ? 1u : 0u) | (traced.hit.frontFace ? 2u : 0u);
    gDebugData.Store4(offset + 32u, uint4(traced.hit.primitive.instanceIndex, traced.hit.primitive.geometryIndex,
        traced.hit.primitive.primitiveIndex, flags));
    gDebugData.Store4(offset + 48u, uint4(0u, instanceMask, traced.candidates, traced.alphaTests));
}

/// @brief 1 画素を撃ち、カメラのレイと当たった点を記録する
void TracePixel(RTSceneTables tables, uint2 pixel)
{
    const RayDesc cameraRay = MakeCameraRay(pixel);
    const DebugTraceResult primary = TraceDebugRay(tables, cameraRay, gInstanceMask, false, 0.0f);
    StoreCameraRay(cameraRay, gInstanceMask, primary);

    float3 position = float3(0.0f, 0.0f, 0.0f);
    float3 normal = float3(0.0f, 0.0f, 0.0f);
    float4 baseColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float2 uv = float2(0.0f, 0.0f);
    if (primary.hit.hit)
    {
        position = cameraRay.Origin + cameraRay.Direction * primary.hit.hitT;
        normal = -cameraRay.Direction;
        if (tables.valid)
        {
            const RTHitTriangle tri = LoadRTHitTriangle(tables, primary.hit.primitive, cameraRay.Direction);
            normal = tri.faceNormal;
            uv = tri.uv;
            const float coneWidth = gConeSpread * primary.hit.hitT;
            baseColor = SampleRTHitBaseColor(tri, RTHitFootprintLog2(tri, cameraRay.Direction, coneWidth));
        }
    }

    gDebugData.Store4(kDataTraceHeader, uint4(asuint(position), 1u));
    gDebugData.Store4(kDataTraceHeader + 16u, uint4(asuint(normal), primary.hit.hit ? 1u : 0u));
    gDebugData.Store4(kDataTraceHeader + 32u, asuint(baseColor));
    gDebugData.Store4(kDataTraceHeader + 48u, uint4(asuint(uv), pixel.x, pixel.y));
}

/// @brief 1 画素の書き出す値（w = 1 なら当たった・0 なら当たっていない）
float4 EvaluateScenePixel(RTSceneTables tables, uint2 pixel, out uint candidates, out uint alphaTests, out bool hit)
{
    const RayDesc ray = MakeCameraRay(pixel);
    const bool countAll = (gMode == kSceneModeCandidates);
    const DebugTraceResult traced = TraceDebugRay(tables, ray, gInstanceMask, countAll, 0.0f);
    candidates = traced.candidates;
    alphaTests = traced.alphaTests;
    hit = traced.hit.hit;

    if (gMode == kSceneModeCandidates)
    {
        return float4(float(traced.candidates), 0.0f, 0.0f, 1.0f);
    }
    if (gMode == kSceneModeAlphaTests)
    {
        return float4(float(traced.alphaTests), 0.0f, 0.0f, 1.0f);
    }
    if (gMode == kSceneModeDepthDifference)
    {
        const float depth = gSceneDepth.Load(int3(pixel, 0));
        const bool rasterHit = !IsBackgroundDepth(depth);
        if (!rasterHit && !traced.hit.hit)
        {
            return float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        if (!rasterHit)
        {
            return float4(-kDepthDifferenceOneSided, 0.0f, 0.0f, 1.0f);
        }
        const float2 uv = (float2(pixel) + 0.5f) / float2(gWidth, gHeight);
        const float3 rasterPosition = ReconstructWorldPosition(ScreenUVToNDC(uv), depth, gInvViewProj);
        const float rasterDistance = length(rasterPosition - ray.Origin);
        if (!traced.hit.hit)
        {
            return float4(kDepthDifferenceOneSided, rasterDistance, 0.0f, 1.0f);
        }
        return float4(traced.hit.hitT - rasterDistance, rasterDistance, traced.hit.hitT, 1.0f);
    }
    if (!traced.hit.hit)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const RTHitPrimitive primitive = traced.hit.primitive;
    const uint4 rowInfo = LoadRowInfo(primitive.instanceIndex);
    switch (gMode)
    {
    case kSceneModeInstance:
        return float4(float(primitive.instanceIndex), 0.0f, 0.0f, 1.0f);
    case kSceneModeSubMesh:
        return float4(float(primitive.geometryIndex), float(primitive.instanceIndex), 0.0f, 1.0f);
    case kSceneModeTriangle:
        return float4(float(primitive.primitiveIndex), float(primitive.geometryIndex), float(primitive.instanceIndex), 1.0f);
    case kSceneModeDistance:
        return float4(traced.hit.hitT, 0.0f, 0.0f, 1.0f);
    case kSceneModeNormal:
        return float4(HitFaceNormal(tables, traced.hit, ray.Direction), 1.0f);
    case kSceneModeBaseColor:
    {
        if (!tables.valid)
        {
            return float4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        const RTHitTriangle tri = LoadRTHitTriangle(tables, primitive, ray.Direction);
        const float coneWidth = gConeSpread * traced.hit.hitT;
        const float4 baseColor = SampleRTHitBaseColor(tri, RTHitFootprintLog2(tri, ray.Direction, coneWidth));
        return float4(baseColor.rgb, 1.0f);
    }
    case kSceneModeMask:
        return float4(float(rowInfo.x), 0.0f, 0.0f, 1.0f);
    case kSceneModeFrontFace:
        return float4(traced.hit.frontFace ? 1.0f : -1.0f, 0.0f, 0.0f, 1.0f);
    case kSceneModeMotion:
    {
        const float motion = ((rowInfo.y & kRowFlagDynamicShape) != 0) ? 2.0f
            : (((rowInfo.y & kRowFlagMoving) != 0) ? 1.0f : 0.0f);
        return float4(motion, 0.0f, 0.0f, 1.0f);
    }
    default:
        return float4(traced.hit.hitT, 0.0f, 0.0f, 1.0f);
    }
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const RTSceneTables tables = MakeRTSceneTables(gInstanceTableIndex, gSubMeshTableIndex);
    if (gTraceOnly != 0)
    {
        if (all(dispatchThreadId.xy == uint2(0, 0)) && gTraceX < gWidth && gTraceY < gHeight)
        {
            TracePixel(tables, uint2(gTraceX, gTraceY));
        }
        return;
    }

    const uint2 pixel = dispatchThreadId.xy;
    const bool inside = all(pixel < uint2(gWidth, gHeight));
    uint candidates = 0;
    uint alphaTests = 0;
    bool hit = false;
    if (inside)
    {
        gSceneOutput[pixel] = EvaluateScenePixel(tables, pixel, candidates, alphaTests, hit);
    }

    // 画面全体の合計（候補の数・アルファを調べた数・当たった画素・画素）
    const uint candidateSum = WaveActiveSum(candidates);
    const uint alphaTestSum = WaveActiveSum(alphaTests);
    const uint hitSum = WaveActiveCountBits(inside && hit);
    const uint pixelSum = WaveActiveCountBits(inside);
    if (WaveIsFirstLane())
    {
        uint previous;
        gDebugData.InterlockedAdd(kDataSceneTotals + 0u, candidateSum, previous);
        gDebugData.InterlockedAdd(kDataSceneTotals + 4u, alphaTestSum, previous);
        gDebugData.InterlockedAdd(kDataSceneTotals + 8u, hitSum, previous);
        gDebugData.InterlockedAdd(kDataSceneTotals + 12u, pixelSum, previous);
    }
}
