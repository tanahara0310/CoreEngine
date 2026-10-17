#include "pch.h"
#include "SpriteRenderer.h"
#include "Camera/Camera.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include <cassert>


namespace CoreEngine
{
    void SpriteRenderer::InitializePipeline(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Sprite/Sprite.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Sprite/Sprite.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob.Get(), pixelShaderBlob.Get(), "SpriteRenderer");
        
        // 線形補間とドット絵向けの 2 つを持ち、どちらで読むかはマテリアルが決める
        RootSignatureConfig config;
        config.ConfigureSampler("gSampler", SamplerConfig::Linear());
        config.ConfigureSampler("gPointSampler", SamplerConfig::Point());

        // RootSignatureを構築
        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
        
        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Sprite Root Signature: " + buildResult.errorMessage);
        }

        bool result = psoMg_->CreateBuilder()
            .SetDebugName("Sprite")
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob.Get(), pixelShaderBlob.Get(), rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create Sprite Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);
        currentBlendMode_ = BlendMode::kBlendModeNormal;
    }

    void SpriteRenderer::Initialize(ID3D12Device* /*device*/) {
        assert(false && "SpriteRenderer: Use Initialize(GraphicsCore*) instead of Initialize(ID3D12Device*).");
    }

    int SpriteRenderer::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void SpriteRenderer::Initialize(GraphicsCore* dxCommon) {
        dxCommon_ = dxCommon;

        // パイプラインをデバイスで初期化
        InitializePipeline(dxCommon->GetDevice());
    }

    void SpriteRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        if (blendMode != currentBlendMode_) {
            currentBlendMode_ = blendMode;
            pipelineState_ = psoMg_->GetPipelineState(blendMode);
        }

        cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(pipelineState_);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void SpriteRenderer::EndPass() {
        // 今は空
    }

    void SpriteRenderer::SetCamera(const Camera* camera) {
        // 行列は描くたびにビューから取る
        (void)camera;
    }
}
