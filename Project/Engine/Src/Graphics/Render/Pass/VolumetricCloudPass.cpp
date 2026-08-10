#include "pch.h"
#include "VolumetricCloudPass.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void VolumetricCloudPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // SceneColor / SceneDepth を読み、SceneColor へ雲を合成する。
        // （SceneColor の SRV/COPY 遷移はパス内部で状態参照を通じて管理する）
        // AerialPerspectivePass と同一の宣言。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

        // 合成 compute はディスクリプタテーブルを使うためパス自身が SRV ヒープをバインドする。
        ID3D12DescriptorHeap* descriptorHeaps[] = { context.dxCommon->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(1, descriptorHeaps);

        auto* sceneColorTarget = dynamic_cast<OffscreenRenderTarget*>(
            context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName));
        if (!sceneColorTarget || !sceneColorTarget->GetResource()) {
            return;
        }

        context.volumetricCloudManager->RenderClouds(
            cmdList,
            sceneColorTarget->GetResource(),
            sceneColorTarget->GetCurrentState(),
            sceneColorTarget->GetSRVHandle(),
            context.dxCommon->GetDepthStencilSRV(),
            context.atmosphereManager);
    }
}
