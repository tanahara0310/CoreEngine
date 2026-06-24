#pragma once

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include <cstdint>

static constexpr uint32_t kMaxWaterWaveCount = 16;
static constexpr uint32_t kWaterWaveLayerCount = 3;
static constexpr uint32_t kMaxWaterLightningImpactCount = 6;

enum class WaterWaveLayerType : uint32_t {
    Large = 0,
    Medium = 1,
    Small = 2,
};

/// @brief 水面デバッグ可視化モード
enum class WaterDebugViewMode : uint32_t {
    None = 0,
    RawDepth = 1,
    LinearDepth = 2,
    DepthDelta = 3,
    ScreenUV = 4,
    SceneColor = 5,
    Reflection = 6,
    Fresnel = 7,
    RTRefraction = 8,
    RTRefractionReason = 9,
    RTRefractionVsScene = 10,
    Transmission = 11,
    Absorption = 12,
    Reflectance = 13,
    WaterComposite = 14,
    RTRefractionSuccessMask = 15,
};

struct WaterWaveLayerConfig {
    uint32_t count = 0;
    float amplitudeMin = 0.0f;
    float amplitudeMax = 0.0f;
    float wavelengthMin = 1.0f;
    float wavelengthMax = 1.0f;
    float speedMin = 0.0f;
    float speedMax = 0.0f;
    float steepnessMin = 0.0f;
    float steepnessMax = 0.0f;
    float directionSpreadRadians = 0.0f;
};

/// @brief Gerstner Wave 1 本分のパラメータ
/// @note HLSL 側の WaveParams 構造体とメモリレイアウトを一致させること
struct WaveParams {
    CoreEngine::Vector2 direction = { 1.0f, 0.0f }; ///< 進行方向（XZ, 正規化済み）
    float amplitude = 0.5f;  ///< 振幅（波の高さ）
    float wavelength = 10.0f; ///< 波長（山から山の距離）
    float speed = 2.0f;  ///< 位相速度（波が進む速さ）
    float steepness = 0.5f;  ///< 横揺れ係数 Q（0=正弦波, 1=完全Gerstner）
    float phaseOffset = 0.0f; ///< 位相オフセット（干渉パターンの規則性低減用）
    float padding = 0.0f;     ///< 16 バイトアライメント用
};

/// @brief 水面 Gerstner Wave の定数バッファ全体
/// @note HLSL 側の WaterConstants cbuffer とメモリレイアウトを一致させること
struct WaterConstants {
    WaveParams waves[kMaxWaterWaveCount]; ///< 重ね合わせる波（最大 16 本）
    uint32_t   activeWaveCount = kMaxWaterWaveCount; ///< 実際に評価する波本数
    float      time = 0.0f;     ///< 経過時間（秒）
    float      padding[2] = {}; ///< 16 バイトアライメント用

    static_assert(sizeof(WaveParams) == 32, "WaveParams must be 32 bytes");
};

struct WaterLightningImpactData {
    float center[2] = { 0.0f, 0.0f };
    float radius = 0.0f;
    float intensity = 0.0f;
    float chargeRadius = 0.0f;
    float chargeIntensity = 0.0f;
    float impactTime = 0.0f;
    float screenFlash = 0.0f;
};

static_assert(sizeof(WaterLightningImpactData) == 32, "WaterLightningImpactData must be 32 bytes");

/// @brief 毎フレーム更新する水面用フレーム定数バッファ
/// クリップ平面（反射パス用）を格納する。HLSL 側の WaterFrameConstants と一致させること
struct WaterFrameConstants {
    /// @brief クリップ平面 (A, B, C, D)。dot(worldPos, clipPlane) > 0 なら描画する
    float clipPlane[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

    /// @brief 1 = クリップ平面を有効にする、0 = 無効
    int   clipEnabled = 0;

    /// @brief 1 = 反射テクスチャ（gReflectionTexture）が有効、0 = IBL フォールバック
    int   reflectionEnabled = 0;

    /// @brief Fresnel 反射率のスケール値（反射ブレンドの強さ）
    float fresnelReflectanceScale = 1.0f;

    /// @brief 正面入射時の Fresnel 反射率 F0（水面の既定値は約 0.02）
    float fresnelBaseReflectance = 0.02f;

    // ---- Depth Fade（Beer-Lambert 則）----

    /// @brief 光吸収係数（大きいほど短距離で不透明になる）
    float absorptionCoeff = 0.3f;

    /// @brief 1 = 深度テクスチャによる Depth Fade を有効にする、0 = 無効
    int   depthFadeEnabled = 1;

    /// @brief 1 = Depth Fade のデバッグ表示を有効にする、0 = 無効
    int   depthFadeDebugEnabled = 0;

    /// @brief Depth Fade のデバッグ表示倍率
    float depthFadeDebugScale = 1.5f;

    // ---- 浅瀬 / 深場の水色 ----

    /// @brief 浅瀬（d ≈ 0）のときの水色（RGB）
    float shallowColor[3] = { 0.10f, 0.85f, 0.65f };

    /// @brief shallowColor アライメント用
    float shallowColorPad = 0.0f;

    /// @brief 深場（d 大）のときの水色（RGB）
    float deepColor[3] = { 0.02f, 0.08f, 0.45f };

    /// @brief deepColor アライメント用
    float deepColorPad = 0.0f;

    /// @brief 水面デバッグ可視化モード
    uint32_t depthDebugViewMode = static_cast<uint32_t>(WaterDebugViewMode::None);

    /// @brief 有効な雷着弾波紋数
    uint32_t lightningImpactCount = 0;

    /// @brief cbuffer 16 バイトアライメント用
    float debugPadding[2] = {};

    /// @brief 同時保持する雷着弾波紋群
    WaterLightningImpactData lightningImpacts[kMaxWaterLightningImpactCount] = {};
};

// ===========================================================================
// PBR プリセット
// ===========================================================================

/// @brief 水面 PBR プリセットの種類
enum class WaterPresetType : int {
    Lake = 0, ///< 湖（静かな水）
    Ocean = 1, ///< 海（波が立つ水）
    Pool = 2, ///< 池・プール
    Rain = 3, ///< 雨水・水たまり
};

/// @brief 水面プリセット 1 種分のパラメータ
struct WaterPresetData {
    const char* name;               ///< 表示名
    uint32_t recommendedWaveCount;  ///< プリセット推奨の有効波本数

    // ---- マテリアル ----
    CoreEngine::Vector4 baseColor;  ///< ベースカラー RGBA
    float roughness;                ///< 粗さ
    float metallic;                 ///< 金属度（水は常に 0）

    // ---- Depth Fade / 水色 ----
    float absorptionCoeff;          ///< 光吸収係数
    CoreEngine::Vector3 shallowColor; ///< 浅瀬の水色
    CoreEngine::Vector3 deepColor;    ///< 深場の水色

    // ---- Fresnel ----
    float fresnelReflectanceScale;  ///< Fresnel 反射率スケール
    float fresnelBaseReflectance;   ///< 正面入射時の反射率 F0

    // ---- レイヤー分け Gerstner Wave 生成設定 ----
    CoreEngine::Vector2 dominantDirection; ///< 主波向
    float directionJitterRadians;          ///< 全体方向の揺らぎ量
    WaterWaveLayerConfig layers[kWaterWaveLayerCount]; ///< 大波・中波・小波の生成設定

    // ---- UV ----
    CoreEngine::Vector2 scrollSpeed; ///< UV スクロール速度
    CoreEngine::Vector2 uvTiling;    ///< UV タイリング
};

/// @brief プリセットデータを返す
/// @param type プリセット種類
/// @return プリセットデータの参照
inline const WaterPresetData& GetWaterPresetData(WaterPresetType type) {
    // 湖（静かな水）: 鏡面に近く周囲が映り込む
    static const WaterPresetData kLake = {
        "湖（静かな水）",
        16,
        { 0.04f, 0.18f, 0.28f, 0.85f }, // baseColor
        0.03f,  // roughness
        0.0f,   // metallic
        0.8f,   // absorptionCoeff
        { 0.08f, 0.70f, 0.55f }, // shallowColor
        { 0.01f, 0.06f, 0.30f }, // deepColor
        1.0f, 0.02f, // fresnelReflectanceScale, fresnelBaseReflectance
        { 1.0f, 0.0f },
        1.4f,
        {
            { 3, 0.060f, 0.105f, 12.0f, 18.0f, 1.1f, 1.7f, 0.12f, 0.26f, 0.35f },
            { 5, 0.026f, 0.052f,  7.0f, 12.0f, 1.5f, 2.2f, 0.06f, 0.16f, 0.70f },
            { 8, 0.008f, 0.022f,  4.5f,  8.0f, 2.0f, 2.9f, 0.03f, 0.09f, 1.20f },
        },
        { 0.03f, 0.01f }, // scrollSpeed
        { 4.0f,  4.0f  }, // uvTiling
    };

    // 海（波が立つ水）: やや荒れた表面感
    static const WaterPresetData kOcean = {
        "海（波が立つ水）",
        16,
        { 0.03f, 0.12f, 0.22f, 0.88f },
        0.10f,
        0.0f,
        1.5f,
        { 0.05f, 0.55f, 0.60f },
        { 0.01f, 0.05f, 0.40f },
        1.0f, 0.02f,
        { 0.95f, 0.25f },
        1.9f,
        {
            { 4, 0.145f, 0.255f, 14.0f, 24.0f, 2.2f, 3.1f, 0.14f, 0.30f, 0.50f },
            { 5, 0.062f, 0.118f,  8.0f, 14.0f, 2.9f, 3.8f, 0.07f, 0.18f, 0.90f },
            { 7, 0.024f, 0.048f,  4.0f,  8.0f, 3.8f, 5.2f, 0.03f, 0.09f, 1.40f },
        },
        { 0.06f, 0.03f },
        { 3.0f,  3.0f  },
    };

    // 池・プール: 非常に鏡面的で透明
    static const WaterPresetData kPool = {
        "池・プール",
        16,
        { 0.02f, 0.22f, 0.35f, 0.78f },
        0.01f,
        0.0f,
        2.0f,
        { 0.12f, 0.90f, 0.80f },
        { 0.02f, 0.10f, 0.50f },
        1.0f, 0.02f,
        { 1.0f, 0.1f },
        1.0f,
        {
            { 2, 0.020f, 0.038f, 10.0f, 14.0f, 0.8f, 1.1f, 0.05f, 0.12f, 0.18f },
            { 4, 0.008f, 0.018f,  6.5f, 10.0f, 1.0f, 1.4f, 0.03f, 0.07f, 0.45f },
            { 10, 0.003f, 0.008f, 3.5f,  6.5f, 1.1f, 1.6f, 0.01f, 0.04f, 0.90f },
        },
        { 0.02f, 0.008f },
        { 5.0f,  5.0f   },
    };

    // 雨水・水たまり: やや曇り感のある反射
    static const WaterPresetData kRain = {
        "雨水・水たまり",
        16,
        { 0.06f, 0.10f, 0.15f, 0.72f },
        0.06f,
        0.0f,
        1.0f,
        { 0.15f, 0.35f, 0.40f },
        { 0.04f, 0.08f, 0.18f },
        1.0f, 0.02f,
        { 0.8f, 0.2f },
        2.2f,
        {
            { 1, 0.013f, 0.022f, 6.0f, 8.0f, 2.4f, 3.1f, 0.04f, 0.07f, 0.20f },
            { 4, 0.010f, 0.018f, 3.0f, 5.5f, 3.3f, 4.4f, 0.03f, 0.06f, 0.80f },
            { 11, 0.005f, 0.012f, 1.2f, 3.0f, 4.4f, 7.2f, 0.01f, 0.04f, 1.80f },
        },
        { 0.04f, 0.02f },
        { 6.0f,  6.0f  },
    };

    static const WaterPresetData* kTable[] = { &kLake, &kOcean, &kPool, &kRain };
    const int idx = static_cast<int>(type);
    return *kTable[(idx >= 0 && idx < 4) ? idx : 0];
}
