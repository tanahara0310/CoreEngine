#pragma once

#include <optional>

#include "Graphics/Water/System/WaterSettings.h"
#include "Graphics/Water/Wave/WaterWaveModel.h"

namespace CoreEngine
{
    struct WaterFFTOceanInput;

    /// @brief FFT の海（FFTOceanManager が GPU で解く波面）
    /// @details GPU のシミュレーションはシーンに 1 つで、水面の有無や描き方と関係なく毎フレーム進む
    class FFTOceanWaveModel final : public WaterWaveModel {
    public:
        WaterWaveModelType GetType() const override { return WaterWaveModelType::FFTOcean; }

        WaterWaveCapabilities GetCapabilities() const override { return { true, true }; }

        /// @brief 描いているかどうかと関係なく時刻を進める
        void Advance(float deltaSeconds, bool drawn) override;

        /// @brief スペクトルを決める値が前に渡した値から変わったときだけ FFTOceanManager へ渡す
        void ApplySettings(RenderDomainContext& domain, const WaterWaveSettings& settings) override;

        /// @brief 波群エンベロープの位相を水面へ渡す
        void ApplyToSurface(RenderDomainContext& domain, WaterSurfaceComponent& waterPlane) const override;

        /// @brief 水面の高さ・時刻と、波面のテクスチャ（snapshot.fftOcean）を書く
        void Capture(
            RenderDomainContext& domain, const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const override;

        /// @brief 風波とうねりの有義波高を合わせた値
        float ComputeSignificantWaveHeight(RenderDomainContext& domain, const WaterSurfaceData& snapshot) const override;

        /// @brief 有義波高から見積もった波頭の高さに、波群エンベロープの最大の倍率を掛けた値
        float ComputeMaxCrestHeight(RenderDomainContext& domain, const WaterSurfaceData& snapshot) const override;

        /// @brief 波面のテクスチャとカスケードごとの傾きの大きさを書く
        void Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const override;

    private:
        /// @brief 今の時刻の波面のテクスチャ（FFTOceanManager が無いか初期化前なら enabled = 0）
        WaterFFTOceanInput BuildInput(RenderDomainContext& domain) const;

        /// @brief 最後に FFTOceanManager へ渡した設定（まだ渡していなければ空）
        std::optional<WaterWaveSettings> appliedSettings_;
    };
}
