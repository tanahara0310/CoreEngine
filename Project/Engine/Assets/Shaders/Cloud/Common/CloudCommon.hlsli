/// @file CloudCommon.hlsli
/// @brief ボリューメトリック雲の共通定数バッファ・ジオメトリ・密度関数
/// @details C++ 側 VolumetricCloudShaderConstants（304 バイト）と一致させること。
///          座標系は 1unit=1m。惑星中心はカメラ基準で下方 planetRadiusM に置く。

#ifndef CLOUD_COMMON_HLSLI
#define CLOUD_COMMON_HLSLI

#include "CloudNoiseCommon.hlsli"
#include "CloudTuning.hlsli"

struct CloudConstants
{
    float4x4 invViewProj;                                                // 0
    float3 cameraWorldPos;      float timeSec;                           // 64
    float3 sunDirection;        float sunIntensity;                      // 80
    float3 sunColor;            float planetRadiusM;                     // 96
    float layerBottomAltitudeM; float layerThicknessM;
    float groundLevelY;         float globalCoverage;                    // 112
    float baseNoiseScaleM;      float detailNoiseScaleM;
    float detailErosionStrength; float densityScale;                    // 128
    float windDirX;             float windDirZ;
    float windSpeedMPerS;       float weatherMapScaleM;                  // 144
    float phaseG0;              float phaseG1;
    float phaseBlend;           float ambientIntensity;                  // 160
    float beerPowderStrength;   float lightMarchStepM;
    float earlyExitTransmittance; float maxMarchDistanceM;              // 176
    uint maxSteps;              uint outputWidth;
    uint outputHeight;          uint pad0;                               // 192
    float sunLightScale;        float msAttenuation;
    float msContribution;       float msEccentricity;                    // 208
    float3 moonDirection;       float moonIntensity;                     // 224 月光の進行方向 / 強度
    float3 moonColor;           float hasMoon;                           // 240 月光色 / 月有効(0/1)
    float baseNoiseVerticalScale; float heightSkewM;
    float detailFadeDistanceM;  float farFadeWidthM;                     // 256
    float hazeDistanceM;        float maxSunOpticalDepth;
    float ambientCosZenith;     float ambientBottomOcclusion;            // 272
    float ambientChroma;        float pad1;
    float pad2;                 float pad3;                              // 288
};                                                                       // = 304

// ===== 雲層ジオメトリ =====

/// @brief 惑星中心（カメラ基準で真下 planetRadiusM）
float3 CloudPlanetCenter(CloudConstants c)
{
    return float3(c.cameraWorldPos.x, c.groundLevelY - c.planetRadiusM, c.cameraWorldPos.z);
}

/// @brief 雲層内の高さ率 [0,1]（0=雲底, 1=雲頂）
float CloudHeightFraction(float3 pos, CloudConstants c)
{
    float3 center = CloudPlanetCenter(c);
    float rIn = c.planetRadiusM + c.layerBottomAltitudeM;
    float dist = length(pos - center);
    return (dist - rIn) / c.layerThicknessM;
}

/// @brief レイと球の交差（a=1 の縮約形）。戻り値 (t0, t1)。交差なしは (-1,-1)。
float2 RaySphere(float3 ro, float3 rd, float3 center, float radius)
{
    float3 L = ro - center;
    float b = dot(rd, L);
    float c = dot(L, L) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f)
    {
        return float2(-1.0f, -1.0f);
    }
    float s = sqrt(disc);
    return float2(-b - s, -b + s);
}

/// @brief 雲層シェルとのマーチ区間を求める（カメラが層の下／中／上のいずれでも正しく動く）
/// @return x=marchStart, y=marchEnd（y<=x のとき区間なし）
float2 CloudLayerInterval(float3 ro, float3 rd, CloudConstants c)
{
    float3 center = CloudPlanetCenter(c);
    float rIn = c.planetRadiusM + c.layerBottomAltitudeM;
    float rOut = rIn + c.layerThicknessM;
    float r = length(ro - center);

    float2 innerT = RaySphere(ro, rd, center, rIn);
    float2 outerT = RaySphere(ro, rd, center, rOut);

    // 外殻に当たらない = 層に入らない（RaySphere は非交差で (-1,-1) を返す）
    if (outerT.y < 0.0f)
    {
        return float2(0.0f, -1.0f);
    }

    float marchStart;
    float marchEnd;

    if (r < rIn)
    {
        // カメラは層より下（内殻の内側）: 内殻の出口 → 外殻の出口
        marchStart = innerT.y;
        marchEnd = outerT.y;
    }
    else if (r < rOut)
    {
        // カメラは層の中: 直ちに開始。下向きなら内殻の手前交点で、上向きなら外殻の出口で抜ける
        marchStart = 0.0f;
        marchEnd = (innerT.x > 0.0f) ? innerT.x : outerT.y;
    }
    else
    {
        // カメラは層より上: 外殻の入口から。下向きなら内殻の手前交点で抜ける
        if (outerT.x < 0.0f)
        {
            return float2(0.0f, -1.0f); // 層は背後
        }
        marchStart = outerT.x;
        marchEnd = (innerT.x > 0.0f) ? innerT.x : outerT.y;
    }

    return float2(max(marchStart, 0.0f), marchEnd);
}

/// @brief 画面空間 Interleaved Gradient Noise（レイマーチ開始位置のジッタ用）
/// @details 時間依存にすると TAA 無しではちらつくため、フレーム非依存の静的パターンにする。
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(0.06711056f * pixel.x + 0.00583715f * pixel.y));
}

// ===== 密度（GPU Pro 7 / Schneider 方式） =====
// weather map カバレッジ・雲タイプ別高度勾配・風移流・ディテール侵食からなる。

/// @brief 雲タイプ（0:層雲〜1:積乱雲）に応じた高度勾配を返す
/// @note 積雲は層の 8 割程度まで発達させる。縦の伸びが小さいと横長のパンケーキに見える
float CloudHeightGradient(float h, float cloudType)
{
    float gStratus =
        saturate(Remap(h, 0.00f, 0.10f, 0.0f, 1.0f)) * saturate(Remap(h, 0.20f, 0.30f, 1.0f, 0.0f));
    float gCumulus =
        saturate(Remap(h, 0.00f, 0.20f, 0.0f, 1.0f)) * saturate(Remap(h, 0.40f, 0.85f, 1.0f, 0.0f));
    float gCumulonimbus =
        saturate(Remap(h, 0.00f, 0.10f, 0.0f, 1.0f)) * saturate(Remap(h, 0.70f, 1.00f, 1.0f, 0.0f));

    float lowBlend = saturate(cloudType * 2.0f);            // [0,0.5] を 0→1
    float highBlend = saturate((cloudType - 0.5f) * 2.0f);  // [0.5,1] を 0→1
    return lerp(lerp(gStratus, gCumulus, lowBlend), gCumulonimbus, highBlend);
}

/// @brief 風の移流（ワールド XZ 平面）と高度スキューを適用したサンプル座標
float3 CloudAdvectedPos(float3 worldPos, float h, CloudConstants c)
{
    float3 windDir = float3(c.windDirX, 0.0f, c.windDirZ);
    return worldPos + windDir * (c.windSpeedMPerS * c.timeSec) + h * windDir * c.heightSkewM;
}

/// @brief ディテール侵食を含まない密度
/// @details サンライトマーチ・雲シャドウマップ・雲探索の大股走査が使う。
///          縦方向だけ小さいスケールでサンプルする（等方だと層内の縦の変化が乏しく平らな板に見える）。
float SampleCloudDensityCheap(float3 worldPos, float h, CloudConstants c,
                              Texture3D<float4> baseNoise, Texture2D<float4> weatherMap,
                              SamplerState samp)
{
    if (h < 0.0f || h > 1.0f)
    {
        return 0.0f;
    }

    float3 sampleWS = CloudAdvectedPos(worldPos, h, c);

    // ベース形状
    float3 baseUvw = sampleWS / c.baseNoiseScaleM;
    baseUvw.y = sampleWS.y / (c.baseNoiseScaleM * c.baseNoiseVerticalScale);
    float4 base = baseNoise.SampleLevel(samp, baseUvw, 0);
    float lowFreqFBM = base.g * 0.625f + base.b * 0.25f + base.a * 0.125f;
    float baseCloud = Remap(base.r, -(1.0f - lowFreqFBM), 1.0f, 0.0f, 1.0f);

    // 高度勾配（weather.g の雲タイプでブレンド）
    float2 weatherUv = worldPos.xz / c.weatherMapScaleM;
    float4 weather = weatherMap.SampleLevel(samp, weatherUv, 0);
    baseCloud *= CloudHeightGradient(h, weather.g);

    // カバレッジ適用（縁を柔らかくしアンビル状を防ぐ: GPU Pro 7）
    float coverage = saturate(weather.r * c.globalCoverage);
    float cloudWithCoverage = saturate(Remap(baseCloud, 1.0f - coverage, 1.0f, 0.0f, 1.0f));
    return saturate(cloudWithCoverage * coverage);
}

/// @brief ディテール侵食込みの密度
/// @param detailStrength 侵食の強度（遠方では 0 へフェードさせエイリアシングを防ぐ）
float SampleCloudDensity(float3 worldPos, float h, float detailStrength, CloudConstants c,
                         Texture3D<float4> baseNoise, Texture3D<float4> detailNoise,
                         Texture2D<float4> weatherMap, SamplerState samp)
{
    float density = SampleCloudDensityCheap(worldPos, h, c, baseNoise, weatherMap, samp);
    if (density <= 0.0f || detailStrength <= 0.0f)
    {
        return density;
    }

    float3 sampleWS = CloudAdvectedPos(worldPos, h, c);
    float4 detail = detailNoise.SampleLevel(samp, sampleWS / c.detailNoiseScaleM, 0);
    float highFreqFBM = detail.r * 0.625f + detail.g * 0.25f + detail.b * 0.125f;
    // 層底では billowy、上部では wispy に（高さで反転）
    float highFreqModifier = lerp(highFreqFBM, 1.0f - highFreqFBM, saturate(h * 10.0f));
    return saturate(Remap(density, highFreqModifier * detailStrength, 1.0f, 0.0f, 1.0f));
}

#endif // CLOUD_COMMON_HLSLI
