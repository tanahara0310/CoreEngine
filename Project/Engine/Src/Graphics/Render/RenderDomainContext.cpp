#include "pch.h"
#include "RenderDomainContext.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    RenderDomainContext::RenderDomainContext() = default;
    RenderDomainContext::~RenderDomainContext() = default;

    void RenderDomainContext::Initialize(DirectXCommon* dxCommon, int32_t width, int32_t height)
    {
        auto* device = dxCommon->GetDevice();
        auto* descriptorManager = dxCommon->GetDescriptorManager();

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::Initialize: ドメインマネージャーを初期化します\n");

        // G-Bufferの初期化
        gBufferManager_ = std::make_unique<GBufferManager>();
        gBufferManager_->Initialize(device, descriptorManager, width, height);
        Logger::GetInstance().Infof(LogCategory::Graphics, "RenderDomainContext: GBufferManager 初期化完了\n");

        // シャドウマップの初期化
        shadowMapManager_ = std::make_unique<ShadowMapManager>();
        shadowMapManager_->Initialize(device, descriptorManager);
        Logger::GetInstance().Infof(LogCategory::Graphics, "RenderDomainContext: ShadowMapManager 初期化完了\n");

        // 加速構造マネージャーの初期化（DXR 非対応の場合は内部でスキップ）
        accelerationStructureManager_ = std::make_unique<AccelerationStructureManager>();
        accelerationStructureManager_->Initialize(device, descriptorManager);
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext: AccelerationStructureManager 初期化完了 (DXR対応: %s)\n",
            accelerationStructureManager_->IsSupported() ? "true" : "false");

        // レイトレーシングシャドウマネージャーの初期化（DXR対応時のみ）
        rtShadowManager_ = std::make_unique<RayTracingShadowManager>();
        if (accelerationStructureManager_->IsSupported()) {
            rtShadowManager_->Initialize(dxCommon, descriptorManager,
                accelerationStructureManager_.get());
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: RayTracingShadowManager 初期化完了\n");
        }

        rtWaterRefractionManager_ = std::make_unique<WaterRefractionRayTracingManager>();
        if (accelerationStructureManager_->IsSupported()) {
            rtWaterRefractionManager_->Initialize(dxCommon, descriptorManager,
                accelerationStructureManager_.get());
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: WaterRefractionRayTracingManager 初期化完了\n");
        }

        rtWaterCausticsManager_ = std::make_unique<WaterCausticsRayTracingManager>();
        if (accelerationStructureManager_->IsSupported()) {
            rtWaterCausticsManager_->Initialize(dxCommon, descriptorManager,
                accelerationStructureManager_.get());
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: WaterCausticsRayTracingManager 初期化完了\n");
        }

        fftOceanManager_ = std::make_unique<FFTOceanManager>();
        if (fftOceanManager_->Initialize(dxCommon, descriptorManager)) {
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: FFTOceanManager 初期化完了\n");
        }

        atmosphereManager_ = std::make_unique<AtmosphereManager>();
        atmosphereManager_->Initialize(device, descriptorManager);
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext: AtmosphereManager 初期化完了\n");

        volumetricCloudManager_ = std::make_unique<VolumetricCloudManager>();
        volumetricCloudManager_->Initialize(device, descriptorManager);
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext: VolumetricCloudManager 初期化完了\n");

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::Initialize: 全ドメインマネージャーの初期化完了\n");
    }

    void RenderDomainContext::Shutdown()
    {
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::Shutdown: ドメインマネージャーを解放します\n");

        // 依存関係を考慮した逆順解放
        volumetricCloudManager_.reset();
        atmosphereManager_.reset();
        rtWaterCausticsManager_.reset();
        rtWaterRefractionManager_.reset();
        rtShadowManager_.reset();
        fftOceanManager_.reset();
        accelerationStructureManager_.reset();
        shadowMapManager_.reset();
        gBufferManager_.reset();
    }

    void RenderDomainContext::OnWindowResize(int32_t width, int32_t height)
    {
        if (gBufferManager_) {
            gBufferManager_->Resize(width, height);
        }

        // RTシャドウ出力はフレーム先頭で Blackboard に登録されるため、
        // Dispatch 中の遅延再作成に任せず、この時点（GPU アイドル保証済み）で
        // 新サイズへ再作成しておく（旧リソースへのバリア発行を防ぐ）
        if (rtShadowManager_) {
            rtShadowManager_->ResizeAllExisting(
                static_cast<UINT>(width), static_cast<UINT>(height));
        }

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::OnWindowResize: %dx%d にリサイズ完了\n", width, height);
    }

} // namespace CoreEngine
