#include "pch.h"
#include "RayTracingSubsystem.h"
#include <vector>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "ObjectCommon/Model/ModelGameObject.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Scene/SceneManager.h"

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

        auto* worldPosResource = context.gBufferManager->GetResource(GBufferManager::Target::WorldPosition);
        auto* normalResource = context.gBufferManager->GetResource(GBufferManager::Target::NormalRoughness);
        auto* motionVecResource = context.gBufferManager->GetResource(GBufferManager::Target::MotionVector);

        // GBufferManager が管理するステートを直接参照して冗長バリアを防ぐ
        auto& worldPosState = context.gBufferManager->GetCurrentState(GBufferManager::Target::WorldPosition);
        auto& normalState = context.gBufferManager->GetCurrentState(GBufferManager::Target::NormalRoughness);
        auto& motionVecState = context.gBufferManager->GetCurrentState(GBufferManager::Target::MotionVector);

        ResourceBarrierHelper::Transition(cmdList, worldPosResource, worldPosState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalResource, normalState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, motionVecResource, motionVecState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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

        // DeferredLightingPass が PIXEL_SHADER_RESOURCE として読み取るために戻す
        ResourceBarrierHelper::Transition(cmdList, worldPosResource, worldPosState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalResource, normalState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, motionVecResource, motionVecState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
