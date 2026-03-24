#include "DeferredLighting.h"

#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include <cstring>

namespace CoreEngine
{
    // -------------------------------------------------------------------------
    // パスのシェーダーパスを返す
    // -------------------------------------------------------------------------
    const std::wstring& DeferredLighting::GetPixelShaderPath() const
    {
        static const std::wstring path = L"Engine/Assets/Shaders/PostProcess/DeferredLighting.PS.hlsl";
        return path;
    }

    // -------------------------------------------------------------------------
    // ルートシグネチャ設定フック: シャドウ比較サンプラーを追加
    // -------------------------------------------------------------------------
    void DeferredLighting::OnConfigureRootSignature(RootSignatureConfig& config)
    {
        // PCF シャドウサンプリングに必要な比較サンプラーを s1 に追加
        config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());
    }

    // -------------------------------------------------------------------------
    // 初期化: 基底クラス初期化後にライト VP 用 CBV を作成する
    // -------------------------------------------------------------------------
    void DeferredLighting::Initialize(DirectXCommon* dxCommon)
    {
        // PostEffectBase の初期化（シェーダーコンパイル・ルートシグネチャ・PSO 構築）
        PostEffectBase::Initialize(dxCommon);

        // ライトビュープロジェクション行列専用の定数バッファを作成（64 バイト = float4x4）
        // CBV は 256 バイトアライメントが必要なので ResourceFactory 経由で確保する
        lightVPBuffer_ = ResourceFactory::CreateBufferResource(
            dxCommon->GetDevice(), sizeof(float) * 16);
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
            dxCommon->GetDevice(), sizeof(float) * 4);
        iblParamsCBVAddress_ = iblParamsBuffer_->GetGPUVirtualAddress();

        // デフォルト値で初期化 (rotationY=0, intensity=1, padding=0)
        float iblDefaults[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
        float* iblMapped = nullptr;
        iblParamsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&iblMapped));
        std::memcpy(iblMapped, iblDefaults, sizeof(iblDefaults));
        iblParamsBuffer_->Unmap(0, nullptr);
    }

    // -------------------------------------------------------------------------
    // ライト VP 行列を GPU バッファに書き込む（毎フレーム呼び出し）
    // -------------------------------------------------------------------------
    void DeferredLighting::UpdateLightViewProjection(const Matrix4x4& mat)
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
    void DeferredLighting::UpdateIBLParams()
    {
        if (!iblParamsBuffer_) {
            return;
        }
        float params[4] = { environmentRotation_.x, environmentRotation_.y, environmentRotation_.z, iblIntensity_ };
        float* mapped = nullptr;
        iblParamsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(mapped, params, sizeof(params));
        iblParamsBuffer_->Unmap(0, nullptr);
    }

    // -------------------------------------------------------------------------
    // ライティングパスの実行
    // inputSrvHandle = AlbedoAO G-Buffer SRV (t0)
    // -------------------------------------------------------------------------
    void DeferredLighting::Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
    {
        auto* commandList = directXCommon_->GetCommandList();

        commandList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        commandList->SetPipelineState(
            pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // ===== G-Buffer SRV のバインド =====

        // t0: AlbedoAO
        const int albedoIdx = GetRootParamIndex("gAlbedoAO");
        if (albedoIdx >= 0 && inputSrvHandle.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(albedoIdx, inputSrvHandle);
        }

        // t1: NormalRoughness
        const int normalIdx = GetRootParamIndex("gNormalRoughness");
        if (normalIdx >= 0 && normalRoughnessHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(normalIdx, normalRoughnessHandle_);
        }

        // t2: EmissiveMetallic
        const int emissiveIdx = GetRootParamIndex("gEmissiveMetallic");
        if (emissiveIdx >= 0 && emissiveMetallicHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(emissiveIdx, emissiveMetallicHandle_);
        }

        // t3: WorldPosition
        const int worldPosIdx = GetRootParamIndex("gWorldPosition");
        if (worldPosIdx >= 0 && worldPositionHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(worldPosIdx, worldPositionHandle_);
        }

        // ===== カメラ CBV =====
        const int cameraIdx = GetRootParamIndex("gCamera");
        if (cameraIdx >= 0 && cameraCBVAddress_ != 0) {
            commandList->SetGraphicsRootConstantBufferView(cameraIdx, cameraCBVAddress_);
        }

        // ===== ライトバインド（LightManager 経由） =====
        // SetLightsToCommandList は UINT 引数を取るため -1 が渡るとクラッシュする。
        // 各インデックスが有効（≥0）であることを確認してから呼び出す。
        if (lightManager_) {
            const int lcIdx = GetRootParamIndex("gLightCounts");
            const int dlIdx = GetRootParamIndex("gDirectionalLights");
            const int plIdx = GetRootParamIndex("gPointLights");
            const int slIdx = GetRootParamIndex("gSpotLights");
            const int alIdx = GetRootParamIndex("gAreaLights");

            if (lcIdx >= 0 && dlIdx >= 0 && plIdx >= 0 && slIdx >= 0 && alIdx >= 0) {
                lightManager_->SetLightsToCommandList(
                    commandList,
                    static_cast<UINT>(lcIdx),
                    static_cast<UINT>(dlIdx),
                    static_cast<UINT>(plIdx),
                    static_cast<UINT>(slIdx),
                    static_cast<UINT>(alIdx)
                );
            }
        }

        // ===== シャドウマップ SRV =====
        const int shadowIdx = GetRootParamIndex("gShadowMap");
        if (shadowIdx >= 0 && shadowMapHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(shadowIdx, shadowMapHandle_);
        }

        // ===== ライト VP 行列 CBV =====
        const int lightVPIdx = GetRootParamIndex("gLightViewProjection");
        if (lightVPIdx >= 0 && lightVPCBVAddress_ != 0) {
            commandList->SetGraphicsRootConstantBufferView(lightVPIdx, lightVPCBVAddress_);
        }

        // ===== IBL SRV =====

        // Irradiance Map（拡散 IBL）
        const int irradianceIdx = GetRootParamIndex("gIrradianceMap");
        if (irradianceIdx >= 0 && irradianceMapHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(irradianceIdx, irradianceMapHandle_);
        }

        // Prefiltered Map（スペキュラ IBL）
        const int prefilteredIdx = GetRootParamIndex("gPrefilteredMap");
        if (prefilteredIdx >= 0 && prefilteredMapHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(prefilteredIdx, prefilteredMapHandle_);
        }

        // BRDF LUT（スペキュラ IBL 積分）
        const int brdfLUTIdx = GetRootParamIndex("gBRDFLUT");
        if (brdfLUTIdx >= 0 && brdfLUTHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(brdfLUTIdx, brdfLUTHandle_);
        }

        // ===== IBL パラメータ CBV =====
        const int iblParamsIdx = GetRootParamIndex("gIBLParams");
        if (iblParamsIdx >= 0 && iblParamsCBVAddress_ != 0) {
            commandList->SetGraphicsRootConstantBufferView(iblParamsIdx, iblParamsCBVAddress_);
        }

        // フルスクリーントライアングルで描画（頂点バッファなし）
        commandList->DrawInstanced(3, 1, 0, 0);
    }
}

