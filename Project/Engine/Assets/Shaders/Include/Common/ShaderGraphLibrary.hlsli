// ============================================================
// シェーダーグラフのノードが使う関数
// ============================================================
#ifndef SHADER_GRAPH_LIBRARY_HLSLI
#define SHADER_GRAPH_LIBRARY_HLSLI

#include "SceneDepth.hlsli"

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

/// @brief 法線マップの色（0〜1）を接空間の向き（-1〜1）にする
float3 SG_NormalUnpack(float4 color)
{
    return color.rgb * 2.0f - 1.0f;
}

/// @brief 接空間の法線の傾きを strength 倍にする（0 で平ら）
float3 SG_NormalStrength(float3 normal, float strength)
{
    return float3(normal.xy * strength, lerp(1.0f, normal.z, saturate(strength)));
}

/// @brief 接空間の法線 2 つを重ねる（Whiteout）
float3 SG_NormalBlend(float3 a, float3 b)
{
    return normalize(float3(a.xy + b.xy, a.z * b.z));
}

/// @brief 面を斜めから見るほど 1 に近づく値
float SG_FresnelEffect(float3 normal, float3 viewDirection, float power)
{
    return pow(max(1.0f - saturate(dot(normalize(normal), normalize(viewDirection))), 1e-6f), power);
}

/// @brief 3 方向から貼ったテクスチャの色（Position × Tile の zy・xz・xy で読み、法線の向きで混ぜる）
float4 SG_Triplanar(Texture2D<float4> tex, SamplerState samplerState, float3 position, float3 normal, float tile,
    float blend)
{
    float3 uv = position * tile;
    float3 weight = pow(abs(normal), blend);
    weight /= max(dot(weight, 1.0f), 1e-5f);
    float4 x = tex.Sample(samplerState, uv.zy);
    float4 y = tex.Sample(samplerState, uv.xz);
    float4 z = tex.Sample(samplerState, uv.xy);
    return x * weight.x + y * weight.y + z * weight.z;
}

/// @brief 3 方向から貼った法線マップの向き（ワールド・正規化済み）
float3 SG_TriplanarNormal(Texture2D<float4> tex, SamplerState samplerState, float3 position, float3 normal, float tile,
    float blend)
{
    float3 uv = position * tile;
    float3 weight = max(pow(abs(normal), blend), 0.0f);
    weight /= max(dot(weight, 1.0f), 1e-5f);
    float3 x = tex.Sample(samplerState, uv.zy).rgb * 2.0f - 1.0f;
    float3 y = tex.Sample(samplerState, uv.xz).rgb * 2.0f - 1.0f;
    float3 z = tex.Sample(samplerState, uv.xy).rgb * 2.0f - 1.0f;
    x = float3(x.xy + normal.zy, abs(x.z) * normal.x);
    y = float3(y.xy + normal.xz, abs(y.z) * normal.y);
    z = float3(z.xy + normal.xy, abs(z.z) * normal.z);
    return normalize(x.zyx * weight.x + y.xzy * weight.y + z.xyz * weight.z);
}

/// @brief ワールドの向きを、UV の向きの軸（x = +u・y = +v・z = 面の法線）で表す（軸は画面の微分から作る）
float3 SG_DirectionInUVSpace(float3 directionWS, float3 positionWS, float3 normalWS, float2 uv)
{
    float3 N = normalize(normalWS);
    float3 dp1 = ddx(positionWS);
    float3 dp2 = ddy(positionWS);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float orientation = dot(N, cross(dp1, dp2)) < 0.0f ? -1.0f : 1.0f;
    float3 axisU = (dp2perp * duv1.x + dp1perp * duv2.x) * orientation;
    float3 axisV = (dp2perp * duv1.y + dp1perp * duv2.y) * orientation;
    axisU *= rsqrt(max(dot(axisU, axisU), 1e-20f));
    axisV *= rsqrt(max(dot(axisV, axisV), 1e-20f));
    return float3(dot(directionWS, axisU), dot(directionWS, axisV), dot(directionWS, N));
}

/// @brief 高さのテクスチャで UV を 1 回ずらす（Amplitude は 0.01 倍で効く）
/// @param viewDirUV 面からカメラへの向き（SG_DirectionInUVSpace で UV の軸に直したもの）
float2 SG_ParallaxMapping(Texture2D<float4> heightmap, SamplerState samplerState, float4 channel, float amplitude,
    float2 uv, float3 viewDirUV)
{
    float height = dot(heightmap.Sample(samplerState, uv), channel);
    float scale = amplitude * 0.01f;
    height = height * scale - scale * 0.5f;
    float3 v = normalize(viewDirUV);
    v.z += 0.42f;
    return uv + height * (v.xy / v.z);
}

/// @brief 高さのテクスチャを視線に沿って奥へたどり、見えている所の UV を返す（1 が上・0 が底。いつも面より奥へ掘る）
/// @param viewDirUV 面からカメラへの向き（SG_DirectionInUVSpace で UV の軸に直したもの）
float2 SG_ParallaxOcclusionMapping(Texture2D<float4> heightmap, SamplerState samplerState, float4 channel,
    float amplitude, float steps, float2 uv, float lod, float lodThreshold, float3 viewDirUV)
{
    float maxHeight = amplitude * 0.01f;
    float3 view = normalize(float3(viewDirUV.xy * maxHeight, max(viewDirUV.z, 1e-4f)));
    int count = clamp((int)steps, 1, 256);
    float stepSize = 1.0f / (float)count;
    float2 offsetPerStep = stepSize * (view.xy / -view.z);

    float2 offset = float2(0.0f, 0.0f);
    float prevHeight = dot(heightmap.SampleLevel(samplerState, uv, lod), channel);
    offset += offsetPerStep;
    float currHeight = dot(heightmap.SampleLevel(samplerState, uv + offset, lod), channel);
    float rayHeight = 1.0f - stepSize;

    [loop]
    for (int stepIndex = 0; stepIndex < count; ++stepIndex)
    {
        if (currHeight > rayHeight)
        {
            break;
        }
        prevHeight = currHeight;
        rayHeight -= stepSize;
        offset += offsetPerStep;
        currHeight = dot(heightmap.SampleLevel(samplerState, uv + offset, lod), channel);
    }

    // 見つけた上下 2 点の間を割線法で詰める
    float pt0 = rayHeight + stepSize;
    float pt1 = rayHeight;
    float delta0 = pt0 - prevHeight;
    float delta1 = pt1 - currHeight;
    float2 result = offset;
    for (int refine = 0; refine < 3; ++refine)
    {
        float denominator = delta1 - delta0;
        if (abs(denominator) < 1e-6f)
        {
            break;
        }
        float intersectionHeight = (pt0 * delta1 - pt1 * delta0) / denominator;
        result = (1.0f - intersectionHeight) * offsetPerStep * (float)count;
        currHeight = dot(heightmap.SampleLevel(samplerState, uv + result, lod), channel);
        float delta = intersectionHeight - currHeight;
        if (abs(delta) <= 0.01f)
        {
            break;
        }
        if (delta < 0.0f)
        {
            delta1 = delta;
            pt1 = intersectionHeight;
        }
        else
        {
            delta0 = delta;
            pt0 = intersectionHeight;
        }
    }

    // LOD Threshold より細かくない段では効きを弱める
    result *= 1.0f - saturate(lod - lodThreshold);
    return uv + result;
}

#ifdef POST_EFFECT_MATERIAL_HLSLI

/// @brief SG_Ellipse のポストエフェクト版（縁の幅は画面の UV のままのときの 1 画素）
float SG_EllipseScreen(float2 uv, float width, float height)
{
    float2 size = max(float2(width, height), 1e-5f);
    float d = length((uv * 2.0f - 1.0f) / size);
    float2 step = 2.0f * gPostEffect.invScreenSize / size;
    return saturate((1.0f - d) / max(max(step.x, step.y), 1e-5f));
}

/// @brief SG_Rectangle のポストエフェクト版（縁の幅は画面の UV のままのときの 1 画素）
float SG_RectangleScreen(float2 uv, float width, float height)
{
    float2 d = abs(uv * 2.0f - 1.0f) - float2(width, height);
    d = 1.0f - d / max(2.0f * gPostEffect.invScreenSize, 1e-5f);
    return saturate(min(d.x, d.y));
}

#endif // POST_EFFECT_MATERIAL_HLSLI

#endif // SHADER_GRAPH_LIBRARY_HLSLI
