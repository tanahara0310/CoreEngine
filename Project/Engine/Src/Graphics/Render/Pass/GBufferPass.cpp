#include "pch.h"
#include "GBufferPass.h"

#include <cassert>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"

namespace CoreEngine
{
    void GBufferPass::Execute(const RenderContext& context)
    {
        if (!context.dxCommon || !context.renderManager || !context.gBufferManager) {
#ifdef _DEBUG
            OutputDebugStringA("WARNING: GBufferPass skipped because required context is missing.\n");
#endif
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();
        auto* gBufferManager = context.gBufferManager;

        if (context.depthStencilManager) {
            context.depthStencilManager->BeginDepthWrite(cmdList);
        }

        // GBuffer の各 MRT と深度へ書き込むジオメトリパスを開始する。
        gBufferManager->BeginGeometryPass(
            cmdList,
            context.depthStencilManager,
            context.dxCommon->GetSRVHeap());

        // 不透明オブジェクトを GBuffer へ描画する。
        context.renderManager->DrawGBufferPass();

        if (context.frameBlackboard) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::GBufferAlbedoAO,
                gBufferManager->GetSRVHandle(GBufferManager::Target::AlbedoAO),
                gBufferManager->GetResource(GBufferManager::Target::AlbedoAO),
                &gBufferManager->GetCurrentState(GBufferManager::Target::AlbedoAO));
            context.frameBlackboard->SetResource(
                FrameBlackboard::GBufferNormalRoughness,
                gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness),
                gBufferManager->GetResource(GBufferManager::Target::NormalRoughness),
                &gBufferManager->GetCurrentState(GBufferManager::Target::NormalRoughness));
            context.frameBlackboard->SetResource(
                FrameBlackboard::GBufferEmissiveMetallic,
                gBufferManager->GetSRVHandle(GBufferManager::Target::EmissiveMetallic),
                gBufferManager->GetResource(GBufferManager::Target::EmissiveMetallic),
                &gBufferManager->GetCurrentState(GBufferManager::Target::EmissiveMetallic));
            context.frameBlackboard->SetResource(
                FrameBlackboard::GBufferWorldPosition,
                gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition),
                gBufferManager->GetResource(GBufferManager::Target::WorldPosition),
                &gBufferManager->GetCurrentState(GBufferManager::Target::WorldPosition));
            context.frameBlackboard->SetResource(
                FrameBlackboard::GBufferMotionVector,
                gBufferManager->GetSRVHandle(GBufferManager::Target::MotionVector),
                gBufferManager->GetResource(GBufferManager::Target::MotionVector),
                &gBufferManager->GetCurrentState(GBufferManager::Target::MotionVector));

            // 深度も Blackboard に登録し、後続 Screen Space 系が論理名で参照できるようにする。
            if (context.depthStencilManager) {
                context.frameBlackboard->SetResource(
                    FrameBlackboard::SceneDepth,
                    context.depthStencilManager->GetDepthSRVHandle(),
                    context.depthStencilManager->GetDepthStencilResource(),
                    &context.depthStencilManager->GetCurrentState());
            }
        }

        // GBuffer の各バッファは context.gBufferManager 経由で後続パスが直接取得する。
    }
}
