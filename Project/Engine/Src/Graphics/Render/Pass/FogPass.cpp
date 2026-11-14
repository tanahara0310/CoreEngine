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
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief 今フレームの水面の高さと範囲を集める（水面が無ければ valid = false のまま）
        FogWaterInfo BuildWaterInfo(const WaterSurfaceData* surface)
        {
            FogWaterInfo water{};
            if (!surface || surface->regionValid == 0) {
                return water;
            }
            water.height = surface->waterHeight;
            water.regionCenterXZ = Vector2{ surface->regionCenterXZ[0], surface->regionCenterXZ[1] };
            water.regionHalfExtentXZ = Vector2{ surface->regionHalfExtentXZ[0], surface->regionHalfExtentXZ[1] };
            water.valid = true;
            return water;
        }

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
            const FogVolumetricResources& volumetric = fog.GetForwardVolumetricResources();

            for (const auto passType : { RenderPassType::Model, RenderPassType::SkinnedModel }) {
                if (auto* renderer = dynamic_cast<BaseModelRenderer*>(
                        context.renderManager->GetRenderer(passType))) {
                    renderer->SetFogConstants(full, additive, disabled, volumetric);
                }
            }

            // パーティクルは出力がアルファ事前乗算の加算合成なので、内散乱を足さない
            // 「減衰のみ」バリアントを渡す（足すと背後の不透明面の分と二重に光る）。
            // 合成なしのパーティクルは全画面の合成の前に描かれるので「何もしない」バリアントを使う
            for (const auto passType : { RenderPassType::Particle,
                                        RenderPassType::ModelParticle,
                                        RenderPassType::GpuParticle }) {
                if (auto* renderer = dynamic_cast<BaseParticleRenderer*>(
                        context.renderManager->GetRenderer(passType))) {
                    renderer->SetFogConstants(additive, disabled, volumetric);
                }
            }
        }
    }

    void FogPass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        // SceneDepth はどの View でも読む（定数の用意だけでも深度は不要だが、
        // 合成する View と宣言を揃えておく方が Graph の依存が読みやすい）
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // SceneColor へ書くのは GameView だけ
        if (context.viewSettings.viewType == RenderViewType::GameView) {
            // ボリュメトリックフォグの格子を積み上げ終えてから合成する（後の前方描画の PS も読むので両方の読み取り状態）
            if (context.fogManager && context.fogManager->IsVolumetricRequested()) {
                builder.Read(FrameBlackboard::VolumetricFog,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                // 格子より奥の霧に雲の影を引く（雲が無いシーンでは登録されない）
                builder.Read(FrameBlackboard::CloudShadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                // デバッグ表示が太陽から見た深さの画像を引く（作らないフレームでは登録されない）
                builder.Read(FrameBlackboard::VolumetricSunDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
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
        // フォグが無効なフレームでも定数だけは必ず用意する。ボリュメトリックの格子は GameView のカメラで作る
        context.fogManager->PrepareConstants(view, sky, BuildWaterInfo(context.waterSurfaceState),
            context.viewSettings.viewType == RenderViewType::GameView);
        PublishFogConstants(context);

        // ここから先は SceneColor への合成。GameView かつ有効なときだけ
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
