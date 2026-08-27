#include "pch.h"
#include "CloudRenderContext.h"

#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"

namespace CoreEngine
{
    void CloudRenderContext::DispatchHalfRes() const
    {
        cmdList->Dispatch(
            (resources->TargetsWidth() + 7) / 8,
            (resources->TargetsHeight() + 7) / 8,
            1);
    }

    void CloudRenderContext::DispatchComposite() const
    {
        const D3D12_RESOURCE_DESC desc = resources->compositeResult.Desc();
        cmdList->Dispatch(
            (static_cast<UINT>(desc.Width) + 7) / 8,
            (desc.Height + 7) / 8,
            1);
    }

    void CloudRenderContext::CopyCompositeToSceneColor(GpuResource& sceneColor) const
    {
        Barrier::UAV(cmdList, resources->compositeResult);
        Barrier::Transition(cmdList, resources->compositeResult, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(sceneColor.Get(), resources->compositeResult.Get());

        // 後続パス（Transparent 等）に備えて元の想定状態へ戻す
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, resources->compositeResult, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}
