#pragma once

#include <cstdint>
#include <memory>

#include "Graphics/Common/IResizable.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class DepthStencilManager;
    class GBufferManager;
    class ShadowMapManager;
    class AccelerationStructureManager;
    class RayTracingShadowManager;
    class WaterRefractionRayTracingManager;
    class WaterReflectionRayTracingManager;
    class WaterCausticsRayTracingManager;
    class FFTOceanManager;
    class AtmosphereManager;
    class VolumetricCloudManager;

    /// @brief 描画ドメイン固有マネージャーの所有・初期化クラス
    /// @note GBuffer / シャドウマップ / レイトレーシング等、
    /// DirectXCommon（D3D12インフラ層）から分離したレンダリングドメイン管理。
    class RenderDomainContext : public IResizable {
    public:
        RenderDomainContext();
        ~RenderDomainContext() override; // 前方宣言型の unique_ptr デストラクタは .cpp に実装

        /// @brief 初期化
        /// @param dxCommon DirectXCommon（デバイス・DescriptorManager 取得用）
        /// @param width 初期ウィンドウ幅
        /// @param height 初期ウィンドウ高さ
        void Initialize(DirectXCommon* dxCommon, int32_t width, int32_t height);

        /// @brief シャットダウン（GPU完了後に呼ぶこと）
        void Shutdown();

        /// @brief ウィンドウリサイズ時の処理（IResizable）
        /// @param width 新しい幅
        /// @param height 新しい高さ
        void OnWindowResize(int32_t width, int32_t height) override;

        // アクセッサ
        GBufferManager* GetGBufferManager() { return gBufferManager_.get(); }
        const GBufferManager* GetGBufferManager()         const { return gBufferManager_.get(); }
        /// @brief 平面反射ビュー専用の半解像度 G-Buffer（ReflectionView 実行中のみ context に差し替えて使う）
        GBufferManager* GetReflectionGBufferManager() { return reflectionGBufferManager_.get(); }
        /// @brief 平面反射ビュー専用の半解像度深度（ReflectionView 実行中のみ context に差し替えて使う）
        DepthStencilManager* GetReflectionDepthStencilManager() { return reflectionDepthStencilManager_.get(); }
        ShadowMapManager* GetShadowMapManager() { return shadowMapManager_.get(); }
        AccelerationStructureManager* GetAccelerationStructureManager() { return accelerationStructureManager_.get(); }
        RayTracingShadowManager* GetRayTracingShadowManager() { return rtShadowManager_.get(); }
        WaterRefractionRayTracingManager* GetWaterRefractionRayTracingManager() { return rtWaterRefractionManager_.get(); }
        WaterReflectionRayTracingManager* GetWaterReflectionRayTracingManager() { return rtWaterReflectionManager_.get(); }
        WaterCausticsRayTracingManager* GetWaterCausticsRayTracingManager() { return rtWaterCausticsManager_.get(); }
        FFTOceanManager* GetFFTOceanManager() { return fftOceanManager_.get(); }
        AtmosphereManager* GetAtmosphereManager() { return atmosphereManager_.get(); }
        VolumetricCloudManager* GetVolumetricCloudManager() { return volumetricCloudManager_.get(); }

    private:
        std::unique_ptr<GBufferManager>               gBufferManager_;
        std::unique_ptr<GBufferManager>               reflectionGBufferManager_;      ///< 反射ビュー用（半解像度）
        std::unique_ptr<DepthStencilManager>          reflectionDepthStencilManager_; ///< 反射ビュー用（半解像度）
        std::unique_ptr<ShadowMapManager>             shadowMapManager_;
        std::unique_ptr<AccelerationStructureManager> accelerationStructureManager_;
        std::unique_ptr<RayTracingShadowManager>      rtShadowManager_;
        std::unique_ptr<WaterRefractionRayTracingManager> rtWaterRefractionManager_;
        std::unique_ptr<WaterReflectionRayTracingManager> rtWaterReflectionManager_;
        std::unique_ptr<WaterCausticsRayTracingManager> rtWaterCausticsManager_;
        std::unique_ptr<FFTOceanManager> fftOceanManager_;
        std::unique_ptr<AtmosphereManager> atmosphereManager_;
        std::unique_ptr<VolumetricCloudManager> volumetricCloudManager_;
    };

} // namespace CoreEngine
