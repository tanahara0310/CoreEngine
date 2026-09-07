/// @file CloudCommon.hlsli
/// @brief ボリューメトリック雲の共通定数バッファ・ジオメトリ・密度関数
/// @details C++ 側 VolumetricCloudShaderConstants（432 バイト）と一致させること。
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
    float dropletDiameterUm;    float maxPhase;
    float lightMarchConeSpread; float ambientIntensity;                  // 160
    float beerPowderStrength;   float lightMarchCoverage;
    float earlyExitTransmittance; float maxMarchDistanceM;              // 176
    uint maxSteps;              uint outputWidth;
    uint outputHeight;          uint frameIndex;                         // 192
    float sunLightScale;        float msAttenuation;
    float msContribution;       float msEccentricity;                    // 208
    float3 moonDirection;       float moonIntensity;                     // 224 月光の進行方向 / 強度
    float3 moonColor;           float hasMoon;                           // 240 月光色 / 月有効(0/1)
    float baseNoiseVerticalScale; float heightSkewM;
    float detailFadeDistanceM;  float farFadeWidthM;                     // 256
    float hazeDistanceM;        float maxSunOpticalDepth;
    float ambientCosZenith;     float ambientBottomOcclusion;            // 272
    float ambientChroma;        float ambientGroundStrength;
    float upsampleDepthTolerance; float cloudStreetStretch;              // 288
    float4x4 prevViewProj;                                               // 304 前フレームのビュー射影
    float reprojectEnabled;     float reprojectBlendMin;
    float reprojectTolerance;   float cloudTopVariation;                 // 368
    float cirrusAltitudeM;      float cirrusCoverage;
    float cirrusDensity;        float cirrusScaleM;                      // 384
    float cirrusStretch;        float cirrusWindScale;
    float noiseLodBias;         float paintRegionCenterX;                // 400
    float paintRegionCenterZ;   float paintRegionSizeM;
    float paintEdgeFade;        uint styleIndex;                         // 416 スタイル（CloudBlocky.hlsli）
    float voxelSizeM;           float voxelHeightSizeM;
    float densityThreshold;     float voxelFaceBrightness;               // 432
    float voxelFaceShadeMin;    float pad7;
    float pad8;                 float pad9;                              // 448
};                                                                       // = 464

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
/// @param frame フレーム番号。時間再投影が毎フレーム別のサンプル位置を積み上げられるよう
///              位相を回す。回すのは出力側（黄金比の加算）で、入力座標はいじらない。
///              入力へオフセットを足すと空間パターンそのものが変わり、
///              マーチの粗/細ステップの切り替わり方まで変わってコストが跳ねる。
///              再投影が無効なときは 0 を渡すこと（毎フレーム同じ静的パターンになる）
float InterleavedGradientNoise(float2 pixel, uint frame)
{
    float ign = frac(52.9829189f * frac(0.06711056f * pixel.x + 0.00583715f * pixel.y));
    return frac(ign + float(frame % 64u) * 0.6180339887f);
}

// スタイル（ブロック雲）の量子化。CloudHeightFraction を使うのでジオメトリの後に置く
#include "CloudBlocky.hlsli"

// ===== 密度（GPU Pro 7 / Schneider 方式） =====
// weather map カバレッジ・雲タイプ別高度勾配・風移流・ディテール侵食からなる。

/// @brief 雲タイプ（0:層雲〜1:積乱雲）に応じた高度勾配を返す
/// @param topScale 雲頂の高さ倍率。減衰の開始/終了高度だけを伸縮させ、雲底は動かさない
/// @note 積雲は層の 8 割程度まで発達させる。縦の伸びが小さいと横長のパンケーキに見える
float CloudHeightGradient(float h, float cloudType, float topScale)
{
    float t = max(topScale, 0.05f);
    float gStratus =
        saturate(Remap(h, 0.00f, 0.10f, 0.0f, 1.0f)) * saturate(Remap(h, 0.20f * t, 0.30f * t, 1.0f, 0.0f));
    float gCumulus =
        saturate(Remap(h, 0.00f, 0.20f, 0.0f, 1.0f)) * saturate(Remap(h, 0.40f * t, 0.85f * t, 1.0f, 0.0f));
    float gCumulonimbus =
        saturate(Remap(h, 0.00f, 0.10f, 0.0f, 1.0f)) * saturate(Remap(h, 0.70f * t, 1.00f * t, 1.0f, 0.0f));

    float lowBlend = saturate(cloudType * 2.0f);            // [0,0.5] を 0→1
    float highBlend = saturate((cloudType - 0.5f) * 2.0f);  // [0.5,1] を 0→1
    return lerp(lerp(gStratus, gCumulus, lowBlend), gCumulonimbus, highBlend);
}

/// @brief サンプル間隔からノイズのミップ段を求める
/// @param spacingM 隣り合うサンプルのワールド距離 [m]。0 を渡すと最細ミップ
/// @param noiseScaleM ノイズ 1 周期が覆うワールド距離 [m]
/// @param texels ノイズテクスチャの一辺のテクセル数
/// @details 間隔がテクセルより広いとき、その比の log2 段だけ縮小されたミップを引く。
///          粗いステップで細かいノイズを点サンプルすると、1 サンプルが代表しきれない
///          構造がブロック状の縞として残る。
float CloudNoiseLod(float spacingM, float noiseScaleM, float texels, float bias)
{
    float texelM = max(noiseScaleM, 1e-3f) / texels;
    return clamp(log2(max(spacingM / texelM, 1e-4f)) + bias, 0.0f, kCloudMaxNoiseLod);
}

/// @brief 風の移流（ワールド XZ 平面）と高度スキューを適用したサンプル座標
float3 CloudAdvectedPos(float3 worldPos, float h, CloudConstants c)
{
    float3 windDir = float3(c.windDirX, 0.0f, c.windDirZ);
    return worldPos + windDir * (c.windSpeedMPerS * c.timeSec) + h * windDir * c.heightSkewM;
}

/// @brief 天候マップのサンプル UV
/// @details 風方向の座標を縮めると、その方向へカバレッジの特徴が伸びて雲が筋状に並ぶ。
///          線形変換なので天候マップのタイル可能性は保たれる。
float2 CloudWeatherUv(float2 worldXZ, CloudConstants c)
{
    float2 wind = float2(c.windDirX, c.windDirZ);
    float windLen = length(wind);
    if (c.cloudStreetStretch <= 1.0f || windLen < 1e-4f)
    {
        return worldXZ / c.weatherMapScaleM;
    }

    float2 w = wind / windLen;
    float2 aligned = float2(dot(worldXZ, w) / c.cloudStreetStretch,
                            dot(worldXZ, float2(-w.y, w.x)));
    return aligned / c.weatherMapScaleM;
}

/// @brief 天候マップのサンプル（半径を与えると 4 タップの箱平均になる）
/// @param radiusM 平均する半径 [m]。0 で従来どおり 1 点サンプル
/// @details ブロック雲は密度を二値化するので、天候マップのエイリアスがそのまま
///          「しきい値をぎりぎり超えた孤立ボクセル」として空に散る。テクセル（百数十 m）より
///          広い間隔で 1 点だけ拾うのが原因なので、ボクセル 1 個分を平均して帯域制限する。
///          ベースノイズと違い天候マップはミップを持たない（CloudResources::CreateNoiseTextures）
///          ため、ミップ段では落とせない。
float4 SampleCloudWeather(float2 worldXZ, CloudConstants c, float radiusM,
                          Texture2D<float4> weatherMap, SamplerState samp)
{
    if (radiusM <= 0.0f)
    {
        return weatherMap.SampleLevel(samp, CloudWeatherUv(worldXZ, c), 0);
    }

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    [unroll] for (int i = 0; i < 4; ++i)
    {
        float2 offset = float2((i & 1) ? radiusM : -radiusM,
                               (i & 2) ? radiusM : -radiusM);
        sum += weatherMap.SampleLevel(samp, CloudWeatherUv(worldXZ + offset, c), 0);
    }
    return sum * 0.25f;
}

/// @brief 配置ペイントのサンプル
/// @return xyz = 置く雲の性質（雲量 / 雲タイプ / 雲頂高さ）、w = 影響度（0 でペイント無し）
/// @details 天候マップと違いワールド固定の矩形領域を 1 枚で覆う（タイルしない）。
///          領域外は影響度 0 なので、ペイントしていない空は手続き生成そのままになる。
///          paintRegionSizeM が 0 のときはサンプル自体を行わない（未使用時のコストをゼロにする）。
float4 SampleCloudPaint(float2 worldXZ, CloudConstants c,
                        Texture2D<float4> paintMap, SamplerState samp)
{
    if (c.paintRegionSizeM <= 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float2 uv = (worldXZ - float2(c.paintRegionCenterX, c.paintRegionCenterZ)) / c.paintRegionSizeM
              + 0.5f;
    if (any(uv < 0.0f) || any(uv > 1.0f))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float4 paint = paintMap.SampleLevel(samp, uv, 0);

    // 領域の外周で影響度を落とす。ここを切らないと領域境界に雲の断崖ができる
    float2 toEdge = min(uv, 1.0f - uv);
    paint.w *= saturate(min(toEdge.x, toEdge.y) / max(c.paintEdgeFade, 1e-4f));
    return paint;
}

/// @brief ディテール侵食を含まない密度
/// @param sampleSpacingM 隣り合うサンプルのワールド距離 [m]。ミップ段の決定に使う
/// @details サンライトマーチ・雲シャドウマップ・雲探索の大股走査が使う。
///          縦方向だけ小さいスケールでサンプルする（等方だと層内の縦の変化が乏しく平らな板に見える）。
float SampleCloudDensityCheap(float3 worldPos, float h, float sampleSpacingM, CloudConstants c,
                              Texture3D<float4> baseNoise, Texture2D<float4> weatherMap,
                              Texture2D<float4> paintMap, SamplerState samp)
{
    // スタイルによるサンプル座標の量子化。Realistic では恒等変換（CloudBlocky.hlsli）。
    // 範囲判定は量子化の後に行う。こうすると雲層の上端／下端がボクセル格子で切られ、
    // ブロック雲の天井と底が平らな面になる
    CloudQuantized q = CloudQuantizeSample(worldPos, h, c);
    worldPos = q.worldPos;
    h = q.heightFraction;

    if (h < 0.0f || h > 1.0f)
    {
        return 0.0f;
    }

    float3 sampleWS = CloudAdvectedPos(worldPos, h, c);

    // ミップを決める間隔。ブロック雲はマーチのステップ幅ではなくボクセルの大きさから引く
    // （視点距離でミップが変わるとブロックが点滅するため。理由は CloudQuantized::lodSpacingM）
    float lodSpacingM = (q.lodSpacingM > 0.0f) ? q.lodSpacingM : sampleSpacingM;

    // ベース形状。板スタイルは 3D ノイズを引かず、雲量だけで平面形状を決める
    float baseCloud = 1.0f;
    if (q.useBaseNoise)
    {
        float3 baseUvw = sampleWS / c.baseNoiseScaleM;
        baseUvw.y = sampleWS.y / (c.baseNoiseScaleM * c.baseNoiseVerticalScale);
        float baseLod = CloudNoiseLod(lodSpacingM, c.baseNoiseScaleM, kCloudBaseNoiseTexels, c.noiseLodBias);
        float4 base = baseNoise.SampleLevel(samp, baseUvw, baseLod);
        float lowFreqFBM = base.g * 0.625f + base.b * 0.25f + base.a * 0.125f;
        baseCloud = Remap(base.r, -(1.0f - lowFreqFBM), 1.0f, 0.0f, 1.0f);
    }

    // 天候マップと配置ペイントの合成。
    // ペイント側は「置く雲の性質」の絶対値なので、globalCoverage を掛けた後の
    // カバレッジへ混ぜる（掛ける前に混ぜると、曇り度を下げた空へ雲を描けなくなる）。
    // ブロック雲は両方を移流後の座標で引く。ボクセル格子だけが風で動くのに参照先が
    // 静止していると、セルがその場を滑って引いてくる値が変わり、二値化しているせいで
    // ブロックが 1 フレームで湧く／消える。ここを揃えるとセルの中身が時間不変になる
    float2 mapXZ = q.advectMaps ? sampleWS.xz : worldPos.xz;
    float4 weather = SampleCloudWeather(mapXZ, c, q.weatherFilterRadiusM, weatherMap, samp);
    float4 paint = SampleCloudPaint(mapXZ, c, paintMap, samp);

    float coverage = lerp(saturate(weather.r * c.globalCoverage), paint.r, paint.w);
    float cloudType = lerp(weather.g, paint.g, paint.w);
    float cloudTop = lerp(weather.b, paint.b, paint.w);

    // 高度勾配（雲タイプでブレンド）。板スタイルは層全体を一定厚みの箱にする
    if (!q.flatProfile)
    {
        float topScale = lerp(1.0f, cloudTop * 2.0f, c.cloudTopVariation);
        baseCloud *= CloudHeightGradient(h, cloudType, topScale);
    }

    // カバレッジ適用（縁を柔らかくしアンビル状を防ぐ: GPU Pro 7）。
    // 板スタイルは baseCloud が定数 1 なので、この Remap は coverage>0 なら恒等的に 1 になり、
    // coverage==0 では 0/0 の NaN を踏む。適用せず雲量そのものを密度にする
    float cloudWithCoverage = q.flatProfile
        ? 1.0f
        : saturate(Remap(baseCloud, 1.0f - coverage, 1.0f, 0.0f, 1.0f));
    // スタイルによる二値化。Realistic では素通し（CloudBlocky.hlsli）
    return CloudApplyStyleThreshold(saturate(cloudWithCoverage * coverage), c);
}

/// @brief ディテール侵食込みの密度
/// @param sampleSpacingM 隣り合うサンプルのワールド距離 [m]。ミップ段の決定に使う
/// @param detailStrength 侵食の強度（遠方では 0 へフェードさせエイリアシングを防ぐ）
float SampleCloudDensity(float3 worldPos, float h, float sampleSpacingM, float detailStrength,
                         CloudConstants c,
                         Texture3D<float4> baseNoise, Texture3D<float4> detailNoise,
                         Texture2D<float4> weatherMap, Texture2D<float4> paintMap,
                         SamplerState samp)
{
    float density = SampleCloudDensityCheap(worldPos, h, sampleSpacingM, c,
                                            baseNoise, weatherMap, paintMap, samp);
    // ブロック雲は縁の侵食を行わない。二値化して作った立方体の面を高周波ノイズで
    // 削ると、せっかくの平らな面がモヤに戻る
    if (density <= 0.0f || detailStrength <= 0.0f || c.styleIndex != CLOUD_STYLE_REALISTIC)
    {
        return density;
    }

    float3 sampleWS = CloudAdvectedPos(worldPos, h, c);
    float detailLod = CloudNoiseLod(sampleSpacingM, c.detailNoiseScaleM, kCloudDetailNoiseTexels,
                                    c.noiseLodBias);
    float4 detail = detailNoise.SampleLevel(samp, sampleWS / c.detailNoiseScaleM, detailLod);
    float highFreqFBM = detail.r * 0.625f + detail.g * 0.25f + detail.b * 0.125f;
    // 層底では billowy、上部では wispy に（高さで反転）
    float highFreqModifier = lerp(highFreqFBM, 1.0f - highFreqFBM, saturate(h * 10.0f));
    return saturate(Remap(density, highFreqModifier * detailStrength, 1.0f, 0.0f, 1.0f));
}

#endif // CLOUD_COMMON_HLSLI
