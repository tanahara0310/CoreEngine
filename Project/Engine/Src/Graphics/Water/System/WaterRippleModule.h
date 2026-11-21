#pragma once

namespace CoreEngine
{
    class RenderDomainContext;
    class WaterSurfaceComponent;
    struct WaterFrameBinding;
    struct WaterFrameContext;
    struct WaterSurfaceData;

    /// @brief 水の波紋の格子（WaterRippleSimulation）を進め、範囲と面を水面へ渡す
    class WaterRippleModule {
    public:
        /// @brief 波紋の時刻を進め、格子をゲームのカメラの前へ寄せる（フレームに 1 回、Bind・Capture より前）
        void Advance(const WaterFrameContext& frame, float deltaSeconds);

        /// @brief 格子の範囲と面の SRV を結線へ書く（水面ごと）
        void Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const;

        /// @brief RT の水面へ渡す状態へ、格子の範囲と水の倍率を書く（水面ごと）
        void Capture(RenderDomainContext& domain, const WaterSurfaceComponent& waterPlane, WaterSurfaceData& out) const;

        /// @brief 主の水の海の範囲マップを、波紋の岸と物、物の泡の流れの陸として格子へ渡す（地図が無ければ外す。フレームに 1 回）
        void CaptureShore(RenderDomainContext& domain, const WaterSurfaceData& snapshot) const;

        /// @brief 波紋と物の泡の格子から岸と物の地図を外す
        static void ClearShore(RenderDomainContext& domain);

    private:
        float cameraXZ_[2] = { 0.0f, 0.0f }; ///< 最後のカメラの XZ [m]
        float viewXZ_[2] = { 0.0f, 0.0f };   ///< 最後のカメラが見ている向きの水平の成分
    };
}
