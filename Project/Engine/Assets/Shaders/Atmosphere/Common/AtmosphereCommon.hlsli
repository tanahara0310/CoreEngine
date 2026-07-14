/// @file AtmosphereCommon.hlsli
/// @brief 大気散乱の共通定義（Bruneton/Hillaire 方式）
/// @details 単位系: 距離は km、散乱・吸収係数は 1/km。
///          float32 精度を保つため、CPU 側（メートル）から km へ変換された値を受け取る。
///          座標系は惑星中心を原点とし、ワールド +Y を天頂方向として扱う。

#ifndef ATMOSPHERE_COMMON_HLSLI
#define ATMOSPHERE_COMMON_HLSLI

static const float ATMOSPHERE_PI = 3.14159265358979323846f;

/// @brief 大気散乱の定数バッファ（C++ 側 AtmosphereShaderConstants と一致させること）
struct AtmosphereConstants
{
    float3 sunDirection;          // 太陽光の進行方向（正規化。太陽の位置方向は -sunDirection）
    float sunIntensity;           // 太陽光強度スケール

    float3 sunColor;              // 太陽光の色
    float planetRadiusKm;         // 惑星半径 [km]

    float3 rayleighScattering;    // レイリー散乱係数 [1/km]
    float rayleighScaleHeightKm;  // レイリー密度スケールハイト [km]

    float3 ozoneAbsorption;       // オゾン吸収係数 [1/km]
    float atmosphereTopRadiusKm;  // 大気圏上端半径 [km]

    float mieScattering;          // ミー散乱係数 [1/km]
    float mieAbsorption;          // ミー吸収係数 [1/km]
    float mieScaleHeightKm;       // ミー密度スケールハイト [km]
    float miePhaseG;              // Henyey-Greenstein 非対称度

    float ozoneLayerCenterKm;     // オゾン層中心高度 [km]
    float ozoneLayerHalfWidthKm;  // オゾン層半幅 [km]
    float cameraRadiusKm;         // カメラの惑星中心距離 [km]
    float sunDiskHalfAngleRad;    // 太陽の視半径 [rad]

    float3 groundAlbedo;          // 地表アルベド
    float sunDiskLuminanceScale;  // 太陽ディスクの輝度スケール

    float4x4 invViewProj;         // 逆ビュープロジェクション行列（Aerial Perspective 用）

    float3 cameraWorldPos;        // カメラのワールド座標 [m]
    float groundLevelY;           // 地表とみなすワールドY座標 [m]（InfiniteGroundObject 等の実床と揃える）

    float multiScatteringFactor;  // 多重散乱寄与のスケール（1=近似そのまま。等方Psi_ms近似は薄明時に過大評価するため抑制用）
    float3 constantsPad;
};

/// @brief レイと原点中心球の交差判定
/// @param rayOrigin レイ原点（惑星中心基準）
/// @param rayDir 正規化済みレイ方向
/// @param radius 球半径
/// @return 最近傍の正の交差距離 t。交差しない場合は -1
float RaySphereIntersectNearest(float3 rayOrigin, float3 rayDir, float radius)
{
    float b = dot(rayOrigin, rayDir);
    float c = dot(rayOrigin, rayOrigin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
    {
        return -1.0f;
    }
    float s = sqrt(discriminant);
    float t0 = -b - s;
    float t1 = -b + s;
    if (t1 < 0.0f)
    {
        return -1.0f;
    }
    return (t0 < 0.0f) ? t1 : t0;
}

/// @brief 高度 [km] における媒質の相対密度（x: レイリー, y: ミー, z: オゾン）
/// @details レイリー・ミーは指数分布、オゾンは層中心をピークとするテント型分布
float3 ComputeMediumDensity(float heightKm, AtmosphereConstants atm)
{
    float h = max(heightKm, 0.0f);
    float densityRayleigh = exp(-h / atm.rayleighScaleHeightKm);
    float densityMie = exp(-h / atm.mieScaleHeightKm);
    float densityOzone = saturate(1.0f - abs(h - atm.ozoneLayerCenterKm) / atm.ozoneLayerHalfWidthKm);
    return float3(densityRayleigh, densityMie, densityOzone);
}

/// @brief 高度 [km] における消散係数（散乱＋吸収） [1/km]
float3 ComputeExtinction(float heightKm, AtmosphereConstants atm)
{
    float3 density = ComputeMediumDensity(heightKm, atm);
    return atm.rayleighScattering * density.x
         + (atm.mieScattering + atm.mieAbsorption).xxx * density.y
         + atm.ozoneAbsorption * density.z;
}

/// @brief レイリー位相関数
float RayleighPhase(float cosTheta)
{
    return 3.0f / (16.0f * ATMOSPHERE_PI) * (1.0f + cosTheta * cosTheta);
}

/// @brief Henyey-Greenstein 位相関数（ミー散乱の前方散乱を近似）
float HenyeyGreensteinPhase(float g, float cosTheta)
{
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * ATMOSPHERE_PI * denom * sqrt(max(denom, 1e-6f)));
}

// ============================================================================
// Transmittance LUT パラメータ化（Bruneton/Hillaire 方式）
// 入力: 惑星中心距離 r [km]、天頂角余弦 mu
// 地平線付近の精度を確保する非線形マッピング。
// LUT の範囲外（地平線下へ向かうレイ）は UV クランプにより
// 地平線すれすれの値（ほぼ0）へ滑らかに収束する。
// ============================================================================

static const uint TRANSMITTANCE_LUT_WIDTH = 256;
static const uint TRANSMITTANCE_LUT_HEIGHT = 64;

/// @brief (r, mu) から Transmittance LUT の UV を求める
float2 TransmittanceParamsToUv(float radiusKm, float mu, float bottomRadiusKm, float topRadiusKm)
{
    float H = sqrt(max(0.0f, topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm));
    float rho = sqrt(max(0.0f, radiusKm * radiusKm - bottomRadiusKm * bottomRadiusKm));

    // レイが大気圏上端に到達するまでの距離
    float discriminant = radiusKm * radiusKm * (mu * mu - 1.0f) + topRadiusKm * topRadiusKm;
    float d = max(0.0f, -radiusKm * mu + sqrt(max(0.0f, discriminant)));

    float dMin = topRadiusKm - radiusKm;
    float dMax = rho + H;
    float xMu = (d - dMin) / max(dMax - dMin, 1e-6f);
    float xR = rho / max(H, 1e-6f);

    // 地平線より下を向くレイでは xMu が 1 を超える（このパラメータ化は大気圏上端へ
    // 到達するレイしか表現できない）。呼び出し側のサンプラーが WRAP だと反対端の値
    // （＝ほぼ透過率 1.0）を拾い、太陽が沈む瞬間に透過率が跳ね上がる。
    // サンプラー設定に依存しないよう、ここで明示的にクランプする。
    return saturate(float2(xMu, xR));
}

/// @brief Transmittance LUT の UV から (r, mu) を復元する
void UvToTransmittanceParams(float2 uv, float bottomRadiusKm, float topRadiusKm,
                             out float radiusKm, out float mu)
{
    float xMu = uv.x;
    float xR = uv.y;

    float H = sqrt(max(0.0f, topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm));
    float rho = H * xR;
    radiusKm = sqrt(rho * rho + bottomRadiusKm * bottomRadiusKm);

    float dMin = topRadiusKm - radiusKm;
    float dMax = rho + H;
    float d = dMin + xMu * (dMax - dMin);

    mu = (d <= 0.0f)
        ? 1.0f
        : (H * H - rho * rho - d * d) / (2.0f * radiusKm * d);
    mu = clamp(mu, -1.0f, 1.0f);
}

/// @brief 惑星本体による太陽の可視率（1=完全に見える, 0=地平線下に完全に沈んだ）
/// @param radiusKm 評価点の惑星中心距離 [km]
/// @param muSun 評価点での太陽天頂角余弦
/// @details Transmittance LUT のパラメータ化は「大気圏上端へ抜けるレイ」しか表現できず、
///          地平線より下の太陽に対しては地平線すれすれの透過率で頭打ちになる。
///          そのため LUT 単体では日没後も光が消えない。ここで幾何的な遮蔽を掛け直す。
///          太陽は角半径を持つため、地平線を跨ぐ間は部分的に隠れる（＝滑らかに減衰する）。
float PlanetSunVisibility(float radiusKm, float muSun, AtmosphereConstants atm)
{
    float cosHorizon = -sqrt(max(0.0f,
        1.0f - (atm.planetRadiusKm * atm.planetRadiusKm) / (radiusKm * radiusKm)));

    // 地平線付近では d(cos)/d(角度) ≈ -1 なので、角半径をそのまま余弦の幅として使える
    float softness = max(atm.sunDiskHalfAngleRad, 1e-4f);
    return smoothstep(cosHorizon - softness, cosHorizon + softness, muSun);
}

/// @brief Transmittance LUT から指定点→太陽方向の透過率を取得する
/// @param transmittanceLUT 事前計算済み透過率 LUT
/// @param position 惑星中心基準の位置 [km]
/// @param toSun 太陽の位置方向（正規化）
/// @details 惑星による遮蔽（アースシャドウ）を含む。日没後に空・雲・太陽ディスクが
///          正しく消えるために必須。
float3 SampleTransmittanceToSun(Texture2D<float4> transmittanceLUT, SamplerState lutSampler,
                                float3 position, float3 toSun, AtmosphereConstants atm)
{
    float radiusKm = length(position);
    float mu = dot(position / radiusKm, toSun);
    float2 uv = TransmittanceParamsToUv(radiusKm, mu, atm.planetRadiusKm, atm.atmosphereTopRadiusKm);
    float3 transmittance = transmittanceLUT.SampleLevel(lutSampler, uv, 0).rgb;
    return transmittance * PlanetSunVisibility(radiusKm, mu, atm);
}

// ============================================================================
// Multi-Scattering LUT パラメータ化（Hillaire 2020）
// 入力: 太陽天頂角余弦 muSun (x軸)、高度（惑星中心距離 r）(y軸)
// 各テクセルに「無限次数の多重散乱寄与 Psi_ms」を格納する。
// ============================================================================

static const uint MULTISCATTERING_LUT_SIZE = 32;

/// @brief (muSun, r) から Multi-Scattering LUT の UV を求める
/// @details muSun は線形ではなく √エンコードで格納する。昼→夜の遷移（muSun ≈ 0、
///          太陽が地平線を跨ぐ瞬間）は数度の範囲で急激に起こるが、線形マッピングだと
///          32 テクセルで 1 テクセル ≈ 3.6°となり、日没直後でもバイリニア補間が
///          「太陽が地平線上にある構成」の明るい多重散乱を拾ってしまう。
///          その漏れは等方（方位無依存）なので、反太陽側の空が太陽側と同じ明るさに
///          持ち上がる（= 日没時に反太陽側が暗くならない不具合の主因）。
///          √エンコードは terminator 付近の分解能を約16倍にし、この漏れを解消する。
float2 MultiScatteringParamsToUv(float muSun, float radiusKm, float bottomRadiusKm, float topRadiusKm)
{
    float xMuSun = 0.5f + 0.5f * sign(muSun) * sqrt(abs(muSun));
    float xR = saturate((radiusKm - bottomRadiusKm) / max(topRadiusKm - bottomRadiusKm, 1e-6f));
    return float2(saturate(xMuSun), xR);
}

/// @brief Multi-Scattering LUT の UV から (muSun, r) を復元する
void UvToMultiScatteringParams(float2 uv, float bottomRadiusKm, float topRadiusKm,
                               out float muSun, out float radiusKm)
{
    float t = uv.x * 2.0f - 1.0f;
    muSun = sign(t) * t * t; // √エンコードの逆変換
    radiusKm = lerp(bottomRadiusKm, topRadiusKm, uv.y);
}

/// @brief 多重散乱項に掛ける惑星影の減衰
/// @details MS LUT は「2次散乱の発生点」での地球影しか含まず、無限次数への増幅
///          1/(1 - f_ms) は影をまったく考慮しない（Hillaire 2020 が自認する
///          薄明時の過大評価）。高次散乱の実効的な光源は各点の上空の日照大気なので、
///          太陽が「大気の実効厚（レイリースケールハイト×3 ≈ 24km）の上空から見た
///          地平線」より沈むにつれて滑らかに 0 へ減衰させる（地上なら太陽高度 0°→
///          約 -5° で 1→0。civil twilight の減衰と一致する）。
///          これが無いと日没時に等方の多重散乱だけが空全体を持ち上げ、
///          地球影側（反太陽側）が太陽側と同じ明るさになってしまう。
float MultiScatteringPlanetShadow(float radiusKm, float muSun, AtmosphereConstants atm)
{
    float cosHorizonLocal = -sqrt(max(0.0f,
        1.0f - (atm.planetRadiusKm * atm.planetRadiusKm) / (radiusKm * radiusKm)));
    float rEffKm = radiusKm + 3.0f * atm.rayleighScaleHeightKm;
    float cosHorizonEff = -sqrt(max(0.0f,
        1.0f - (atm.planetRadiusKm * atm.planetRadiusKm) / (rEffKm * rEffKm)));
    return smoothstep(cosHorizonEff, cosHorizonLocal, muSun);
}

/// @brief Multi-Scattering LUT から指定点の多重散乱寄与 Psi_ms を取得する
/// @details 惑星影の減衰（MultiScatteringPlanetShadow）込み。
float3 SampleMultiScattering(Texture2D<float4> multiScatteringLUT, SamplerState lutSampler,
                             float3 position, float3 toSun, AtmosphereConstants atm)
{
    float radiusKm = length(position);
    float muSun = dot(position / radiusKm, toSun);
    float2 uv = MultiScatteringParamsToUv(muSun, radiusKm, atm.planetRadiusKm, atm.atmosphereTopRadiusKm);
    return multiScatteringLUT.SampleLevel(lutSampler, uv, 0).rgb
         * MultiScatteringPlanetShadow(radiusKm, muSun, atm);
}

// ============================================================================
// Sky-View LUT パラメータ化（Hillaire 2020）
// 経度方向(u): 太陽との相対方位角の余弦（対称性により半周分のみ格納、√で太陽側に解像度集中）
// 緯度方向(v): 地平線を境に上下分割し、√エンコードで地平線付近に解像度集中
// ============================================================================

static const uint SKYVIEW_LUT_WIDTH = 192;
static const uint SKYVIEW_LUT_HEIGHT = 108;

/// @brief (視線天頂角余弦, 太陽相対方位角余弦) から Sky-View LUT の UV を求める
/// @param intersectGround 視線が地面と交差するか
float2 SkyViewParamsToUv(bool intersectGround, float viewZenithCos, float lightViewCos,
                         float radiusKm, float bottomRadiusKm)
{
    float2 uv;

    float vHorizon = sqrt(max(0.0f, radiusKm * radiusKm - bottomRadiusKm * bottomRadiusKm));
    float cosBeta = vHorizon / radiusKm; // 地平線角の余弦
    float beta = acos(clamp(cosBeta, -1.0f, 1.0f));
    float zenithHorizonAngle = ATMOSPHERE_PI - beta;
    float viewZenithAngle = acos(clamp(viewZenithCos, -1.0f, 1.0f));

    if (!intersectGround)
    {
        float coord = viewZenithAngle / zenithHorizonAngle;
        coord = 1.0f - coord;
        coord = sqrt(max(coord, 0.0f));
        coord = 1.0f - coord;
        uv.y = coord * 0.5f;
    }
    else
    {
        float coord = (viewZenithAngle - zenithHorizonAngle) / max(beta, 1e-5f);
        coord = sqrt(max(coord, 0.0f));
        uv.y = coord * 0.5f + 0.5f;
    }

    float coordX = -lightViewCos * 0.5f + 0.5f;
    coordX = sqrt(saturate(coordX));
    uv.x = coordX;

    return uv;
}

/// @brief Sky-View LUT の UV から (視線天頂角余弦, 太陽相対方位角余弦) を復元する
void UvToSkyViewParams(float2 uv, float radiusKm, float bottomRadiusKm,
                       out float viewZenithCos, out float lightViewCos)
{
    float vHorizon = sqrt(max(0.0f, radiusKm * radiusKm - bottomRadiusKm * bottomRadiusKm));
    float cosBeta = vHorizon / radiusKm;
    float beta = acos(clamp(cosBeta, -1.0f, 1.0f));
    float zenithHorizonAngle = ATMOSPHERE_PI - beta;

    float viewZenithAngle;
    if (uv.y < 0.5f)
    {
        float coord = 2.0f * uv.y;
        coord = 1.0f - coord;
        coord = coord * coord;
        coord = 1.0f - coord;
        viewZenithAngle = zenithHorizonAngle * coord;
    }
    else
    {
        float coord = uv.y * 2.0f - 1.0f;
        viewZenithAngle = zenithHorizonAngle + beta * (coord * coord);
    }
    viewZenithCos = cos(viewZenithAngle);

    float coordX = uv.x;
    coordX = coordX * coordX;
    lightViewCos = -(coordX * 2.0f - 1.0f);
}

/// @brief 視線レイに沿った散乱（単一＋多重）の積分
/// @details Sky-View LUT 生成と（LUT を使わない場合の）直接描画で共有する。
/// @param rayOrigin 惑星中心基準のレイ原点 [km]
/// @param rayDir 正規化済み視線方向
/// @param toSun 太陽の位置方向（正規化）
/// @param stepCount 積分ステップ数
float3 IntegrateScatteredLuminance(
    float3 rayOrigin, float3 rayDir, float3 toSun, AtmosphereConstants atm,
    Texture2D<float4> transmittanceLUT, Texture2D<float4> multiScatteringLUT,
    SamplerState lutSampler, int stepCount)
{
    float tTop = RaySphereIntersectNearest(rayOrigin, rayDir, atm.atmosphereTopRadiusKm);
    if (tTop < 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float tMax = tTop;
    float tGround = RaySphereIntersectNearest(rayOrigin, rayDir, atm.planetRadiusKm);
    if (tGround >= 0.0f)
    {
        tMax = tGround;
    }

    float dt = tMax / stepCount;

    float cosTheta = dot(rayDir, toSun);
    float phaseRayleigh = RayleighPhase(cosTheta);
    float phaseMie = HenyeyGreensteinPhase(atm.miePhaseG, cosTheta);

    float3 luminance = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < stepCount; ++i)
    {
        float3 p = rayOrigin + rayDir * ((i + 0.5f) * dt);
        float heightKm = length(p) - atm.planetRadiusKm;

        float3 density = ComputeMediumDensity(heightKm, atm);
        float3 sigmaT = max(ComputeExtinction(heightKm, atm), 1e-7f);
        float3 stepTransmittance = exp(-sigmaT * dt);

        float3 transmittanceToSun = SampleTransmittanceToSun(transmittanceLUT, lutSampler, p, toSun, atm);

        float3 scattering = atm.rayleighScattering * density.x * phaseRayleigh
                          + atm.mieScattering.xxx * density.y * phaseMie;

        float3 scatteringNoPhase = atm.rayleighScattering * density.x
                                 + atm.mieScattering.xxx * density.y;
        float3 psiMs = SampleMultiScattering(multiScatteringLUT, lutSampler, p, toSun, atm);

        // 放射源項をステップ内で解析的に積分する（Hillaire 2020 §5.3、エネルギー保存）。
        // 旧実装（ステップ末端の透過率 × S × dt）は地平線際の長経路（dt が数十 km）で
        // 濃い低高度の寄与を大きく取りこぼし、特に日没の太陽側の赤い散乱帯を過小評価していた
        float3 S = transmittanceToSun * scattering
                 + psiMs * scatteringNoPhase * atm.multiScatteringFactor;
        float3 Sint = (S - S * stepTransmittance) / sigmaT;

        luminance += throughput * Sint;
        throughput *= stepTransmittance;
    }

    return luminance;
}

// ============================================================================
// Camera Volume (Aerial Perspective) LUT
// froxel 状 3D テクスチャ。xy = スクリーンUV、z = カメラからの距離スライス。
// 各 froxel に「カメラからその距離までの inscattering (rgb) と平均透過率 (a)」を格納。
// ============================================================================

static const uint CAMERA_VOLUME_SIZE = 32;
static const float CAMERA_VOLUME_KM_PER_SLICE = 4.0f; // 1スライスあたりの距離 [km]（最大 32*4=128km）

/// @brief カメラからの距離 [km] を CameraVolume の Wスライス座標 [0,1] へ変換する
float CameraVolumeDistanceToW(float distanceKm)
{
    float slice = distanceKm / CAMERA_VOLUME_KM_PER_SLICE;
    return saturate(slice / CAMERA_VOLUME_SIZE);
}

/// @brief 視線レイに沿った散乱の積分（距離指定版・透過率出力付き）
/// @details Aerial Perspective 用。tMaxKm まで積分し、視点までの透過率も返す。
float3 IntegrateScatteredLuminanceToDistance(
    float3 rayOrigin, float3 rayDir, float3 toSun, float tMaxKm, AtmosphereConstants atm,
    Texture2D<float4> transmittanceLUT, Texture2D<float4> multiScatteringLUT,
    SamplerState lutSampler, int stepCount, out float3 transmittanceOut)
{
    transmittanceOut = float3(1.0f, 1.0f, 1.0f);

    // 大気圏上端・地面でクリップ
    float tTop = RaySphereIntersectNearest(rayOrigin, rayDir, atm.atmosphereTopRadiusKm);
    float tMax = tMaxKm;
    if (tTop >= 0.0f)
    {
        tMax = min(tMax, tTop);
    }
    float tGround = RaySphereIntersectNearest(rayOrigin, rayDir, atm.planetRadiusKm);
    if (tGround >= 0.0f)
    {
        tMax = min(tMax, tGround);
    }
    if (tMax <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float dt = tMax / stepCount;

    float cosTheta = dot(rayDir, toSun);
    float phaseRayleigh = RayleighPhase(cosTheta);
    float phaseMie = HenyeyGreensteinPhase(atm.miePhaseG, cosTheta);

    float3 luminance = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < stepCount; ++i)
    {
        float3 p = rayOrigin + rayDir * ((i + 0.5f) * dt);
        float heightKm = length(p) - atm.planetRadiusKm;

        float3 density = ComputeMediumDensity(heightKm, atm);
        float3 sigmaT = max(ComputeExtinction(heightKm, atm), 1e-7f);
        float3 stepTransmittance = exp(-sigmaT * dt);

        float3 transmittanceToSun = SampleTransmittanceToSun(transmittanceLUT, lutSampler, p, toSun, atm);

        float3 scattering = atm.rayleighScattering * density.x * phaseRayleigh
                          + atm.mieScattering.xxx * density.y * phaseMie;

        float3 scatteringNoPhase = atm.rayleighScattering * density.x
                                 + atm.mieScattering.xxx * density.y;
        float3 psiMs = SampleMultiScattering(multiScatteringLUT, lutSampler, p, toSun, atm);

        // ステップ内解析積分（IntegrateScatteredLuminance と同じエネルギー保存形式）
        float3 S = transmittanceToSun * scattering
                 + psiMs * scatteringNoPhase * atm.multiScatteringFactor;
        float3 Sint = (S - S * stepTransmittance) / sigmaT;

        luminance += throughput * Sint;
        throughput *= stepTransmittance;
    }

    transmittanceOut = throughput;
    return luminance;
}

/// @brief 指定点から太陽方向へ大気圏上端まで積分した透過率（レイマーチ版）
/// @param position 惑星中心基準の位置 [km]
/// @param toSun 太陽の位置方向（正規化）
/// @param stepCount 積分ステップ数
/// @note Phase 2 で Transmittance LUT サンプルに置き換える
float3 ComputeTransmittanceToSunRayMarch(float3 position, float3 toSun, AtmosphereConstants atm, int stepCount)
{
    // 惑星本体に遮られる場合は届かない
    if (RaySphereIntersectNearest(position, toSun, atm.planetRadiusKm) >= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float tTop = RaySphereIntersectNearest(position, toSun, atm.atmosphereTopRadiusKm);
    if (tTop <= 0.0f)
    {
        return float3(1.0f, 1.0f, 1.0f);
    }

    float dt = tTop / stepCount;
    float3 opticalDepth = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < stepCount; ++i)
    {
        float3 p = position + toSun * ((i + 0.5f) * dt);
        float heightKm = length(p) - atm.planetRadiusKm;
        opticalDepth += ComputeExtinction(heightKm, atm) * dt;
    }
    return exp(-opticalDepth);
}

#endif // ATMOSPHERE_COMMON_HLSLI
