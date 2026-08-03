// ============================================================
// DXR 水面共通サーフェス評価
// RTWaterRefraction / RTWaterReflection / RTWaterCaustics から共有する。
//   - Gerstner 経路: EvaluateWaterOffsetGerstner / EvaluateWaterNormalGerstner
//   - FFT Ocean 経路: SampleFFTOceanCascadeDisplacement / SampleFFTOceanCascadeNormal
// どちらを使うかは各シェーダーの IsFFTOceanSurfaceActive() が判定する
// （以前はここに simulationType 分岐と平坦を返す FFT スタブがあり、
//   呼ぶと「波の無い水面」になる罠だったため撤去した）。
// ============================================================
#ifndef RT_WATER_SURFACE_COMMON_HLSLI
#define RT_WATER_SURFACE_COMMON_HLSLI

#include "../Common/GerstnerWave.hlsli"
#include "../Common/FFTOceanCascade.hlsli"

cbuffer WaterSurfaceData : register(b1)
{
    float gSurfaceWaterHeight;
    uint gSurfaceActiveWaveCount;
    float gSurfaceTime;
    uint gSurfaceSimulationType;
    GerstnerWave gSurfaceWaves[GERSTNER_MAX_WAVE_COUNT];
    // ---- FFT 有効情報（Phase 3 で各シェーダー b0 の 3 重定義から一本化）----
    // C++ 側 WaterRayTracingPassBase::WaterSurfaceConstants と一致必須。
    uint gSurfaceFFTOceanEnabled;
    uint gSurfaceFFTOceanResolution;
    // 水面メッシュの頂点グリッド分割数（coverage 判定のメッシュ同一基準化用。
    // 以前は RTWaterCaustics.hlsl に 256 がハードコードされていた）
    float gSurfaceMeshSubdivisions;
    float gSurfacePad0;
};

static const uint kWaterSurfaceModelTypeGerstner = 0;
static const uint kWaterSurfaceModelTypeFFTOcean = 1;

/// @brief FFT Ocean 波面テクスチャを使うか（3 シェーダー共通の有効判定）
bool UseFFTOceanSurface()
{
    return gSurfaceSimulationType == kWaterSurfaceModelTypeFFTOcean
        && gSurfaceFFTOceanEnabled != 0
        && gSurfaceFFTOceanResolution > 0;
}

float3 EvaluateWaterOffsetGerstner(float2 worldXZ)
{
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < kMaxGerstnerWaveCount; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        totalOffset += EvaluateGerstnerWaveOffset(gSurfaceWaves[waveIndex], gSurfaceTime, worldXZ);
    }

    return totalOffset;
}

float3 EvaluateWaterNormalGerstner(float2 worldXZ)
{
    float3 dPdX = float3(1.0f, 0.0f, 0.0f);
    float3 dPdZ = float3(0.0f, 0.0f, 1.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < kMaxGerstnerWaveCount; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        AccumulateGerstnerWaveDerivatives(
            gSurfaceWaves[waveIndex], gSurfaceTime, worldXZ, dPdX, dPdZ);
    }

    return BuildGerstnerNormal(dPdX, dPdZ);
}

// ------------------------------------------------------------
// FFT Ocean テクスチャ（変位・法線）のバイリニアサンプリング
// FFT Ocean はテクセル解像度が有限のため、Load によるニアレストサンプリングでは
// テクセル境界で法線が階段状に変化し、屈折方向がテクセル単位でジャンプする。
// このジャンプは再投影 UV の飛びとなり、画面上に FFT 解像度グリッドに一致した
// 四角いモザイク状の破綻として現れる。RTWaterRefraction / RTWaterCaustics の
// 両方で同じバイリニア補間を使い、この量子化を避ける。
// ------------------------------------------------------------
uint WrapFFTOceanCoord(int coord, int resolution)
{
    int wrapped = coord % resolution;
    if (wrapped < 0)
    {
        wrapped += resolution;
    }

    return (uint)wrapped;
}

// ============================================================
// マルチスケール・カスケード（Texture2DArray）
// ラスタ描画（FFTWater.VS / Water.PS）と同一の「回転格子系ワールドXZ / パッチ長」写像で
// 各スライスをサンプルして合算する。定数と写像は Common/FFTOceanCascade.hlsli が唯一の情報源。
// ============================================================

/// @brief 配列テクスチャの1スライスをワールドXZ/パッチ長でバイリニアサンプルする
float4 SampleFFTOceanArraySlice(Texture2DArray<float4> textureData, float2 worldXZ, float patchLength, uint slice, uint resolution)
{
    const float2 uv = frac(worldXZ / patchLength);
    const float resolutionF = (float)resolution;
    const float2 texelPos = uv * resolutionF - 0.5f.xx;
    const int2 baseCoord = int2(floor(texelPos));
    const float2 fracCoord = frac(texelPos);
    const int wrapResolution = (int)resolution;

    const uint2 p00 = uint2(WrapFFTOceanCoord(baseCoord.x, wrapResolution), WrapFFTOceanCoord(baseCoord.y, wrapResolution));
    const uint2 p10 = uint2(WrapFFTOceanCoord(baseCoord.x + 1, wrapResolution), WrapFFTOceanCoord(baseCoord.y, wrapResolution));
    const uint2 p01 = uint2(WrapFFTOceanCoord(baseCoord.x, wrapResolution), WrapFFTOceanCoord(baseCoord.y + 1, wrapResolution));
    const uint2 p11 = uint2(WrapFFTOceanCoord(baseCoord.x + 1, wrapResolution), WrapFFTOceanCoord(baseCoord.y + 1, wrapResolution));

    const float4 c00 = textureData.Load(int4(p00, slice, 0));
    const float4 c10 = textureData.Load(int4(p10, slice, 0));
    const float4 c01 = textureData.Load(int4(p01, slice, 0));
    const float4 c11 = textureData.Load(int4(p11, slice, 0));

    const float4 cx0 = lerp(c00, c10, fracCoord.x);
    const float4 cx1 = lerp(c01, c11, fracCoord.x);
    return lerp(cx0, cx1, fracCoord.y);
}

/// @brief 全カスケードの変位を合算する
float3 SampleFFTOceanCascadeDisplacement(Texture2DArray<float4> textureData, float2 worldXZ, uint resolution)
{
    float3 displacement = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int c = 0; c < kFFTCascadeCount; ++c)
    {
        const float2 gridXZ = RotateToFFTCascadeGrid(worldXZ, c);
        const float3 d = SampleFFTOceanArraySlice(textureData, gridXZ, kFFTCascadePatch[c], (uint)c, resolution).xyz;
        const float2 horizontal = RotateFromFFTCascadeGrid(float2(d.x, d.z), c);
        displacement += float3(horizontal.x, d.y, horizontal.y);
    }
    // 波群エンベロープでタイル周期を崩す（ラスタ描画と同一の変調）
    return displacement * ComputeFFTWaveGroupEnvelope(worldXZ);
}

/// @brief 全カスケードの法線（傾き）を合算したワールド法線を返す
float3 SampleFFTOceanCascadeNormal(Texture2DArray<float4> textureData, float2 worldXZ, uint resolution)
{
    float2 slope = float2(0.0f, 0.0f);
    [unroll]
    for (int c = 0; c < kFFTCascadeCount; ++c)
    {
        const float2 gridXZ = RotateToFFTCascadeGrid(worldXZ, c);
        const float3 enc = SampleFFTOceanArraySlice(textureData, gridXZ, kFFTCascadePatch[c], (uint)c, resolution).xyz;
        const float3 nLocal = normalize(enc * 2.0f - 1.0f);
        // 回転格子系の傾きをワールドへ逆回転してから合算する
        slope += RotateFromFFTCascadeGrid(nLocal.xz / max(nLocal.y, 1.0e-3f), c);
    }
    // 波群エンベロープ: 変位と同じ変調を傾きへ掛けて波面の幾何と一致させる
    slope *= ComputeFFTWaveGroupEnvelope(worldXZ);
    float3 n = normalize(float3(slope.x, 1.0f, slope.y));
    return n.y < 0.0f ? -n : n;
}

// ============================================================
// 方式選択つきの波面評価（Gerstner / FFT を b1 の情報で自動選択）
// 以前は同型のラッパーが 3 シェーダーに 6 個複製されていた。
// テクスチャは引数で受ける（このヘッダーにリソース宣言を置くと、
// 参照しないシェーダー側に未参照宣言が残り lib_6_6 の Trace が 2 倍になる既知問題）。
// ============================================================

/// @brief 波面の変位（水平＋高さ）を評価する
float3 EvaluateWaterOffset(Texture2DArray<float4> displacementTex, float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterOffsetGerstner(worldXZ);
    }
    return SampleFFTOceanCascadeDisplacement(displacementTex, worldXZ, gSurfaceFFTOceanResolution);
}

/// @brief 波面のワールド法線を評価する
float3 EvaluateWaterNormal(Texture2DArray<float4> normalTex, float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterNormalGerstner(worldXZ);
    }
    return SampleFFTOceanCascadeNormal(normalTex, worldXZ, gSurfaceFFTOceanResolution);
}

// ============================================================
// レイペイロードと失敗理由コード（3 シェーダー共通）
// ============================================================

/// @brief ヒット距離と有無だけを運ぶ共通ペイロード（旧 Caustics/Refraction/ReflectionPayload）
struct RTWaterPayload
{
    float hitT;
    float hitFlag;
};

// 失敗理由コード（[0, 0.5) の範囲。値は旧実装から不変＝デバッグ表示の色対応を維持）。
// 読み手（Water.PS / デバッグ可視化）は alpha >= 0.5 を成功と判定する。
static const float kRTReasonBackground = 1.0f / 255.0f;              // 背景（空）/ ワールド位置なし
static const float kRTReasonNearZeroSceneDistance = 2.0f / 255.0f;
static const float kRTReasonParallelToWater = 3.0f / 255.0f;
static const float kRTReasonInvalidPlaneIntersection = 4.0f / 255.0f;
static const float kRTReasonInvalidBounceVector = 5.0f / 255.0f;     // refract/reflect の結果が無効
static const float kRTReasonTraceMiss = 6.0f / 255.0f;
static const float kRTReasonInvalidClip = 7.0f / 255.0f;
static const float kRTReasonDepthMismatch = 9.0f / 255.0f;

// ============================================================
// スクリーン空間再投影の共通ヘルパー
// ============================================================

/// @brief 画面外へはみ出した距離に応じた対称フェード（画面内は減衰しない）
float ComputeRTScreenBoundsFade(float2 uv, float2 screenSize)
{
    const float2 outsideDistancePixels = abs(uv - saturate(uv)) * screenSize;
    const float outsidePixels = max(outsideDistancePixels.x, outsideDistancePixels.y);
    const float fadeMarginPixels = 32.0f;
    return 1.0f - smoothstep(0.0f, fadeMarginPixels, outsidePixels);
}

/// @brief スカラー診断値のトーンマップ表示（1 - exp(-x·scale)）
float3 VisualizeRTScalar(float value, float displayScale)
{
    const float scaled = 1.0f - exp(-max(value, 0.0f) * max(displayScale, 1.0e-4f));
    return scaled.xxx;
}

/// @brief フラット水面平面での交点をシードに、波を反映した固定点反復（3回）で
///        実際の水面交点を求める（屈折・反射で完全同一のロジック）
/// @return シードが有限値なら true。waterPos に精密化後の交点が入る
bool RefineWaterSurfaceIntersection(
    Texture2DArray<float4> displacementTex,
    float3 cameraPosition,
    float3 primaryDir,
    float sceneDistance,
    out float3 waterPos)
{
    waterPos = 0.0f.xxx;

    // 呼び出し側で |primaryDir.y| > 1e-5 は保証済み
    const float denom = primaryDir.y;

    // フラット平面（波なし）での交点は、あくまで反復解法の初期値（シード）に過ぎない。
    // シード段階で 0 < t < sceneDistance を厳密に要求すると、カメラ直下の近い/浅い
    // ジオメトリでフラット高さの誤差が相対的に大きくなり有効な交点まで弾かれる
    // （「カメラを近づけるとカメラ下側の屈折が消える」既知バグ）。判定は精密化後に行う。
    const float tSurfaceSeed = (gSurfaceWaterHeight - cameraPosition.y) / denom;
    if (!isfinite(tSurfaceSeed))
    {
        return false;
    }
    const float tSeedClamped = clamp(tSurfaceSeed, 0.0f, sceneDistance);

    waterPos = cameraPosition + primaryDir * tSeedClamped;
    float safeDenom = denom;
    if (abs(safeDenom) < 1.0e-4f)
    {
        safeDenom = (safeDenom < 0.0f) ? -1.0e-4f : 1.0e-4f;
    }
    [unroll]
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        const float3 waveOffset = EvaluateWaterOffset(displacementTex, waterPos.xz);
        const float surfaceY = gSurfaceWaterHeight + waveOffset.y;
        const float deltaY = waterPos.y - surfaceY;
        waterPos -= primaryDir * (deltaY / safeDenom);
    }
    return true;
}

#endif // RT_WATER_SURFACE_COMMON_HLSLI
