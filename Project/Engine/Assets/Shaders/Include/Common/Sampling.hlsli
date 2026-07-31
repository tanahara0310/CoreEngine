/// @file Sampling.hlsli
/// @brief 準乱数列と重要度サンプリングの共通実装
/// @details Hammersley 点列と GGX 重要度サンプリングは IBL・大気プリフィルタ・SSAO で
///          まったく同じコードが複製されていた。ここに集約する。

#ifndef SAMPLING_HLSLI
#define SAMPLING_HLSLI

#include "ShaderMath.hlsli"

// ===================================================================
// 低不一致列（Hammersley）
// ===================================================================
/// @brief Van der Corput 列（基数2のビット反転）
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

/// @brief Hammersley 点列の i 番目（全 N 点）を返す
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// ===================================================================
// 接空間の基底
// ===================================================================
/// @brief 法線から正規直交基底（接線・従法線）を構築する
/// @param N 単位法線
/// @param tangent 出力: N に直交する接線
/// @param bitangent 出力: N・tangent に直交する従法線
void BuildOrthonormalBasis(float3 N, out float3 tangent, out float3 bitangent)
{
    // N が Z 軸とほぼ平行なときは cross が退化するので基準軸を切り替える
    float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(up, N));
    bitangent = cross(N, tangent);
}

// ===================================================================
// 重要度サンプリング
// ===================================================================
/// @brief GGX（Trowbridge-Reitz）分布に従うハーフベクトルをサンプリングする
/// @param Xi [0,1]^2 の一様乱数
/// @param N 法線ベクトル（ワールド空間）
/// @param roughness 粗さ [0,1]
/// @return ワールド空間のハーフベクトル
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;

    // 球面座標での角度計算
    float phi = TWO_PI * Xi.x;
    float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

    // 接空間でのハーフベクトル
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // 接空間 → ワールド空間
    float3 tangent, bitangent;
    BuildOrthonormalBasis(N, tangent, bitangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

/// @brief コサイン重み付き半球サンプリング（接空間・+Z が法線）
/// @param xi [0,1]^2 の一様乱数
float3 HemisphereSampleCosine(float2 xi)
{
    float phi = TWO_PI * xi.x;
    float cosTheta = sqrt(1.0f - xi.y);
    float sinTheta = sqrt(xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

#endif // SAMPLING_HLSLI
