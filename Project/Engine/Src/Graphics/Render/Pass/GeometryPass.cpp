#include "pch.h"
#include "GeometryPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include <cassert>
#include <memory>

namespace CoreEngine
{
    void GeometryPass::Execute(const RenderContext& context)
    {
        // RenderTargetManagerが必要
        if (!context.renderTargetManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: GeometryPass: RenderTargetManager is null in RenderContext!\n");
#endif
            assert(false && "GeometryPass requires RenderTargetManager in RenderContext");
            return;
        }

        // 名前ベースでターゲットを取得
        RenderTarget* targetToUse = context.renderTargetManager->GetRenderTarget(targetName_);

        if (!targetToUse) {
#ifdef _DEBUG
            std::string msg = "ERROR: GeometryPass: RenderTarget '" + targetName_ + "' not found in RenderTargetManager!\n";
            OutputDebugStringA(msg.c_str());
#endif
            assert(false && "GeometryPass requires a valid RenderTarget.");
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

        // DeferredLightingPass が先に書き込んでいる場合はクリアしない
        targetToUse->SetClearEnabled(clearEnabled_);

        // フォワード描画区間では深度値をシェーダーから参照できるよう
        // DEPTH_WRITE → DEPTH_READ|PIXEL_SHADER_RESOURCE に遷移する。
        // ScopedDepthReadSRV がスコープ終了時に DEPTH_WRITE へ自動復元する。
        // depthStencilManager が未設定の場合はバリアなしで描画する（後方互換）。
        std::unique_ptr<DepthStencilManager::ScopedDepthReadSRV> depthScope;
        if (context.depthStencilManager) {
            depthScope = std::make_unique<DepthStencilManager::ScopedDepthReadSRV>(
                context.depthStencilManager, cmdList);
        }

        targetToUse->Begin(cmdList);
        if (renderCallback_) {
            renderCallback_();
        }
        targetToUse->End(cmdList);

        output_.srvHandle = targetToUse->GetSRVHandle();
        output_.resource = targetToUse->GetResource();
        output_.isValid = true;
    }
}
