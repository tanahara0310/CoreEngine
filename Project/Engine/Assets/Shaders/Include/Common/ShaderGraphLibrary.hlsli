// ============================================================
// シェーダーグラフのノードが使う関数
// ============================================================
#ifndef SHADER_GRAPH_LIBRARY_HLSLI
#define SHADER_GRAPH_LIBRARY_HLSLI

static const float SG_TwoPi = 6.28318530718f;

/// @brief 2 次元の点から 0〜1 の乱数 1 つ
float SG_Hash21(float2 p)
{
    float3 q = frac(float3(p.xyx) * 0.1031f);
    q += dot(q, q.yzx + 33.33f);
    return frac((q.x + q.y) * q.z);
}

/// @brief 2 次元の点から 0〜1 の乱数 2 つ
float2 SG_Hash22(float2 p)
{
    float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    q += dot(q, q.yzx + 33.33f);
    return frac((q.xx + q.yz) * q.zy);
}

/// @brief 格子点の乱数をなめらかにつないだノイズ（0〜1）
float SG_ValueNoise(float2 p)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = SG_Hash21(cell);
    float b = SG_Hash21(cell + float2(1.0f, 0.0f));
    float c = SG_Hash21(cell + float2(0.0f, 1.0f));
    float d = SG_Hash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

/// @brief 細かさの違う値のノイズを 3 枚重ねたもの（0〜1）
float SG_SimpleNoise(float2 uv, float scale)
{
    float2 p = uv * scale;
    float value = SG_ValueNoise(p * 0.25f) * 0.5f;
    value += SG_ValueNoise(p * 0.5f) * 0.25f;
    value += SG_ValueNoise(p) * 0.125f;
    return value / 0.875f;
}

/// @brief 格子点の向きの乱数
float2 SG_GradientDirection(float2 cell)
{
    float angle = SG_Hash21(cell) * SG_TwoPi;
    return float2(cos(angle), sin(angle));
}

/// @brief 格子点の向きをなめらかにつないだノイズ（おおむね 0〜1）
float SG_GradientNoise(float2 uv, float scale)
{
    float2 p = uv * scale;
    float2 cell = floor(p);
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float a = dot(SG_GradientDirection(cell), f);
    float b = dot(SG_GradientDirection(cell + float2(1.0f, 0.0f)), f - float2(1.0f, 0.0f));
    float c = dot(SG_GradientDirection(cell + float2(0.0f, 1.0f)), f - float2(0.0f, 1.0f));
    float d = dot(SG_GradientDirection(cell + float2(1.0f, 1.0f)), f - float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) + 0.5f;
}

/// @brief 細胞の模様（いちばん近い点までの距離と、その細胞の乱数）
void SG_Voronoi(float2 uv, float angleOffset, float cellDensity, out float distanceOut, out float cellsOut)
{
    float2 p = uv * cellDensity;
    float2 cell = floor(p);
    float2 f = frac(p);
    distanceOut = 8.0f;
    cellsOut = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 lattice = float2(x, y);
            float2 random = SG_Hash22(cell + lattice);
            float2 offset = 0.5f + 0.5f * sin(random * SG_TwoPi + angleOffset);
            float d = distance(lattice + offset, f);
            if (d < distanceOut)
            {
                distanceOut = d;
                cellsOut = random.x;
            }
        }
    }
}

/// @brief UV を center のまわりに rotation（ラジアン）回す
float2 SG_Rotate(float2 uv, float2 center, float rotation)
{
    float s = sin(rotation);
    float c = cos(rotation);
    float2 p = uv - center;
    return float2(p.x * c - p.y * s, p.x * s + p.y * c) + center;
}

/// @brief UV を center からの距離（x）と角度（y・1 周で 1）にする
float2 SG_PolarCoordinates(float2 uv, float2 center, float radialScale, float lengthScale)
{
    float2 delta = uv - center;
    float radius = length(delta) * 2.0f * radialScale;
    float angle = atan2(delta.y, delta.x) / SG_TwoPi * lengthScale;
    return float2(radius, angle);
}

/// @brief UV の真ん中の楕円（内側が 1・縁は 1 px でなめらかに）
float SG_Ellipse(float2 uv, float width, float height)
{
    float d = length((uv * 2.0f - 1.0f) / max(float2(width, height), 1e-5f));
    return saturate((1.0f - d) / max(fwidth(d), 1e-5f));
}

/// @brief UV の真ん中の長方形（内側が 1・縁は 1 px でなめらかに）
float SG_Rectangle(float2 uv, float width, float height)
{
    float2 d = abs(uv * 2.0f - 1.0f) - float2(width, height);
    d = 1.0f - d / max(fwidth(d), 1e-5f);
    return saturate(min(d.x, d.y));
}

#endif // SHADER_GRAPH_LIBRARY_HLSLI
