#include "pch.h"
#include "CloudRenderContext.h"

#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Math/IntegerMath.h"

namespace CoreEngine
{
    void CloudRenderContext::DispatchHalfRes() const
    {
        cmdList->Dispatch(
            DivRoundUp(resources->TargetsWidth(), 8),
            DivRoundUp(resources->TargetsHeight(), 8),
            1);
    }

    void CloudRenderContext::DispatchTraceCells() const
    {
        const uint32_t n = traceInterleave > 0 ? traceInterleave : 1;
        const uint32_t cellsX = DivRoundUp(resources->TargetsWidth(), n);
        const uint32_t cellsY = DivRoundUp(resources->TargetsHeight(), n);
        cmdList->Dispatch(DivRoundUp(cellsX, 8), DivRoundUp(cellsY, 8), 1);
    }

    void CloudRenderContext::DispatchCompositeInPlace(GpuResource& sceneColor) const
    {
        const D3D12_RESOURCE_DESC desc = sceneColor.Desc();
        cmdList->Dispatch(
            DivRoundUp(static_cast<UINT>(desc.Width), 8),
            DivRoundUp(desc.Height, 8),
            1);

        // 書き込み完了を待たせてから、後続パス（Transparent 等）の想定状態へ戻す
        Barrier::UAV(cmdList, sceneColor);
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
