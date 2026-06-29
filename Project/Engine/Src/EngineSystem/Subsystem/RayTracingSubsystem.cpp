#include "pch.h"
#include "RayTracingSubsystem.h"
#include <vector>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "ObjectCommon/Model/ModelGameObject.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Camera/ICamera.h"
#include "Math/MathCore.h"
#include "Scene/SceneManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void RayTracingSubsystem::BuildAccelerationStructures(
        const RenderContext& context,
        DirectXCommon* dx,
        ModelManager* modelManager,
        SceneManager* sceneManager)
    {
        auto* asMgr = context.accelerationStructureManager;
        if (!asMgr || !asMgr->IsSupported() || !dx) {
            return;
        }

        // フレーム開始時に RT シャドウの状態をリセット
        if (context.rtShadowManager) {
            context.rtShadowManager->ResetFrameState();
        }

        // BLAS 遅延ビルド（未構築のモデルリソース全てを対象）
        if (modelManager) {
            auto* cmdListForBLAS = dx->GetCommandList();
            modelManager->ForEachResource([&](ModelResource* resource) {
                asMgr->BuildBLASFromModelResource(cmdListForBLAS, resource);
                });
        }

        // DXR TLAS 構築（シーン内の全 ModelGameObject からインスタンスを収集）
        if (!sceneManager) {
            return;
        }

        auto* objMgr = sceneManager->GetCurrentGameObjectManager();
        if (!objMgr) {
            return;
        }

        std::vector<AccelerationStructureManager::InstanceDesc> tlasInstances;

        for (auto& obj : objMgr->GetAllObjects()) {
            if (!obj || !obj->IsActive()) continue;

            // ModelGameObject にダウンキャスト
            auto* modelObj = dynamic_cast<ModelGameObject*>(obj.get());
            if (!modelObj) continue;

            // 半透明オブジェクト（水面など）は RT シャドウのキャスターから除外する
            if (modelObj->GetBlendMode() != BlendMode::kBlendModeNone) continue;

            auto* model = modelObj->GetModel();
            if (!model) continue;

            auto* resource = model->GetModelResource();
            if (!resource || !resource->HasBLAS()) continue;

            AccelerationStructureManager::InstanceDesc inst;
            inst.blasIndex = resource->GetBLASIndex();
            inst.SetTransform(modelObj->GetTransform().GetWorldMatrix());
            tlasInstances.push_back(inst);
        }

        if (!tlasInstances.empty()) {
            asMgr->BuildTLAS(dx->GetCommandList(), tlasInstances);
        }
    }

    void RayTracingSubsystem::DispatchRTShadow(
        const RenderContext& context,
        DirectXCommon* dx,
        ID3D12GraphicsCommandList* cmdList,
        RayTracingShadowManager::ViewID viewId)
    {
        auto* rtShadow = context.rtShadowManager;
        if (!rtShadow || !rtShadow->IsInitialized()) return;
        if (!context.gBufferManager || !context.lightManager) return;
        if (!dx || !cmdList) return;

        auto worldPosSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition);
        auto normalSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness);
        auto motionVecSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::MotionVector);

        const uint32_t maxLights = RayTracingShadowManager::kMaxDirectionalLights;
        const UINT width = static_cast<UINT>(dx->GetClientWidth());
        const UINT height = static_cast<UINT>(dx->GetClientHeight());

        // 全ライト分 DispatchRays を先に実行（GPU パイプラインを詰めるため）
        for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && li < maxLights; ++li) {
            auto* dirLight = context.lightManager->GetDirectionalLight(li);
            if (!dirLight || !dirLight->enabled) continue;

            rtShadow->Dispatch(
                cmdList,
                worldPosSRV,
                normalSRV,
                motionVecSRV,
                dirLight->direction,
                width,
                height,
                viewId,
                li);
        }

        // 全ライト分 テンポラル蓄積パス（空間前処理+再投影+Variance Clamping）
        for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && li < maxLights; ++li) {
            auto* dirLight = context.lightManager->GetDirectionalLight(li);
            if (!dirLight || !dirLight->enabled) continue;

            rtShadow->ApplyTemporal(
                cmdList,
                normalSRV,
                worldPosSRV,
                motionVecSRV,
                width,
                height,
                viewId,
                li);
        }

        // 全ライト分 A-Trous デノイズをまとめて実行
        for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && li < maxLights; ++li) {
            auto* dirLight = context.lightManager->GetDirectionalLight(li);
            if (!dirLight || !dirLight->enabled) continue;

            rtShadow->Denoise(
                cmdList,
                normalSRV,
                worldPosSRV,
                width,
                height,
                viewId,
                li);
        }

        // GBuffer 入力の前後状態遷移は RenderGraph 側の自動遷移へ委譲する。
    }

    void RayTracingSubsystem::DispatchWaterRefraction(
        const RenderContext& context,
        DirectXCommon* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterRefractionRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterRefraction = context.rtWaterRefractionManager;
        if (!rtWaterRefraction) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: water refraction dispatch skipped. manager is null.");
            return;
        }
        if (!rtWaterRefraction->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: water refraction dispatch skipped. manager is not initialized.");
            return;
        }
        if (!context.gBufferManager || !context.sceneManager) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: water refraction dispatch skipped. gBufferManager={} sceneManager={}",
                context.gBufferManager != nullptr,
                context.sceneManager != nullptr);
            return;
        }
        if (!dx || !cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RayTracingSubsystem: water refraction dispatch skipped. dx={} cmdList={}",
                dx != nullptr,
                cmdList != nullptr);
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV{};
        bool hasSceneColorSnapshot = false;
        if (context.frameBlackboard) {
            hasSceneColorSnapshot = context.frameBlackboard->TryGetSrvHandle(
                FrameBlackboard::SceneColorSnapshot,
                sceneColorSRV);

            if (!hasSceneColorSnapshot) {
                context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneColor, sceneColorSRV);
            }
        }
        if (sceneColorSRV.ptr == 0) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::RenderTarget,
                "RayTracingSubsystem: water refraction dispatch skipped. SceneColorSnapshot/SceneColor SRV is invalid.");
            return;
        }

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::RenderTarget,
            "RayTracingSubsystem: water refraction input selected. viewId={} usesSceneColorSnapshot={} srv=0x{:X}",
            static_cast<uint32_t>(viewId),
            hasSceneColorSnapshot,
            sceneColorSRV.ptr);

        ICamera* camera = context.sceneManager->GetGameViewCamera3D();
        if (!camera) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: water refraction dispatch skipped. camera is null.");
            return;
        }

        auto worldPosSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition);
        const Matrix4x4 viewProjection = camera->GetViewMatrix() * camera->GetProjectionMatrix();
        const Vector3 cameraPosition = camera->GetPosition();
        const UINT width = static_cast<UINT>(dx->GetClientWidth());
        const UINT height = static_cast<UINT>(dx->GetClientHeight());

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "RayTracingSubsystem: water refraction dispatch request. viewId={} waterHeight={:.3f} activeWaveCount={} waveTime={:.3f} width={} height={} worldPosSRV=0x{:X} sceneColorSRV=0x{:X} cameraPos=({:.3f}, {:.3f}, {:.3f}) firstWave(dir=({:.3f}, {:.3f}) amp={:.4f} len={:.3f} speed={:.3f} steep={:.3f} phase={:.3f})",
            static_cast<uint32_t>(viewId),
            surfaceData.waterHeight,
            surfaceData.activeWaveCount,
            surfaceData.time,
            width,
            height,
            worldPosSRV.ptr,
            sceneColorSRV.ptr,
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z,
            surfaceData.activeWaveCount > 0 ? surfaceData.waves[0].direction[0] : 0.0f,
            surfaceData.activeWaveCount > 0 ? surfaceData.waves[0].direction[1] : 0.0f,
            surfaceData.activeWaveCount > 0 ? surfaceData.waves[0].amplitude : 0.0f,
            surfaceData.activeWaveCount > 0 ? surfaceData.waves[0].wavelength : 0.0f,
            surfaceData.activeWaveCount > 0 ? surfaceData.waves[0].speed : 0.0f,
            surfaceData.activeWaveCount > 0 ? surfaceData.waves[0].steepness : 0.0f,
            surfaceData.activeWaveCount > 0 ? surfaceData.waves[0].phaseOffset : 0.0f);

        rtWaterRefraction->Dispatch(
            cmdList,
            worldPosSRV,
            sceneColorSRV,
            viewProjection,
            cameraPosition,
            surfaceData,
            width,
            height,
            viewId);
    }

    void RayTracingSubsystem::DispatchWaterCaustics(
        const RenderContext& context,
        DirectXCommon* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterCausticsRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterCaustics = context.rtWaterCausticsManager;
        if (!rtWaterCaustics || !rtWaterCaustics->IsInitialized()) {
            return;
        }
        if (!context.gBufferManager) {
            return;
        }
        if (!dx || !cmdList) {
            return;
        }

        auto worldPosSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition);
        auto normalRoughnessSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness);
        const UINT width = static_cast<UINT>(dx->GetClientWidth());
        const UINT height = static_cast<UINT>(dx->GetClientHeight());
        Vector3 lightDirection = { 0.0f, -1.0f, 0.0f };
        if (context.lightManager) {
            if (DirectionalLightData* mainLight = context.lightManager->GetDirectionalLight(0);
                mainLight && mainLight->enabled) {
                lightDirection = MathCore::Vector::Normalize(mainLight->direction);
            }
        }

        rtWaterCaustics->Dispatch(
            cmdList,
            worldPosSRV,
            normalRoughnessSRV,
            lightDirection,
            surfaceData,
            width,
            height,
            viewId);
    }
}
