#include "pch.h"
#include "WaterSurfacePass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Water/WaterCVars.h"
#include "Scene/SceneManager.h"
#include "Graphics/Render/RenderGraph.h"
#include <cassert>

namespace CoreEngine
{
    void WaterSurfacePass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::RTWaterRefractionColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
        // 水面も自分のモーションベクターを書く（TAA の再投影用）。
        // GBuffer が書いた値は「水の背後の地形」の動きなので、水面ピクセルでは
        // 上書きしないと TAA がカメラ移動中だけ水面をぼかす。
        // SSAO / RTShadow の Temporal は不透明ジオメトリ向けで、このパスより前に
        // MotionVector を読み終わっているため、ここでの上書きの影響は受けない。
        builder.Write(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void WaterSurfacePass::ConfigureForView(const RenderContext& context)
    {
        SetRenderTargetName(context.viewSettings.sceneColorTargetName);
    }

    void WaterSurfacePass::Execute(const RenderContext& context)
    {
        if (!context.renderTargetManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: WaterSurfacePass: RenderTargetManager is null in RenderContext!\n");
#endif
            assert(false && "WaterSurfacePass requires RenderTargetManager in RenderContext");
            return;
        }

        RenderTarget* targetToUse = context.renderTargetManager->GetRenderTarget(targetName_);
        if (!targetToUse) {
#ifdef _DEBUG
            std::string msg = "ERROR: WaterSurfacePass: RenderTarget '" + targetName_ + "' not found in RenderTargetManager!\n";
            OutputDebugStringA(msg.c_str());
#endif
            assert(false && "WaterSurfacePass requires a valid RenderTarget.");
            return;
        }

        if (!context.dxCommon) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: WaterSurfacePass: DirectXCommon is null in RenderContext!\n");
#endif
            assert(false && "WaterSurfacePass requires DirectXCommon in RenderContext");
            return;
        }

        auto* cmdList = context.cmdList;
        targetToUse->SetClearEnabled(false);
        targetToUse->Begin(cmdList);

        // ---- SceneColor に加えて MotionVector も束ねる（MRT 2 枚）----
        // Begin() が済ませた遷移・ビューポート・シザー・DSV 選択はそのまま活かし、
        // OMSetRenderTargets だけを 2 枚版へ張り替える。
        // ★水面 PSO（WaterPlaneObject::WritesMotionVector）と枚数が必ず一致すること★
        // 食い違うと D3D12 のバリデーションエラーになる。
        // PSO 側（WaterPlaneObject::WritesMotionVector）と同じ CVar で枚数を揃える
        const bool writeMotionVector = WaterCVars::WriteMotionVector.Get();
        auto* offscreenTarget = writeMotionVector ? dynamic_cast<OffscreenRenderTarget*>(targetToUse) : nullptr;
        // MRT を張れたかどうかは一度だけログする。張れないまま PSO だけ 2 枚だと
        // SV_TARGET1 への書き込みが黙って捨てられ、原因が見えなくなるため。
        static bool loggedMrtState = false;
        if (!loggedMrtState) {
            loggedMrtState = true;
            Logger::GetInstance().Infof(
                LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSurfacePass: motion vector MRT {} (offscreen={}, gbuffer={})",
                (offscreenTarget && context.gBufferManager) ? "ENABLED" : "DISABLED",
                offscreenTarget != nullptr,
                context.gBufferManager != nullptr);
        }
        if (offscreenTarget && context.gBufferManager) {
            const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = {
                offscreenTarget->GetRTVHandle(),
                context.gBufferManager->GetRTVHandle(GBufferManager::Target::MotionVector),
            };
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = offscreenTarget->GetBoundDSVHandle();
            cmdList->OMSetRenderTargets(2, rtvHandles, FALSE, &dsvHandle);
        }

        if (context.renderManager) {
            // 描画に使うビューは FrameViews として RenderManager が保持済み
            context.renderManager->SetDebugLineRenderingEnabled(true);
            context.renderManager->DrawWaterQueuePass(cmdList, context.viewSettings.viewType);
        }
        targetToUse->End(cmdList);

        // SceneColor は RegisterFrameResources で同一の実体が登録済みのため、
        // 実行中の Blackboard 再登録は行わない（パス分離契約 3）。
    }
}
