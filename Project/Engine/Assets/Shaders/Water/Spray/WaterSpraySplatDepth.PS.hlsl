// ============================================================
// 流体描画：しぶきの球の手前の面の深度
// ------------------------------------------------------------
// 深度バッファで一番手前の球を残し、カメラの前方向の距離を書く。シーンの物の陰になる所は描かない
// ============================================================

#include "WaterSprayView.hlsli"

struct WaterSpraySplatVSOutput
{
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;
    float viewDepth : TEXCOORD1;
    float radius : TEXCOORD2;
    float2 motion : TEXCOORD3;
};

struct WaterSpraySplatDepthOutput
{
    float viewDepth : SV_TARGET0;
    float depth : SV_Depth;
};

WaterSpraySplatDepthOutput main(WaterSpraySplatVSOutput input)
{
    const float r2 = dot(input.corner, input.corner);
    if (r2 >= 1.0f)
    {
        discard;
    }
    const float viewDepth = input.viewDepth - sqrt(1.0f - r2) * input.radius;
    const float depth = ViewDepthToSprayNdcDepth(viewDepth);
    if (depth >= LoadSpraySceneDepth(int2(input.position.xy)))
    {
        discard;
    }

    WaterSpraySplatDepthOutput output;
    output.viewDepth = viewDepth;
    output.depth = depth;
    return output;
}
