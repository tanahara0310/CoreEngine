// ============================================================
// しぶきの板（霧と、流体描画を使わないときのしぶき）の色
// ------------------------------------------------------------
// しぶきは球の法線で照らした白い粒、霧は前方へ光を散らす柔らかい円。
// シーンの物に近づくほど薄くする
// ============================================================

#include "WaterSprayView.hlsli"

struct WaterSprayParticleVSOutput
{
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;
    float viewDepth : TEXCOORD1;
    float opacity : TEXCOORD2;
    float radius : TEXCOORD3;
    nointerpolation uint kind : TEXCOORD4;
    float3 worldCenter : TEXCOORD5;
};

/// @brief 物との境目を薄くする距離 [m]
static const float kSpraySoftDistance = 0.4f;

float4 main(WaterSprayParticleVSOutput input) : SV_TARGET0
{
    const float r2 = dot(input.corner, input.corner);
    if (r2 >= 1.0f)
    {
        discard;
    }

    const float sceneViewDepth = LoadSpraySceneViewDepth(int2(input.position.xy));
    const float soft = saturate((sceneViewDepth - input.viewDepth) / max(kSpraySoftDistance, input.radius));
    if (soft <= 0.0f)
    {
        discard;
    }

    const float3 toCamera = normalize(gCameraPosition - input.worldCenter);
    float3 color;
    float alpha;
    if (input.kind == kSprayKindMist)
    {
        const float shape = (1.0f - r2) * (1.0f - r2);
        color = kSprayFoamAlbedo
            * (EvaluateSprayDirectScattering(toCamera, 0.6f) + EvaluateSpraySkyIrradiance(float3(0.0f, 1.0f, 0.0f)));
        alpha = shape;
    }
    else
    {
        const float nz = sqrt(1.0f - r2);
        const float3 normal = normalize(
            input.corner.x * gCameraRight + input.corner.y * gCameraUp - nz * gCameraForward);
        color = ComputeSprayFoamColor(normal)
            + EvaluateSprayDirectSpecular(normal, toCamera, 0.25f, 0.02f);
        alpha = 1.0f - r2 * r2 * r2;
    }
    return float4(color, alpha * input.opacity * soft);
}
