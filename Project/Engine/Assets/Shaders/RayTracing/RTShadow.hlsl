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

// テンポラル蓄積用: 前フレームのシャドウ蓄積結果
Texture2D<float> gHistoryShadow : register(t3);

// モーションベクター（GBuffer で書き込まれた NDC 差分）
Texture2D<float2> gMotionVector : register(t4);

// ライト方向 + ソフトシャドウ + テンポラル蓄積パラメータ
// C++ 側 ShadowRayConstants 構造体と厳密にレイアウトを合わせること
cbuffer ShadowRayConstants : register(b0)
{
    float3 gLightDirection; // 正規化済みライト方向（光源→シーン）
    float gShadowBias; // シャドウバイアス
    float gMaxRayDistance; // レイの最大距離
    float gLightRadius; // 光源の角半径（ラジアン）：ペナンブラ幅を制御
    int gSoftShadowSamples; // ソフトシャドウのサンプル数（1=ハードシャドウ）
    uint gFrameIndex; // フレームカウンタ（ノイズのテンポラル変化用）
    float gHistoryAlpha; // テンポラル蓄積ブレンド係数（初回フレームは 1.0）
    float gScreenWidth; // スクリーン幅（ピクセル）
    float gScreenHeight; // スクリーン高さ（ピクセル）
    float gPadding_[1]; // 16バイトアライメント用パディング
};

// ============================================================
// ペイロード（シャドウレイでは最小限）
// ============================================================
struct ShadowPayload
{
    float shadowFactor; // 0=遮蔽, 1=非遮蔽
};

// ============================================================
// ソフトシャドウ用ユーティリティ
// ============================================================
static const float kPi = 3.14159265358979f;

// PCG ハッシュ
uint PcgHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// @brief dir を中心軸としたコーン内でランダム方向をサンプリング
/// @param dir       コーンの中心軸（正規化済み）
/// @param coneAngle コーンの半頂角（ラジアン）
/// @param r1, r2    [0,1) 一様乱数 2 つ
float3 SampleConeDirection(float3 dir, float coneAngle, float r1, float r2)
{
    // コーン内の cosθ を一様サンプリング
    float cosMax = cos(coneAngle);
    float cosTheta = r1 * (1.0f - cosMax) + cosMax;
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    float phi = r2 * 2.0f * kPi;

    // ローカル座標
    float lx = sinTheta * cos(phi);
    float ly = sinTheta * sin(phi);
    float lz = cosTheta;

    // dir を Z 軸とするフレームを構築
    float3 up = (abs(dir.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, dir));
    float3 bitan = cross(dir, tangent);

    return normalize(lx * tangent + ly * bitan + lz * dir);
}

// ============================================================
// Ray Generation シェーダー：生シャドウ値を出力するのみ。
// テンポラル蓄積は RTShadowTemporal.CS.hlsl で行う。
// ============================================================
[shader("raygeneration")]
void RTShadowRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;

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

    // ソフトシャドウ：コーン内ジッターレイの平均（N=1 はバイナリ）
    static const int kMaxSamples = 16;
    int   numSamples = clamp(gSoftShadowSamples, 1, kMaxSamples);
    float shadowSum = 0.0f;

    uint baseSeed = launchIndex.x * 1973u + launchIndex.y * 9277u + gFrameIndex * 26699u;

    [loop]
    for (int s = 0; s < numSamples; ++s)
    {
        uint seed1 = PcgHash(baseSeed + uint(s) * 6571u);
        uint seed2 = PcgHash(seed1);
        float r1 = float(seed1) * (1.0f / 4294967296.0f);
        float r2 = float(seed2) * (1.0f / 4294967296.0f);

        float3 jitteredDir = (gLightRadius > 0.0f)
            ? SampleConeDirection(rayDir, gLightRadius, r1, r2)
            : rayDir;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = jitteredDir;
        ray.TMin = 0.001f;
        ray.TMax = gMaxRayDistance;

        ShadowPayload payload;
        payload.shadowFactor = 0.0f;

        TraceRay(
            gScene,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
            0xFF,
            0,
            1,
            0,
            ray,
            payload
        );

        shadowSum += payload.shadowFactor;
    }

    // 生値をそのまま出力（履歴参照は Temporal パスで行う）
    gShadowOutput[launchIndex] = shadowSum / float(numSamples);
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
// RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH により最初のヒットで即終了するため
// 通常は呼ばれないが、State Object の完全性のために定義
// ============================================================
[shader("closesthit")]
void RTShadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.shadowFactor = 0.0f;
}
