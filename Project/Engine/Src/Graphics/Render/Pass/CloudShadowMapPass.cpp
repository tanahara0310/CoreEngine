#include "pch.h"
#include "CloudShadowMapPass.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void CloudShadowMapPass::DeclareResources(
        RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 生成した雲シャドウマップを DeferredLightingPass と GodRayPass が読む
        builder.Write(FrameBlackboard::CloudShadowMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    void CloudShadowMapPass::Execute(const RenderContext& context)
    {
        // GameView のみで有効（水面反射などの補助 View には適用しない）
        if (context.viewSettings.viewType != RenderViewType::GameView) {
            return;
        }

        if (!context.volumetricCloudManager || !context.frameBlackboard) {
            return;
        }

        // 雲を使うシーン（Update() を呼ぶシーン）でのみ生成する
        if (!context.volumetricCloudManager->AreCloudsActive()) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            return;
        }

        context.volumetricCloudManager->RenderCloudShadowMap(
            cmdList, context.atmosphereManager, context.gpuProfiler);

        // 後続の DeferredLighting / GodRay が読めるようグラフへ公開する
        CloudResources& cloudResources = context.volumetricCloudManager->GetResources();
        context.frameBlackboard->SetResource(FrameBlackboard::CloudShadowMap,
            cloudResources.cloudShadowMap.srv.gpuHandle, &cloudResources.cloudShadowMap);
    }
}
