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
        /// @brief 泡のしきい値・濃さを水面へ、風と消える速さを白波と岸の泡へ、流れた距離と残りの泡の格子の範囲を水面へ渡す
        /// @param waves        水面が描いている波のモデルの機能（泡を使えないモデルでは泡を進めない）
        /// @param fftOceanTime FFT の海の時刻 [s]
        void ApplySettings(const WaterFrameContext& frame, const WaterWaveCapabilities& waves, float fftOceanTime) const;

        /// @brief 泡を出さない水面（ほかの水）へ、泡を切った値を渡す（白波と岸の泡には触れない）
        void ApplyWithoutFoam(const WaterFrameContext& frame) const;

        /// @brief 物が水をかき回した所の泡の源を波紋の格子から受け取り、物の泡の格子を進める支度をする
        /// @details 波紋の Advance の後・フレームに 1 回。格子をゲームのカメラの前へ寄せる
        void AdvanceObjectFoam(const WaterFrameContext& frame, float deltaSeconds);

        /// @brief 溜まった白波の泡と、物の泡の SRV と範囲を結線へ書く（主の水）
        void Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const;

        /// @brief 物の泡の SRV と範囲を結線へ書く（ほかの水）
        void BindObjectFoam(RenderDomainContext& domain, WaterFrameBinding& binding) const;

    private:
        float cameraXZ_[2] = { 0.0f, 0.0f }; ///< 最後のカメラの XZ [m]
        float viewXZ_[2] = { 0.0f, 0.0f };   ///< 最後のカメラが見ている向きの水平の成分
    };
}
