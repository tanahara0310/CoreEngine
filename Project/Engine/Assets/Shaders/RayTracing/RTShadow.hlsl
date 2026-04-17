// ============================================================
// DXR レイトレーシングシャドウシェーダー
// TLAS に対してシャドウレイを飛ばし、遮蔽結果を UAV テクスチャに書き込む
// ============================================================

// 出力: シャドウマスク（0=影, 1=光）
RWTexture2D<float> gShadowOutput : register(u0);

// TLAS
RaytracingAccelerationStructure gScene : register(t0);

// G-Buffer: ワールド座標
Texture2D<float4> gWorldPosition : register(t1);

// G-Buffer: 法線（セルフシャドウバイアス用）
Texture2D<float4> gNormalRoughness : register(t2);

// ライト方向 + パディング
cbuffer ShadowRayConstants : register(b0)
{
    float3 gLightDirection;  // 正規化済みライト方向（光源→シーン）
    float  gShadowBias;     // シャドウバイアス
    float  gMaxRayDistance;  // レイの最大距離
    float3 gPadding;
};

// ============================================================
// ペイロード（シャドウレイでは最小限）
// ============================================================
struct ShadowPayload
{
    float shadowFactor; // 0=遮蔽, 1=非遮蔽
};

// ============================================================
// Ray Generation シェーダー
// ============================================================
[shader("raygeneration")]
void RTShadowRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    // G-Buffer からワールド座標を読み取り
    float4 worldPosSample = gWorldPosition.Load(int3(launchIndex, 0));

    // 背景ピクセル（a < 0.5）はスキップ → 影なし（1.0）
    if (worldPosSample.a < 0.5f)
    {
        gShadowOutput[launchIndex] = 1.0f;
        return;
    }

    float3 worldPos = worldPosSample.xyz;

    // G-Buffer から法線を読み取り（セルフシャドウ防止用）
    float4 normalSample = gNormalRoughness.Load(int3(launchIndex, 0));
    float3 N = normalize(normalSample.rgb * 2.0f - 1.0f);

    // ライト方向の逆（光源へ向かう方向）
    float3 rayDir = normalize(-gLightDirection);

    // 法線方向 + ライト方向のバイアスでセルフシャドウを防止
    float3 origin = worldPos + N * gShadowBias + rayDir * gShadowBias * 0.5f;

    // シャドウレイの設定
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;
    ray.TMin = 0.001f;
    ray.TMax = gMaxRayDistance;

    ShadowPayload payload;
    payload.shadowFactor = 0.0f; // デフォルト: 遮蔽あり（ClosestHit で確定、Miss で 1.0 に上書き）

    // RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH で最初のヒットで即終了（シャドウレイ最適化）
    TraceRay(
        gScene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,           // InstanceInclusionMask
        0,              // RayContributionToHitGroupIndex
        1,              // MultiplierForGeometryContributionToHitGroupIndex
        0,              // MissShaderIndex
        ray,
        payload
    );

    gShadowOutput[launchIndex] = payload.shadowFactor;
}

// ============================================================
// Miss シェーダー（遮蔽物なし → 光が届く）
// ============================================================
[shader("miss")]
void RTShadowMiss(inout ShadowPayload payload)
{
    payload.shadowFactor = 1.0f;
}

// ============================================================
// Closest Hit シェーダー（遮蔽物あり → 影）
// RAY_FLAG_SKIP_CLOSEST_HIT_SHADER を使うため通常は呼ばれないが、
// State Object の完全性のために定義
// ============================================================
[shader("closesthit")]
void RTShadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.shadowFactor = 0.0f;
}
