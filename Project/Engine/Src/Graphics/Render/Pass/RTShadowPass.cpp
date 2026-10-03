#include "pch.h"
#include "RTShadowPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/Pass/RTWaterCausticsPass.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    namespace {
        constexpr RayTracingShadowManager::ViewID kShadowView = RayTracingShadowManager::ViewID::GameView;

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

        /// @brief 水中の受光点を屈折した経路で調べるための水面を作る
        /// @details RT コースティクスが水中の直接光を置き換えるときだけ有効にする
        ///          （コースティクスが水中の区間の遮蔽を、影が水より上の区間の遮蔽を受け持つ）
        RayTracingShadowWaterSurface BuildShadowWaterSurface(const RenderContext& context)
        {
            RayTracingShadowWaterSurface water{};
            if (!IsRayTracedWaterCausticsSelected(context)
                || !context.rtWaterCausticsManager
                || !context.rtWaterCausticsManager->IsInitialized()) {
                return water;
            }

            const WaterSurfaceData& surface = *context.waterSurfaceState;
            water.enabled = true;
            water.height = surface.waterHeight;
            water.refractiveIndex = context.rtWaterCausticsManager->GetSettings().refractiveIndex;
            water.regionCenterXZ[0] = surface.regionCenterXZ[0];
            water.regionCenterXZ[1] = surface.regionCenterXZ[1];
            water.regionHalfExtentXZ[0] = surface.regionHalfExtentXZ[0];
            water.regionHalfExtentXZ[1] = surface.regionHalfExtentXZ[1];
            return water;
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
            context, context.dxCommon, cmdList, kShadowView, BuildShadowWaterSurface(context));
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
            context, context.dxCommon, cmdList, kShadowView);
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
            context, context.dxCommon, cmdList, kShadowView);

        // RTShadowMask の Blackboard 登録は RegisterFrameResources が行うため、
        // 実行中の再登録は不要（パス分離契約 3）。
    }
}
