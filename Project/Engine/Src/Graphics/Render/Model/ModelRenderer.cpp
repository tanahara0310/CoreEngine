#include "ModelRenderer.h"
#include "Camera/ICamera.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Model/TransformationMatrix.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Utility/Logger/Logger.h"
#include <cassert>


namespace CoreEngine
{
    void ModelRenderer::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/Object3d.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "ModelRenderer");

        // 新しい設定ベースのRootSignature構築
        RootSignatureConfig config = RootSignatureConfig::PerformanceOptimized();

        // CBVは全てRoot Descriptor（高速アクセス）
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);

        // SRVは個別のDescriptor Table
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);

        // シャドウマップ用サンプラーを特別設定
        config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());

        // 通常サンプラーはリニア
        config.ConfigureSampler("gSampler", SamplerConfig::Anisotropic());

        // RootSignatureを構築
        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);

        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Root Signature: " + buildResult.errorMessage);
        }

        // CBVサイズ検証（C++構造体とHLSL構造体のサイズ一致を確認）
        reflectionData_->ValidateAllCBVSizes({
            {"gTransformationMatrix", sizeof(TransformationMatrix)},
            {"gMaterial", sizeof(MaterialConstants)}
            });

        // 必須リソースの存在チェック
        if (GetRootParamIndex("gMaterial") < 0) {
            throw std::runtime_error("gMaterial constant buffer not found in Object3d.PS.hlsl");
        }
        if (GetRootParamIndex("gTransformationMatrix") < 0) {
            throw std::runtime_error("gTransformationMatrix constant buffer not found in Object3d.VS.hlsl");
        }
        if (GetRootParamIndex("gTexture") < 0) {
            throw std::runtime_error("gTexture resource not found in Object3d.PS.hlsl");
        }

        bool result = psoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);
    }

    int ModelRenderer::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void ModelRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {

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

    void ModelRenderer::EndPass() {
    }

    void ModelRenderer::SetCamera(const ICamera* camera) {
        if (camera) {
            cameraCBV_ = camera->GetGPUVirtualAddress();
        } else {
            cameraCBV_ = 0;
        }
    }
}
