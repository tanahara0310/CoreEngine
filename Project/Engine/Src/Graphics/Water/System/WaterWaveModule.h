#pragma once

#include <memory>
#include <optional>

#include "Graphics/Water/Simulation/WaterSurfaceSimulator.h"
#include "Graphics/Water/System/WaterSettings.h"

namespace CoreEngine
{
    class RenderDomainContext;
    class WaterSurfaceComponent;
    struct WaterFrameBinding;
    struct WaterFrameContext;

    /// @brief 波（Gerstner と FFT の海）の時計と設定を受け持つ
    class WaterWaveModule {
    public:
        WaterWaveModule();
        ~WaterWaveModule();

        /// @brief 波の時刻を進める
        /// @details FFT の時刻は水面の有無や表示・方式の切り替えと関係なく進める。
        ///          Gerstner の時刻は Gerstner で描いているときだけ進める
        void Advance(float deltaSeconds, const WaterSurfaceComponent* waterPlane);

        /// @brief 水面が使っている方式の波の時刻 [s]
        float GetActiveTime(const WaterSurfaceComponent& waterPlane) const;

        /// @brief FFT の海の時刻 [s]
        float GetFFTOceanTime() const;

        /// @brief FFT の海の設定を、前に渡した値から変わったときだけ FFTOceanManager へ渡す
        void ApplyFFTSettings(RenderDomainContext& domain, const WaterWaveSettings& waves);

        /// @brief 波の方式・FFT の設定・波群の位相を水面とマネージャへ渡す
        void ApplySettings(const WaterFrameContext& frame);

        /// @brief 水面の高さ・時刻・波を snapshot へ書く（snapshot の他の値は消える）
        void Capture(const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const;

        /// @brief FFT の海のテクスチャと傾きの大きさを結線へ書く
        void Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const;

    private:
        /// @brief 水面が使っている方式の simulator
        const WaterSurfaceSimulator& GetActiveSimulator(const WaterSurfaceComponent& waterPlane) const;

        std::unique_ptr<WaterSurfaceSimulator> gerstnerSimulator_;
        std::unique_ptr<WaterSurfaceSimulator> fftOceanSimulator_;

        /// @brief 最後に FFTOceanManager へ渡した設定（まだ渡していなければ空）
        std::optional<WaterWaveSettings> appliedFFTSettings_;
    };
}
