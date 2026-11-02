#pragma once

namespace CoreEngine
{
    class RenderDomainContext;
    struct WaterFrameBinding;
    struct WaterFrameContext;

    /// @brief 白波の泡の設定を水面と WaterFoamSystem へ渡す
    class WaterFoamModule {
    public:
        /// @brief 泡のしきい値・濃さを水面へ、風と消える速さを WaterFoamSystem へ、流れた距離を水面へ渡す
        /// @param fftOceanTime FFT の海の時刻 [s]
        void ApplySettings(const WaterFrameContext& frame, float fftOceanTime) const;

        /// @brief 溜まった泡の SRV を結線へ書く
        void Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const;
    };
}
