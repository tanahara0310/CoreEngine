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
    // ソフトシャドウ: コーン内でジッターした複数レイの平均
    // ============================================================
    static const int kMaxSamples = 16;
    int   numSamples = clamp(gSoftShadowSamples, 1, kMaxSamples);
    float shadowSum  = 0.0f;

    uint baseSeed = launchIndex.x * 1973u + launchIndex.y * 9277u + gFrameIndex * 26699u;

    [loop]
    for (int s = 0; s < kMaxSamples; ++s)
    {
        if (s >= numSamples)
            break;

        uint seed1 = PcgHash(baseSeed + uint(s) * 6571u);
        uint seed2 = PcgHash(seed1);
        float r1 = float(seed1) * (1.0f / 4294967296.0f);
        float r2 = float(seed2) * (1.0f / 4294967296.0f);

        float3 jitteredDir = (gLightRadius > 0.0f)
            ? SampleConeDirection(rayDir, gLightRadius, r1, r2)
            : rayDir;

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

    // ============================================================
    // 現フレームのサンプル平均を計算
    // ============================================================
    float currentMean = shadowSum / float(numSamples);

    // ============================================================
    // テンポラル蓄積
    // ============================================================
    // 【影の遅れ問題と解決策】
    //
    //   問題: モーションベクターは「受影サーフェス（床等）の動き」を記録するが、
    //         キャスター（移動キャラ等）が動いた場合、床のMV≈0なので
    //         前フレームの正確な位置に移動しても「古い影」を参照してしまう。
    //         結果: 影がキャスターの動きに1フレーム以上遅れてついてくる。
    //
    //   解決: 「影の値差（Luminance Disocclusion）」で棄却判定を行う。
    //         | currentMean - historyShadow | が大きい
    //         → 影の状態が変わった（キャスターが動いた）
    //         → blendAlpha を 1.0 に近づけて現フレームを優先
    //         → 遅延なくシャドウを更新
    //
    //   副作用: キャスター移動時はノイズが一時的に増えるが、
    //           A-Trous空間フィルタ（Denoise）が直後に適用されるので
    //           1フレームのノイズスパイクは見えない。

    int2 pixelCoord = int2(launchIndex);

    // モーションベクターを読み取り（NDC差分: 現-前、クリップ空間 Y+1=上）
    float2 mv = gMotionVector.Load(int3(pixelCoord, 0));

    // NDC差分をスクリーンピクセル差分に変換してリプロジェクション
    // NDC X: +1=右  → スクリーン X と同方向 → 符号そのまま
    // NDC Y: +1=上  → スクリーン Y は下方向 → 符号反転
    float2 prevPixelF;
    prevPixelF.x = float(pixelCoord.x) - mv.x * gScreenWidth  * 0.5f;
    prevPixelF.y = float(pixelCoord.y) + mv.y * gScreenHeight * 0.5f;
    int2 prevPixel = int2(round(prevPixelF));

    bool inBounds = prevPixel.x >= 0 && prevPixel.y >= 0
                 && prevPixel.x < int(gScreenWidth) && prevPixel.y < int(gScreenHeight);

    float blendAlpha;
    float historyShadow;
    if (inBounds && gHistoryAlpha < 1.0f)
    {
        historyShadow = gHistoryShadow.Load(int3(prevPixel, 0));

        // ── Luminance Disocclusion ──────────────────────────────
        // 現フレームと履歴の影の値の差を検出する。
        // 影はバイナリ（0=影、1=光）に近い値なので差が大きい = キャスターが動いた。
        //
        // 例:
        //   前フレーム: histShadow=1.0 (光) / 今フレーム: currentMean=0.0 (影出現)
        //   → diff=1.0 → blendAlpha≒1.0 → 現フレームを即採用（遅延なし）
        //
        //   前フレーム: histShadow=0.2 / 今フレーム: currentMean=0.18
        //   → diff=0.02 → blendAlpha=historyAlpha → 高蓄積でノイズ削減
        // ────────────────────────────────────────────────────────
        float shadowDiff = abs(currentMean - historyShadow);

        // diff が kShadowChangeThreshold を超えた割合だけ alpha を 1.0 に引き上げる
        // kShadowChangeThreshold = 0.15f: ソフトシャドウのペナンブラ変動より大きい値に設定
        static const float kShadowChangeThreshold = 0.15f;
        float disocclusionWeight = saturate(shadowDiff / kShadowChangeThreshold);

        // さらにカメラ/オブジェクト移動量も考慮（既存のMVベース補正）
        float mvPixelLen = length(mv) * max(gScreenWidth, gScreenHeight);
        float motionWeight = saturate(mvPixelLen / 10.0f);

        // 両方の要因で大きい方を採用
        float rejectionWeight = max(disocclusionWeight, motionWeight);
        blendAlpha = lerp(gHistoryAlpha, 1.0f, rejectionWeight);
    }
    else
    {
        historyShadow = currentMean;
        blendAlpha = 1.0f;
    }

    gShadowOutput[launchIndex] = lerp(historyShadow, currentMean, blendAlpha);
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
