#include "PostEffectBase.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
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
            L"FullScreen.VS.hlsl", L"vs_6_0");
        pixelShaderBlob_ = shaderCompiler.CompileShader(
            GetPixelShaderPath(), L"ps_6_0");

        // リフレクション
        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());
        reflectionData_ = reflectionBuilder.BuildFromShaders(
            fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(), GetEffectName());

        // シンプルな設定でRootSignatureを構築
        RootSignatureConfig config = RootSignatureConfig::Simple();
        config.ConfigureSampler("gSampler", SamplerConfig::LinearClamp());

        // サブクラスの追加サンプラー設定を適用（例: DeferredLighting のシャドウ比較サンプラー）
        OnConfigureRootSignature(config);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(dxCommon->GetDevice(), *reflectionData_, config);

        if (!buildResult.success) {
            throw std::runtime_error("Failed to create PostEffect Root Signature: " + buildResult.errorMessage);
        }

        // ビルダーパターンでPSOを構築（オフスクリーンRT用: R16G16B16A16_FLOAT）
        bool result = pipelineStateManager_.CreateBuilder()
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false) // ポストエフェクトは深度不要
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .Build(dxCommon->GetDevice(), fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(),
                rootSignatureManager_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create PSO in PostEffectBase");
        }

        // バックバッファ用PSO（_SRGB フォーマット）を別途構築
        bool bbResult = backBufferPipelineStateManager_.CreateBuilder()
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .SetRenderTargetFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            .Build(dxCommon->GetDevice(), fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(),
                rootSignatureManager_->GetRootSignature());

        if (!bbResult) {
            throw std::runtime_error("Failed to create BackBuffer PSO in PostEffectBase");
        }
    }

    int PostEffectBase::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void PostEffectBase::Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
    {
        auto* commandList = directXCommon_->GetCommandList();

        commandList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        commandList->SetPipelineState(
            pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        int textureIdx = GetRootParamIndex("gTexture");
        if (textureIdx >= 0) {
            commandList->SetGraphicsRootDescriptorTable(textureIdx, inputSrvHandle);
        }

        // オプション定数バッファをバインド
        BindOptionalCBVs(commandList);

        commandList->DrawInstanced(3, 1, 0, 0);
    }

    void PostEffectBase::DrawToBackBuffer(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
    {
        auto* commandList = directXCommon_->GetCommandList();

        commandList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        commandList->SetPipelineState(
            backBufferPipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        int textureIdx = GetRootParamIndex("gTexture");
        if (textureIdx >= 0) {
            commandList->SetGraphicsRootDescriptorTable(textureIdx, inputSrvHandle);
        }

        BindOptionalCBVs(commandList);

        commandList->DrawInstanced(3, 1, 0, 0);
    }
}
