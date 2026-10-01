#ifndef WATER_SUN_TRANSMISSION_HLSLI
#define WATER_SUN_TRANSMISSION_HLSLI

// ============================================================
// 水面を通って水中の受光面へ届く日光（RT コースティクスと RT 屈折のヒットシェーディングで共有）
// ============================================================

/// @brief Schlick 近似のフレネル透過率 (1 - F) を返す
/// @param cosIncident     入射角の余弦（dot(-光の進行方向, 水面法線)）
/// @param refractiveIndex 水の屈折率
float FresnelTransmittanceSchlick(float cosIncident, float refractiveIndex)
{
    const float f0 = ((refractiveIndex - 1.0f) / (refractiveIndex + 1.0f))
                   * ((refractiveIndex - 1.0f) / (refractiveIndex + 1.0f));
    const float oneMinusCos = 1.0f - saturate(cosIncident);
    const float oneMinusCos2 = oneMinusCos * oneMinusCos;
    return 1.0f - (f0 + (1.0f - f0) * oneMinusCos2 * oneMinusCos2 * oneMinusCos);
}

/// @brief 受光面の幾何項を、空中と同じ角度応答から屈折後の幾何へ切り替え終える水深 [m]
/// @details 幾何項は水深 0 で G(0) = dot(N, -L) / (-L.y)、この水深より深い所で G = dot(N, -r) / (-r.y)
///          （L は光の進む向き、r は屈折後の光の向き）。その間は水深で補間する
static const float kGeometryBlendDepthMeters = 0.6f;

/// @brief 空中（屈折なし）と同じ角度応答の幾何項
float ComputeAerialGeometryFactor(float3 receiverNormal, float3 lightDir)
{
    return saturate(dot(receiverNormal, -lightDir)) / max(-lightDir.y, 0.05f);
}

/// @brief 屈折後の幾何項をどれだけ採用するか（水深 0 で 0、kGeometryBlendDepthMeters で 1）
float ComputeRefractedGeometryWeight(float submergedDepth)
{
    return smoothstep(0.0f, kGeometryBlendDepthMeters, submergedDepth);
}

#endif // WATER_SUN_TRANSMISSION_HLSLI
