// TAA（Temporal Anti-Aliasing）ピクセルシェーダー
//
// 毎フレーム、プロジェクション行列にサブピクセルジッタを入れて描画された SceneColor を、
// モーションベクターで再投影した前フレームの結果と蓄積する。
// 蓄積が進むほど 1 ピクセルあたりの実効サンプル数が増え、スーパーサンプリング相当になる。
//
// 出力先はそのまま次フレームの履歴になる（ping-pong）。
//
// ゴースト対策は「近傍 AABB クリップ」:
//   現フレームの 3x3 近傍が作る色空間の箱に履歴を押し込むことで、
//   再投影が外れた（＝ディスオクルージョンや MV が取れない）ピクセルの履歴を棄却する。
//   RTShadowTemporal.CS.hlsl の Variance Clamping と同じ考え方だが、
//   カラーは 3 チャンネルあるため YCoCg 空間の箱に対する「クリップ」を使う。

#include "FullScreen.hlsli"

Texture2D<float4> gSceneColor   : register(t0); // 現フレーム（ジッタ付きで描画された HDR）
Texture2D<float4> gHistoryColor : register(t1); // 前フレームの TAA 出力
Texture2D<float2> gMotionVector : register(t2); // NDC 差分（GBuffer 産）

SamplerState gSampler : register(s0); // 履歴のバイリニア再投影用

cbuffer TAAParams : register(b0)
{
    float2 gScreenSize;
    // 現フレームと前フレームのジッタ差分（NDC）。
    // モーションベクターは「ジッタ込みのクリップ座標」から作られるため、
    // ジッタ分を引かないと静止時でも毎フレーム再投影がぶれる。
    float2 gJitterDelta;
    float gBlendAlpha;     // 現フレームの寄与率（0.1 = 履歴 90%）
    float gClampScale;     // 近傍 AABB の拡張率（大きいほどゴースト寄り・小さいほどちらつき寄り）
    float gDisableHistory; // 1.0 で履歴を完全無効化（初回フレーム・リサイズ直後）
    float gBlendAlphaMax;  // 履歴が現フレームと食い違う画素で使う寄与率の上限
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

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    const int2 coord = int2(input.position.xy);
    const float3 current = gSceneColor.Load(int3(coord, 0)).rgb;

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

    const int2 screenMax = int2(gScreenSize) - int2(1, 1);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 nc = clamp(coord + int2(dx, dy), int2(0, 0), screenMax);
            const float3 n = RGBToYCoCg(gSceneColor.Load(int3(nc, 0)).rgb);
            neighborMin = min(neighborMin, n);
            neighborMax = max(neighborMax, n);
        }
    }

    // 箱をわずかに広げると、収束が速くなる代わりにゴーストが出やすくなる。
    // gClampScale はそのトレードオフを実行時に振るためのつまみ。
    {
        const float3 center = 0.5f * (neighborMax + neighborMin);
        const float3 extent = 0.5f * (neighborMax - neighborMin) * gClampScale;
        neighborMin = center - extent;
        neighborMax = center + extent;
    }

    // ===== モーションベクターで前フレームへ再投影 =====
    // MV は NDC 差分。ジッタ分を除いた「本当の動き」だけを使う。
    const float2 motion = gMotionVector.Load(int3(coord, 0)) - gJitterDelta;

    // 現在の UV から NDC 差分を UV 差分へ変換して引く（NDC Y は画面 Y と逆向き）
    const float2 currentUV = (float2(coord) + 0.5f) / gScreenSize;
    const float2 historyUV = currentUV - motion * float2(0.5f, -0.5f);

    // 再投影先が画面外なら履歴が存在しないので現フレームを採用する
    if (any(historyUV < 0.0f) || any(historyUV > 1.0f))
    {
        output.color = float4(current, 1.0f);
        return output;
    }

    const float3 historyRaw = gHistoryColor.SampleLevel(gSampler, historyUV, 0.0f).rgb;

    // ===== 履歴のクリップ =====
    const float3 currentYCoCg = RGBToYCoCg(current);
    const float3 historyClippedYCoCg = ClipToAABB(neighborMin, neighborMax, RGBToYCoCg(historyRaw));
    const float3 historyClipped = YCoCgToRGB(historyClippedYCoCg);

    // ===== 時間的な食い違いに応じて寄与率を上げる =====
    // ★水面がぼける問題の対策★
    // AABB クリップは「箱の外」の履歴しか弾けない。水面のように毎フレーム表面自体が
    // 変わるサーフェスでは、履歴（前フレームの泡）が現在の近傍の箱（水〜泡のレンジ）に
    // すっぽり収まってしまうため一切弾かれず、そのまま 90% の重みで混ざって高周波が
    // 溶ける（実測: TAA 無効 16.3 に対し有効 8.3 まで低下。カメラ静止でも同じ）。
    // ここでは「履歴と現フレームの輝度差」を近傍のコントラストで正規化し、
    // 食い違うほど現フレーム寄りにする。静止した不透明面は差が出ないので
    // 従来どおり gBlendAlpha のまま収束し、AA 品質は落ちない。
    const float neighborLumaExtent = max(neighborMax.x - neighborMin.x, 1.0e-4f);
    const float temporalDisagreement =
        saturate(abs(currentYCoCg.x - historyClippedYCoCg.x) / neighborLumaExtent);
    const float blendAlpha = lerp(gBlendAlpha, gBlendAlphaMax, temporalDisagreement);

    // ===== 蓄積 =====
    // 明るさで重み付けした加重平均にすることで、
    // 1 フレームだけ現れた極端に明るい点が履歴に残り続けるのを防ぐ。
    const float weightCurrent = blendAlpha * TonemapWeight(current);
    const float weightHistory = (1.0f - blendAlpha) * TonemapWeight(historyClipped);
    const float weightSum = max(weightCurrent + weightHistory, 1e-5f);

    const float3 resolved = (current * weightCurrent + historyClipped * weightHistory) / weightSum;

    output.color = float4(resolved, 1.0f);
    return output;
}
