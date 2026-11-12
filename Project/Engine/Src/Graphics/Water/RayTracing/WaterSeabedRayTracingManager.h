#pragma once

#include <d3d12.h>
#include <cstdint>

#include "Graphics/Water/WaterSurfaceData.h"
#include "WaterRayTracingPassBase.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class AccelerationStructureManager;

    /// @brief 海底の高さを測る範囲（ワールド XZ の正方形）
    struct WaterSeabedWindow {
        float originX = 0.0f; ///< 範囲の X の最小 [m]
        float originZ = 0.0f; ///< 範囲の Z の最小 [m]
        float size = 0.0f;    ///< 一辺の長さ [m]
        bool valid = false;
    };

    /// @brief カメラの周りの範囲で、水面より下で最初に当たる面（海底・水中の物）の高さを
    ///        レイトレーシングで測るマネージャ
    /// @details 出力は範囲を kResolution 四方に分けた R32_FLOAT のワールド Y。
    ///          範囲が動いたフレームは全部を、それ以外のフレームは r.Water.Seabed.RefreshFrames 行おきの 1 組の行だけを測り直す
    class WaterSeabedRayTracingManager : public WaterRayTracingPassBase {
    public:
        using ViewID = RTWaterViewID;

        /// @brief 範囲の一辺のテクセル数
        static constexpr uint32_t kResolution = 1024;
        /// @brief 1 テクセルの幅 [m]
        static constexpr float kTexelSize = 0.25f;
        /// @brief 範囲の角を揃える格子の幅 [m]（テクセルの幅の整数倍）
        static constexpr float kWindowSnapMeters = 0.5f;
        /// @brief 海底を探す水面からの深さの上限 [m]
        static constexpr float kMaxDepth = 50.0f;
        /// @brief 測り直しを分けるフレーム数の上限
        static constexpr uint32_t kMaxRefreshFrames = 16;

        /// @brief パイプラインを作り始める（完成は別スレッド）
        bool Initialize(const RTEffectInitContext& init) override;

        /// @brief 範囲をカメラの真下を中心に置き直す（範囲の角は kWindowSnapMeters の格子に揃える）
        /// @return 置き直した範囲（初期化されていなければ valid = false）
        const WaterSeabedWindow& UpdateWindow(float cameraX, float cameraZ);

        /// @brief 今の範囲
        const WaterSeabedWindow& GetWindow() const { return window_; }

        /// @brief 今の範囲の海底の高さを測る
        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput,
            ViewID viewId = ViewID::GameView);

        /// @brief 海底の高さの SRV（まだ一度も測っていなければ ptr = 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSeabedSRVHandle(ViewID viewId = ViewID::GameView) const;
        /// @brief 海底の高さのテクスチャ（ステート追跡つき）
        GpuResource& GetSeabedResource(ViewID viewId = ViewID::GameView);

    private:
        WaterSeabedWindow window_{};
        /// @brief 出力の全部を測ったときの範囲の角（measuredResource_ が null なら一度も測っていない）
        float measuredOriginX_ = 0.0f;
        float measuredOriginZ_ = 0.0f;
        /// @brief 全部を測ったときの出力のテクスチャ（作り直されたら全部を測り直す）
        const ID3D12Resource* measuredResource_ = nullptr;
        /// @brief 次に測り直す行の組（行番号を分けるフレーム数で割った余り）
        uint32_t nextRowGroup_ = 0;
    };
}
