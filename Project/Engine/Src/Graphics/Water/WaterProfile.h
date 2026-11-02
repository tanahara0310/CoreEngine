#pragma once

#include "Graphics/Water/WaterFoamDefaults.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
    /// @brief 水 1 つ分の性質（見た目・水質・波・うねり・泡）
    /// @details 水面コンポーネントが持ち、シーンへ保存する。`.waterprofile` を指していれば、その値で上書きする。
    ///          風はシーンの `r.Wind.*`、描画の品質と負荷はプロジェクトの `r.Water.*` が持つ。
    struct WaterProfile {
        // ---- 見た目 ----
        Vector4 baseColor{ 0.04f, 0.18f, 0.28f, 0.85f }; ///< 反射が無いときの色（DXR の反射も空のキューブも無いときだけ使う）
        float roughness = 0.03f;                         ///< 粗さ（ハイライトの広がり）
        float fresnelScale = 1.0f;                       ///< 反射の強さの倍率（1 = 物理値）
        float fresnelF0 = 0.02f;                         ///< 正面入射時の反射率
        bool depthFadeEnabled = true;                    ///< 水柱の厚さで透け方を変える

        // ---- 水質 ----
        Vector3 absorption{ 0.45f, 0.09f, 0.06f };   ///< 吸収係数 σa のベース値 [1/m]（RGB）
        Vector3 scattering{ 0.010f, 0.032f, 0.028f }; ///< 散乱係数 σs のベース値 [1/m]（RGB）
        float turbidity = 0.0f;                       ///< 濁り（青の吸収と粒子の散乱を足す）
        float refractiveIndex = 1.333f;               ///< 空気 1 に対する屈折率

        // ---- 風波（FFT の海。風速と向きはシーンの風）----
        bool fftEnabled = true;       ///< FFT の海で描く（false = Gerstner の波）
        float fetchKm = 200.0f;       ///< 吹送距離 [km]
        float amplitudeScale = 1.0f;  ///< 較正した波高に掛ける倍率
        float choppiness = 1.8f;      ///< 水平変位の強さ（波頭の尖り）
        int componentCount = 48;      ///< スペクトルの成分の数（短い波の量）

        // ---- うねり ----
        bool swellEnabled = true;                                   ///< 遠くから届く長周期の波を重ねる
        float swellHeight = 1.2f;                                   ///< 有義波高 [m]
        float swellPeriod = 12.0f;                                  ///< ピーク周期 [s]
        Vector2 swellDirection{ 0.3420201433f, -0.9396926208f };    ///< 進む向き（XZ）
        float swellRelativeWidth = 0.06f;                           ///< 周波数の帯域幅（ピークに対する比）
        float swellSpread = 24.0f;                                  ///< 向きのそろい方

        // ---- 泡 ----
        bool foamEnabled = WaterFoamDefaults::kEnabled;
        float foamWhitecapScale = WaterFoamDefaults::kWhitecapScale; ///< 白波の被覆率の倍率（1 = Monahan の観測式）
        float foamOpacity = WaterFoamDefaults::kOpacity;
        Vector3 foamCascadeWeights{
            WaterFoamDefaults::kCascadeWeights[0],
            WaterFoamDefaults::kCascadeWeights[1],
            WaterFoamDefaults::kCascadeWeights[2] };                 ///< カスケード別の勾配の重み（大・中・小）
        float foamDecaySeconds = WaterFoamDefaults::kDecaySeconds;   ///< 泡が e^-1 まで減る時間 [s]

        /// @brief 実効吸収係数（ベース値＋濁りの分）
        Vector3 EffectiveAbsorption() const;

        /// @brief 実効散乱係数（ベース値＋濁りの分）
        Vector3 EffectiveScattering() const;
    };
}
