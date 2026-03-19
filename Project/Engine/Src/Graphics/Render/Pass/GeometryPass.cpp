#include "GeometryPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include <cassert>

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

        // clearEnabled_ フラグを RenderTarget に伝播する
        // DeferredLightingPass が先に Offscreen0 に書き込んでいる場合はクリアしない
        targetToUse->SetClearEnabled(clearEnabled_);

        // レンダーターゲット開始（自動でRTV/DSV/ビューポート/シザー設定）
        targetToUse->Begin(cmdList);

        // シーン固有の描画処理を実行（透過・Skybox・UI など）
        if (renderCallback_) {
            renderCallback_();
        }

        // レンダーターゲット終了（自動でリソースバリア）
        targetToUse->End(cmdList);

        // 出力を設定（次のパスに渡す）
        output_.srvHandle = targetToUse->GetSRVHandle();
        output_.resource = targetToUse->GetResource();
        output_.isValid = true;
    }
}
