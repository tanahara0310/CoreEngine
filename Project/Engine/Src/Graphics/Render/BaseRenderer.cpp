#include "pch.h"
#include "BaseRenderer.h"
#include "Graphics/Shader/ShaderReflectionData.h"

namespace CoreEngine
{
    GraphicsPipelineUtil::ShaderPair BaseRenderer::BuildShadersAndRootSignature(
        ID3D12Device* device, const std::wstring& vsPath, const std::wstring& psPath,
        const std::string& reflectionName, const RootSignatureConfig& config, const std::string& errorLabel)
    {
        shaderCompiler_->Initialize();
        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());

        GraphicsPipelineUtil::ShaderPair shaders = GraphicsPipelineUtil::CompileShaders(
            *shaderCompiler_, *reflectionBuilder_, vsPath, psPath, reflectionName, reflectionData_);
        GraphicsPipelineUtil::BuildRootSignature(*rootSignatureMg_, device, *reflectionData_, config, errorLabel);
        return shaders;
    }

    void BaseRenderer::BeginBlendModePass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode)
    {
        if (blendMode != currentBlendMode_) {
            currentBlendMode_ = blendMode;
            pipelineState_ = psoMg_->GetPipelineState(blendMode);
        }
        BindPassPipeline(cmdList, rootSignatureMg_->GetRootSignature(), pipelineState_);
    }
}
