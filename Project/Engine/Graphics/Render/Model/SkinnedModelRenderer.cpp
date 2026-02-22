#include "SkinnedModelRenderer.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Graphics/Light/LightManager.h"
#include "Engine/Graphics/Shader/ShaderReflectionData.h"
#include "Engine/Graphics/RootSignature/RootSignatureConfig.h"
#include "Engine/Graphics/Structs/TransformationMatrix.h"
#include "Engine/Graphics/Structs/MaterialConstants.h"
#include "Engine/Utility/Logger/Logger.h"
#include <cassert>


namespace CoreEngine
{
    void SkinnedModelRenderer::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        // シェーダーのコンパイル
        auto skinningVertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Skinning/SkinningObject3d.VS.hlsl", L"vs_6_0");
        assert(skinningVertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        // シェーダーリフレクション
        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(skinningVertexShaderBlob, pixelShaderBlob, "SkinnedModelRenderer");

        // 新しい設定ベースのRootSignature構築
        RootSignatureConfig config = RootSignatureConfig::PerformanceOptimized();

        // CBVは全てRoot Descriptor（高速アクセス）
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);

        // SRVは個別のDescriptor Table
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);

        // シャドウマップ用サンプラーを特別設定
        config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());

        // 通常サンプラーは異方性フィルタリング
        config.ConfigureSampler("gSampler", SamplerConfig::Anisotropic());

        // RootSignatureを構築
        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);

        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Skinned Root Signature: " + buildResult.errorMessage);
        }

        // CBVサイズ検証（C++構造体とHLSL構造体のサイズ一致を確認）
        reflectionData_->ValidateAllCBVSizes({
            {"gTransformationMatrix", sizeof(TransformationMatrix)},
            {"gMaterial", sizeof(MaterialConstants)}
        });

        // PSO作成（自動スロット検出でWEIGHT/INDEXは自動的にスロット1に割り当て）
        bool skinningResult = psoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, skinningVertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!skinningResult) {
            throw std::runtime_error("Failed to create Skinning Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);
    }

    int SkinnedModelRenderer::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void SkinnedModelRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {

        if (blendMode != currentBlendMode_) {
            currentBlendMode_ = blendMode;
            pipelineState_ = psoMg_->GetPipelineState(blendMode);
        }

        cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(pipelineState_);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        int cameraIdx = GetRootParamIndex("gCamera");
        if (cameraCBV_ != 0 && cameraIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(cameraIdx, cameraCBV_);
        }

        if (lightManager_) {
            lightManager_->SetLightsToCommandList(
                cmdList,
                GetRootParamIndex("gLightCounts"),
                GetRootParamIndex("gDirectionalLights"),
                GetRootParamIndex("gPointLights"),
                GetRootParamIndex("gSpotLights"),
                GetRootParamIndex("gAreaLights")
            );
        }

        int envMapIdx = GetRootParamIndex("gEnvironmentTexture");
        if (environmentMapHandle_.ptr != 0 && envMapIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(envMapIdx, environmentMapHandle_);
        }

        int lightVPIdx = GetRootParamIndex("gLightViewProjection");
        if (lightViewProjectionCBV_ != 0 && lightVPIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(lightVPIdx, lightViewProjectionCBV_);
        }

        int shadowMapIdx = GetRootParamIndex("gShadowMap");
        if (shadowMapHandle_.ptr != 0 && shadowMapIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(shadowMapIdx, shadowMapHandle_);
        }

        int irradianceIdx = GetRootParamIndex("gIrradianceMap");
        if (irradianceMapHandle_.ptr != 0 && irradianceIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(irradianceIdx, irradianceMapHandle_);
        }

        int prefilteredIdx = GetRootParamIndex("gPrefilteredMap");
        if (prefilteredMapHandle_.ptr != 0 && prefilteredIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(prefilteredIdx, prefilteredMapHandle_);
        }

        int brdfLUTIdx = GetRootParamIndex("gBRDFLUT");
        if (brdfLUTHandle_.ptr != 0 && brdfLUTIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(brdfLUTIdx, brdfLUTHandle_);
        }
    }

    void SkinnedModelRenderer::EndPass() {
    }

    void SkinnedModelRenderer::SetCamera(const ICamera* camera) {
        if (camera) {
            cameraCBV_ = camera->GetGPUVirtualAddress();
        } else {
            cameraCBV_ = 0;
        }
    }
}
