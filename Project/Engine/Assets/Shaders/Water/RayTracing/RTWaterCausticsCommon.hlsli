#ifndef RT_WATER_CAUSTICS_COMMON_HLSLI
#define RT_WATER_CAUSTICS_COMMON_HLSLI

// 水面を通って水中の受光点へ届く日光（RT コースティクス）。RTWaterCaustics と RTWaterRefraction で共有する。
// 受光点まで光が届くかを確かめるレイは呼び出し側が撃つ:
//   BeginWaterCaustics → traceRequired なら path.ray を最近接ヒットで撃つ → FinishWaterCaustics

#include "RTWaterSurfaceCommon.hlsli"
// フレネル透過率・受光面の幾何項
#include "../Common/WaterSunTransmission.hlsli"

/// @brief コースティクスの計算に使う日光と水の値
struct WaterCausticsParams
{
    float3 lightDirection;     // 光の進む向き（下向き）
    float3 lightColor;
    float lightIntensity;
    float intensityScale;      // 強さの倍率
    float refractiveIndex;     // 水の屈折率
    float3 absorption;         // 吸収係数 σa [1/m]（RGB）
    float surfaceBias;         // 入射点から水中へずらす距離 [m]
    float maxTraceDistance;    // 確かめるレイの長さの上限 [m]
    float2 regionCenterXZ;     // 水面メッシュのワールド XZ 範囲
    float2 regionHalfExtentXZ;
    uint regionValid;          // 0 = 範囲が分からない
};

/// @brief 受光点 1 つ分の途中の値（BeginWaterCaustics が作り、FinishWaterCaustics が使う）
struct WaterCausticsPath
{
    float coverage;            // 水に覆われている割合（メインライトを透過光へ置き換える割合）
    float submergedDepth;      // 描かれる水面からの深さ [m]
    float3 baselineRadiance;   // 平らな水面を通って届く光（集光なし）
    bool traceRequired;        // true = ray を撃って FinishWaterCaustics で仕上げる / false = baselineRadiance が答え
    RayDesc ray;               // 入射点から受光点へ向かうレイ
    float3 entryPosition;      // 日光が水面へ入る点
    float3 entryNormal;        // 入射点の波の法線
    float3 refractedDir;       // 入射点で屈折した日光の向き
    float receiverRayT;        // レイ上で受光点にいちばん近い点までの距離
    float receiverRayDistance; // その点と受光点の距離
    float receiverMatchRadius; // 入射点が求まったとみなす半径
};

/// @brief FinishWaterCaustics の結果
struct WaterCausticsResult
{
    float3 radiance;           // 受光点へ届く光（coverage を掛け済み）
    bool patternEvaluated;     // true = 集光率まで求めた（以下の値が有効）
    float matchFactor;         // 入射点の収束と遮蔽の整合の重み
    float3 transmittance;      // 入射点から受光点までの吸収の透過率
    float geometryFactor;      // 受光面の幾何項
    float concentration;       // 集光率
};

/// @brief 水面メッシュ頂点 1 点分の変位（FFTWater.VS と同じ式: 先頭のカスケード＋波群エンベロープ）
float3 SampleMeshVertexDisplacement(Texture2DArray<float4> displacementTex, float2 baseXZ)
{
    float3 disp = 0.0f.xxx;
    [unroll]
    for (int c = 0; c < kFFTGeometryCascadeCount; ++c)
    {
        const float2 gridXZ = RotateToFFTCascadeGrid(baseXZ, c);
        const float3 d = SampleFFTOceanArraySlice(
            displacementTex, gridXZ, kFFTCascadePatch[c], (uint)c, gSurfaceFFTOceanResolution).xyz;
        const float2 horizontal = RotateFromFFTCascadeGrid(float2(d.x, d.z), c);
        disp += float3(horizontal.x, d.y, horizontal.y);
    }
    return disp * ComputeFFTWaveGroupEnvelope(baseXZ);
}

/// @brief ラスタライザが実際に描く水面の高さ（基準面からのオフセット）を返す
/// @details 水面メッシュの頂点（PlaneMeshGenerator の格子）でだけ変位を評価し、
///          ラスタライザと同じ三角形（対角は (0,1)-(1,0)）で補間する
float EvaluateDrawnSurfaceHeight(
    WaterCausticsParams params, Texture2DArray<float4> displacementTex, float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterOffsetGerstner(worldXZ).y;
    }
    if (params.regionValid == 0)
    {
        // 格子が分からないときは波面そのものの高さ
        return SampleMeshVertexDisplacement(displacementTex, worldXZ).y;
    }

    // 水平変位を 1 回だけ逆にたどって、変位前の格子上の位置を求める
    const float2 baseXZ = worldXZ - SampleMeshVertexDisplacement(displacementTex, worldXZ).xz;

    const float2 gridOrigin = params.regionCenterXZ - params.regionHalfExtentXZ;
    const float2 cellSize = max((2.0f * params.regionHalfExtentXZ) / max(gSurfaceMeshSubdivisions, 1.0f), 1.0e-4f.xx);
    const float2 gridCoord = (baseXZ - gridOrigin) / cellSize;
    const float2 cellIndex = floor(gridCoord);
    const float2 cellFrac = gridCoord - cellIndex;
    const float2 cellCorner = gridOrigin + cellIndex * cellSize;

    // 対角上の 2 頂点は両方の三角形で共通なので、評価するのは 3 頂点
    const float h10 = SampleMeshVertexDisplacement(displacementTex, cellCorner + float2(cellSize.x, 0.0f)).y;
    const float h01 = SampleMeshVertexDisplacement(displacementTex, cellCorner + float2(0.0f, cellSize.y)).y;
    if (cellFrac.x + cellFrac.y <= 1.0f)
    {
        const float h00 = SampleMeshVertexDisplacement(displacementTex, cellCorner).y;
        return h00 + cellFrac.x * (h10 - h00) + cellFrac.y * (h01 - h00);
    }
    const float h11 = SampleMeshVertexDisplacement(displacementTex, cellCorner + cellSize).y;
    return h11 + (1.0f - cellFrac.x) * (h01 - h11) + (1.0f - cellFrac.y) * (h10 - h11);
}

/// @brief 水面上の点（XZ）へ入射した日光を屈折させ、床平面 y = floorY への着地点 XZ を返す
/// @details 集光率（ヤコビアン）の差分に使う。入射点探索と同じ波面評価・屈折計算をする
/// @return 屈折が有効（全反射・上向きでない）なら true
bool ProjectRefractedToFloor(
    Texture2DArray<float4> displacementTex, Texture2DArray<float4> normalTex,
    float2 surfaceXZ, float floorY, float3 lightDir, float eta, float footprintMeters,
    out float2 landingXZ)
{
    const float surfaceY = gSurfaceWaterHeight + EvaluateWaterOffset(displacementTex, surfaceXZ).y;
    const float3 surfaceNormal = EvaluateWaterNormal(normalTex, surfaceXZ, footprintMeters);
    float3 refracted = refract(lightDir, surfaceNormal, eta);
    if (dot(refracted, refracted) <= 1.0e-6f || refracted.y >= -1.0e-4f)
    {
        landingXZ = surfaceXZ;
        return false;
    }
    refracted = normalize(refracted);
    const float travel = (surfaceY - floorY) / max(-refracted.y, 1.0e-4f);
    landingXZ = surfaceXZ + refracted.xz * travel;
    return true;
}

/// @brief 受光点の水深・被覆率・集光なしの透過光を求め、日光が水面へ入る点を解いて確かめるレイを用意する
/// @param receiverWorldPos        受光点
/// @param receiverNormal          受光面の法線
/// @param receiverFootprintMeters 受光点で 1 回の評価が覆う幅 [m]（波の縮小フィルタと集光率の差分幅）
WaterCausticsPath BeginWaterCaustics(
    WaterCausticsParams params,
    float3 receiverWorldPos,
    float3 receiverNormal,
    float receiverFootprintMeters,
    Texture2DArray<float4> displacementTex,
    Texture2DArray<float4> normalTex)
{
    WaterCausticsPath path;
    path.coverage = 0.0f;
    path.submergedDepth = 0.0f;
    path.baselineRadiance = 0.0f.xxx;
    path.traceRequired = false;
    path.ray.Origin = receiverWorldPos;
    path.ray.Direction = float3(0.0f, -1.0f, 0.0f);
    path.ray.TMin = 0.001f;
    path.ray.TMax = 0.0f;
    path.entryPosition = receiverWorldPos;
    path.entryNormal = float3(0.0f, 1.0f, 0.0f);
    path.refractedDir = float3(0.0f, -1.0f, 0.0f);
    path.receiverRayT = 0.0f;
    path.receiverRayDistance = 0.0f;
    path.receiverMatchRadius = 0.0f;

    // 水面メッシュの範囲の外には落とさない（範囲の内側 2m で薄める）
    float regionFade = 1.0f;
    if (params.regionValid != 0)
    {
        float2 regionDelta = abs(receiverWorldPos.xz - params.regionCenterXZ) - params.regionHalfExtentXZ;
        float outsideDistance = max(regionDelta.x, regionDelta.y);
        const float kRegionEdgeFadeMeters = 2.0f;
        regionFade = saturate(-outsideDistance / kRegionEdgeFadeMeters);
        if (regionFade <= 0.0f)
        {
            return path;
        }
    }

    // 描かれる水面からの深さ。水面より上なら水に覆われていない（直接光は通常のまま）
    const float surfaceYAboveReceiver =
        gSurfaceWaterHeight + EvaluateDrawnSurfaceHeight(params, displacementTex, receiverWorldPos.xz);
    float submergedDepth = surfaceYAboveReceiver - receiverWorldPos.y;
    if (submergedDepth <= 0.0f)
    {
        return path;
    }
    path.submergedDepth = submergedDepth;

    // 被覆率 = 水深 5cm までのクロスフェード × 範囲の縁のフェード
    const float crossFade = saturate(submergedDepth / 0.05f);
    const float coverage = crossFade * regionFade;
    path.coverage = coverage;

    const float eta = 1.0f / max(params.refractiveIndex, 1.001f);
    // refract() には光の進む向き（下向き）を渡す
    float3 lightDir = normalize(params.lightDirection);

    // ===== 平らな水面を通って届く光（集光なし） =====
    float3 baselineRadiance = 0.0f.xxx;
    {
        const float3 flatNormal = float3(0.0f, 1.0f, 0.0f);
        float3 flatRefracted = refract(lightDir, flatNormal, eta);
        if (dot(flatRefracted, flatRefracted) > 1.0e-6f && flatRefracted.y < -1.0e-4f)
        {
            flatRefracted = normalize(flatRefracted);
            const float flatFresnelT = FresnelTransmittanceSchlick(-lightDir.y, params.refractiveIndex);
            const float flatPath = submergedDepth / max(-flatRefracted.y, 1.0e-4f);
            const float3 flatTransmittance = exp(-params.absorption * flatPath);
            float flatGeometry = saturate(dot(receiverNormal, -flatRefracted)) / max(-flatRefracted.y, 0.05f);
            flatGeometry = min(lerp(
                ComputeAerialGeometryFactor(receiverNormal, lightDir),
                flatGeometry,
                ComputeRefractedGeometryWeight(submergedDepth)), 4.0f);
            const float flatIlluminance = params.lightIntensity * saturate(-lightDir.y);
            baselineRadiance = params.lightColor
                * (params.intensityScale * flatIlluminance * flatFresnelT * flatGeometry * coverage)
                * flatTransmittance;
        }
    }
    path.baselineRadiance = baselineRadiance;

    // 水深がほぼ 0 のときは入射点を解かず、平らな水面の透過光を使う
    if (submergedDepth <= 1.0e-3f)
    {
        return path;
    }

    // ===== 入射点（この受光点へ屈折光を届ける水面上の点）を固定点反復で解く =====
    // 受光点から屈折した向きを逆にたどって水面へ戻すことを 3 回繰り返す
    float3 waterPos = float3(receiverWorldPos.x, surfaceYAboveReceiver, receiverWorldPos.z);
    [unroll]
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        const float3 iterNormal = EvaluateWaterNormal(normalTex, waterPos.xz, receiverFootprintMeters);
        float3 iterRefracted = refract(lightDir, iterNormal, eta);
        if (dot(iterRefracted, iterRefracted) <= 1.0e-6f || iterRefracted.y >= -1.0e-4f)
        {
            // 全反射・上向きの屈折。下の判定で棄却する
            break;
        }
        iterRefracted = normalize(iterRefracted);

        const float travel = (waterPos.y - receiverWorldPos.y) / max(-iterRefracted.y, 1.0e-4f);
        waterPos.xz = receiverWorldPos.xz - iterRefracted.xz * travel;
        waterPos.y = gSurfaceWaterHeight + EvaluateWaterOffset(displacementTex, waterPos.xz).y;
    }

    // 入射点での法線・屈折した向き。求まらなければ平らな水面の透過光を使う
    float3 waterNormal = EvaluateWaterNormal(normalTex, waterPos.xz, receiverFootprintMeters);
    float3 refractedDir = refract(lightDir, waterNormal, eta);
    if (dot(refractedDir, refractedDir) <= 1.0e-6f || refractedDir.y >= -1.0e-4f)
    {
        return path;
    }
    refractedDir = normalize(refractedDir);

    RayDesc ray;
    ray.Origin = waterPos - waterNormal * params.surfaceBias;
    ray.Direction = refractedDir;
    ray.TMin = 0.001f;

    float3 rayToReceiver = receiverWorldPos - ray.Origin;
    float receiverRayT = dot(rayToReceiver, ray.Direction);
    if (receiverRayT <= 0.0f)
    {
        return path;
    }

    // レイが受光点の近くを通るか。半径は水深 × 0.30 を斜め入射の光路の伸びで広げる
    float3 projectedReceiverPos = ray.Origin + ray.Direction * receiverRayT;
    float receiverRayDistance = length(projectedReceiverPos - receiverWorldPos);
    const float obliquityScale = 1.0f / max(-refractedDir.y, 0.15f);
    float receiverMatchRadius = max(0.03f, submergedDepth * 0.30f * obliquityScale);
    if (receiverRayDistance > receiverMatchRadius)
    {
        return path;
    }

    ray.TMax = min(params.maxTraceDistance, receiverRayT + receiverMatchRadius * 2.0f);

    path.traceRequired = true;
    path.ray = ray;
    path.entryPosition = waterPos;
    path.entryNormal = waterNormal;
    path.refractedDir = refractedDir;
    path.receiverRayT = receiverRayT;
    path.receiverRayDistance = receiverRayDistance;
    path.receiverMatchRadius = receiverMatchRadius;
    return path;
}

/// @brief path.ray を撃った結果から、受光点へ届く光を仕上げる
/// @param hit  レイが何かに当たったか
/// @param hitT 最も近い当たりの距離
/// @details 受光点より手前に当たれば影（0）、当たらない・奥に当たれば平らな水面の透過光、
///          受光点に当たれば集光率を掛けた透過光を返す
WaterCausticsResult FinishWaterCaustics(
    WaterCausticsPath path,
    WaterCausticsParams params,
    float3 receiverWorldPos,
    float3 receiverNormal,
    float receiverFootprintMeters,
    bool hit,
    float hitT,
    Texture2DArray<float4> displacementTex,
    Texture2DArray<float4> normalTex)
{
    WaterCausticsResult result;
    result.radiance = path.baselineRadiance;
    result.patternEvaluated = false;
    result.matchFactor = 0.0f;
    result.transmittance = float3(1.0f, 1.0f, 1.0f);
    result.geometryFactor = 0.0f;
    result.concentration = 1.0f;

    if (!hit)
    {
        return result;
    }

    float hitDistanceError = abs(hitT - path.receiverRayT);
    float hitMatchRadius = max(0.10f, path.receiverMatchRadius * 2.0f);
    if (hitT < path.receiverRayT - hitMatchRadius)
    {
        // 受光点より手前で遮られた（被覆率はそのまま）
        result.radiance = 0.0f.xxx;
        return result;
    }
    if (hitT > path.receiverRayT + hitMatchRadius)
    {
        return result;
    }

    const float eta = 1.0f / max(params.refractiveIndex, 1.001f);
    const float3 lightDir = normalize(params.lightDirection);
    const float3 waterPos = path.entryPosition;
    const float3 refractedDir = path.refractedDir;

    // 入射点の収束と、当たった距離の受光点との一致の重み
    float rayMatchFactor = saturate(1.0f - path.receiverRayDistance / path.receiverMatchRadius);
    float hitMatchFactor = saturate(1.0f - hitDistanceError / hitMatchRadius);
    float matchFactor = rayMatchFactor * hitMatchFactor;

    // ===== 集光率: 入射点の写像（水面 XZ → 受光点の高さの水平面 XZ）の面積拡大率 |det J| の逆数 =====
    // 差分幅は受光点で 1 回の評価が覆う幅（最細カスケードのテクセル幅 〜 16m）。焦線での発散は上限で止める
    const float kJacobianMinEps = 0.08f;
    const float kJacobianMaxEps = 16.0f;
    const float kMaxConcentration = 8.0f;
    const float jacobianEps = clamp(receiverFootprintMeters, kJacobianMinEps, kJacobianMaxEps);
    float concentration = 1.0f;
    {
        const float floorY = receiverWorldPos.y;
        const float t0 = (waterPos.y - floorY) / max(-refractedDir.y, 1.0e-4f);
        const float2 landing0 = waterPos.xz + refractedDir.xz * t0;
        float2 landingX;
        float2 landingZ;
        const bool validX = ProjectRefractedToFloor(
            displacementTex, normalTex,
            waterPos.xz + float2(jacobianEps, 0.0f), floorY, lightDir, eta,
            receiverFootprintMeters, landingX);
        const bool validZ = ProjectRefractedToFloor(
            displacementTex, normalTex,
            waterPos.xz + float2(0.0f, jacobianEps), floorY, lightDir, eta,
            receiverFootprintMeters, landingZ);
        if (validX && validZ)
        {
            const float2 dFdX = (landingX - landing0) / jacobianEps;
            const float2 dFdZ = (landingZ - landing0) / jacobianEps;
            const float detJ = dFdX.x * dFdZ.y - dFdX.y * dFdZ.x;
            concentration = min(1.0f / max(abs(detJ), 1.0e-3f), kMaxConcentration);
        }
    }

    // ===== 受光面の幾何項: 水平面の照度を受光面の照度へ換算する（E × dot(N, -r) / (-r.y)） =====
    float geometryFactor = saturate(dot(receiverNormal, -refractedDir)) / max(-refractedDir.y, 0.05f);
    geometryFactor = min(lerp(
        ComputeAerialGeometryFactor(receiverNormal, lightDir),
        geometryFactor,
        ComputeRefractedGeometryWeight(path.submergedDepth)), 4.0f);

    // 入射点の波の法線でのフレネル透過率
    const float fresnelTransmittance =
        FresnelTransmittanceSchlick(dot(-lightDir, path.entryNormal), params.refractiveIndex);

    // 入射点から受光点までの吸収（波長ごと）
    const float3 transmittance = exp(-params.absorption * path.receiverRayT);

    // 水平な水面が受ける照度 × 集光率 × 幾何項 × フレネル透過率 × 被覆率。一致の重みで平らな水面の透過光と混ぜる
    const float horizontalIlluminance = params.lightIntensity * saturate(-lightDir.y);
    const float scalarIntensity = params.intensityScale * horizontalIlluminance * fresnelTransmittance
        * concentration * geometryFactor * path.coverage;
    result.radiance = lerp(
        path.baselineRadiance,
        params.lightColor * scalarIntensity * transmittance,
        matchFactor);
    result.patternEvaluated = true;
    result.matchFactor = matchFactor;
    result.transmittance = transmittance;
    result.geometryFactor = geometryFactor;
    result.concentration = concentration;
    return result;
}

#endif // RT_WATER_CAUSTICS_COMMON_HLSLI
