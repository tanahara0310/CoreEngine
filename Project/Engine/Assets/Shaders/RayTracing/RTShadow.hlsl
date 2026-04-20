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

// ライト方向 + ソフトシャドウパラメータ
cbuffer ShadowRayConstants : register(b0)
{
    float3 gLightDirection;    // 正規化済みライト方向（光源→シーン）
    float  gShadowBias;        // シャドウバイアス
    float  gMaxRayDistance;    // レイの最大距離
    float  gLightRadius;       // 光源の角半径（ラジアン）：ペナンブラ幅を制御
    int    gSoftShadowSamples; // ソフトシャドウのサンプル数（1=ハードシャドウ）
    uint   gFrameIndex;        // フレームカウンタ（ノイズのテンポラル変化用）
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

// PCG ハッシュ（高品質な疑似乱数）
uint PcgHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// [0, 1) の一様乱数
float RandomFloat(uint seed)
{
    return float(PcgHash(seed)) * (1.0f / 4294967296.0f);
}

/// @brief dir を中心軸としたコーン内でランダム方向をサンプリング
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

    // ============================================================
    // ソフトシャドウ: コーン内でジッターした複数レイの平均をとる
    // DXR コンパイラが動的ループ上限を扱えるよう、コンパイル時定数で
    // 最大ループ回数を定義し、実行時に early break する。
    // ============================================================
    static const int kMaxSamples = 16; // コンパイル時定数（上限）
    // === デバッグ: cbuffer を無視してハードコードで強制テスト ===
    // 問題解決後に削除し、cbuffer 値を使用すること
    int   numSamples = 8;    // 強制 8 サンプル
    float testRadius = 0.15f; // 強制: 大きめの角半径
    // ==========================================================
    float shadowSum  = 0.0f;

    // ピクセル固有のランダムシード（フレームインデックスで毎フレーム変化）
    uint baseSeed = launchIndex.x * 1973u + launchIndex.y * 9277u + gFrameIndex * 26699u;

    [loop]
    for (int s = 0; s < kMaxSamples; ++s)
    {
        if (s >= numSamples)
            break;

        // サンプルごとに独立した乱数を生成
        uint seed1 = PcgHash(baseSeed + uint(s) * 6571u);
        uint seed2 = PcgHash(seed1);
        float r1 = float(seed1) * (1.0f / 4294967296.0f);
        float r2 = float(seed2) * (1.0f / 4294967296.0f);

        // デバッグ: testRadius でコーン内ジッターを強制
        float3 jitteredDir = SampleConeDirection(rayDir, testRadius, r1, r2);

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = jitteredDir;
        ray.TMin      = 0.001f;
        ray.TMax      = gMaxRayDistance;

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

    // サンプル平均で [0,1] の連続値を出力（ペナンブラが自然なグラデーションになる）
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
