#include "pch.h"
#include "RTShadowPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    namespace {
        /// @brief 現在の描画対象ビューに対応する RT シャドウ ViewID を解決する
        RayTracingShadowManager::ViewID ResolveRTShadowViewId(const RenderContext& context)
        {
            return (context.currentRTShadowViewId == static_cast<uint32_t>(RayTracingShadowManager::ViewID::ReflectionView))
                ? RayTracingShadowManager::ViewID::ReflectionView
                : RayTracingShadowManager::ViewID::GameView;
        }

        /// @brief RT シャドウ各ステージ共通の実行前提を検証してコマンドリストを返す
        ID3D12GraphicsCommandList* ResolveRTShadowCommandList(const RenderContext& context)
        {
            if (!context.rayTracingSubsystem || !context.dxCommon || !context.rtShadowManager) {
                return nullptr;
            }
            if (!context.rtShadowManager->IsInitialized()) {
                return nullptr;
            }
            return context.cmdList;
        }
    }

    void RTShadowPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // GBuffer を読み取り、ライトごとのレイトレース結果を生成する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTShadowPass::Execute(const RenderContext& context)
    {
        ID3D12GraphicsCommandList* cmdList = ResolveRTShadowCommandList(context);
        if (!cmdList) {
            return;
        }

        context.rayTracingSubsystem->DispatchRTShadowTrace(
            context, context.dxCommon, cmdList, ResolveRTShadowViewId(context));
    }

    void RTShadowTemporalPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // レイトレース結果（RTShadowMask）へ再投影と Variance Clamping を適用する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTShadowTemporalPass::Execute(const RenderContext& context)
    {
        ID3D12GraphicsCommandList* cmdList = ResolveRTShadowCommandList(context);
        if (!cmdList) {
            return;
        }

        context.rayTracingSubsystem->DispatchRTShadowTemporal(
            context, context.dxCommon, cmdList, ResolveRTShadowViewId(context));
    }

    void RTShadowDenoisePass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // テンポラル蓄積済みの RTShadowMask へ A-Trous デノイズを適用する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTShadowDenoisePass::Execute(const RenderContext& context)
    {
        ID3D12GraphicsCommandList* cmdList = ResolveRTShadowCommandList(context);
        if (!cmdList) {
            return;
        }

        context.rayTracingSubsystem->DispatchRTShadowDenoise(
            context, context.dxCommon, cmdList, ResolveRTShadowViewId(context));

        // RTShadowMask の Blackboard 登録は RegisterFrameResources（View 対応済み）が行うため、
        // 実行中の再登録は不要（パス分離契約 3）。
    }
}
