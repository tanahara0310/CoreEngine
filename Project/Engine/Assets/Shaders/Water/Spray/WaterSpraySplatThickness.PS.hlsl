// ============================================================
// 流体描画：しぶきの厚みと動きを足し合わせる
// ------------------------------------------------------------
// 出力は (厚み, 厚み × 動き x, 厚み × 動き y)。加算で重ねる。シーンの物の陰になる所は足さない
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

float4 main(WaterSpraySplatVSOutput input) : SV_TARGET0
{
    const float r2 = dot(input.corner, input.corner);
    if (r2 >= 1.0f)
    {
        discard;
    }
    const float nz = sqrt(1.0f - r2);
    const float depth = ViewDepthToSprayNdcDepth(input.viewDepth - nz * input.radius);
    if (depth >= LoadSpraySceneDepth(int2(input.position.xy)))
    {
        discard;
    }

    const float thickness = 2.0f * nz * input.radius * kSpraySplatThicknessScale;
    return float4(thickness, thickness * input.motion.x, thickness * input.motion.y, 1.0f);
}
