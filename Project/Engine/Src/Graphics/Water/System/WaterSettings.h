#pragma once

#include <cstdint>

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
    /// @brief 水面の見た目と水質の設定
    struct WaterSurfaceSettings {
        Vector4 baseColor{};
        float roughness = 0.0f;
        float fresnelScale = 1.0f;
        float fresnelF0 = 0.02f;
        bool depthFadeEnabled = true;
        Vector3 absorption{};       ///< 実効吸収係数 σa [1/m]（ベース＋濁度）
        Vector3 scattering{};       ///< 実効散乱係数 σs [1/m]（ベース＋濁度）
        float refractiveIndex = 1.333f;
    };

    /// @brief FFT の海の波の設定
    struct WaterWaveSettings {
        bool fftEnabled = true;
        float amplitudeScale = 1.0f;
        Vector2 windDirection{};
        float windSpeed = 0.0f;
        float fetchMeters = 0.0f;
        float choppiness = 0.0f;
        uint32_t activeComponentCount = 1;
        bool swellEnabled = false;
        float swellHeightMeters = 0.0f;
        float swellPeriodSeconds = 0.0f;
        Vector2 swellDirection{};
        float swellRelativeWidth = 0.0f;
        float swellSpreadExponent = 0.0f;
    };

    /// @brief 白波の泡の設定
    struct WaterFoamSettings {
        bool enabled = true;
        float whitecapScale = 1.0f;
        float opacity = 1.0f;
        Vector3 cascadeWeights{};
        float decaySeconds = 0.0f;
    };

    /// @brief コースティクスの設定
    struct WaterCausticsSettings {
        bool screenSpace = false;   ///< true = スクリーンスペース、false = レイトレーシング
        float intensity = 0.0f;
        float depthAttenuation = 0.0f;
        float curvatureScale = 0.0f;
        float surfaceSampleRadius = 0.0f;
        float receiverNormalStrength = 0.0f;
        float alignmentPower = 0.0f;
    };

    /// @brief 水の機能すべての設定（1 フレームの間は変わらない）
    struct WaterSettings {
        WaterSurfaceSettings surface{};
        WaterWaveSettings waves{};
        WaterFoamSettings foam{};
        WaterCausticsSettings caustics{};
        float refractionMaxOffsetPixels = 0.0f;
        bool shoreFineMeshEnabled = true;
        bool seaMapEnabled = true;
        bool wetnessEnabled = true;
        float wetnessDrySeconds = 60.0f;
    };

    struct WaterProfile;

    /// @brief 水の性質・シーンの風・プロジェクトの品質の設定から、このフレームの設定を作る
    WaterSettings BuildWaterSettings(const WaterProfile& profile);
}
