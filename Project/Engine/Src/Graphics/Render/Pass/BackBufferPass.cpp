#include "BackBufferPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include <cassert>

namespace CoreEngine
{
    void BackBufferPass::Execute(const RenderContext& context)
    {
        // RenderTargetManagerが必要
        if (!context.renderTargetManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: BackBufferPass: RenderTargetManager is null in RenderContext!\n");
#endif
            assert(false && "BackBufferPass requires RenderTargetManager in RenderContext");
            return;
        }

        // 名前ベースでターゲットを取得
        RenderTarget* targetToUse = context.renderTargetManager->GetRenderTarget(targetName_);

        if (!targetToUse) {
#ifdef _DEBUG
            std::string msg = "ERROR: BackBufferPass: RenderTarget '" + targetName_ + "' not found in RenderTargetManager!\n";
            OutputDebugStringA(msg.c_str());
#endif
            assert(false && "BackBufferPass requires a valid RenderTarget.");
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
        targetToUse->Begin(cmdList);

        // 最終結果をバックバッファに描画
        context.postEffectManager->ExecuteEffect("FullScreen", inputHandle_);

        // NOTE: End()はここでは呼ばない
        // ImGuiの描画が終わった後、Render::FinalizeFrame()で呼ばれる
    }
}
