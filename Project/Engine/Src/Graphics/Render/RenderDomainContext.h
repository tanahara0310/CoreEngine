#pragma once

#include <cstdint>
#include <memory>

#include "Graphics/RHI/IResizable.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorManager;
    class GBufferManager;
    class AccelerationStructureManager;
    class RayTracingShadowManager;
    class WaterRefractionRayTracingManager;
    class WaterReflectionRayTracingManager;
    class WaterCausticsRayTracingManager;
    class FFTOceanManager;
    class AtmosphereManager;
    class VolumetricCloudManager;
    struct WaterSurfaceData;

    /// @brief 描画ドメイン固有マネージャーの所有・初期化クラス
    /// @note GBuffer / シャドウマップ / レイトレーシング等、
    /// GraphicsCore（D3D12インフラ層）から分離したレンダリングドメイン管理。
    class RenderDomainContext : public IResizable {
    public:
        RenderDomainContext();
        ~RenderDomainContext() override; // 前方宣言型の unique_ptr デストラクタは .cpp に実装

        /// @brief 初期化
        /// @param dxCommon GraphicsCore（デバイス・DescriptorManager 取得用）
        /// @param width 初期ウィンドウ幅
        /// @param height 初期ウィンドウ高さ
        void Initialize(GraphicsCore* dxCommon, int32_t width, int32_t height);

        /// @brief シャットダウン（GPU完了後に呼ぶこと）
        void Shutdown();

        /// @brief ウィンドウリサイズ時の処理（IResizable）
        /// @param width 新しい幅
        /// @param height 新しい高さ
        void OnWindowResize(int32_t width, int32_t height) override;

        // アクセッサ
        GBufferManager* GetGBufferManager() { return gBufferManager_.get(); }
        const GBufferManager* GetGBufferManager()         const { return gBufferManager_.get(); }
        AccelerationStructureManager* GetAccelerationStructureManager() { return accelerationStructureManager_.get(); }
        RayTracingShadowManager* GetRayTracingShadowManager() { return rtShadowManager_.get(); }
        WaterRefractionRayTracingManager* GetWaterRefractionRayTracingManager() { return rtWaterRefractionManager_.get(); }
        WaterReflectionRayTracingManager* GetWaterReflectionRayTracingManager() { return rtWaterReflectionManager_.get(); }
        WaterCausticsRayTracingManager* GetWaterCausticsRayTracingManager() { return rtWaterCausticsManager_.get(); }
        FFTOceanManager* GetFFTOceanManager() { return fftOceanManager_.get(); }
        AtmosphereManager* GetAtmosphereManager() { return atmosphereManager_.get(); }
        VolumetricCloudManager* GetVolumetricCloudManager() { return volumetricCloudManager_.get(); }

        // ===== 水面サーフェス状態の publish =====

        /// @brief 水面サーフェス状態を publish する（WaterRenderFeature が毎フレーム呼ぶ）
        /// @param state 水面データ。所有者は WaterRenderFeature
        /// @warning Feature の Finalize で必ず nullptr へ戻すこと（ダングリング防止）
        void PublishWaterSurfaceState(const WaterSurfaceData* state) { waterSurfaceState_ = state; }

        /// @brief publish 済みの水面サーフェス状態を返す（水面不在なら nullptr）
        const WaterSurfaceData* GetWaterSurfaceState() const { return waterSurfaceState_; }

        /// @brief FFT Ocean のシミュレーション時刻を publish する
        /// @details 水面の表示・非表示や Gerstner / FFT の切り替えとは独立に進む。
        ///          サーフェス状態の time を流用していたときは、水面を非表示にすると
        ///          0 秒へ巻き戻って波形が飛んでいた。
        void PublishFFTOceanSimulationTime(float timeSeconds) { fftOceanSimulationTime_ = timeSeconds; }

        /// @brief publish 済みの FFT Ocean シミュレーション時刻を返す
        float GetFFTOceanSimulationTime() const { return fftOceanSimulationTime_; }

    private:
        std::unique_ptr<GBufferManager>               gBufferManager_;
        std::unique_ptr<AccelerationStructureManager> accelerationStructureManager_;
        std::unique_ptr<RayTracingShadowManager>      rtShadowManager_;
        std::unique_ptr<WaterRefractionRayTracingManager> rtWaterRefractionManager_;
        std::unique_ptr<WaterReflectionRayTracingManager> rtWaterReflectionManager_;
        std::unique_ptr<WaterCausticsRayTracingManager> rtWaterCausticsManager_;
        std::unique_ptr<FFTOceanManager> fftOceanManager_;
        std::unique_ptr<AtmosphereManager> atmosphereManager_;
        std::unique_ptr<VolumetricCloudManager> volumetricCloudManager_;

        /// @brief WaterRenderFeature が publish した水面状態（非所有）
        const WaterSurfaceData* waterSurfaceState_ = nullptr;
        /// @brief FFT Ocean のシミュレーション時刻（水面の表示状態と独立）
        float fftOceanSimulationTime_ = 0.0f;
    };

} // namespace CoreEngine
