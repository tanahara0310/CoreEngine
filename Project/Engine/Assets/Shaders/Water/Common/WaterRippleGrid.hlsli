// ============================================================
// 水の波紋の格子（WaterRippleSimulation）
// カメラの前に大きさの違う 3 段の格子を置き、段ごとに分散のある波を進める。
// 状態は R32_FLOAT の配列で、段 L・組 p（0/1 を交互に使う）の高さ h_n と h_{n-1} を
// スライス L*4 + p*2 + 0 と L*4 + p*2 + 1 に置く。
// 水面が読む面は RGBA16F の配列で、スライス L に (高さ, ∂h/∂x, ∂h/∂z, 0) を置く。
// テクセル (i, j) は XZ = 段の最小の角 + ((i, j) + 0.5) × 1 マスの幅 の点の値。
// ============================================================
#ifndef WATER_RIPPLE_GRID_INCLUDED
#define WATER_RIPPLE_GRID_INCLUDED

static const uint kRippleLevelCount = 3;
static const uint kRippleResolution = 512;
// 端から内側へ、波を吸い込む帯の幅 [マス]
static const float kRippleSpongeCells = 16.0f;

uint RippleStateSlice(uint level, uint pair, uint component)
{
    return level * 4 + pair * 2 + component;
}

/// @brief 段の面を双線形で読む（範囲の外は 0）
/// @param cellCoord 段の格子のマス座標（テクセルの中心が整数 + 0.5）
float4 LoadRippleSurfaceBilinear(Texture2DArray<float4> surface, float2 cellCoord, uint level)
{
    const float2 p = cellCoord - 0.5f;
    const float2 base = floor(p);
    const float2 f = p - base;
    const int2 i0 = int2(base);

    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    [unroll]
    for (int k = 0; k < 4; ++k)
    {
        const int2 offset = int2(k & 1, k >> 1);
        const int2 texel = i0 + offset;
        if (all(texel >= 0) && all(texel < int(kRippleResolution)))
        {
            const float wx = (offset.x == 0) ? (1.0f - f.x) : f.x;
            const float wz = (offset.y == 0) ? (1.0f - f.y) : f.y;
            result += surface.Load(int4(texel, level, 0)) * (wx * wz);
        }
    }
    return result;
}

/// @brief 波紋の傾きを全部の段から足し合わせる
/// @param worldXZ 水面の点のワールド XZ [m]
/// @param footprintMeters 1 画素（1 本のレイ）が覆う水面の幅 [m]
/// @param originsXZ01 段 0・段 1 の最小の角 [m]（xy = 段 0・zw = 段 1）
/// @param origin2XZ 段 2 の最小の角 [m]
/// @param invSizes 段ごとの一辺の長さの逆数 [1/m]（0 の段は読まない）
/// @param unresolvedMeanSquareSlope 画素より細かいので傾きから外した分の平均二乗傾斜
/// @return 傾き n.xz / n.y（= -∇h）
float2 EvaluateRippleSlope(
    Texture2DArray<float4> surface,
    float2 worldXZ,
    float footprintMeters,
    float4 originsXZ01,
    float2 origin2XZ,
    float3 invSizes,
    float strength,
    out float unresolvedMeanSquareSlope)
{
    unresolvedMeanSquareSlope = 0.0f;
    float2 slope = float2(0.0f, 0.0f);

    [unroll]
    for (uint level = 0; level < kRippleLevelCount; ++level)
    {
        const float invSize = (level == 0) ? invSizes.x : ((level == 1) ? invSizes.y : invSizes.z);
        if (invSize <= 0.0f)
        {
            continue;
        }
        const float2 origin = (level == 0) ? originsXZ01.xy : ((level == 1) ? originsXZ01.zw : origin2XZ);
        const float2 cellCoord = (worldXZ - origin) * (invSize * (float)kRippleResolution);
        const float2 edgeDistance = min(cellCoord, (float)kRippleResolution - cellCoord);
        const float edgeFade = smoothstep(0.0f, kRippleSpongeCells, min(edgeDistance.x, edgeDistance.y));
        if (edgeFade <= 0.0f)
        {
            continue;
        }

        // 1 画素が 1 マスより広くなったら段の傾きを外し、外した分を粗さへ回す
        const float cellSize = 1.0f / (invSize * (float)kRippleResolution);
        const float fade = 1.0f - smoothstep(1.0f, 4.0f, footprintMeters / cellSize);
        const float2 gradient = LoadRippleSurfaceBilinear(surface, cellCoord, level).yz * (strength * edgeFade);
        slope -= gradient * fade;
        unresolvedMeanSquareSlope += (1.0f - fade * fade) * dot(gradient, gradient);
    }
    return slope;
}

// 段の代表の波長 [マス]
static const float kRippleGeometryWavelengthCells = 16.0f;
// 代表の波長あたりの頂点の数がこの数以下の段は形に出さず、この数以上の段はそのまま出す
static const float kRippleGeometryMinVerticesPerWave = 4.0f;
static const float kRippleGeometryFullVerticesPerWave = 8.0f;

/// @brief 頂点の間隔 vertexSpacing [m] の格子で、段の波紋を形（頂点の高さ）に入れる重み [0,1]
float ComputeRippleGeometryLevelWeight(float cellSize, float vertexSpacing)
{
    const float verticesPerWave = kRippleGeometryWavelengthCells * cellSize / max(vertexSpacing, 1.0e-4f);
    return smoothstep(kRippleGeometryMinVerticesPerWave, kRippleGeometryFullVerticesPerWave, verticesPerWave);
}

/// @brief 水面の頂点に足す波紋の高さ [m]（頂点の格子で表せる段の和に strength を掛けた値）
/// @param worldXZ 頂点が描かれる位置のワールド XZ [m]
/// @param vertexSpacing 頂点の格子の間隔 [m]（ComputeRippleGeometryLevelWeight）
float EvaluateRippleVertexHeight(
    Texture2DArray<float4> surface,
    float2 worldXZ,
    float vertexSpacing,
    float4 originsXZ01,
    float2 origin2XZ,
    float3 invSizes,
    float strength)
{
    float height = 0.0f;
    [unroll]
    for (uint level = 0; level < kRippleLevelCount; ++level)
    {
        const float invSize = (level == 0) ? invSizes.x : ((level == 1) ? invSizes.y : invSizes.z);
        if (invSize <= 0.0f)
        {
            continue;
        }
        const float cellSize = 1.0f / (invSize * (float)kRippleResolution);
        const float weight = ComputeRippleGeometryLevelWeight(cellSize, vertexSpacing);
        if (weight <= 0.0f)
        {
            continue;
        }
        const float2 origin = (level == 0) ? originsXZ01.xy : ((level == 1) ? originsXZ01.zw : origin2XZ);
        const float2 cellCoord = (worldXZ - origin) * (invSize * (float)kRippleResolution);
        const float2 edgeDistance = min(cellCoord, (float)kRippleResolution - cellCoord);
        const float edgeFade = smoothstep(0.0f, kRippleSpongeCells, min(edgeDistance.x, edgeDistance.y));
        if (edgeFade <= 0.0f)
        {
            continue;
        }
        height += LoadRippleSurfaceBilinear(surface, cellCoord, level).x * (weight * edgeFade);
    }
    return height * strength;
}

/// @brief デバッグ表示用に、全部の段の高さの和と段ごとの傾きの大きさを返す（strength を掛けた値）
void EvaluateRippleDebug(
    Texture2DArray<float4> surface,
    float2 worldXZ,
    float4 originsXZ01,
    float2 origin2XZ,
    float3 invSizes,
    float strength,
    out float height,
    out float3 levelSlope)
{
    height = 0.0f;
    levelSlope = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (uint level = 0; level < kRippleLevelCount; ++level)
    {
        const float invSize = (level == 0) ? invSizes.x : ((level == 1) ? invSizes.y : invSizes.z);
        if (invSize <= 0.0f)
        {
            continue;
        }
        const float2 origin = (level == 0) ? originsXZ01.xy : ((level == 1) ? originsXZ01.zw : origin2XZ);
        const float2 cellCoord = (worldXZ - origin) * (invSize * (float)kRippleResolution);
        const float2 edgeDistance = min(cellCoord, (float)kRippleResolution - cellCoord);
        const float edgeFade = smoothstep(0.0f, kRippleSpongeCells, min(edgeDistance.x, edgeDistance.y));
        const float4 value = LoadRippleSurfaceBilinear(surface, cellCoord, level) * (strength * edgeFade);
        height += value.x;
        const float slopeLength = length(value.yz);
        levelSlope.x = (level == 0) ? slopeLength : levelSlope.x;
        levelSlope.y = (level == 1) ? slopeLength : levelSlope.y;
        levelSlope.z = (level == 2) ? slopeLength : levelSlope.z;
    }
}

/// @brief 面の法線へ波紋の傾きを足す（高さの場の傾きとして足し合わせる）
float3 AddRippleSlopeToNormal(float3 normal, float2 rippleSlope)
{
    const float3 slopeForm = normal / max(normal.y, 1.0e-3f);
    return normalize(slopeForm + float3(rippleSlope.x, 0.0f, rippleSlope.y));
}

#endif // WATER_RIPPLE_GRID_INCLUDED
