#include "pch.h"
#include "PostEffectGraphicsBase.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include <cassert>
#include <stdexcept>


namespace CoreEngine
{
    void PostEffectGraphicsBase::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        fullscreenVertexShaderBlob_ = shaderCompiler.CompileShader(
            L"FullScreen.VS.hlsl", L"vs_6_0");
        pixelShaderBlob_ = shaderCompiler.CompileShader(
            GetPixelShaderPath(), L"ps_6_0");

        // リフレクション
        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());
        reflectionData_ = reflectionBuilder.BuildFromShaders(
            fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(), GetEffectName());

        // RootSignature 構築
        RootSignatureConfig config = RootSignatureConfig::Simple();
        config.ConfigureSampler("gSampler", SamplerConfig::LinearClamp());
        OnConfigureRootSignature(config);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(dxCommon->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error(
                GetEffectName() + ": Failed to create PostEffect Root Signature: " + buildResult.errorMessage);
        }

        // オフスクリーン RT 用 PSO (R16G16B16A16_FLOAT)
        bool result = pipelineStateManager_.CreateBuilder()
            .SetDebugName(GetEffectName())
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .Build(dxCommon->GetDevice(), fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(),
                rootSignatureManager_->GetRootSignature());

        if (!result) {
            throw std::runtime_error(GetEffectName() + ": Failed to create PSO");
        }

        // バックバッファ用 PSO (_SRGB フォーマット)
        bool bbResult = backBufferPipelineStateManager_.CreateBuilder()
            .SetDebugName(GetEffectName() + "_BackBuffer")
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .SetRenderTargetFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            .Build(dxCommon->GetDevice(), fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(),
                rootSignatureManager_->GetRootSignature());

        if (!bbResult) {
            throw std::runtime_error(GetEffectName() + ": Failed to create BackBuffer PSO");
        }
    }

    void PostEffectGraphicsBase::DrawInternal(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle, PipelineStateManager& psm)
    {
        auto* commandList = directXCommon_->GetCommandList();

        commandList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        commandList->SetPipelineState(psm.GetPipelineState(BlendMode::kBlendModeNone));
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        int textureIdx = GetRootParamIndex("gTexture");
        if (textureIdx >= 0) {
            commandList->SetGraphicsRootDescriptorTable(textureIdx, inputSrvHandle);
        }

        BindOptionalCBVs(commandList);

        commandList->DrawInstanced(3, 1, 0, 0);
    }

    void PostEffectGraphicsBase::Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
    {
        DrawInternal(inputSrvHandle, pipelineStateManager_);
    }

    void PostEffectGraphicsBase::DrawToBackBuffer(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
    {
        DrawInternal(inputSrvHandle, backBufferPipelineStateManager_);
    }
}
