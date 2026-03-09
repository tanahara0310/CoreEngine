#include "BackBufferPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/PostEffectManager.h"

namespace CoreEngine
{
    void BackBufferPass::Execute(const RenderContext& context)
    {
        if (!context.render || !context.postEffectManager) {
            return;
        }

        // バックバッファの描画開始
        context.render->BackBufferPreDraw();

        // 最終結果をバックバッファに描画
        context.postEffectManager->ExecuteEffect("FullScreen", inputHandle_);

        // ImGuiはEngineSystem側で描画されるため、ここではバックバッファの描画終了はしない
        // バックバッファの描画終了はImGuiの後に行われる
    }
}
