#include "pch.h"
#include "DeferredLightingTechnique.h"
#include "DeferredLightingBindings.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Utility/Logger/Logger.h"
#include <cstring>
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
        CreateConstantBuffers();
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
    // 定数バッファの作成
    // -------------------------------------------------------------------------
    void DeferredLightingTechnique::CreateConstantBuffers()
    {
        assert(graphicsCore_);

        // 単位行列（各定数バッファの初期値）
        const float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };

        // 深度復元用 View*Projection 逆行列専用の定数バッファをビュー種別ごとに作成（64 バイト = float4x4）
        for (size_t vi = 0; vi < kViewTypeCount; ++vi) {
            depthReconstructionBuffers_[vi] = ResourceFactory::CreateBufferResource(
                graphicsCore_->GetDevice(), sizeof(float) * 16);
            depthReconstructionCBVAddresses_[vi] = depthReconstructionBuffers_[vi]->GetGPUVirtualAddress();
            float* drMapped = nullptr;
            depthReconstructionBuffers_[vi]->Map(0, nullptr, reinterpret_cast<void**>(&drMapped));
            std::memcpy(drMapped, identity, sizeof(identity));
            depthReconstructionBuffers_[vi]->Unmap(0, nullptr);
        }

        // IBL パラメータ定数バッファを作成（float x 4 = 16 バイト）
        iblParamsBuffer_ = ResourceFactory::CreateBufferResource(
            graphicsCore_->GetDevice(), sizeof(float) * 4);
        iblParamsCBVAddress_ = iblParamsBuffer_->GetGPUVirtualAddress();

        // デフォルト値で初期化 (rotation=0, intensity=1)
        float iblDefaults[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float* iblMapped = nullptr;
        iblParamsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&iblMapped));
        std::memcpy(iblMapped, iblDefaults, sizeof(iblDefaults));
        iblParamsBuffer_->Unmap(0, nullptr);

        waterCausticsDebugBuffer_ = ResourceFactory::CreateBufferResource(
            graphicsCore_->GetDevice(), sizeof(WaterCausticsDebugSettings));
        waterCausticsDebugCBVAddress_ = waterCausticsDebugBuffer_->GetGPUVirtualAddress();
        UpdateWaterCausticsDebugBuffer();

        // 空アンビエントパラメータ定数バッファ（既定は無効。Execute で毎フレーム更新）
        skyAmbientBuffer_ = ResourceFactory::CreateBufferResource(
            graphicsCore_->GetDevice(), sizeof(SkyAmbientParams));
        skyAmbientCBVAddress_ = skyAmbientBuffer_->GetGPUVirtualAddress();
        SkyAmbientParams skyDefaults{};
        SkyAmbientParams* skyMapped = nullptr;
        skyAmbientBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&skyMapped));
        *skyMapped = skyDefaults;
        skyAmbientBuffer_->Unmap(0, nullptr);
    }

    // -------------------------------------------------------------------------
    // 深度復元用 View*Projection 逆行列を GPU バッファに書き込む（ビューごとに呼び出し）
    // -------------------------------------------------------------------------
    void DeferredLightingTechnique::UpdateDepthReconstruction(RenderViewType viewType, const Matrix4x4& invViewProj)
    {
        const size_t vi = static_cast<size_t>(viewType);
        if (vi >= kViewTypeCount || !depthReconstructionBuffers_[vi]) {
            return;
        }
        float* mapped = nullptr;
        depthReconstructionBuffers_[vi]->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(mapped, &invViewProj, sizeof(Matrix4x4));
        depthReconstructionBuffers_[vi]->Unmap(0, nullptr);
    }

    // -------------------------------------------------------------------------
    // IBL パラメータを GPU バッファに書き込む（毎フレーム呼び出し）
    // -------------------------------------------------------------------------
    void DeferredLightingTechnique::UpdateIBLParams()
    {
        if (!iblParamsBuffer_) {
            return;
        }
        float params[4] = { 
            environmentRotation_.x, 
            environmentRotation_.y, 
            environmentRotation_.z, 
            iblIntensity_ 
        };
        float* mapped = nullptr;
        iblParamsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(mapped, params, sizeof(params));
        iblParamsBuffer_->Unmap(0, nullptr);
    }

    void DeferredLightingTechnique::SetWaterCausticsDebugSettings(const WaterCausticsDebugSettings& settings)
    {
        waterCausticsDebugSettings_ = settings;
        UpdateWaterCausticsDebugBuffer();
    }

    void DeferredLightingTechnique::UpdateWaterCausticsDebugBuffer()
    {
        if (!waterCausticsDebugBuffer_) {
            return;
        }

        float* mapped = nullptr;
        waterCausticsDebugBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(mapped, &waterCausticsDebugSettings_, sizeof(WaterCausticsDebugSettings));
        waterCausticsDebugBuffer_->Unmap(0, nullptr);
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
        if (cameraCBVAddress_ != 0) {
            binder.Set(bindings_[DeferredLightingBind::gCamera], cameraCBVAddress_);
        }

        // ===== 深度復元用 CBV（ビュー種別ごとに独立したバッファを参照） =====
        {
            const size_t vi = static_cast<size_t>(context.viewSettings.viewType);
            if (vi < kViewTypeCount && depthReconstructionCBVAddresses_[vi] != 0) {
                binder.Set(bindings_[DeferredLightingBind::gDepthReconstruction], depthReconstructionCBVAddresses_[vi]);
            }
        }

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

        // ===== IBL SRV =====
        if (context.renderManager) {
            // Irradiance Map（拡散 IBL）
            if (auto handle = context.renderManager->GetIrradianceMapHandle(); handle.ptr != 0) {
                binder.Set(bindings_[DeferredLightingBind::gIrradianceMap], handle);
            }
            // Prefiltered Map（スペキュラ IBL）
            if (auto handle = context.renderManager->GetPrefilteredMapHandle(); handle.ptr != 0) {
                binder.Set(bindings_[DeferredLightingBind::gPrefilteredMap], handle);
            }
            // BRDF LUT（スペキュラ IBL 積分）
            if (auto handle = context.renderManager->GetBRDFLUTHandle(); handle.ptr != 0) {
                binder.Set(bindings_[DeferredLightingBind::gBRDFLUT], handle);
            }
        }

        // ===== IBL パラメータ CBV =====
        if (iblParamsCBVAddress_ != 0) {
            binder.Set(bindings_[DeferredLightingBind::gIBLParams], iblParamsCBVAddress_);
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

        if (waterCausticsDebugCBVAddress_ != 0) {
            binder.Set(bindings_[DeferredLightingBind::gWaterCausticsDebug], waterCausticsDebugCBVAddress_);
        }

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

            // フラグ・スケールを毎フレーム CB へ反映する
            if (skyAmbientBuffer_) {
                SkyAmbientParams params{};
                params.enabled = skyAmbientUsable ? 1u : 0u;
                params.scale = atmosphere ? atmosphere->GetSkyAmbientScale() : 0.0f;
                params.specularEnabled = skySpecularUsable ? 1u : 0u;
                SkyAmbientParams* mapped = nullptr;
                skyAmbientBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
                *mapped = params;
                skyAmbientBuffer_->Unmap(0, nullptr);
            }

            if (skyAmbientCBVAddress_ != 0) {
                binder.Set(bindings_[DeferredLightingBind::gSkyAmbient], skyAmbientCBVAddress_);
            }
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
