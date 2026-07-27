#include "pch.h"
#include "WaterCausticsPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void WaterCausticsPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::WaterCaustics, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void WaterCausticsPass::Execute(const RenderContext& context)
    {
        if (!context.renderingTechniqueManager || !context.renderTargetManager || !context.dxCommon) {
            return;
        }

        auto* caustics = context.renderingTechniqueManager->GetTechnique<WaterCausticsTechnique>(
            RenderingTechniqueNames::WaterCaustics);
        if (!caustics || !caustics->IsEnabled()) {
            return;
        }

        // RT 方式が選択されているフレームはスクリーンスペース版を実行しない
        // （合成されるのは片方だけなので、描いても捨てるだけになる）
        if (caustics->GetBackend() != WaterCausticsTechnique::Backend::ScreenSpace) {
            return;
        }

        caustics->SetRenderTargetName(targetName_);

        D3D12_GPU_DESCRIPTOR_HANDLE outputHandle{};
        caustics->Execute(context, outputHandle);

        if (context.frameBlackboard) {
            ID3D12Resource* resource = nullptr;
            D3D12_RESOURCE_STATES* stateRef = nullptr;
            if (RenderTarget* target = context.renderTargetManager->GetRenderTarget(targetName_)) {
                resource = target->GetResource();
                if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(target)) {
                    stateRef = &offscreen->GetCurrentState();
                }
            }

            context.frameBlackboard->SetResource(
                FrameBlackboard::WaterCaustics,
                outputHandle,
                resource,
                stateRef);

            if (caustics->GetParams().debugLogEnabled != 0) {
                Logger::GetInstance().Infof(
                    LogCategory::Graphics,
                    LogSubCategory::RenderTarget,
                    "WaterCausticsPass: blackboard updated. handle=0x{:X} resource={} state={} waves={} mainLight={} debugViewMode={}",
                    outputHandle.ptr,
                    resource != nullptr,
                    stateRef ? static_cast<uint32_t>(*stateRef) : 0u,
                    caustics->GetDiagnostics().activeWaveCount,
                    caustics->GetDiagnostics().mainLightEnabled,
                    caustics->GetParams().debugViewMode);
            }
        }
    }
}
