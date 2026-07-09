/// @file CloudNoiseCommon.hlsli
/// @brief ボリューメトリック雲用のタイル可能ノイズ生成ユーティリティ
/// @details Perlin-Worley 方式（Hillaire "TileableVolumeNoise" 相当）。
///          タイル可能性は「整数格子を周波数でラップ」して実現する。
///          時刻非依存のハッシュを使い、生成結果は再現可能。

#ifndef CLOUD_NOISE_COMMON_HLSLI
#define CLOUD_NOISE_COMMON_HLSLI

// ===== 汎用 =====

/// @brief 雲実装全域で使う値の再マップ（分母 0 に注意して呼ぶこと）
float Remap(float v, float lowOld, float highOld, float lowNew, float highNew)
{
    return lowNew + (v - lowOld) * (highNew - lowNew) / (highOld - lowOld);
}

/// @brief 正の剰余（負値でも周期境界が正しくラップする）
float3 Wrap3(float3 v, float freq)
{
    return v - floor(v / freq) * freq;
}
float2 Wrap2(float2 v, float freq)
{
    return v - floor(v / freq) * freq;
}

// ===== ハッシュ（整数格子座標 → [0,1)） =====
// 入力は必ずラップ済みの整数格子座標。float 系の frac ハッシュ（Dave Hoskins 等）は
// 0〜数十程度の小さな整数入力に対して出力が相関し、Worley のセル中心や Perlin の勾配が
// 規則的に並んでしまう（雲に格子状の穴が見える）。整数専用のビット混合（PCG）を使う。

uint3 PCG3D(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

uint2 PCG2D(uint2 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u; v.y += v.x * 1664525u;
    v ^= v >> 16u;
    v.x += v.y * 1664525u; v.y += v.x * 1664525u;
    v ^= v >> 16u;
    return v;
}

/// @brief 整数格子座標 → [0,1)^3（オフセット 1024 で負の座標にも耐える）
float3 Hash33(float3 gridPos)
{
    uint3 v = uint3(int3(round(gridPos)) + 1024);
    return float3(PCG3D(v)) * (1.0f / 4294967296.0f);
}

/// @brief 整数格子座標 → [0,1)^2
float2 Hash22(float2 gridPos)
{
    uint2 v = uint2(int2(round(gridPos)) + 1024);
    return float2(PCG2D(v)) * (1.0f / 4294967296.0f);
}

// ===== 3D Perlin（勾配ノイズ・タイル可能） =====

float GradientDot3D(float3 cell, float3 offset, float3 f, float freq)
{
    float3 g = Hash33(Wrap3(cell + offset, freq)) * 2.0f - 1.0f;
    return dot(g, f - offset);
}

/// @brief タイル可能 3D Perlin。p は [0,1]、freq は整数。戻り値は概ね [-1,1]。
float PerlinNoise3D(float3 p, float freq)
{
    p *= freq;
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    float n = lerp(
        lerp(
            lerp(GradientDot3D(i, float3(0, 0, 0), f, freq),
                 GradientDot3D(i, float3(1, 0, 0), f, freq), u.x),
            lerp(GradientDot3D(i, float3(0, 1, 0), f, freq),
                 GradientDot3D(i, float3(1, 1, 0), f, freq), u.x), u.y),
        lerp(
            lerp(GradientDot3D(i, float3(0, 0, 1), f, freq),
                 GradientDot3D(i, float3(1, 0, 1), f, freq), u.x),
            lerp(GradientDot3D(i, float3(0, 1, 1), f, freq),
                 GradientDot3D(i, float3(1, 1, 1), f, freq), u.x), u.y),
        u.z);
    return n;
}

/// @brief [0,1] 正規化 Perlin FBM。startFreq は整数、以降オクターブごとに倍化。
float PerlinFBM3D(float3 p, float startFreq, int octaves)
{
    float sum = 0.0f;
    float amp = 1.0f;
    float totalAmp = 0.0f;
    float freq = startFreq;
    [loop] for (int i = 0; i < octaves; ++i)
    {
        sum += amp * (PerlinNoise3D(p, freq) * 0.5f + 0.5f);
        totalAmp += amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return sum / totalAmp;
}

// ===== 3D Worley（セルノイズ・タイル可能） =====

/// @brief タイル可能 3D Worley。戻り値 = 1 - 正規化最近傍距離（セル中心が明るい）。
float WorleyNoise3D(float3 p, float freq)
{
    p *= freq;
    float3 i = floor(p);
    float3 f = frac(p);

    float minDist = 1.0f;
    [loop] for (int x = -1; x <= 1; ++x)
    {
        [loop] for (int y = -1; y <= 1; ++y)
        {
            [loop] for (int z = -1; z <= 1; ++z)
            {
                float3 neighbor = float3(x, y, z);
                float3 cell = i + neighbor;
                float3 featurePoint = Hash33(Wrap3(cell, freq)); // セル内 [0,1] オフセット
                float3 diff = neighbor + featurePoint - f;
                minDist = min(minDist, length(diff));
            }
        }
    }
    return saturate(1.0f - minDist);
}

/// @brief Worley FBM（設計書 5.2 の重み: w(f)*0.625 + w(2f)*0.25 + w(4f)*0.125）
float WorleyFBM3D(float3 p, float freq)
{
    return WorleyNoise3D(p, freq) * 0.625f
         + WorleyNoise3D(p, freq * 2.0f) * 0.25f
         + WorleyNoise3D(p, freq * 4.0f) * 0.125f;
}

/// @brief テクスチャ解像度に応じて安全な最大周波数でクランプする Worley FBM
/// @details WorleyFBM3D は内部で freq, 2freq, 4freq の3オクターブを合成する。
///          このテクスチャ解像度に対して最高オクターブの周波数がナイキスト限界
///          （解像度の半分）に近い・超えると、生成時点でエイリアシングした
///          規則的なパターンがテクセルへ焼き込まれ、レンダリング結果に格子模様として
///          現れる。maxSafeFreq（解像度の 1/4 程度を推奨）でクランプして防ぐ。
float WorleyFBM3D_Safe(float3 p, float freq, float maxSafeFreq)
{
    float f0 = min(freq, maxSafeFreq);
    float f1 = min(freq * 2.0f, maxSafeFreq);
    float f2 = min(freq * 4.0f, maxSafeFreq);
    return WorleyNoise3D(p, f0) * 0.625f
         + WorleyNoise3D(p, f1) * 0.25f
         + WorleyNoise3D(p, f2) * 0.125f;
}

// ===== 2D Perlin（天候マップ用・タイル可能） =====

float GradientDot2D(float2 cell, float2 offset, float2 f, float freq)
{
    float2 g = Hash22(Wrap2(cell + offset, freq)) * 2.0f - 1.0f;
    return dot(g, f - offset);
}

float PerlinNoise2D(float2 p, float freq)
{
    p *= freq;
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    float n = lerp(
        lerp(GradientDot2D(i, float2(0, 0), f, freq),
             GradientDot2D(i, float2(1, 0), f, freq), u.x),
        lerp(GradientDot2D(i, float2(0, 1), f, freq),
             GradientDot2D(i, float2(1, 1), f, freq), u.x), u.y);
    return n;
}

float PerlinFBM2D(float2 p, float startFreq, int octaves)
{
    float sum = 0.0f;
    float amp = 1.0f;
    float totalAmp = 0.0f;
    float freq = startFreq;
    [loop] for (int i = 0; i < octaves; ++i)
    {
        sum += amp * (PerlinNoise2D(p, freq) * 0.5f + 0.5f);
        totalAmp += amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return sum / totalAmp;
}

#endif // CLOUD_NOISE_COMMON_HLSLI
