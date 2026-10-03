#include "pch.h"
#include "DeferredLightingTechnique.h"
#include "DeferredLightingBindings.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Utility/Logger/Logger.h"
#include <cassert>

namespace CoreEngine
{
    // -------------------------------------------------------------------------
    // ピクセルシェーダーパスを返す
    // -------------------------------------------------------------------------
    const std::wstring& DeferredLightingTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"DeferredLighting.PS.hlsl";
        return path;
    }

    // -------------------------------------------------------------------------
    // ルートシグネチャ設定フック: シャドウ比較サンプラーを追加
    // -------------------------------------------------------------------------
    void DeferredLightingTechnique::OnConfigureRootSignature(RootSignatureConfig& config)
    {
        // PCF シャドウサンプリングに必要な比較サンプラーを s1 に追加
        config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());
    }

    // -------------------------------------------------------------------------
    // 初期化
    // -------------------------------------------------------------------------
    void DeferredLightingTechnique::Initialize(GraphicsCore* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        ResolveBindings();

        // 毎フレーム変わる定数は、使うフレームの UploadRing に置く
        UploadRing& uploadRing = dxCommon->GetUploadRing();
        fallbackCamera_.Initialize(uploadRing);
        depthReconstruction_.Initialize(uploadRing);
        depthReconstruction_.Set(MathCore::Matrix::Identity());
        waterCausticsDebug_.Initialize(uploadRing);
    }

    void DeferredLightingTechnique::ResolveBindings()
    {
        assert(reflectionData_ && "RootSignature 構築後に呼ぶこと");

        // 宣言表とシェーダー実体を突き合わせる。必須リソースの改名・削除、
        // 種別の食い違いはここで throw される（無言で描画が壊れるのを防ぐ）。
        bindings_ = BindingTable::Resolve(
            *reflectionData_, DeferredLightingBind::kDecls, GetTechniqueName());
    }

    // -------------------------------------------------------------------------
    // ライティングパスの実行
    // -------------------------------------------------------------------------
    void DeferredLightingTechnique::Execute(const RenderContext& context, 
                                            D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        if (!IsEnabled() || !context.renderTargetManager || !context.gBufferManager 
            || !context.dxCommon) {
            outputSrvHandle = {};
            return;
        }

        auto* renderTargetManager = context.renderTargetManager;
        auto* gBufferManager = context.gBufferManager;
        auto* cmdList = context.cmdList;

        // 出力先 RenderTarget を名前で取得
        auto* target = renderTargetManager->GetRenderTarget(targetName_);
        if (!target) {
            outputSrvHandle = {};
            return;
        }

        // フルスクリーンクアッドなので深度テスト／書き込みは不要。useDepthBuffer_=false にして
        // DSV をバインドせず、GBufferPass が書いた深度（後続の GeometryPass/SkyBox が使う）を守る。
        // clearEnabled_ はここで true に戻し、毎フレーム確実に RTV をクリアしてチラつきを防ぐ。
        if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(target)) {
            offscreen->SetUseDepthBuffer(false);
        }
        target->SetClearEnabled(true);

        // レンダリング開始
        target->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        // 以降のバインドは全て ShaderBinder 経由。ルートパラメータは初期化時に
        // 解決済み（slots_）なので、描画中に名前で map を引くことはもう無い。
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);

        // ===== G-Buffer SRV のバインド =====
        binder.Set(bindings_[DeferredLightingBind::gAlbedoAO],
            gBufferManager->GetSRVHandle(GBufferManager::Target::AlbedoAO));
        binder.Set(bindings_[DeferredLightingBind::gNormalRoughness],
            gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness));
        binder.Set(bindings_[DeferredLightingBind::gEmissiveMetallic],
            gBufferManager->GetSRVHandle(GBufferManager::Target::EmissiveMetallic));

        // SceneDepth（WorldPosition ターゲット廃止に伴い、深度から復元する）
        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE depthHandle{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, depthHandle)) {
                binder.Set(bindings_[DeferredLightingBind::gSceneDepth], depthHandle);
            }
        }

        // ===== カメラ CBV =====
        // カメラ不在フレーム（シーン構築中）でも必ず差す。差さないまま描くと、
        // ルート CBV が未定義の GPU 仮想アドレスを指したままシェーダに読まれ、
        // ページフォルト＝デバイスロストになる。G-Buffer の NormalRoughness は
        // アルファ 1.0 でクリアされるため、空のフレームでもアンリット判定の
        // 早期 return には入らず、全ピクセルが gCamera を読みに行く。
        binder.Set(bindings_[DeferredLightingBind::gCamera],
            (cameraCBVAddress_ != 0) ? cameraCBVAddress_ : fallbackCamera_.Address());

        // ===== 深度復元用 CBV（このビューを描く前に設定した行列） =====
        binder.Set(bindings_[DeferredLightingBind::gDepthReconstruction], depthReconstruction_.Address());

        // ===== ライトバインド（LightManager 経由） =====
        // 未解決スロットは ShaderBinder 側で no-op になるので、ここでの IsValid 判定は不要
        if (context.lightManager) {
            context.lightManager->SetLightsToCommandList(
                binder,
                bindings_[DeferredLightingBind::gLightCounts],
                bindings_[DeferredLightingBind::gDirectionalLights],
                bindings_[DeferredLightingBind::gPointLights],
                bindings_[DeferredLightingBind::gSpotLights],
                bindings_[DeferredLightingBind::gAreaLights]
            );
        }

        // ===== RT シャドウマスク SRV（ライトごとに個別バインド） =====
        for (uint32_t li = 0; li < kMaxRTShadowLights; ++li) {
            if (rtShadowHandles_[li].ptr != 0) {
                binder.Set(bindings_[DeferredLightingBind::gRTShadowMask0 + li], rtShadowHandles_[li]);
            }
        }

        // ===== SSAO SRV =====
        if (ssaoHandle_.ptr != 0) {
            binder.Set(bindings_[DeferredLightingBind::gSSAO], ssaoHandle_);
        }

        if (waterCausticsHandle_.ptr != 0) {
            binder.Set(bindings_[DeferredLightingBind::gWaterCaustics], waterCausticsHandle_);
        }

        binder.Set(bindings_[DeferredLightingBind::gWaterCausticsDebug], waterCausticsDebug_.Address());

        // ===== 空アンビエント（大気散乱 SH。Sky Light 相当） =====
        {
            auto* atmosphere = context.atmosphereManager;
            const bool skyAmbientUsable = atmosphere
                && atmosphere->IsAtmosphereActive()
                && atmosphere->IsSkyAmbientEnabled()
                && atmosphere->IsSkyAmbientReady()
                && atmosphere->GetSkyIrradianceSHSRVHandle().ptr != 0;

            // 空スペキュラIBL（Phase 3b）はキューブマップ生成済みのフレームのみ有効
            const bool skySpecularUsable = skyAmbientUsable
                && atmosphere->IsSkySpecularEnabled()
                && atmosphere->IsSkyEnvironmentReady()
                && atmosphere->GetSkySpecularSRVHandle().ptr != 0;

            // フラグ・スケールを毎フレーム UploadRing に置く
            SkyAmbientParams params{};
            params.enabled = skyAmbientUsable ? 1u : 0u;
            params.scale = atmosphere ? atmosphere->GetSkyAmbientScale() : 0.0f;
            params.specularEnabled = skySpecularUsable ? 1u : 0u;
            binder.Set(bindings_[DeferredLightingBind::gSkyAmbient],
                context.dxCommon->GetUploadRing().AllocateConstants(params));
            // SH バッファはバッファ自体が常に存在する（AtmosphereManager 初期化時に生成）。
            // enabled=0 のフレームではシェーダーが読まないため内容は問われない
            if (atmosphere && atmosphere->GetSkyIrradianceSHSRVHandle().ptr != 0) {
                binder.Set(bindings_[DeferredLightingBind::gSkyIrradianceSH], atmosphere->GetSkyIrradianceSHSRVHandle());
            }
            // 空スペキュラキューブマップ（specularEnabled=0 のフレームではシェーダーが読まない）
            if (atmosphere && atmosphere->GetSkySpecularSRVHandle().ptr != 0) {
                binder.Set(bindings_[DeferredLightingBind::gSkySpecularMap], atmosphere->GetSkySpecularSRVHandle());
            }
        }

        // ===== 雲シャドウ（CloudShadowMapPass が生成済みのフレームのみ） =====
        // マップと CB は必ず対で差す。片方だけだとシェーダーが寸法で有効と判定した上で
        // 前フレームの範囲パラメータを読み、影が別の場所に出る
        if (context.volumetricCloudManager && context.volumetricCloudManager->AreCloudsActive()) {
            D3D12_GPU_DESCRIPTOR_HANDLE shadowHandle{};
            const D3D12_GPU_VIRTUAL_ADDRESS shadowCbv =
                context.volumetricCloudManager->GetCloudShadowConstantsAddress();
            if (shadowCbv != 0 && context.frameBlackboard
                && context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::CloudShadowMap, shadowHandle)) {
                binder.Set(bindings_[DeferredLightingBind::gCloudShadowMap], shadowHandle);
                binder.Set(bindings_[DeferredLightingBind::gCloudShadow], shadowCbv);
            }
        }

        // 必須リソースの差し忘れをここで捕まえる（Dev のみ）。
        // 通り抜けると GPU が前フレームの descriptor を読んだ絵が出る。
        binder.ValidateBeforeDraw(bindings_);

        // フルスクリーンクアッドで描画
        DrawFullscreenQuad(cmdList);

        target->End(cmdList);

        // 深度バッファ使用フラグを元に戻す（後続の GeometryPass が DSV を使用するため）
        if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(target)) {
            offscreen->SetUseDepthBuffer(true);
        }

        // 出力SRVハンドルを設定
        outputSrvHandle = target->GetSRVHandle();
    }
}
