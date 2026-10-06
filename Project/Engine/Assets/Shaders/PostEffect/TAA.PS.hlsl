// TAA（Temporal Anti-Aliasing）ピクセルシェーダー
//
// 毎フレーム、プロジェクション行列にサブピクセルジッタを入れて描画された SceneColor を、
// モーションベクターで再投影した前フレームの結果と蓄積する。
// 蓄積が進むほど 1 ピクセルあたりの実効サンプル数が増え、スーパーサンプリング相当になる。
//
// 出力先はそのまま次フレームの履歴になる（ping-pong）。
//
// 履歴の棄却は「近傍 AABB クリップ」:
//   現フレームの 3x3 近傍が作る YCoCg 空間の箱へ履歴を押し込み、
//   再投影が外れたピクセルの履歴を削る。
//
// モーションベクター（G-Buffer MotionVector）:
//   rg = NDC 差分（現フレーム - 前フレーム、ジッタ込み）
//   b  = 水面フラグ（1 = 水面が書いた画素）
//   深度が背景の画素は、深度 1 の点を前フレームの行列で投影して動きを求める。

#include "FullScreen.hlsli"
#include "../Include/Common/DepthReconstruction.hlsli"

Texture2D<float4> gSceneColor   : register(t0); // 現フレーム（ジッタ付きで描画された HDR）
Texture2D<float4> gHistoryColor : register(t1); // 前フレームの TAA 出力
Texture2D<float4> gMotionVector : register(t2); // rg=NDC 差分 / b=水面フラグ（GBuffer 産）
Texture2D<float>  gSceneDepth   : register(t3); // 背景画素の判定と再投影に使う深度

SamplerState gSampler : register(s0); // 履歴のバイリニア再投影用

cbuffer TAAParams : register(b0)
{
    float2 gScreenSize;
    float2 gJitterDelta;   // 現フレームと前フレームのジッタ差分（NDC）
    float gBlendAlpha;     // 現フレームの寄与率（0.1 = 履歴 90%）
    float gClampScale;     // 近傍 AABB の拡張率（大きいほどゴースト寄り・小さいほどちらつき寄り）
    float gDisableHistory; // 1.0 で履歴を完全無効化（初回フレーム・リサイズ直後）
    float gBlendAlphaMax;  // 水面の画素で履歴が食い違うときに使う寄与率の上限
    float4x4 gInvViewProj;  // 今フレームの View*Projection の逆行列（ジッタ込み）
    float4x4 gPrevViewProj; // 前フレームの View*Projection（ジッタ込み）
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PixelShaderOutput
{
    float4 color : SV_Target;
};

/// @brief NaN / Inf / 負値を除き、FP16 で表せる範囲へ収める
float3 SanitizeHdr(float3 c)
{
    return clamp(c, 0.0f, 60000.0f);
}

/// @brief RGB → YCoCg。輝度（Y）と色差を分離することで、
///        近傍 AABB が「明るさの箱」として素直に効くようになる
float3 RGBToYCoCg(float3 c)
{
    return float3(
         0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
         0.5f * c.r - 0.5f * c.b,
        -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

/// @brief YCoCg → RGB
float3 YCoCgToRGB(float3 c)
{
    return float3(
        c.x + c.y - c.z,
        c.x + c.z,
        c.x - c.y - c.z);
}

/// @brief 履歴を近傍 AABB へクリップする
/// @details min/max clamp と違い、中心からの方向を保ったまま箱の表面まで引き戻すため、
///          色相を壊さずにゴーストだけを削れる。
float3 ClipToAABB(float3 aabbMin, float3 aabbMax, float3 history)
{
    const float3 center = 0.5f * (aabbMax + aabbMin);
    const float3 extent = 0.5f * (aabbMax - aabbMin) + 1e-5f;

    const float3 offset = history - center;
    const float3 unitsFromCenter = abs(offset) / extent;
    const float maxUnit = max(unitsFromCenter.x, max(unitsFromCenter.y, unitsFromCenter.z));

    // 箱の中にあるならそのまま、外にあるなら表面まで戻す
    return (maxUnit > 1.0f) ? (center + offset / maxUnit) : history;
}

/// @brief HDR のファイアフライが履歴へ焼き付くのを防ぐためのブレンド重み
/// @details 明るいピクセルほど寄与を下げる（Karis のトーンマップ加重平均）
float TonemapWeight(float3 color)
{
    return 1.0f / (1.0f + max(color.r, max(color.g, color.b)));
}

/// @brief 範囲内へクランプしたうえで現フレームの 1 テクセルを読む
float3 LoadCurrent(int2 coord, int2 screenMax)
{
    return SanitizeHdr(gSceneColor.Load(int3(clamp(coord, int2(0, 0), screenMax), 0)).rgb);
}

/// @brief 背景（深度が最遠）の画素の動きを、前フレームの行列でのカメラ由来の再投影から求める
/// @details 深度 1 の点（遠方の空）を前フレームの View*Projection で投影し、
///          現在の NDC との差を返す。ジッタ込みの行列同士なので G-Buffer の規約と一致する。
float2 ComputeBackgroundMotion(float2 currentUV)
{
    const float2 ndc = ScreenUVToNDC(currentUV);
    const float3 farWorld = ReconstructWorldPosition(ndc, 1.0f, gInvViewProj);
    const float4 prevClip = mul(float4(farWorld, 1.0f), gPrevViewProj);
    if (prevClip.w <= 1.0e-6f)
    {
        return float2(0.0f, 0.0f);
    }
    return ndc - prevClip.xy / prevClip.w;
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    const int2 coord = int2(input.position.xy);
    const int2 screenMax = int2(gScreenSize) - int2(1, 1);
    const float3 current = LoadCurrent(coord, screenMax);

    // 履歴無効時（初回フレーム等）は現フレームをそのまま出す
    if (gDisableHistory > 0.5f)
    {
        output.color = float4(current, 1.0f);
        return output;
    }

    // ===== 近傍 3x3 の統計を取る =====
    // ここで得た箱が、この後の履歴クリップの判定基準になる。
    float3 neighborMin = float3(1e20f, 1e20f, 1e20f);
    float3 neighborMax = float3(-1e20f, -1e20f, -1e20f);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const float3 n = RGBToYCoCg(LoadCurrent(coord + int2(dx, dy), screenMax));
            neighborMin = min(neighborMin, n);
            neighborMax = max(neighborMax, n);
        }
    }

    // gClampScale で箱の広さを実行時に振る
    {
        const float3 center = 0.5f * (neighborMax + neighborMin);
        const float3 extent = 0.5f * (neighborMax - neighborMin) * gClampScale;
        neighborMin = center - extent;
        neighborMax = center + extent;
    }

    // ===== モーションベクターで前フレームへ再投影 =====
    const float2 currentUV = (float2(coord) + 0.5f) / gScreenSize;
    const float4 motionPacked = gMotionVector.Load(int3(coord, 0));
    const bool isWater = motionPacked.z > 0.5f;

    float2 motion = motionPacked.xy;
    if (!isWater && IsBackgroundDepth(gSceneDepth.Load(int3(coord, 0))))
    {
        motion = ComputeBackgroundMotion(currentUV);
    }

    // ジッタ分を除いた動きだけを使う。NDC Y は画面 Y と逆向き
    motion -= gJitterDelta;
    const float2 historyUV = currentUV - motion * float2(0.5f, -0.5f);

    // 再投影先が画面外なら履歴が存在しないので現フレームを採用する
    if (any(historyUV < 0.0f) || any(historyUV > 1.0f))
    {
        output.color = float4(current, 1.0f);
        return output;
    }

    const float3 historyRaw = SanitizeHdr(gHistoryColor.SampleLevel(gSampler, historyUV, 0.0f).rgb);

    // ===== 履歴のクリップ =====
    const float3 currentYCoCg = RGBToYCoCg(current);
    const float3 historyClippedYCoCg = ClipToAABB(neighborMin, neighborMax, RGBToYCoCg(historyRaw));
    const float3 historyClipped = YCoCgToRGB(historyClippedYCoCg);

    // ===== 寄与率 =====
    // 通常の画素は固定の gBlendAlpha。
    // 水面の画素だけ、履歴と現フレームの輝度差を近傍のコントラストで正規化した値で
    // gBlendAlpha 〜 gBlendAlphaMax を補間し、食い違うほど現フレーム寄りにする。
    float blendAlpha = gBlendAlpha;
    if (isWater)
    {
        const float neighborLumaExtent = max(neighborMax.x - neighborMin.x, 1.0e-4f);
        const float temporalDisagreement =
            saturate(abs(currentYCoCg.x - historyClippedYCoCg.x) / neighborLumaExtent);
        blendAlpha = lerp(gBlendAlpha, gBlendAlphaMax, temporalDisagreement);
    }

    // ===== 蓄積 =====
    // 明るさで重み付けした加重平均で、極端に明るい点が履歴に残り続けるのを防ぐ。
    const float weightCurrent = blendAlpha * TonemapWeight(current);
    const float weightHistory = (1.0f - blendAlpha) * TonemapWeight(historyClipped);
    const float weightSum = max(weightCurrent + weightHistory, 1e-5f);

    const float3 resolved = (current * weightCurrent + historyClipped * weightHistory) / weightSum;

    output.color = float4(SanitizeHdr(resolved), 1.0f);
    return output;
}
