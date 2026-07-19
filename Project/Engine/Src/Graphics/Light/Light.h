#pragma once

#include "Math/MathCore.h"
#include <cstdint>
#include <string>

/// @brief ライトのオーサリング表現（統一 Light 構造体・物理単位）
/// GPU 転送レイアウト（LightData.h）から独立しており、LightManager が
/// UpdateAll() で毎フレーム GPU 用構造体へ変換（単位変換・cos 変換・基底導出）する。

namespace CoreEngine
{
    /// @brief ライトの種類
    enum class LightType : uint8_t {
        Directional,
        Point,
        Spot,
        Area,
    };

    /// @brief ライトの物理単位系と較正定数
    /// @details オーサリングは測光量（lx / cd / nt）で行い、GPU 転送時に従来の
    ///          シェーダー単位へ変換する。シェーダー単位はレンダラ内部専用で、
    ///          ACES・自動露出（EV 上限 8）が想定するシーンリファード値域を保つ。
    namespace LightUnits {
        /// @brief 快晴の太陽直下照度 [lx]（大気シーンの太陽の基準値）
        constexpr float kSunIlluminanceLux = 100000.0f;

        /// @brief 較正定数: 照度 [lx] → シェーダー単位の変換係数
        /// @details 既存シーンの太陽（旧シェーダー単位 1.75 = 旧 BaseScene::kAtmosphereSurfaceSunIntensity）
        ///          を 100,000 lx に対応付ける。この係数を変えると全ライトの見た目の明るさが一斉に変わる。
        constexpr float kShaderUnitsPerLux = 1.75f / kSunIlluminanceLux;

        /// @brief 照度 [lx] → シェーダー単位（ディレクショナルライト）
        constexpr float LuxToShader(float lux) { return lux * kShaderUnitsPerLux; }

        /// @brief 光度 [cd] → シェーダー単位（ポイント / スポット）
        /// @details シェーダー側で 1/d² を掛けることで表面照度 [lx] 相当になる（E = I / d²）
        constexpr float CandelaToShader(float candela) { return candela * kShaderUnitsPerLux; }

        /// @brief 輝度 [nt = cd/m²] → シェーダー単位（エリア）
        /// @details 発光面の光度を I ≈ L・A（面法線方向のランバート面近似）とみなして
        ///          ポイントライトと同じ逆二乗経路に乗せる
        constexpr float NitToShader(float nit, float areaM2) { return nit * areaM2 * kShaderUnitsPerLux; }
    }

    /// @brief 全ライト共通のオーサリング表現
    /// @details intensity の単位はライト種で異なる:
    ///          Directional = 照度 [lx] / Point・Spot = 光度 [cd] / Area = 輝度 [nt]
    struct Light {
        std::string name;                  ///< エディタ表示・FindLightByName 用
        LightType type = LightType::Point;
        bool enabled = true;

        Vector3 color = { 1.0f, 1.0f, 1.0f };      ///< リニア RGB（明るさは intensity に分離）
        float intensity = 0.0f;                    ///< 物理強度（単位は type 依存。上記参照）

        Vector3 position = { 0.0f, 0.0f, 0.0f };   ///< Point / Spot / Area（Directional では未使用）
        Vector3 direction = { 0.0f, -1.0f, 0.0f }; ///< Directional / Spot / Area（Area では発光面の法線）

        float range = 10.0f;               ///< 光の到達距離 [m]（Point / Spot / Area）
        float innerConeAngleDeg = 30.0f;   ///< スポット減衰開始角（半頂角）[deg]
        float outerConeAngleDeg = 45.0f;   ///< スポット外角（半頂角）[deg]
        float areaWidth = 2.0f;            ///< エリア発光面の幅 [m]
        float areaHeight = 2.0f;           ///< エリア発光面の高さ [m]

        // ---- CPU 専用の役割フラグ（GPU には転送されない）----
        bool isAtmosphereSun = false;      ///< 大気散乱の太陽として扱う
        bool isAtmosphereMoon = false;     ///< 大気散乱の月（第2大気ライト）として扱う

        /// @brief 大気散乱（空・雲）の輝度スケール（LUT ドメインの無次元値。太陽の既定 ≈ 20）
        /// @details サーフェス直接光の intensity とは単位系が別。大気の LUT は散乱係数を掛けた
        ///          結果を返すため空が破綻しない輝度を要求する。0 のときは
        ///          LightUnits::LuxToShader(intensity) にフォールバックする（AtmosphereManager 側）。
        float atmosphereIntensity = 0.0f;
    };

    /// @brief ライトの世代付きハンドル
    /// @details 削除されたライトへの参照は LightManager::GetLight() が nullptr を返すことで
    ///          検出できる（スロット再利用を世代番号で判別）。Light* を保持し続けるより安全。
    struct LightHandle {
        uint16_t index = UINT16_MAX;
        uint16_t generation = 0;

        bool IsValid() const { return index != UINT16_MAX; }
        bool operator==(const LightHandle& rhs) const {
            return index == rhs.index && generation == rhs.generation;
        }
        bool operator!=(const LightHandle& rhs) const { return !(*this == rhs); }
    };
}
