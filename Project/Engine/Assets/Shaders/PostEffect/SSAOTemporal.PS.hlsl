// SSAO テンポラル蓄積
//
// TAA のジッタで深度・法線入力が毎フレーム半ピクセル揺れるため、SSAO の
// 2 値遮蔽判定はカメラが静止していても毎フレーム別の値を出す（＝ちかちか）。
// RTShadowTemporal と同じ考え方で、モーションベクター再投影＋近傍クランプ付きの
// 指数移動平均をかけ、時間方向に安定させる。
//
// SSAO 生成 → このパス → SSAOBlur の順。出力はそのまま次フレームの履歴になる。

#include "FullScreen.hlsli"

Texture2D<float4> gCurrentAO    : register(t0); // 今フレームの生 SSAO
Texture2D<float4> gHistoryAO    : register(t1); // 前フレームの蓄積結果
Texture2D<float2> gMotionVector : register(t2); // NDC 差分（GBuffer 産）

SamplerState gSampler : register(s0); // 履歴のバイリニア再投影用

cbuffer SSAOTemporalParams : register(b0)
{
    float2 gScreenSize;
    float gBlendAlpha;     // 現フレームの寄与率（0.1 = 履歴 90%）
    float gDisableHistory; // 1.0 で履歴無効（初回フレーム・リサイズ直後）
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

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    const int2 coord = int2(input.position.xy);
    const float current = gCurrentAO.Load(int3(coord, 0)).r;

    if (gDisableHistory > 0.5f)
    {
        output.color = float4(current, current, current, 1.0f);
        return output;
    }

    // 近傍 3x3 の範囲を取る。履歴をこの範囲へクランプすることで、
    // ディスオクルージョン（再投影が外れた画素）の古い AO を棄却する
    float aoMin = 1.0f;
    float aoMax = 0.0f;
    const int2 screenMax = int2(gScreenSize) - int2(1, 1);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 nc = clamp(coord + int2(dx, dy), int2(0, 0), screenMax);
            const float n = gCurrentAO.Load(int3(nc, 0)).r;
            aoMin = min(aoMin, n);
            aoMax = max(aoMax, n);
        }
    }

    // モーションベクターで前フレームへ再投影（TAA.PS.hlsl と同じ規約）
    const float2 motion = gMotionVector.Load(int3(coord, 0));
    const float2 currentUV = (float2(coord) + 0.5f) / gScreenSize;
    const float2 historyUV = currentUV - motion * float2(0.5f, -0.5f);

    if (any(historyUV < 0.0f) || any(historyUV > 1.0f))
    {
        output.color = float4(current, current, current, 1.0f);
        return output;
    }

    const float history = clamp(gHistoryAO.SampleLevel(gSampler, historyUV, 0.0f).r, aoMin, aoMax);
    const float resolved = lerp(history, current, gBlendAlpha);

    output.color = float4(resolved, resolved, resolved, 1.0f);
    return output;
}
