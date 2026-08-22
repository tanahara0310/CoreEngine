#include "pch.h"
#include "BackBufferPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/FullScreen.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderGraph.h"
#include <cassert>

namespace CoreEngine
{
    void BackBufferPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 最終入力を読み、Present 前のレンダーターゲットとして書き込む。
        builder.Read(inputResourceName_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void BackBufferPass::Execute(const RenderContext& context)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE finalInput{};
        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(inputResourceName_, finalInput);
        }

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
            OutputDebugStringA("ERROR: BackBufferPass: GraphicsCore is null in RenderContext!\n");
#endif
            assert(false && "BackBufferPass requires GraphicsCore");
            return;
        }

        if (!context.postEffectManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: BackBufferPass: PostEffectManager is null in RenderContext!\n");
#endif
            assert(false && "BackBufferPass requires PostEffectManager");
            return;
        }

        auto* cmdList = context.cmdList;

        // バックバッファへのレンダリング開始（自動でRTV/DSV/ビューポート/シザー設定）
        targetToUse->Begin(cmdList);

        // 最終結果をバックバッファに描画（_SRGB用PSOを使用）。
        // 名前で引いて Draw させる汎用 API は撤去した。ここは「FullScreen をバックバッファへ」の
        // 一点しか用がないので、型付きで直接呼ぶ
        if (auto* fullScreen = context.postEffectManager->GetEffect<FullScreen>(PostEffectNames::FullScreen)) {
            fullScreen->DrawToBackBuffer(finalInput);
        }

        // NOTE: End()はここでは呼ばない
        // ImGuiの描画が終わった後、Render::FinalizeFrame()で呼ばれる
    }
}
