#pragma once

#include "Graphics/Water/Wave/WaterWaveModelType.h"

namespace CoreEngine
{
    class RenderDomainContext;
    class WaterSurfaceComponent;
    struct WaterFrameBinding;
    struct WaterSurfaceData;
    struct WaterWaveSettings;

    /// @brief 波のモデルで水面を描いたときに使える機能
    struct WaterWaveCapabilities {
        bool foam = false;      ///< 白波と岸の泡
        bool shoreMesh = false; ///< 岸の細かい格子
    };

    /// @brief 水面の波のモデル（Gerstner の波・FFT の海）
    /// @details どのモデルも毎フレーム時刻と設定を受け取り、水面はそのうち 1 つで描く
    class WaterWaveModel {
    public:
        virtual ~WaterWaveModel() = default;

        /// @brief モデルの種類
        virtual WaterWaveModelType GetType() const = 0;

        /// @brief このモデルで描いたときに使える機能
        virtual WaterWaveCapabilities GetCapabilities() const = 0;

        /// @brief 波の時刻 [s]
        float GetTime() const { return time_; }

        /// @brief 時刻を進める
        /// @param drawn 水面がこのモデルで描いているか
        virtual void Advance(float deltaSeconds, bool drawn) = 0;

        /// @brief 波の設定を受け取る
        virtual void ApplySettings(RenderDomainContext&, const WaterWaveSettings&) {}

        /// @brief 波の値を水面の定数へ渡す
        virtual void ApplyToSurface(RenderDomainContext&, WaterSurfaceComponent&) const {}

        /// @brief 水面の高さ・時刻・波を snapshot へ書く（snapshot の他の値は消える）
        virtual void Capture(
            RenderDomainContext& domain, const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const = 0;

        /// @brief 波の高さ（有義波高）[m]
        /// @param snapshot このモデルが Capture した snapshot
        virtual float ComputeSignificantWaveHeight(RenderDomainContext& domain, const WaterSurfaceData& snapshot) const = 0;

        /// @brief 波のテクスチャと傾きの大きさを結線へ書く
        virtual void Bind(RenderDomainContext&, WaterFrameBinding&) const {}

    protected:
        float time_ = 0.0f;
    };
}
