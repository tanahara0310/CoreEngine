#pragma once

namespace CoreEngine
{
    class RenderDomainContext;
    struct WaterFrameBinding;
    struct WaterFrameContext;
    struct WaterWaveCapabilities;

    /// @brief 泡の設定を水面と、白波（WaterWhitecapFoam）・岸の泡（WaterShoreFoam）へ渡す
    class WaterFoamModule {
    public:
        /// @brief 泡のしきい値・濃さを水面へ、風と消える速さを白波と岸の泡へ、流れた距離を水面へ渡す
        /// @param waves        水面が描いている波のモデルの機能（泡を使えないモデルでは泡を進めない）
        /// @param fftOceanTime FFT の海の時刻 [s]
        void ApplySettings(const WaterFrameContext& frame, const WaterWaveCapabilities& waves, float fftOceanTime) const;

        /// @brief 泡を出さない水面（ほかの水）へ、泡を切った値を渡す（白波と岸の泡には触れない）
        void ApplyWithoutFoam(const WaterFrameContext& frame) const;

        /// @brief 溜まった白波の泡の SRV を結線へ書く
        void Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const;
    };
}
