#include "pch.h"
#include "RenderingTechniqueBase.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include <cassert>

namespace CoreEngine
{
    std::wstring RenderingTechniqueBase::emptyPath_ = L"";

    void RenderingTechniqueBase::Initialize(GraphicsCore* dxCommon)
    {
        assert(dxCommon);
        graphicsCore_ = dxCommon;
        assert(shaderProgramCache_
            && "SetShaderProgramCache() を Initialize() の前に呼ぶこと（Manager が行う）");

        // Compute Shader を使う場合
        if (IsComputeShader()) {
            const std::wstring& csPath = GetComputeShaderPath();
            if (!csPath.empty()) {
                // コンパイルとリフレクションはキャッシュが担当する。
                // 同じシェーダーを使う技術が増えても DXC は 1 回しか走らない。
                shaderProgram_ = shaderProgramCache_->GetOrCreateCompute(csPath, GetTechniqueName());
                if (!shaderProgram_) {
                    throw std::runtime_error(
                        "Failed to compile compute shader for RenderingTechnique: " + GetTechniqueName());
                }
                reflectionData_ = &shaderProgram_->GetReflection();

                // ルートシグネチャ構築
                // Compute には入力アセンブラが無いので ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT を落とす
                // （既定はグラフィックス向けに立てたままになっている）
                RootSignatureConfig config;
                config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
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
                // FullScreen.VS.hlsl は多くの技術が共有するので、キャッシュが効く
                shaderProgram_ = shaderProgramCache_->GetOrCreateGraphics(
                    vsPath, psPath, GetTechniqueName());
                if (!shaderProgram_) {
                    throw std::runtime_error(
                        "Failed to compile shaders for RenderingTechnique: " + GetTechniqueName());
                }
                reflectionData_ = &shaderProgram_->GetReflection();

                // ルートシグネチャ構築
                RootSignatureConfig config;
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
                    .SetDebugName(GetTechniqueName())
                    .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
                    .SetDepthStencil(false, false) // レンダリング技術は深度書き込み不要
                    .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
                    .Build(dxCommon->GetDevice(), shaderProgram_->GetVS(), shaderProgram_->GetPS(),
                        rootSignatureManager_->GetRootSignature());

                if (!result) {
                    throw std::runtime_error("Failed to create PSO in RenderingTechniqueBase");
                }
            }
        }
    }

    RootSlot RenderingTechniqueBase::GetRootSlot(const std::string& resourceName) const {
        if (!reflectionData_) {
            return RootSlot{};
        }
        return reflectionData_->GetRootSlot(resourceName);
    }

    int RenderingTechniqueBase::GetRootParamIndex(const std::string& resourceName) const {
        const RootSlot slot = GetRootSlot(resourceName);
        return slot.IsValid() ? static_cast<int>(slot.index) : -1;
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
