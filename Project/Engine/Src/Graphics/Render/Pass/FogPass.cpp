#include "pch.h"
#include "FogPass.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Fog/FogManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Particle/BaseParticleRenderer.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief 大気から空色ブレンド用の値を集める（LUT が無ければ valid = false のまま）
        FogSkyInfo BuildSkyInfo(const AtmosphereManager* atmosphere)
        {
            FogSkyInfo sky{};
            if (!atmosphere || !atmosphere->IsAtmosphereActive() || !atmosphere->AreLUTsReady()) {
                return sky;
            }
            sky.skyViewLutSrv = atmosphere->GetSkyViewLUTSRVHandle();
            sky.cameraRadiusKm = atmosphere->GetDistanceFromPlanetCenter() * 0.001f;
            sky.planetRadiusKm = atmosphere->GetParameters().planetRadius * 0.001f;
            sky.valid = (sky.skyViewLutSrv.ptr != 0);
            return sky;
        }

        /// @brief 前方描画のモデルレンダラーへ今フレームのフォグ定数を配る
        /// @details 半透明・水面は全画面合成の後に描かれるため、各シェーダーが
        ///          Fog.hlsli の ApplyFog を自分で呼ぶ。その入力がこれ。
        void PublishFogConstants(const RenderContext& context)
        {
            if (!context.renderManager || !context.fogManager) {
                return;
            }

            const FogManager& fog = *context.fogManager;
            const D3D12_GPU_VIRTUAL_ADDRESS full = fog.GetConstantsGpuAddress(FogVariant::Full);
            const D3D12_GPU_VIRTUAL_ADDRESS additive = fog.GetConstantsGpuAddress(FogVariant::Additive);
            const D3D12_GPU_VIRTUAL_ADDRESS disabled = fog.GetConstantsGpuAddress(FogVariant::Disabled);

            for (const auto passType : { RenderPassType::Model, RenderPassType::SkinnedModel }) {
                if (auto* renderer = dynamic_cast<BaseModelRenderer*>(
                        context.renderManager->GetRenderer(passType))) {
                    renderer->SetFogConstants(full, additive, disabled);
                }
            }

            // パーティクルは出力がアルファ事前乗算の加算合成なので、内散乱を足さない
            // 「減衰のみ」バリアントだけを渡す（足すと背後の不透明面の分と二重に光る）
            for (const auto passType : { RenderPassType::Particle,
                                        RenderPassType::ModelParticle,
                                        RenderPassType::GpuParticle }) {
                if (auto* renderer = dynamic_cast<BaseParticleRenderer*>(
                        context.renderManager->GetRenderer(passType))) {
                    renderer->SetFogConstants(additive);
                }
            }
        }
    }

    void FogPass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        // SceneDepth はどの View でも読む（定数の用意だけでも深度は不要だが、
        // 合成する View と宣言を揃えておく方が Graph の依存が読みやすい）
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // SceneColor へ書くのはメイン GameView だけ。補助 View では定数を配るだけなので、
        // 書き込みを宣言すると不要な UAV 遷移が入る
        if (context.viewSettings.viewType == RenderViewType::GameView) {
            builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    void FogPass::Execute(const RenderContext& context)
    {
        if (!context.dxCommon || !context.fogManager || !context.frameViews) {
            return;
        }

        // 深度復元にはフレーム先頭で確定したビューを使う（カメラを直接読むと
        // 読み取り時刻で行列が変わり、パスごとに違う復元結果になる）
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
        if (!view.isValid) {
            return;
        }

        const FogSkyInfo sky = BuildSkyInfo(context.atmosphereManager);

        // 前方描画（半透明・水面）は全画面合成の有無に関わらず gFog を差すので、
        // フォグが無効なフレーム・合成しない補助 View でも定数だけは必ず用意する
        context.fogManager->PrepareConstants(view, sky);
        PublishFogConstants(context);

        // ここから先は SceneColor への合成。メイン GameView かつ有効なときだけ
        if (context.viewSettings.viewType != RenderViewType::GameView) {
            return;
        }
        if (!context.fogManager->IsFogActive() || !context.renderTargetManager) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            return;
        }

        auto* sceneColorTarget = dynamic_cast<OffscreenRenderTarget*>(
            context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName));
        if (!sceneColorTarget || !sceneColorTarget->GetResource()) {
            return;
        }

        // 深度は DeclareResources で Read 宣言した Blackboard の SceneDepth から取る
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
        if (!context.frameBlackboard
            || !context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, sceneDepthSrv)) {
            return;
        }

        context.fogManager->ApplyFog(
            cmdList,
            sceneColorTarget->Resource(),
            sceneColorTarget->GetUAVHandle(),
            sceneDepthSrv,
            view,
            sky);
    }
}
