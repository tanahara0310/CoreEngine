/// @file GodRayComposite.CS.hlsl
/// @brief 半解像度ゴッドレイバッファをフル解像度 SceneColor へ合成する
/// @details 式: 出力 = max(シーン色 + Δ輝度, 0)。Δ は負を含む差分（雲影で暗く・
///          切れ間は変化なし）＋ 加算ミー項。CloudComposite と同じ
///          中心 + 対角 4 タップのテントアップサンプルで半解像度の階段を均す。
///          gOutput は SceneColor 自身。各スレッドが自分のテクセルだけを読んで書き戻す。

#include "Common/GodRayCommon.hlsli"

ConstantBuffer<GodRayConstants> gGodRay : register(b0);
Texture2D<float4> gGodRayBuffer : register(t0);
SamplerState gLUTSampler : register(s0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gOutput.GetDimensions(width, height);
    if (dtid.x >= width || dtid.y >= height)
    {
        return;
    }

    float4 scene = gOutput[dtid.xy];
    float2 uv = (float2(dtid.xy) + 0.5f) / float2(width, height);

    // テントアップサンプル（gLUTSampler は CLAMP のため画面端の巻き込みは起きない）
    float2 texel = 1.0f / float2(gGodRay.outputWidth, gGodRay.outputHeight);
    float3 delta = gGodRayBuffer.SampleLevel(gLUTSampler, uv, 0).rgb * 0.5f;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        float2 offset = float2((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f) * texel;
        delta += gGodRayBuffer.SampleLevel(gLUTSampler, uv + offset, 0).rgb * 0.125f;
    }

    // 差分は負を含むため、アンダーシュート保険としてクランプする
    gOutput[dtid.xy] = float4(max(scene.rgb + delta, 0.0f), scene.a);
}
