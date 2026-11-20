#include "pch.h"
#include "WaterRipplePass.h"

#include "Graphics/Water/Ripple/WaterRippleSimulation.h"

namespace CoreEngine
{
    void WaterRipplePass::Execute(const RenderContext& context)
    {
        WaterRippleSimulation* const simulation = context.waterRippleSimulation;
        if (!simulation || !simulation->IsInitialized() || !context.cmdList) {
            return;
        }
        // 格子はゲームのカメラで置き場所を決めてビューに依らないので、フレームに 1 回だけ進める
        simulation->Dispatch(context.cmdList, context.frameNumber);
    }
}
