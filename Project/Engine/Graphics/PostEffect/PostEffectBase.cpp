#include "PostEffectBase.h"
#include <cassert>


namespace CoreEngine
{
void PostEffectBase::Initialize(DirectXCommon* dxCommon)
{
    assert(dxCommon);
    directXCommon_ = dxCommon;

    ShaderCompiler shaderCompiler;
    shaderCompiler.Initialize();

    fullscreenVertexShaderBlob_ = shaderCompiler.CompileShader(
        L"Assets/Shaders/PostProcess/FullScreen.VS.hlsl", L"vs_6_0");
    pixelShaderBlob_ = shaderCompiler.CompileShader(
        GetPixelShaderPath(), L"ps_6_0");

    RootSignatureManager rootSignatureManager;
    
    // Root Parameter 0: テクスチャ用ディスクリプタテーブル (t0, Pixel Shader)
    RootSignatureManager::DescriptorRangeConfig textureRange;
    textureRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.numDescriptors = 1;
    textureRange.baseShaderRegister = 0;  // t0
    rootSignatureManager.AddDescriptorTable({ textureRange }, D3D12_SHADER_VISIBILITY_PIXEL);
    
    // Root Parameter 1: 定数バッファ用CBV (b0, Pixel Shader) - オプション
    RootSignatureManager::RootDescriptorConfig paramsCBV;
    paramsCBV.shaderRegister = 0;
    paramsCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootSignatureManager.AddRootCBV(paramsCBV);

    // Static Sampler (s0, Pixel Shader)
    RootSignatureManager::StaticSamplerConfig samplerConfig;
    samplerConfig.shaderRegister = 0;
    samplerConfig.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplerConfig.filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerConfig.addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerConfig.addressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerConfig.addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerConfig.comparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerConfig.maxLOD = D3D12_FLOAT32_MAX;
    rootSignatureManager.AddStaticSampler(samplerConfig);

    rootSignatureManager.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    rootSignatureManager.Create(dxCommon->GetDevice());
    rootSignature_ = rootSignatureManager.GetRootSignature();

    // ビルダーパターンでPSOを構築
    bool result = pipelineStateManager_.CreateBuilder()
        .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
        .SetDepthStencil(false, false) // ポストエフェクトは深度不要
        .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .Build(dxCommon->GetDevice(), fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(), rootSignature_.Get());

    if (!result) {
        throw std::runtime_error("Failed to create PSO in PostEffectBase");

    }
}

void PostEffectBase::Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
{
    auto* commandList = directXCommon_->GetCommandList();

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(
        pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootDescriptorTable(0, inputSrvHandle);

    // オプション定数バッファをバインド
    BindOptionalCBVs(commandList);
    
    commandList->DrawInstanced(3, 1, 0, 0);
}
}
