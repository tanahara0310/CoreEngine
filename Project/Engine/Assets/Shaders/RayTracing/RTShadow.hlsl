// ============================================================
// DXR レイトレーシングシャドウシェーダー
// TLAS に対してシャドウレイを飛ばし、遮蔽結果を UAV テクスチャに書き込む
// ============================================================

#include "../Include/Common/DepthReconstruction.hlsli"
#include "../Include/Common/ShaderMath.hlsli" // PI / TWO_PI

// 出力: シャドウマスク（0=影, 1=光）。トレース解像度（ハーフ解像度時はフルの半分）
RWTexture2D<float> gShadowOutput : register(u0);

// TLAS
RaytracingAccelerationStructure gScene : register(t0);

// G-Buffer: 深度（WorldPosition ターゲット廃止に伴い深度から復元する）
Texture2D<float> gSceneDepth : register(t1);

// G-Buffer: 法線（セルフシャドウバイアス用）
Texture2D<float4> gNormalRoughness : register(t2);

// ライト方向 + ソフトシャドウ + 解像度パラメータ
// C++ 側 ShadowRayConstants 構造体と厳密にレイアウトを合わせること
// 注意: この cbuffer は全てスカラーで構成する。float2/float3 や配列を混ぜると
// 16 バイト境界への切り上げが入り、gInvViewProj のオフセットが C++ 側とずれる
// （2026-07-25 に影が全消失した原因がこれ）。
cbuffer ShadowRayConstants : register(b0)
{
    float3 gLightDirection; // 正規化済みライト方向（光源→シーン）
    float gShadowBias; // シャドウバイアス
    float gMaxRayDistance; // レイの最大距離
    float gLightRadius; // 光源の角半径（ラジアン）：ペナンブラ幅を制御
    int gSoftShadowSamples; // ソフトシャドウのサンプル数（1=ハードシャドウ）
    uint gFrameIndex; // フレームカウンタ（ノイズのテンポラル変化用）
    float gScreenWidth; // フル解像度の幅（ピクセル）
    float gScreenHeight; // フル解像度の高さ（ピクセル）
    int gTraceOffsetX; // ハーフ解像度時の 2x2 サンプル位置（フレームごとに巡回）
    int gTraceOffsetY;
    int gTraceScale; // 1 = フル解像度 / 2 = ハーフ解像度
    int gPad0_;
    int gPad1_;
    int gPad2_;
    float4x4 gInvViewProj; // WorldPosition ターゲット廃止に伴う深度復元用
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
    float phi = r2 * TWO_PI;

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

    // ハーフ解像度時は、トレース座標に対応するフル解像度ピクセルを選ぶ。
    // オフセットはフレームごとに 2x2 を巡回するので、テンポラル蓄積が
    // 4 フレームで全サブピクセルの情報を回収する。
    int2 fullCoord = int2(launchIndex) * gTraceScale + int2(gTraceOffsetX, gTraceOffsetY);
    fullCoord = min(fullCoord, int2(int(gScreenWidth) - 1, int(gScreenHeight) - 1));

    // G-Buffer から深度を読み取り、ワールド座標を復元する
    float ndcDepth = gSceneDepth.Load(int3(fullCoord, 0));

    // 背景ピクセルはスキップ → 影なし（1.0）
    if (IsBackgroundDepth(ndcDepth))
    {
        gShadowOutput[launchIndex] = 1.0f;
        return;
    }

    float2 screenUV = (float2(fullCoord) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);
    float3 worldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gInvViewProj);

    // G-Buffer から法線を読み取り（セルフシャドウ防止用）
    float4 normalSample = gNormalRoughness.Load(int3(fullCoord, 0));
    float3 N = normalize(normalSample.rgb * 2.0f - 1.0f);

    // ライト方向の逆（光源へ向かう方向）
    float3 rayDir = normalize(-gLightDirection);

    // ===== セルフシャドウ防止バイアス（深度復元の誤差に追従させる） =====
    // ワールド座標は深度バッファ（D24_UNORM）から復元しているため、深度 1 コード分の
    // 量子化がワールド空間では「カメラからの距離の 2 乗」に比例した誤差に拡大される
    // （near=0.1 / far=100000 の構成では 500m 先で約 0.15m）。固定値 0.05 のままだと
    // 数百メートル先で誤差がバイアスを上回り、レイが自分の面の内側から始まって
    // 即座に自己交差する＝モデル全体が真っ黒になる。カメラが動くと量子化コードが
    // 変わるたびに交差する/しないが切り替わるため「移動中だけチカチカ」する。
    // 誤差量は解析式ではなく実際に 1 コードずらして復元した差分で測る（near/far 非依存）。
    const float kDepthQuantum = 1.0f / 16777216.0f; // D24_UNORM の 1 コード
    float3 worldPosQuantized = ReconstructWorldPosition(
        ScreenUVToNDC(screenUV), max(ndcDepth - kDepthQuantum, 0.0f), gInvViewProj);
    float depthPrecisionError = length(worldPos - worldPosQuantized);

    // かすめ角では法線方向のオフセットが実効的に薄くなるので傾斜スケールを掛ける
    float NdotL = saturate(dot(N, rayDir));
    float slopeScale = clamp(1.0f / max(NdotL, 0.15f), 1.0f, 6.0f);
    float bias = max(gShadowBias, depthPrecisionError * 4.0f) * slopeScale;

    // 法線方向 + ライト方向のバイアスでセルフシャドウを防止
    float3 origin = worldPos + N * bias + rayDir * bias * 0.5f;

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
