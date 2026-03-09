#include "GeometryPass.h"
#include "Graphics/Render/Render.h"

namespace CoreEngine
{
    void GeometryPass::Execute(const RenderContext& context)
    {
        if (!context.render) {
            return;
        }

        // オフスクリーンの描画開始
        context.render->OffscreenPreDraw(offscreenIndex_);

        // シーン固有の描画処理を実行
        if (renderCallback_) {
            renderCallback_();
        }

        // オフスクリーン描画の終了
        context.render->OffscreenPostDraw(offscreenIndex_);
    }
}
