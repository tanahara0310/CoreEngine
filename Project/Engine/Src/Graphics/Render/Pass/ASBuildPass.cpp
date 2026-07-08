#include "pch.h"
#include "ASBuildPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/Common/DirectXCommon.h"

namespace CoreEngine
{
    void ASBuildPass::Execute(const RenderContext& context)
    {
        if (!context.rayTracingSubsystem || !context.dxCommon) {
            return;
        }

        // TLAS 構築と RT シャドウ状態リセットはフレーム内 1 回だけ行う。
        // （補助 View → GameView の順で複数 Graph が実行されるため）
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
