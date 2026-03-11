#include "BackBufferPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include <cassert>

namespace CoreEngine
{
    void BackBufferPass::Execute(const RenderContext& context)
    {
        // RenderTargetが設定されていない場合はエラー
        if (!renderTarget_) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: BackBufferPass: RenderTarget not set! Call SetRenderTarget() in BuildDefaultRenderPipeline().\n");
#endif
            assert(false && "BackBufferPass requires a valid RenderTarget. Call SetRenderTarget() before execution.");
            return;
        }

        // 必須コンポーネントのチェック
        if (!context.dxCommon) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: BackBufferPass: DirectXCommon is null in RenderContext!\n");
#endif
            assert(false && "BackBufferPass requires DirectXCommon");
            return;
        }

        if (!context.postEffectManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: BackBufferPass: PostEffectManager is null in RenderContext!\n");
#endif
            assert(false && "BackBufferPass requires PostEffectManager");
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();

        // バックバッファへのレンダリング開始（自動でRTV/DSV/ビューポート/シザー設定）
        renderTarget_->Begin(cmdList);

        // 最終結果をバックバッファに描画
        context.postEffectManager->ExecuteEffect("FullScreen", inputHandle_);

        // NOTE: End()はここでは呼ばない
        // ImGuiの描画が終わった後、Render::FinalizeFrame()で呼ばれる
    }
}
