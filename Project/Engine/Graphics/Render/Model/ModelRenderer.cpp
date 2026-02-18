#include "ModelRenderer.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Graphics/Light/LightManager.h"
#include "Engine/Graphics/Shader/ShaderReflectionData.h"
#include "Engine/Utility/Logger/Logger.h"
#include <cassert>


namespace CoreEngine
{
    void ModelRenderer::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Object/Object3d.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob);
        
        // リフレクション結果をデバッグ出力
#ifdef _DEBUG
        Logger::GetInstance().Log("=== ModelRenderer Reflection Results ===", LogLevel::INFO, LogCategory::Shader);
        Logger::GetInstance().Log(reflectionData_->ToString(), LogLevel::INFO, LogCategory::Shader);
#endif
        
        rootSignatureMg_->BuildFromReflection(device, *reflectionData_, true);

        // シェーダーのリソース名からルートパラメータインデックスを取得
        materialRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gMaterial");
        transformRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gTransformationMatrix");
        textureRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gTexture");
        lightCountsRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gLightCounts");
        cameraRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gCamera");
        directionalLightsRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gDirectionalLights");
        pointLightsRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gPointLights");
        spotLightsRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gSpotLights");
        areaLightsRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gAreaLights");
        environmentMapRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gEnvironmentTexture");
        shadowMapRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gShadowMap");
        normalMapRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gNormalMap");
        metallicMapRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gMetallicMap");
        roughnessMapRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gRoughnessMap");
        aoMapRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gAOMap");
        irradianceMapRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gIrradianceMap");
        prefilteredMapRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gPrefilteredMap");
        brdfLUTRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gBRDFLUT");
        lightViewProjectionRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("gLightViewProjection");

#ifdef _DEBUG
        Logger::GetInstance().Log("=== ModelRenderer Root Parameter Indices ===", LogLevel::INFO, LogCategory::Shader);
        Logger::GetInstance().Log("gMaterial: " + std::to_string(materialRootParamIndex_), LogLevel::INFO, LogCategory::Shader);
        Logger::GetInstance().Log("gTransformationMatrix: " + std::to_string(transformRootParamIndex_), LogLevel::INFO, LogCategory::Shader);
        Logger::GetInstance().Log("gTexture: " + std::to_string(textureRootParamIndex_), LogLevel::INFO, LogCategory::Shader);
        Logger::GetInstance().Log("gCamera: " + std::to_string(cameraRootParamIndex_), LogLevel::INFO, LogCategory::Shader);
        Logger::GetInstance().Log("gLightCounts: " + std::to_string(lightCountsRootParamIndex_), LogLevel::INFO, LogCategory::Shader);
#endif

        if (materialRootParamIndex_ < 0) {
            throw std::runtime_error("gMaterial constant buffer not found in Object3d.PS.hlsl");
        }
        if (transformRootParamIndex_ < 0) {
            throw std::runtime_error("gTransformationMatrix constant buffer not found in Object3d.VS.hlsl");
        }
        if (textureRootParamIndex_ < 0) {
            throw std::runtime_error("gTexture resource not found in Object3d.PS.hlsl");
        }


        bool result = psoMg_->CreateBuilder()
            .AddInputElement("POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
            .AddInputElement("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
            .AddInputElement("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
            .AddInputElement("TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);
    }

    void ModelRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {

        if (blendMode != currentBlendMode_) {
            currentBlendMode_ = blendMode;
            pipelineState_ = psoMg_->GetPipelineState(blendMode);
        }

        cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(pipelineState_);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (cameraCBV_ != 0 && cameraRootParamIndex_ >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(cameraRootParamIndex_, cameraCBV_);
        }

        if (lightManager_) {
            lightManager_->SetLightsToCommandList(
                cmdList,
                lightCountsRootParamIndex_,
                directionalLightsRootParamIndex_,
                pointLightsRootParamIndex_,
                spotLightsRootParamIndex_,
                areaLightsRootParamIndex_
            );
        }

        if (environmentMapHandle_.ptr != 0 && environmentMapRootParamIndex_ >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(environmentMapRootParamIndex_, environmentMapHandle_);
        }

        if (lightViewProjectionCBV_ != 0 && lightViewProjectionRootParamIndex_ >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(lightViewProjectionRootParamIndex_, lightViewProjectionCBV_);
        }

        if (shadowMapHandle_.ptr != 0 && shadowMapRootParamIndex_ >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(shadowMapRootParamIndex_, shadowMapHandle_);
        }

        if (irradianceMapHandle_.ptr != 0 && irradianceMapRootParamIndex_ >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(irradianceMapRootParamIndex_, irradianceMapHandle_);
        }

        if (prefilteredMapHandle_.ptr != 0 && prefilteredMapRootParamIndex_ >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(prefilteredMapRootParamIndex_, prefilteredMapHandle_);
        }

        if (brdfLUTHandle_.ptr != 0 && brdfLUTRootParamIndex_ >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(brdfLUTRootParamIndex_, brdfLUTHandle_);
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
