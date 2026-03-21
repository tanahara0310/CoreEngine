#include "ModelRenderer.h"
#include "Camera/ICamera.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Model/TransformationMatrix.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"
#include <cassert>
#include <cstring>

namespace
{
    // gIBLParams 定数バッファのレイアウト（HLSL IBLSceneParams 構造体と一致）
    struct IBLSceneParamsCPU {
        float rotationX;
        float rotationY;
        float rotationZ;
        float padding;
    };
    static_assert(sizeof(IBLSceneParamsCPU) == 16, "IBLSceneParams size mismatch");
}

namespace
{
    /// @brief GBuffer PSO のレンダーターゲットフォーマット（GBufferManager::Target と共必ず順序を合わせる）
    constexpr DXGI_FORMAT kModelGBufferFormats[] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,       // AlbedoAO
        DXGI_FORMAT_R16G16B16A16_FLOAT,   // NormalRoughness
        DXGI_FORMAT_R8G8B8A8_UNORM,       // EmissiveMetallic
        DXGI_FORMAT_R32G32B32A32_FLOAT    // WorldPosition
    };
}


namespace CoreEngine
{
    void ModelRenderer::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/Object3d.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        auto gBufferVertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/GBuffer.VS.hlsl", L"vs_6_0");
        assert(gBufferVertexShaderBlob != nullptr);

        auto gBufferPixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/GBuffer.PS.hlsl", L"ps_6_0");
        assert(gBufferPixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        forwardReflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "ModelRenderer");
        gBufferReflectionData_ = reflectionBuilder_->BuildFromShaders(gBufferVertexShaderBlob, gBufferPixelShaderBlob, "ModelRenderer_GBuffer");

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
        auto buildResult = forwardRootSignatureMg_->Build(device, *forwardReflectionData_, config);

        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Root Signature: " + buildResult.errorMessage);
        }

        auto gBufferBuildResult = gBufferRootSignatureMg_->Build(device, *gBufferReflectionData_, config);

        if (!gBufferBuildResult.success) {
            throw std::runtime_error("Failed to create GBuffer Root Signature: " + gBufferBuildResult.errorMessage);
        }

        // CBVサイズ検証（C++構造体とHLSL構造体のサイズ一致を確認）
        forwardReflectionData_->ValidateAllCBVSizes({
            {"gTransformationMatrix", sizeof(TransformationMatrix)},
            {"gMaterial", sizeof(MaterialConstants)},
            {"gIBLParams", sizeof(IBLSceneParamsCPU)}
            });

        gBufferReflectionData_->ValidateAllCBVSizes({
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

        // IBL シーンパラメータ定数バッファの作成（environmentRotationY 専用）
        iblParamsBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(IBLSceneParamsCPU));
        iblParamsCBVAddress_ = iblParamsBuffer_->GetGPUVirtualAddress();
        IBLSceneParamsCPU defaults{ 0.0f, 0.0f, 0.0f, 0.0f };
        void* mapped = nullptr;
        iblParamsBuffer_->Map(0, nullptr, &mapped);
        std::memcpy(mapped, &defaults, sizeof(defaults));
        iblParamsBuffer_->Unmap(0, nullptr);

        if (GetGBufferRootParamIndex("gMaterial") < 0) {
            throw std::runtime_error("gMaterial constant buffer not found in GBuffer.PS.hlsl");
        }
        if (GetGBufferRootParamIndex("gTransformationMatrix") < 0) {
            throw std::runtime_error("gTransformationMatrix constant buffer not found in GBuffer.VS.hlsl");
        }
        if (GetGBufferRootParamIndex("gTexture") < 0) {
            throw std::runtime_error("gTexture resource not found in GBuffer.PS.hlsl");
        }

        bool result = forwardPsoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*forwardReflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, forwardRootSignatureMg_->GetRootSignature());

        bool gBufferResult = gBufferPsoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*gBufferReflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .SetRenderTargetFormats(kModelGBufferFormats, static_cast<UINT>(std::size(kModelGBufferFormats)))
            .BuildGBuffer(device, gBufferVertexShaderBlob, gBufferPixelShaderBlob, gBufferRootSignatureMg_->GetRootSignature());

        if (!result || !gBufferResult) {
            throw std::runtime_error("Failed to create Pipeline State Object");
        }

        forwardPipelineState_ = forwardPsoMg_->GetPipelineState(BlendMode::kBlendModeNone);
        gBufferPipelineState_ = gBufferPsoMg_->GetPipelineState(BlendMode::kBlendModeNone);
    }

    int ModelRenderer::GetRootParamIndex(const std::string& resourceName) const {
        if (!forwardReflectionData_) {
            return -1;
        }
        return forwardReflectionData_->GetRootParameterIndexByName(resourceName);
    }

    int ModelRenderer::GetGBufferRootParamIndex(const std::string& resourceName) const {
        if (!gBufferReflectionData_) {
            return -1;
        }
        return gBufferReflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void ModelRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        isInGBufferPass_ = false;

        if (blendMode != currentBlendMode_) {
            currentBlendMode_ = blendMode;
            forwardPipelineState_ = forwardPsoMg_->GetPipelineState(blendMode);
        }

        cmdList->SetGraphicsRootSignature(forwardRootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(forwardPipelineState_);
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

        // IBL シーンパラメータ（environmentRotationY）を更新・バインド
        if (iblParamsBuffer_) {
            IBLSceneParamsCPU params{ iblRotation_.x, iblRotation_.y, iblRotation_.z, 0.0f };
            void* mapped = nullptr;
            iblParamsBuffer_->Map(0, nullptr, &mapped);
            std::memcpy(mapped, &params, sizeof(params));
            iblParamsBuffer_->Unmap(0, nullptr);

            int iblParamsIdx = GetRootParamIndex("gIBLParams");
            if (iblParamsIdx >= 0) {
                cmdList->SetGraphicsRootConstantBufferView(iblParamsIdx, iblParamsCBVAddress_);
            }
        }
    }

    void ModelRenderer::BeginGBufferPass(ID3D12GraphicsCommandList* cmdList) {
        isInGBufferPass_ = true;
        cmdList->SetGraphicsRootSignature(gBufferRootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(gBufferPipelineState_);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        int materialIdx = GetGBufferRootParamIndex("gMaterial");
        int transformIdx = GetGBufferRootParamIndex("gTransformationMatrix");
        int textureIdx = GetGBufferRootParamIndex("gTexture");

        (void)materialIdx;
        (void)transformIdx;

#ifdef _DEBUG
        if (textureIdx < 0) {
            Logger::GetInstance().Logf(
                LogLevel::Warn,
                LogCategory::Graphics,
                "ModelRenderer::BeginGBufferPass() could not find gTexture in GBuffer root signature.");
        }
#endif
    }

    void ModelRenderer::EndPass() {
        isInGBufferPass_ = false;
    }

    void ModelRenderer::SetCamera(const ICamera* camera) {
        if (camera) {
            cameraCBV_ = camera->GetGPUVirtualAddress();
        } else {
            cameraCBV_ = 0;
        }
    }
}
