#include "pch.h"
#include "DeferredLightingTechnique.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
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
    void DeferredLightingTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        CreateConstantBuffers();
    }

    // -------------------------------------------------------------------------
    // 定数バッファの作成
    // -------------------------------------------------------------------------
    void DeferredLightingTechnique::CreateConstantBuffers()
    {
        assert(directXCommon_);

        // ライトビュープロジェクション行列専用の定数バッファを作成（64 バイト = float4x4）
        lightVPBuffer_ = ResourceFactory::CreateBufferResource(
            directXCommon_->GetDevice(), sizeof(float) * 16);
        lightVPCBVAddress_ = lightVPBuffer_->GetGPUVirtualAddress();

        // 単位行列で初期化
        float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        float* mapped = nullptr;
        lightVPBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(mapped, identity, sizeof(identity));
        lightVPBuffer_->Unmap(0, nullptr);

        // IBL パラメータ定数バッファを作成（float x 4 = 16 バイト）
        iblParamsBuffer_ = ResourceFactory::CreateBufferResource(
            directXCommon_->GetDevice(), sizeof(float) * 4);
        iblParamsCBVAddress_ = iblParamsBuffer_->GetGPUVirtualAddress();

        // デフォルト値で初期化 (rotation=0, intensity=1)
        float iblDefaults[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float* iblMapped = nullptr;
        iblParamsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&iblMapped));
        std::memcpy(iblMapped, iblDefaults, sizeof(iblDefaults));
        iblParamsBuffer_->Unmap(0, nullptr);

        waterCausticsDebugBuffer_ = ResourceFactory::CreateBufferResource(
            directXCommon_->GetDevice(), sizeof(float) * 4);
        waterCausticsDebugCBVAddress_ = waterCausticsDebugBuffer_->GetGPUVirtualAddress();
        UpdateWaterCausticsDebugBuffer();

        // 空アンビエントパラメータ定数バッファ（既定は無効。Execute で毎フレーム更新）
        skyAmbientBuffer_ = ResourceFactory::CreateBufferResource(
            directXCommon_->GetDevice(), sizeof(SkyAmbientParams));
        skyAmbientCBVAddress_ = skyAmbientBuffer_->GetGPUVirtualAddress();
        SkyAmbientParams skyDefaults{};
        SkyAmbientParams* skyMapped = nullptr;
        skyAmbientBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&skyMapped));
        *skyMapped = skyDefaults;
        skyAmbientBuffer_->Unmap(0, nullptr);
    }

    // -------------------------------------------------------------------------
    // ライト VP 行列を GPU バッファに書き込む（毎フレーム呼び出し）
    // -------------------------------------------------------------------------
    void DeferredLightingTechnique::UpdateLightViewProjection(const Matrix4x4& mat)
    {
        if (!lightVPBuffer_) {
            return;
        }
        float* mapped = nullptr;
        lightVPBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(mapped, &mat, sizeof(Matrix4x4));
        lightVPBuffer_->Unmap(0, nullptr);
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
        auto* cmdList = context.dxCommon->GetCommandList();

        // 出力先 RenderTarget を名前で取得
        auto* target = renderTargetManager->GetRenderTarget(targetName_);
        if (!target) {
            outputSrvHandle = {};
            return;
        }

        // DeferredLightingPass はフルスクリーンクアッドを描画するため深度テスト/書き込みは不要。
        // useDepthBuffer_=false にすることで DSV をバインドせず、GBufferPass が書き込んだ
        // 深度情報（後続の GeometryPass/SkyBox で使用）を保護する。
        // また clearEnabled_ が前フレームの GeometryPass によって false に設定されていても
        // ここで true に戻すことで毎フレーム確実に RTV をクリアし、チラつきを防ぐ。
        // DeferredLightingPass はフルスクリーンクアッドを描画するため深度テスト/書き込みは不要。
        // useDepthBuffer_=false にすることで DSV をバインドせず、GBufferPass が書き込んだ
        // 深度情報（後続の GeometryPass/SkyBox で使用）を保護する。
        // また clearEnabled_ が前フレームの GeometryPass によって false に設定されていても
        // ここで true に戻すことで毎フレーム確実に RTV をクリアし、チラつきを防ぐ。
        if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(target)) {
            offscreen->SetUseDepthBuffer(false);
        }
        target->SetClearEnabled(true);

        // レンダリング開始
        target->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        // ===== G-Buffer SRV のバインド =====

        // t0: AlbedoAO
        const int albedoIdx = GetRootParamIndex("gAlbedoAO");
        if (albedoIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(albedoIdx,
                gBufferManager->GetSRVHandle(GBufferManager::Target::AlbedoAO));
        }

        // t1: NormalRoughness
        const int normalIdx = GetRootParamIndex("gNormalRoughness");
        if (normalIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(normalIdx,
                gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness));
        }

        // t2: EmissiveMetallic
        const int emissiveIdx = GetRootParamIndex("gEmissiveMetallic");
        if (emissiveIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(emissiveIdx,
                gBufferManager->GetSRVHandle(GBufferManager::Target::EmissiveMetallic));
        }

        // t3: WorldPosition
        const int worldPosIdx = GetRootParamIndex("gWorldPosition");
        if (worldPosIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(worldPosIdx,
                gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition));
        }

        // ===== カメラ CBV =====
        const int cameraIdx = GetRootParamIndex("gCamera");
        if (cameraIdx >= 0 && cameraCBVAddress_ != 0) {
            cmdList->SetGraphicsRootConstantBufferView(cameraIdx, cameraCBVAddress_);
        }

        // ===== ライトバインド（LightManager 経由） =====
        if (context.lightManager) {
            const int lcIdx = GetRootParamIndex("gLightCounts");
            const int dlIdx = GetRootParamIndex("gDirectionalLights");
            const int plIdx = GetRootParamIndex("gPointLights");
            const int slIdx = GetRootParamIndex("gSpotLights");
            const int alIdx = GetRootParamIndex("gAreaLights");

            if (lcIdx >= 0 && dlIdx >= 0 && plIdx >= 0 && slIdx >= 0 && alIdx >= 0) {
                context.lightManager->SetLightsToCommandList(
                    cmdList,
                    static_cast<UINT>(lcIdx),
                    static_cast<UINT>(dlIdx),
                    static_cast<UINT>(plIdx),
                    static_cast<UINT>(slIdx),
                    static_cast<UINT>(alIdx)
                );
            }
        }

        // ===== シャドウマップ SRV =====
        if (context.shadowMapManager) {
            const int shadowIdx = GetRootParamIndex("gShadowMap");
            if (shadowIdx >= 0) {
                cmdList->SetGraphicsRootDescriptorTable(shadowIdx,
                    context.shadowMapManager->GetSRVHandle());
            }
        }

        // ===== ライト VP 行列 CBV =====
        const int lightVPIdx = GetRootParamIndex("gLightViewProjection");
        if (lightVPIdx >= 0 && lightVPCBVAddress_ != 0) {
            cmdList->SetGraphicsRootConstantBufferView(lightVPIdx, lightVPCBVAddress_);
        }

        // ===== IBL SRV =====
        if (context.renderManager) {
            // Irradiance Map（拡散 IBL）
            const int irradianceIdx = GetRootParamIndex("gIrradianceMap");
            if (irradianceIdx >= 0) {
                auto handle = context.renderManager->GetIrradianceMapHandle();
                if (handle.ptr != 0) {
                    cmdList->SetGraphicsRootDescriptorTable(irradianceIdx, handle);
                }
            }

            // Prefiltered Map（スペキュラ IBL）
            const int prefilteredIdx = GetRootParamIndex("gPrefilteredMap");
            if (prefilteredIdx >= 0) {
                auto handle = context.renderManager->GetPrefilteredMapHandle();
                if (handle.ptr != 0) {
                    cmdList->SetGraphicsRootDescriptorTable(prefilteredIdx, handle);
                }
            }

            // BRDF LUT（スペキュラ IBL 積分）
            const int brdfLUTIdx = GetRootParamIndex("gBRDFLUT");
            if (brdfLUTIdx >= 0) {
                auto handle = context.renderManager->GetBRDFLUTHandle();
                if (handle.ptr != 0) {
                    cmdList->SetGraphicsRootDescriptorTable(brdfLUTIdx, handle);
                }
            }
        }

        // ===== IBL パラメータ CBV =====
        const int iblParamsIdx = GetRootParamIndex("gIBLParams");
        if (iblParamsIdx >= 0 && iblParamsCBVAddress_ != 0) {
            cmdList->SetGraphicsRootConstantBufferView(iblParamsIdx, iblParamsCBVAddress_);
        }

        // ===== RT シャドウマスク SRV（ライトごとに個別バインド） =====
        static const char* rtShadowNames[4] = {
            "gRTShadowMask0", "gRTShadowMask1", "gRTShadowMask2", "gRTShadowMask3"
        };
        for (uint32_t li = 0; li < kMaxRTShadowLights; ++li) {
            const int idx = GetRootParamIndex(rtShadowNames[li]);
            if (idx >= 0 && rtShadowHandles_[li].ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(idx, rtShadowHandles_[li]);
            }
        }

        // ===== SSAO SRV =====
        const int ssaoIdx = GetRootParamIndex("gSSAO");
        if (ssaoIdx >= 0 && ssaoHandle_.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(ssaoIdx, ssaoHandle_);
        }

        const int waterCausticsIdx = GetRootParamIndex("gWaterCaustics");
        if (waterCausticsIdx >= 0 && waterCausticsHandle_.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(waterCausticsIdx, waterCausticsHandle_);
        }

        const int waterCausticsDebugIdx = GetRootParamIndex("gWaterCausticsDebug");
        if (waterCausticsDebugIdx >= 0 && waterCausticsDebugCBVAddress_ != 0) {
            cmdList->SetGraphicsRootConstantBufferView(waterCausticsDebugIdx, waterCausticsDebugCBVAddress_);
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

            const int skyAmbientIdx = GetRootParamIndex("gSkyAmbient");
            if (skyAmbientIdx >= 0 && skyAmbientCBVAddress_ != 0) {
                cmdList->SetGraphicsRootConstantBufferView(skyAmbientIdx, skyAmbientCBVAddress_);
            }
            // SH バッファはバッファ自体が常に存在する（AtmosphereManager 初期化時に生成）。
            // enabled=0 のフレームではシェーダーが読まないため内容は問われない
            const int skyShIdx = GetRootParamIndex("gSkyIrradianceSH");
            if (skyShIdx >= 0 && atmosphere && atmosphere->GetSkyIrradianceSHSRVHandle().ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(skyShIdx, atmosphere->GetSkyIrradianceSHSRVHandle());
            }
            // 空スペキュラキューブマップ（specularEnabled=0 のフレームではシェーダーが読まない）
            const int skySpecIdx = GetRootParamIndex("gSkySpecularMap");
            if (skySpecIdx >= 0 && atmosphere && atmosphere->GetSkySpecularSRVHandle().ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(skySpecIdx, atmosphere->GetSkySpecularSRVHandle());
            }
        }

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
