#include "pch.h"
#include "RenderDomainContext.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterReflectionRayTracingManager.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    RenderDomainContext::RenderDomainContext() = default;

    RenderDomainContext::~RenderDomainContext()
    {
        // Shutdown 済みなら dxCommon_ は null。未呼び出しで破棄された場合の保険
        if (dxCommon_) {
            dxCommon_->UnregisterResizable(this);
            dxCommon_ = nullptr;
        }
    }

    void RenderDomainContext::Initialize(GraphicsCore* dxCommon, int32_t width, int32_t height,
        ShaderProgramCache* shaderProgramCache)
    {
        dxCommon_ = dxCommon;
        auto* device = dxCommon->GetDevice();
        auto* descriptorAllocator = dxCommon->GetDescriptorAllocator();

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::Initialize: ドメインマネージャーを初期化します\n");

        // メインシーンの深度（GBuffer / Geometry が書き、各パスが SRV で読む）
        // D24S8 固定やクリア方針はレンダラの都合なのでここで所有する
        sceneDepth_ = std::make_unique<SceneDepth>();
        sceneDepth_->Initialize(device, descriptorAllocator, width, height);
        Logger::GetInstance().Infof(LogCategory::Graphics, "RenderDomainContext: SceneDepth 初期化完了\n");

        // G-Bufferの初期化
        gBufferManager_ = std::make_unique<GBufferManager>();
        gBufferManager_->Initialize(device, descriptorAllocator, width, height);
        Logger::GetInstance().Infof(LogCategory::Graphics, "RenderDomainContext: GBufferManager 初期化完了\n");

        // 従来型シャドウマップは全面廃止（2026-07-25）: 影はDXRのRTShadowPassが担い、
        // フォワード描画物の受影も gRTShadowMask(t6) 参照へ統一した。
        // 加速構造マネージャーの初期化（DXR 非対応の場合は内部でスキップ）
        accelerationStructureManager_ = std::make_unique<AccelerationStructureManager>();
        accelerationStructureManager_->Initialize(device, descriptorAllocator);
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext: AccelerationStructureManager 初期化完了 (DXR対応: %s)\n",
            accelerationStructureManager_->IsSupported() ? "true" : "false");

        // レイトレーシングシャドウマネージャーの初期化（DXR対応時のみ）
        rtShadowManager_ = std::make_unique<RayTracingShadowManager>();
        if (accelerationStructureManager_->IsSupported()) {
            rtShadowManager_->Initialize(dxCommon, descriptorAllocator,
                accelerationStructureManager_.get(), shaderProgramCache);
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: RayTracingShadowManager 初期化完了\n");
        }

        rtWaterRefractionManager_ = std::make_unique<WaterRefractionRayTracingManager>();
        if (accelerationStructureManager_->IsSupported()) {
            rtWaterRefractionManager_->Initialize(dxCommon, descriptorAllocator,
                accelerationStructureManager_.get(), shaderProgramCache);
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: WaterRefractionRayTracingManager 初期化完了\n");
        }

        rtWaterReflectionManager_ = std::make_unique<WaterReflectionRayTracingManager>();
        if (accelerationStructureManager_->IsSupported()) {
            rtWaterReflectionManager_->Initialize(dxCommon, descriptorAllocator,
                accelerationStructureManager_.get(), shaderProgramCache);
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: WaterReflectionRayTracingManager 初期化完了\n");
        }

        rtWaterCausticsManager_ = std::make_unique<WaterCausticsRayTracingManager>();
        if (accelerationStructureManager_->IsSupported()) {
            rtWaterCausticsManager_->Initialize(dxCommon, descriptorAllocator,
                accelerationStructureManager_.get(), shaderProgramCache);
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: WaterCausticsRayTracingManager 初期化完了\n");
        }

        fftOceanManager_ = std::make_unique<FFTOceanManager>();
        if (fftOceanManager_->Initialize(dxCommon, descriptorAllocator)) {
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: FFTOceanManager 初期化完了\n");
        }

        atmosphereManager_ = std::make_unique<AtmosphereManager>();
        atmosphereManager_->Initialize(device, descriptorAllocator);
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext: AtmosphereManager 初期化完了\n");

        volumetricCloudManager_ = std::make_unique<VolumetricCloudManager>();
        volumetricCloudManager_->Initialize(dxCommon, descriptorAllocator);
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext: VolumetricCloudManager 初期化完了\n");

        // ウィンドウリサイズ通知を受ける（シーン深度 / GBuffer / RT シャドウの作り直し）
        dxCommon->RegisterResizable(this);

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::Initialize: 全ドメインマネージャーの初期化完了\n");
    }

    void RenderDomainContext::Shutdown()
    {
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::Shutdown: ドメインマネージャーを解放します\n");

        if (dxCommon_) {
            dxCommon_->UnregisterResizable(this);
            dxCommon_ = nullptr;
        }

        // 依存関係を考慮した逆順解放
        volumetricCloudManager_.reset();
        atmosphereManager_.reset();
        rtWaterCausticsManager_.reset();
        rtWaterReflectionManager_.reset();
        rtWaterRefractionManager_.reset();
        rtShadowManager_.reset();
        fftOceanManager_.reset();
        accelerationStructureManager_.reset();
        gBufferManager_.reset();
        sceneDepth_.reset();
    }

    void RenderDomainContext::OnWindowResize(int32_t width, int32_t height)
    {
        // 深度を先に作り直す（GBuffer の DSV / 各パスの深度 SRV は同じスロットへ書き直される）
        if (sceneDepth_) {
            sceneDepth_->ResizeResource(width, height);
        }
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
