/// @file NormalMap.hlsli
/// @brief 法線マップ（緑 = 画像の上。glTF と同じ形式）を読む関数と接空間の軸
/// @details 画面の微分を使わないので、どのステージ（VS/PS/CS/lib）からでも include できる。

#ifndef NORMAL_MAP_HLSLI
#define NORMAL_MAP_HLSLI

/// @brief 法線マップの画素（0〜1）を接空間の向き（-1〜1）にする。z は x・y から作る（BC5 は赤と緑だけ）
float3 UnpackNormalMap(float4 texel)
{
    const float2 xy = texel.rg * 2.0f - 1.0f;
    return float3(xy, sqrt(saturate(1.0f - dot(xy, xy))));
}

/// @brief 従法線の符号。cross(N, T) に掛けると画像の上（v が減る向き）を向く
/// @param N    法線
/// @param T    接線（+u の向き）
/// @param dp1  面の上の差 1（位置）
/// @param dp2  面の上の差 2（位置）
/// @param duv1 差 1 の UV
/// @param duv2 差 2 の UV
/// @return +1 か -1。UV が潰れていて決まらなければ -1
float NormalMapBitangentSign(float3 N, float3 T, float3 dp1, float3 dp2, float2 duv1, float2 duv2)
{
    const float det = duv1.x * duv2.y - duv1.y * duv2.x;
    const float3 imageUpTimesDet = dp1 * duv2.x - dp2 * duv1.x;
    return dot(cross(N, T), imageUpTimesDet) * det > 0.0f ? 1.0f : -1.0f;
}

/// @brief 接空間の軸（x = 接線・y = 画像の上・z = 法線）。接線は法線に直交させる
/// @param bitangentSign NormalMapBitangentSign の値
float3x3 NormalMapTangentFrame(float3 normal, float3 tangent, float bitangentSign)
{
    const float3 N = normalize(normal);
    float3 T = normalize(tangent);
    T = normalize(T - dot(T, N) * N);
    return float3x3(T, cross(N, T) * bitangentSign, N);
}

#endif // NORMAL_MAP_HLSLI
