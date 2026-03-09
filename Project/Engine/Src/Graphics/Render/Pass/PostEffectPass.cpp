#include "PostEffectPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/PostEffectManager.h"

namespace CoreEngine
{
    void PostEffectPass::Execute(const RenderContext& context)
    {
        if (!context.postEffectManager || !context.dxCommon) {
            return;
        }

        // ポストエフェクトチェーンの適用
        outputHandle_ = context.postEffectManager->ExecuteEffectChain(
            context.dxCommon->GetOffScreenSrvHandle());
    }
}
