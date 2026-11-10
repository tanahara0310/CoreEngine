#include "pch.h"
#include "RenderDomainContext.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/RayTracing/Debug/RTDebugSystem.h"
#include "Graphics/RayTracing/VertexAnimationDeformer.h"
#include "Graphics/Water/RayTracing/WaterRayTracingPassBase.h"
#include "Graphics/Water/FFT/FFTOceanManager.h"
#include "Graphics/Water/Foam/WaterShoreFoam.h"
#include "Graphics/Water/Foam/WaterWhitecapFoam.h"
#include "Graphics/Water/Query/WaterHeightQueryManager.h"
#include "Graphics/Water/Surface/WaterShoreMeshDisplacement.h"
#include "Graphics/Water/Query/WaterQuery.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Fog/FogManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/Pass/RenderPass.h" // RenderContext（PopulateRenderContext の注入先）
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    RenderDomainContext::RenderDomainContext()
        : waterQuery_(std::make_unique<WaterQuery>())
    {
    }

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
        shaderProgramCache_ = shaderProgramCache;
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

        // TLAS に載せる物と材質の表
        rtScene_ = std::make_unique<RTScene>();
        rtScene_->Initialize(device, descriptorAllocator);

        // 揺れる植物・海草の形をレイトレーシングへ反映する頂点変形（CS は初めて使うときに組む）
        vertexAnimationDeformer_ = std::make_unique<VertexAnimationDeformer>();
        if (accelerationStructureManager_->IsSupported()) {
            vertexAnimationDeformer_->Initialize(dxCommon);
        }

        // レイトレーシングの効果（RT_EFFECT_REGISTER で申告したもの）。起動時の組をここで作る（DXR 非対応なら初期化しない）
        rtEffects_.Initialize(
            { dxCommon, descriptorAllocator, accelerationStructureManager_.get(), shaderProgramCache },
            accelerationStructureManager_->IsSupported());
        rtEffects_.EnsureGroup(nullptr);

#ifdef CORE_EDITOR
        // デバッグ表示のパイプラインは初めて頼まれたときに作る
        rtDebug_ = std::make_unique<RTDebugSystem>();
        rtDebug_->Initialize(dxCommon, shaderProgramCache);
#endif

        atmosphereManager_ = std::make_unique<AtmosphereManager>();
        atmosphereManager_->Initialize(device, descriptorAllocator, dxCommon->GetUploadRing());
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext: AtmosphereManager 初期化完了\n");

        volumetricCloudManager_ = std::make_unique<VolumetricCloudManager>();
        volumetricCloudManager_->Initialize(dxCommon, descriptorAllocator);
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext: VolumetricCloudManager 初期化完了\n");

        fogManager_ = std::make_unique<FogManager>();
        if (fogManager_->Initialize(dxCommon)) {
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: FogManager 初期化完了\n");
        }

        // ウィンドウリサイズ通知を受ける（シーン深度 / GBuffer / レイトレの効果の作り直し）
        dxCommon->RegisterResizable(this);

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::Initialize: 全ドメインマネージャーの初期化完了\n");
    }

    void RenderDomainContext::EnsureWaterInitialized()
    {
        if (waterInitialized_ || !dxCommon_) {
            return;
        }
        waterInitialized_ = true;
        GraphicsCore* dxCommon = dxCommon_;
        ShaderProgramCache* shaderProgramCache = shaderProgramCache_;
        auto* descriptorAllocator = dxCommon->GetDescriptorAllocator();

        // 水面の組のレイトレの効果（屈折・反射・コースティクス・海底の高さ・海の範囲）
        rtEffects_.EnsureGroup(kWaterRTEffectGroup);

        fftOceanManager_ = std::make_unique<FFTOceanManager>();
        if (fftOceanManager_->Initialize(dxCommon, descriptorAllocator)) {
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: FFTOceanManager 初期化完了\n");
        }

        waterWhitecapFoam_ = std::make_unique<WaterWhitecapFoam>();
        waterShoreFoam_ = std::make_unique<WaterShoreFoam>();
        if (fftOceanManager_->IsInitialized()) {
            if (waterWhitecapFoam_->Initialize(dxCommon, descriptorAllocator)) {
                Logger::GetInstance().Infof(LogCategory::Graphics,
                    "RenderDomainContext: WaterWhitecapFoam 初期化完了\n");
            }
            if (waterShoreFoam_->Initialize(dxCommon, descriptorAllocator)) {
                Logger::GetInstance().Infof(LogCategory::Graphics,
                    "RenderDomainContext: WaterShoreFoam 初期化完了\n");
            }
        }

        waterShoreMeshDisplacement_ = std::make_unique<WaterShoreMeshDisplacement>();
        if (fftOceanManager_->IsInitialized()
            && waterShoreMeshDisplacement_->Initialize(dxCommon, descriptorAllocator, shaderProgramCache)) {
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: WaterShoreMeshDisplacement 初期化完了\n");
        }

        waterHeightQueryManager_ = std::make_unique<WaterHeightQueryManager>();
        if (waterHeightQueryManager_->Initialize(dxCommon, descriptorAllocator, shaderProgramCache, waterQuery_.get())) {
            Logger::GetInstance().Infof(LogCategory::Graphics,
                "RenderDomainContext: WaterHeightQueryManager 初期化完了\n");
        }
    }

    void RenderDomainContext::WaitForRayTracingPipelines()
    {
        rtEffects_.WaitForPipelines();
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
        fogManager_.reset();
        volumetricCloudManager_.reset();
        atmosphereManager_.reset();
#ifdef CORE_EDITOR
        rtDebug_.reset();
#endif
        rtEffects_.Shutdown();
        waterHeightQueryManager_.reset();
        waterShoreMeshDisplacement_.reset();
        waterShoreFoam_.reset();
        waterWhitecapFoam_.reset();
        fftOceanManager_.reset();
        vertexAnimationDeformer_.reset();
        rtScene_.reset();
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

        // レイトレの効果の出力を、GPU が止まっているこの時点で新しい大きさへ作り直す
        rtEffects_.OnResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "RenderDomainContext::OnWindowResize: %dx%d にリサイズ完了\n", width, height);
    }

    void RenderDomainContext::PopulateRenderContext(RenderContext& context)
    {
        context.sceneDepth = sceneDepth_.get();
        context.gBufferManager = gBufferManager_.get();
        context.accelerationStructureManager = accelerationStructureManager_.get();
        context.rtScene = rtScene_.get();
#ifdef CORE_EDITOR
        context.rtDebug = rtDebug_.get();
#endif
        context.vertexAnimationDeformer = vertexAnimationDeformer_.get();
        context.rtEffects = &rtEffects_;
        context.fftOceanManager = fftOceanManager_.get();
        context.waterWhitecapFoam = waterWhitecapFoam_.get();
        context.waterShoreFoam = waterShoreFoam_.get();
        context.waterShoreMeshDisplacement = waterShoreMeshDisplacement_.get();
        context.waterHeightQueryManager = waterHeightQueryManager_.get();
        context.atmosphereManager = atmosphereManager_.get();
        context.volumetricCloudManager = volumetricCloudManager_.get();
        context.fogManager = fogManager_.get();

        // 水面の状態は WaterSystem が PublishWaterFrame で渡す
        context.waterSurfaceState = waterSurfaceState_;
        context.otherWaterSurfaceStates = otherWaterSurfaceStates_;
        context.fftOceanSimulationTime = fftOceanSimulationTime_;
    }

    void RenderDomainContext::EndFrame(LightManager* lightManager)
    {
        // 大気・雲は「Update() を呼んだシーンでのみ有効」というフレーム有効化フラグを持つ。
        // 全 View の描画（AerialPerspective 合成を含む）が終わったここで落とし、
        // 他シーンへの漏れ出しを防ぐ。
        if (atmosphereManager_) {
            atmosphereManager_->EndFrame(lightManager);
        }
        if (volumetricCloudManager_) {
            volumetricCloudManager_->EndFrame();
        }
        if (fogManager_) {
            fogManager_->EndFrame();
        }
    }

} // namespace CoreEngine
