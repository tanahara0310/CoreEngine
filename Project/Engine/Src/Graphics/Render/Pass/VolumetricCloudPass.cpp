#include "pch.h"
#include "VolumetricCloudPass.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void VolumetricCloudPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // SceneDepth を読み、SceneColor へ雲を in-place 合成する（合成 CS が UAV で読み書きする）。
        // CloudBuffer は本パスが生成し、GodRayPass が読む（依存をグラフへ見せるため宣言する）。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        builder.Write(FrameBlackboard::CloudBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    void VolumetricCloudPass::Execute(const RenderContext& context)
    {
        // GameView のみで有効（水面反射などの補助 View には適用しない）
        if (context.viewSettings.viewType != RenderViewType::GameView) {
            return;
        }

        if (!context.dxCommon || !context.volumetricCloudManager ||
            !context.atmosphereManager || !context.renderTargetManager) {
            return;
        }

        // 雲を使うシーン（Update() を呼ぶシーン）でのみ SceneColor へ合成する。
        if (!context.volumetricCloudManager->AreCloudsActive()) {
            return;
        }

        // 大気 LUT（太陽色・アンビエント）が未生成なら描かない。
        if (!context.atmosphereManager->AreLUTsReady()) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            return;
        }

        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）

        auto* sceneColorTarget = dynamic_cast<OffscreenRenderTarget*>(
            context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName));
        if (!sceneColorTarget || !sceneColorTarget->GetResource()) {
            return;
        }

        // 深度は DeclareResources で Read 宣言した Blackboard の SceneDepth から取る
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
        if (!context.frameBlackboard
            || !context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, sceneDepthSrv)) {
            return;
        }

        context.volumetricCloudManager->RenderClouds(
            cmdList,
            sceneColorTarget->Resource(),
            sceneColorTarget->GetUAVHandle(),
            sceneDepthSrv,
            context.atmosphereManager);

        // 後続の GodRayPass が読む雲バッファと雲シャドウマップをグラフへ公開する
        CloudResources& cloudResources = context.volumetricCloudManager->GetResources();
        context.frameBlackboard->SetResource(FrameBlackboard::CloudBuffer,
            cloudResources.cloudBuffer.srv.gpuHandle, &cloudResources.cloudBuffer);
        context.frameBlackboard->SetResource(FrameBlackboard::CloudShadowMap,
            cloudResources.cloudShadowMap.srv.gpuHandle, &cloudResources.cloudShadowMap);
    }
}
