#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    class WaterSurfaceComponent;
    struct WaterFrameBinding;
    struct WaterFrameContext;
    struct WaterSurfaceData;
    struct WaterWaveCapabilities;

    /// @brief カメラの周りの海底の高さの範囲と、その範囲を描く岸の細かい格子を受け持つ
    class WaterShoreModule {
    public:
        /// @brief ゲームのカメラの真下を中心に、海底の高さを測る範囲を決める（同じフレームの RTWaterSeabedPass が測る）
        /// @param waves 水面が描いている波のモデルの機能（岸の細かい格子を使えるか）
        void UpdateWindow(const WaterFrameContext& frame, const WaterWaveCapabilities& waves);

        /// @brief 岸の細かい格子の範囲を snapshot へ書く
        void Capture(const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const;

        /// @brief 海底の高さと岸の泡を結線へ書く
        void Bind(WaterFrameBinding& binding) const;

    private:
        /// @brief このフレームの海底の高さの範囲
        struct Window {
            bool valid = false;
            float originXZ[2] = { 0.0f, 0.0f }; ///< 範囲の XZ の最小の角 [m]
            float size = 0.0f;                  ///< 範囲の一辺の長さ [m]
            bool shoreMeshEnabled = false;      ///< 範囲を岸の細かい格子で描くか
            D3D12_GPU_DESCRIPTOR_HANDLE seabedSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE shoreFoamSRV{};
        };

        Window window_{};
    };
}
