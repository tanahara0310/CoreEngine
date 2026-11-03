#pragma once

#include <array>
#include <memory>
#include <span>

#include "Graphics/Water/Wave/WaterWaveModel.h"

namespace CoreEngine
{
    class RenderDomainContext;
    class WaterSurfaceComponent;
    struct WaterFrameBinding;
    struct WaterFrameContext;
    struct WaterSurfaceData;
    struct WaterWaveSettings;

    /// @brief 波のモデル（Gerstner の波と FFT の海）を持ち、水面が描くモデルを選ぶ
    class WaterWaveModule {
    public:
        WaterWaveModule();
        ~WaterWaveModule();

        /// @brief すべてのモデルの時刻を進める
        /// @param waterPlanes シーンの水面（Gerstner の波はどれかの水面が描いているときだけ進む）
        void Advance(float deltaSeconds, std::span<WaterSurfaceComponent* const> waterPlanes);

        /// @brief 水面が描いているモデル
        const WaterWaveModel& GetDrawnModel(const WaterSurfaceComponent& waterPlane) const;

        /// @brief FFT の海の時刻 [s]（シーンに 1 つの GPU のシミュレーションの時刻）
        float GetFFTOceanTime() const;

        /// @brief 波の設定をすべてのモデルへ渡す
        void ApplyModelSettings(RenderDomainContext& domain, const WaterWaveSettings& waves);

        /// @brief 水面が描くモデルを設定に合わせ、すべてのモデルへ設定を渡して水面の定数へ波の値を書く
        void ApplySettings(const WaterFrameContext& frame);

        /// @brief ほかの水の水面を Gerstner の波で描く（その水面の設定はモデルへ渡さない）
        void ApplyToSecondary(const WaterFrameContext& frame) const;

        /// @brief 描いているモデルの水面の高さ・時刻・波を snapshot へ書く（snapshot の他の値は消える）
        void Capture(RenderDomainContext& domain, const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const;

        /// @brief すべてのモデルの波のテクスチャと傾きの大きさを結線へ書く
        void Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const;

    private:
        /// @brief 種類の値を番号にしたモデルの一覧
        std::array<std::unique_ptr<WaterWaveModel>, kWaterWaveModelTypeCount> models_;
    };
}
