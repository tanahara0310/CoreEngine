#include "RenderingTechniqueBase.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include <cassert>

namespace CoreEngine
{
    std::wstring RenderingTechniqueBase::emptyPath_ = L"";

    void RenderingTechniqueBase::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        // Compute Shader を使う場合
        if (IsComputeShader()) {
            const std::wstring& csPath = GetComputeShaderPath();
            if (!csPath.empty()) {
                computeShaderBlob_ = shaderCompiler.CompileShader(csPath, L"cs_6_0");

                // リフレクション（Compute Shader のみ）
                ShaderReflectionBuilder reflectionBuilder;
                reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());
                reflectionData_ = reflectionBuilder.BuildFromComputeShader(
                    computeShaderBlob_.Get(), GetTechniqueName());

                // ルートシグネチャ構築
                RootSignatureConfig config = RootSignatureConfig::Simple();
                OnConfigureRootSignature(config);

                rootSignatureManager_ = std::make_unique<RootSignatureManager>();
                auto buildResult = rootSignatureManager_->Build(
                    dxCommon->GetDevice(), *reflectionData_, config);

                if (!buildResult.success) {
                    throw std::runtime_error("Failed to create RenderingTechnique Root Signature (CS): " 
                        + buildResult.errorMessage);
                }

                // TODO: Compute PSO作成（PipelineStateManager がサポートしたら実装）
                // 現在は Graphics Pipeline のみサポート
                throw std::runtime_error("Compute Pipeline is not supported yet in RenderingTechniqueBase");
            }
        }
        // Graphics Pipeline を使う場合
        else {
            const std::wstring& vsPath = GetVertexShaderPath();
            const std::wstring& psPath = GetPixelShaderPath();

            if (!vsPath.empty() && !psPath.empty()) {
                vertexShaderBlob_ = shaderCompiler.CompileShader(vsPath, L"vs_6_0");
                pixelShaderBlob_ = shaderCompiler.CompileShader(psPath, L"ps_6_0");

                // リフレクション
                ShaderReflectionBuilder reflectionBuilder;
                reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());
                reflectionData_ = reflectionBuilder.BuildFromShaders(
                    vertexShaderBlob_.Get(), pixelShaderBlob_.Get(), GetTechniqueName());

                // ルートシグネチャ構築
                RootSignatureConfig config = RootSignatureConfig::Simple();
                config.ConfigureSampler("gSampler", SamplerConfig::LinearClamp());
                OnConfigureRootSignature(config);

                rootSignatureManager_ = std::make_unique<RootSignatureManager>();
                auto buildResult = rootSignatureManager_->Build(
                    dxCommon->GetDevice(), *reflectionData_, config);

                if (!buildResult.success) {
                    throw std::runtime_error("Failed to create RenderingTechnique Root Signature (PS): " 
                        + buildResult.errorMessage);
                }

                // Graphics PSO作成（HDR用: R16G16B16A16_FLOAT）
                bool result = pipelineStateManager_.CreateBuilder()
                    .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
                    .SetDepthStencil(false, false) // レンダリング技術は深度書き込み不要
                    .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
                    .Build(dxCommon->GetDevice(), vertexShaderBlob_.Get(), pixelShaderBlob_.Get(),
                        rootSignatureManager_->GetRootSignature());

                if (!result) {
                    throw std::runtime_error("Failed to create PSO in RenderingTechniqueBase");
                }
            }
        }
    }

    int RenderingTechniqueBase::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    const std::wstring& RenderingTechniqueBase::GetVertexShaderPath() const
    {
        // デフォルトはフルスクリーンクアッド用頂点シェーダー
        static const std::wstring defaultPath = L"FullScreen.VS.hlsl";
        return defaultPath;
    }

    void RenderingTechniqueBase::DrawFullscreenQuad(ID3D12GraphicsCommandList* commandList)
    {
        assert(commandList);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(3, 1, 0, 0);
    }
}
