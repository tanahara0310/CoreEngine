#include "pch.h"
#include "ASBuildPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/RHI/GraphicsCore.h"

namespace CoreEngine
{
    void ASBuildPass::Execute(const RenderContext& context)
    {
        if (!context.rayTracingSubsystem || !context.dxCommon) {
            return;
        }

        // TLAS 構築と RT シャドウ状態リセットはフレーム内 1 回だけ行う
        if (lastBuiltFrame_ == context.frameNumber) {
            return;
        }
        lastBuiltFrame_ = context.frameNumber;

        context.rayTracingSubsystem->BuildAccelerationStructures(
            context,
            context.dxCommon,
            context.modelManager,
            context.sceneManager);
    }
}
