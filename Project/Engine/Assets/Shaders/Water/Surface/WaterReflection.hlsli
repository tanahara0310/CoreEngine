// ============================================================
// 水面の反射（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// RT 反射の画素ならし・空キューブマップへのフォールバック・反射の輝度圧縮と、
// 太陽のスペキュラのきらめき（解析的 GGX）。
// ============================================================
#ifndef WATER_REFLECTION_INCLUDED
#define WATER_REFLECTION_INCLUDED

#include "WaterSurfaceResources.hlsli"
#include "WaterFoamAppearance.hlsli" // kFoamGlintRoughness
#include "../Common/WaterPhysics.hlsli"
#include "../Common/WaterAppearanceConstants.hlsli"

/// @brief 反射テクスチャを引くタップの間隔（テクセル）
/// @warning 2 テクセルを超えると、ずれた像が離散的に重なって見える
static const float kWaterReflectionTapSpacingTexels = 1.0f;

/// @brief 反射テクスチャを 3×3 のテントでならして取得する（rgb=色 / a=信頼度）
/// @param screenUV スクリーンUV
/// @param grazing  かすめ具合 = 1 - cosθ（大きいほど縦の間隔を広げる）
/// @details 信頼度（a）はぼかさず中心タップの値を返す。
///          rgb は各タップ自身の信頼度で重み付けして平均する（非水面のタップは入れない）。
float4 SampleGlossyReflectionRGBA(float2 screenUV, float grazing)
{
    uint reflWidth = 1;
    uint reflHeight = 1;
    gReflectionTexture.GetDimensions(reflWidth, reflHeight);
    const float2 texel = 1.0f / float2(reflWidth, reflHeight);

    // かすめ角ほど縦の間隔を 1.5 倍まで広げる（双線形で行の間を補間する）
    const float2 radius = kWaterReflectionTapSpacingTexels * texel
        * float2(1.0f, 1.0f + 0.5f * saturate(grazing));

    const float2 kOffsets[9] = {
        float2( 0.0f,  0.0f),
        float2(-1.0f, -1.0f), float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
        float2( 0.0f, -1.0f), float2( 0.0f,  1.0f),
        float2(-1.0f,  0.0f), float2( 1.0f,  0.0f)
    };
    const float kWeights[9] = { 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 2.0f };

    float3 colorSum = float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    float centerConfidence = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        const float2 uv = saturate(screenUV + kOffsets[i] * radius);
        const float4 tap = gReflectionTexture.Sample(gLinearClamp, uv);
        const float tapConfidence = saturate((tap.a - kRTSuccessRangeMin) * 2.0f);
        if (i == 0)
        {
            centerConfidence = tapConfidence;
        }
        const float weight = kWeights[i] * tapConfidence;
        colorSum += tap.rgb * weight;
        weightSum += weight;
    }

    // 全タップが非水面なら色は無効。信頼度 0 で呼び出し側のフォールバックへ渡す。
    if (weightSum <= 1.0e-5f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return float4(colorSum / weightSum, kRTSuccessRangeMin + 0.5f * centerConfidence);
}

/// @brief 水面の反射色を求める
/// @param cosTheta     視線と面法線のなす角の余弦
/// @param viewDir      水面 → カメラ
/// @param normal       面法線
/// @param reflectionMeanSquareSlope 面法線に入っていない傾きの平均二乗傾斜（空キューブマップのミップ選択に使う）
/// @param forwardColor フォワード PBR の色（反射が無効なフレームの反射色）
/// @details RT 反射の alpha は反射色の信頼度（(0.5, 1] が成功、0.5 未満が失敗）。
///          空キューブマップとは信頼度で連続にブレンドする。
///          空は RT パスがトレースしたレイの向きで解決しているので、ここで空を引くのは
///          空キューブマップが RT パスへ渡っていないフレームの保険（そのときも波の法線で引く）。
float3 ResolveWaterReflection(
    float2 screenUV, float cosTheta, float3 viewDir, float3 normal,
    float reflectionMeanSquareSlope, float3 forwardColor)
{
    float3 reflectColor = forwardColor;
    if (gReflectionEnabled)
    {
        const float4 rtReflection =
            SampleGlossyReflectionRGBA(screenUV, saturate(1.0f - cosTheta));
        const float rtConfidence = saturate((rtReflection.a - 0.5f) * 2.0f);

        float3 fallbackReflectColor = reflectColor;
        if (gSkyEnvReflectionEnabled != 0)
        {
            const float envMip = AddSlopeVarianceToRoughness(
                kWaterReflectionMicroRoughness, reflectionMeanSquareSlope) * (kWaterSkyEnvMipCount - 1.0f);
            float3 envReflectDir = reflect(-viewDir, normal);
            envReflectDir.y = max(abs(envReflectDir.y), 0.01f);
            fallbackReflectColor =
                gSkyEnvironmentMap.SampleLevel(gLinearClamp, envReflectDir, envMip).rgb;
        }

        reflectColor = lerp(fallbackReflectColor, rtReflection.rgb, rtConfidence);

        // ---- 反射の輝度圧縮 ----
        // 膝を超えた輝度だけショルダー圧縮する（色相保持）
        const float kReflectionCompressKnee = 2.0f;
        const float kReflectionCompressMax = 6.0f;
        float reflLuma = Luminance(reflectColor);
        if (reflLuma > kReflectionCompressKnee)
        {
            float excessLuma = reflLuma - kReflectionCompressKnee;
            float compressedLuma = kReflectionCompressKnee
                + excessLuma / (1.0f + excessLuma / kReflectionCompressMax);
            reflectColor *= compressedLuma / max(reflLuma, 1.0e-5f);
        }
    }
    return reflectColor;
}

// ===== 太陽のスペキュラのきらめき（解析的 GGX）=====
// 反射が有効なとき、反射色はフォワード PBR の太陽のスペキュラを含まない反射像で置き換わり、
// 反射像の太陽はぼかしと輝度圧縮で消える。太陽の鏡面反射だけをここで解析的に求め、
// フレネル合成の後に加算する。空・環境光は反射像の側にだけ含まれる。
float3 ComputeSunGlintSpecular(
    float3 normal, float3 viewDir, float foamCoverage, float mainLightVisibility, float unresolvedMeanSquareSlope)
{
    const float3 kWaterF0 = kWaterFresnelF0.xxx;
    // グリッターの広がり。マテリアルのラフネスと連動させる
    // （小さいほど鋭く狭いきらめき、大きいほど広く柔らかい光の帯）。
    // 泡域は微細気泡の散乱でハイライトが大きく柔らかくなるためラフネスを引き上げる
    // （泡被覆による輝度の抑制は呼び出し側の (1-coverage) 倍が担当する）。
    // 遠方で法線から外した細かい波の傾きは、1 ピクセルの中の凹凸としてラフネスへ足す
    const float glintRoughness = max(AddSlopeVarianceToRoughness(
        lerp(gMaterial.roughness, kFoamGlintRoughness, saturate(foamCoverage)),
        unresolvedMeanSquareSlope), 0.04f);

    float3 totalGlint = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float3 lightVec = -normalize(gDirectionalLights[i].direction);
        float ndotl = saturate(dot(normal, lightVec));
        if (ndotl <= 0.0f)
        {
            continue;
        }
        float3 brdf = CookTorranceBRDF(normal, viewDir, lightVec, glintRoughness, kWaterF0);
        const float visibility = (i == 0) ? mainLightVisibility : 1.0f;
        totalGlint += brdf * gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * ndotl
            * visibility;
    }
    return totalGlint;
}

#endif // WATER_REFLECTION_INCLUDED
