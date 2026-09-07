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
#include "WinApp/WinApp.h"
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

        // レターボックスの帯はクリア色がそのまま見えるので黒に固定する
        constexpr float kLetterboxColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        targetToUse->SetClearColor(kLetterboxColor);

        // バックバッファへのレンダリング開始（自動でRTV/DSV/ビューポート/シザー設定）
        targetToUse->Begin(cmdList);

        // カメラ・UI は基準解像度の縦横比で描かれている。クライアント領域の縦横比が
        // それと違う場合（フルスクリーン解除など）に全面へ引き伸ばすと絵が伸びるので、
        // 縦横比を保った中央の矩形だけへ転写し、余白は黒帯として残す
        const float targetWidth = static_cast<float>(context.dxCommon->GetClientWidth());
        const float targetHeight = static_cast<float>(context.dxCommon->GetClientHeight());
        if (targetWidth > 0.0f && targetHeight > 0.0f) {
            const float sourceAspect = WinApp::GetReferenceAspect();
            float drawWidth = targetWidth;
            float drawHeight = drawWidth / sourceAspect;
            if (drawHeight > targetHeight) {
                drawHeight = targetHeight;
                drawWidth = drawHeight * sourceAspect;
            }

            D3D12_VIEWPORT viewport{};
            viewport.TopLeftX = (targetWidth - drawWidth) * 0.5f;
            viewport.TopLeftY = (targetHeight - drawHeight) * 0.5f;
            viewport.Width = drawWidth;
            viewport.Height = drawHeight;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            cmdList->RSSetViewports(1, &viewport);

            D3D12_RECT scissor{};
            scissor.left = static_cast<LONG>(viewport.TopLeftX);
            scissor.top = static_cast<LONG>(viewport.TopLeftY);
            scissor.right = static_cast<LONG>(viewport.TopLeftX + drawWidth);
            scissor.bottom = static_cast<LONG>(viewport.TopLeftY + drawHeight);
            cmdList->RSSetScissorRects(1, &scissor);
        }

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
