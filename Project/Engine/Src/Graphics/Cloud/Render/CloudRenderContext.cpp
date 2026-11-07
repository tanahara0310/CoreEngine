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

    void CloudRenderContext::DispatchTraceCells() const
    {
        const uint32_t n = traceInterleave > 0 ? traceInterleave : 1;
        const uint32_t cellsX = (resources->TargetsWidth() + n - 1) / n;
        const uint32_t cellsY = (resources->TargetsHeight() + n - 1) / n;
        cmdList->Dispatch((cellsX + 7) / 8, (cellsY + 7) / 8, 1);
    }

    void CloudRenderContext::DispatchCompositeInPlace(GpuResource& sceneColor) const
    {
        const D3D12_RESOURCE_DESC desc = sceneColor.Desc();
        cmdList->Dispatch(
            (static_cast<UINT>(desc.Width) + 7) / 8,
            (desc.Height + 7) / 8,
            1);

        // 書き込み完了を待たせてから、後続パス（Transparent 等）の想定状態へ戻す
        Barrier::UAV(cmdList, sceneColor);
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
