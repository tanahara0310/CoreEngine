#include "GeometryPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include <cassert>

namespace CoreEngine
{
    void GeometryPass::Execute(const RenderContext& context)
    {
        // RenderTargetが設定されていない場合はエラー
        if (!renderTarget_) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: GeometryPass: RenderTarget not set! Call SetRenderTarget() in BuildDefaultRenderPipeline().\n");
#endif
            assert(false && "GeometryPass requires a valid RenderTarget. Call SetRenderTarget() before execution.");
            return;
        }

        // DirectXCommonが必要
        if (!context.dxCommon) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: GeometryPass: DirectXCommon is null in RenderContext!\n");
#endif
            assert(false && "GeometryPass requires DirectXCommon in RenderContext");
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();

        // レンダーターゲット開始（自動でRTV/DSV/ビューポート/シザー設定）
        renderTarget_->Begin(cmdList);

        // シーン固有の描画処理を実行
        if (renderCallback_) {
            renderCallback_();
        }

        // レンダーターゲット終了（自動でリソースバリア）
        renderTarget_->End(cmdList);

        // 出力を設定（次のパスに渡す）
        output_.srvHandle = renderTarget_->GetSRVHandle();
        output_.resource = renderTarget_->GetResource();
        output_.isValid = true;
    }
}
