// ============================================================
// 流体描画：ならした深度から水の面を作ってシーンへ合成する
// ------------------------------------------------------------
// 深度の差から法線を求め、後ろの景色の屈折・水の吸収・空気を含んだ白さ・空の映り込み・太陽のハイライトを重ねる。
// 厚みの薄い縁ほど後ろの景色を残す。モーションベクターはしぶきの動きの平均を書く
// ============================================================

#include "WaterSprayView.hlsli"
#include "../../PostEffect/FullScreen.hlsli"

Texture2D<float> gSprayDepth : register(t0);
Texture2D<float4> gSprayThickness : register(t1);
Texture2D<float4> gSprayBackground : register(t2);

struct WaterSprayCompositeOutput
{
    float4 color : SV_TARGET0;
    float4 motionVector : SV_TARGET1; ///< rg = NDC の動き、b = 1（水面と同じ扱い）
};

/// @brief 厚みが全部を覆うようになる厚み [m]
static const float kSprayEdgeThickness = 0.04f;
/// @brief 厚み 1m あたりの白さの増え方 [1/m]
static const float kSprayAeration = 12.0f;
/// @brief 屈折で後ろの景色をずらす長さ（厚みに対する割合）
static const float kSprayRefraction = 0.25f;
/// @brief 水の面の粗さ
static const float kSprayRoughness = 0.35f;
/// @brief 水の垂直入射の反射率
static const float kSprayF0 = 0.02f;

/// @brief 画素と、カメラの前方向の距離から、ビュー空間の位置（x 右・y 上・z 前）
float3 ComputeSprayViewPosition(int2 pixel, float viewDepth)
{
    const float2 ndc = ScreenUVToNDC((float2(pixel) + 0.5f) / gScreenSize);
    return float3(ndc.x / gProjScaleX * viewDepth, ndc.y / gProjScaleY * viewDepth, viewDepth);
}

/// @brief 隣の画素を同じ面とみなす、深さの差の上限（1 画素の幅に対する倍率）
static const float kSprayMaxDepthSlope = 4.0f;

/// @brief 1 軸の傾き。両隣のうち深さの差の小さい方を使い、深さが大きく跳ぶ隣は別の塊として使わない
float3 ComputeSprayDerivative(int2 pixel, int2 axis, float3 center)
{
    const int2 maxPixel = int2(gScreenSize) - 1;
    const int2 afterPixel = clamp(pixel + axis, int2(0, 0), maxPixel);
    const int2 beforePixel = clamp(pixel - axis, int2(0, 0), maxPixel);
    const float afterDepth = gSprayDepth.Load(int3(afterPixel, 0));
    const float beforeDepth = gSprayDepth.Load(int3(beforePixel, 0));
    const float pixelWidth = 2.0f * center.z / (gProjScaleY * gScreenSize.y);
    const float maxJump = kSprayMaxDepthSlope * pixelWidth + 0.02f;
    const bool hasAfter = afterDepth > 0.0f && abs(afterDepth - center.z) < maxJump;
    const bool hasBefore = beforeDepth > 0.0f && abs(beforeDepth - center.z) < maxJump;
    const float3 afterDiff = hasAfter ? ComputeSprayViewPosition(afterPixel, afterDepth) - center : float3(0.0f, 0.0f, 0.0f);
    const float3 beforeDiff = hasBefore ? center - ComputeSprayViewPosition(beforePixel, beforeDepth) : float3(0.0f, 0.0f, 0.0f);
    if (hasAfter && (!hasBefore || abs(afterDiff.z) < abs(beforeDiff.z)))
    {
        return afterDiff;
    }
    if (hasBefore)
    {
        return beforeDiff;
    }
    // 同じ面の隣が無いときはカメラへ向いた面とみなす
    return axis.x != 0 ? float3(pixelWidth, 0.0f, 0.0f) : float3(0.0f, -pixelWidth, 0.0f);
}

WaterSprayCompositeOutput main(VertexShaderOutput input)
{
    const int2 pixel = int2(input.position.xy);
    const float viewDepth = gSprayDepth.Load(int3(pixel, 0));
    const float4 accumulated = gSprayThickness.Load(int3(pixel, 0));
    const float thickness = accumulated.r;
    if (viewDepth <= 0.0f || thickness <= 1.0e-4f)
    {
        discard;
    }

    // ---- 面の位置と法線 ----
    const float3 center = ComputeSprayViewPosition(pixel, viewDepth);
    const float3 ddxView = ComputeSprayDerivative(pixel, int2(1, 0), center);
    const float3 ddyView = ComputeSprayDerivative(pixel, int2(0, 1), center);
    float3 normalView = normalize(cross(ddxView, ddyView));
    if (normalView.z > 0.0f)
    {
        normalView = -normalView;
    }
    const float3 normal = normalize(
        normalView.x * gCameraRight + normalView.y * gCameraUp + normalView.z * gCameraForward);
    const float3 worldPosition = gCameraPosition
        + center.x * gCameraRight + center.y * gCameraUp + center.z * gCameraForward;
    const float3 toCamera = normalize(gCameraPosition - worldPosition);
    const float ndotv = saturate(dot(normal, toCamera));
    const float fresnel = kSprayF0 + (1.0f - kSprayF0) * pow(1.0f - ndotv, 5.0f);

    // ---- 透過：法線と厚みの分だけずらした後ろの景色を、水の吸収で減らす ----
    const float pixelsPerMeter = 0.5f * gProjScaleY * gScreenSize.y / max(viewDepth, 0.1f);
    const float2 refractPixels = float2(normalView.x, -normalView.y)
        * min(thickness, 0.6f) * kSprayRefraction * pixelsPerMeter;
    const float2 uv = (float2(pixel) + 0.5f) / gScreenSize;
    const float3 behind = gSprayBackground.SampleLevel(gLinearClamp, uv + refractPixels / gScreenSize, 0.0f).rgb;
    const float3 transmitted = behind * exp(-gWaterAbsorption * thickness);

    // ---- 空気を含んだしぶきの白さ（厚いほど白い）----
    const float aeration = 1.0f - exp(-kSprayAeration * thickness);
    const float3 body = lerp(transmitted, ComputeSprayFoamColor(normal), aeration);

    // ---- 表面の反射：空の映り込みと太陽のハイライト ----
    const float3 reflection = SampleSpraySky(reflect(-toCamera, normal), kSprayRoughness);
    const float3 specular = EvaluateSprayDirectSpecular(normal, toCamera, kSprayRoughness, kSprayF0);
    const float3 spray = body * (1.0f - fresnel) + reflection * fresnel + specular;

    // ---- 薄い縁ほど後ろの景色を残す ----
    const float4 original = gSprayBackground.Load(int3(pixel, 0));
    const float coverage = saturate(thickness / kSprayEdgeThickness);

    WaterSprayCompositeOutput output;
    output.color = float4(lerp(original.rgb, spray, coverage), original.a);
    output.motionVector = float4(accumulated.gb / thickness, 1.0f, 0.0f);
    return output;
}
