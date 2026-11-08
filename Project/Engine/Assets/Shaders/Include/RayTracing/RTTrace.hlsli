#ifndef RT_TRACE_HLSLI
#define RT_TRACE_HLSLI

// ============================================================
// RayQuery でシーンへレイを撃つ共通の関数
// アルファで抜ける材質（葉など）の候補は、材質の表でアルファを調べて当たりを決める。
// 加速構造は引数で受け取る（このヘッダーはサンプラのほかにリソースを宣言しない）。
// ============================================================

#include "RTSceneTables.hlsli"
#include "RTInstanceMask.hlsli"

/// @brief レイの結果
struct RTTraceHit
{
    bool hit;                  // 何かに当たった
    float hitT;                // 当たった距離
    bool frontFace;            // 三角形の表から当たった
    RTHitPrimitive primitive;  // 当たった三角形
};

/// @brief 何にも当たっていない結果
RTTraceHit NoRTTraceHit()
{
    RTTraceHit result;
    result.hit = false;
    result.hitT = 0.0f;
    result.frontFace = false;
    result.primitive.instanceIndex = 0;
    result.primitive.geometryIndex = 0;
    result.primitive.primitiveIndex = 0;
    result.primitive.barycentrics = float2(0.0f, 0.0f);
    return result;
}

/// @brief レイを撃ち、アルファで抜ける所は通り抜けて当たりを返す
/// @param tables            材質の表（表なしならアルファを調べず、どの物も不透明として当てる）
/// @param instanceMask      当てるインスタンスの印（RTInstanceMask）
/// @param extraRayFlags     足すレイのフラグ
/// @param coneWidthAtOrigin レイの始点での広がりの幅（m）
/// @param coneSpread        レイの広がりの角度（rad。距離に比例して幅が増える）
RTTraceHit TraceRTScene(
    RaytracingAccelerationStructure scene, RTSceneTables tables, RayDesc ray, uint instanceMask,
    uint extraRayFlags, float coneWidthAtOrigin, float coneSpread)
{
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    const uint rayFlags = extraRayFlags | (tables.valid ? RAY_FLAG_NONE : RAY_FLAG_FORCE_OPAQUE);
    query.TraceRayInline(scene, rayFlags, instanceMask, ray);
    [loop]
    while (query.Proceed())
    {
        // アルファを調べる材質の物だけが不透明でない候補として来る
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            RTHitPrimitive candidate;
            candidate.instanceIndex = query.CandidateInstanceID();
            candidate.geometryIndex = query.CandidateGeometryIndex();
            candidate.primitiveIndex = query.CandidatePrimitiveIndex();
            candidate.barycentrics = query.CandidateTriangleBarycentrics();
            const float coneWidth = coneWidthAtOrigin + coneSpread * query.CandidateTriangleRayT();
            if (!IsRTHitCutout(tables, candidate, ray.Direction, coneWidth))
            {
                query.CommitNonOpaqueTriangleHit();
            }
        }
    }

    RTTraceHit result = NoRTTraceHit();
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        result.hit = true;
        result.hitT = query.CommittedRayT();
        result.frontFace = query.CommittedTriangleFrontFace();
        result.primitive.instanceIndex = query.CommittedInstanceID();
        result.primitive.geometryIndex = query.CommittedGeometryIndex();
        result.primitive.primitiveIndex = query.CommittedPrimitiveIndex();
        result.primitive.barycentrics = query.CommittedTriangleBarycentrics();
    }
    return result;
}

/// @brief 最も近い当たりを返す（アルファで抜ける所は通り抜ける）
RTTraceHit TraceRTClosestHit(
    RaytracingAccelerationStructure scene, RTSceneTables tables, RayDesc ray, uint instanceMask,
    float coneWidthAtOrigin, float coneSpread)
{
    return TraceRTScene(scene, tables, ray, instanceMask, RAY_FLAG_NONE, coneWidthAtOrigin, coneSpread);
}

/// @brief レイが何かに遮られるかを返す（最初の当たりで止める。アルファで抜ける所は通り抜ける）
bool TraceRTOcclusion(
    RaytracingAccelerationStructure scene, RTSceneTables tables, RayDesc ray, uint instanceMask,
    float coneWidthAtOrigin, float coneSpread)
{
    return TraceRTScene(
        scene, tables, ray, instanceMask, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        coneWidthAtOrigin, coneSpread).hit;
}

/// @brief 最も近い当たりを、どの物も不透明として返す（材質を見ない）
RTTraceHit TraceRTOpaqueClosestHit(RaytracingAccelerationStructure scene, RayDesc ray, uint instanceMask)
{
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, instanceMask, ray);
    query.Proceed();

    RTTraceHit result = NoRTTraceHit();
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        result.hit = true;
        result.hitT = query.CommittedRayT();
        result.frontFace = query.CommittedTriangleFrontFace();
        result.primitive.instanceIndex = query.CommittedInstanceID();
        result.primitive.geometryIndex = query.CommittedGeometryIndex();
        result.primitive.primitiveIndex = query.CommittedPrimitiveIndex();
        result.primitive.barycentrics = query.CommittedTriangleBarycentrics();
    }
    return result;
}

#endif // RT_TRACE_HLSLI
