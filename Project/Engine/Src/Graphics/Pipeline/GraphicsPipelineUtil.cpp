#include "pch.h"
#include "GraphicsPipelineUtil.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"

#include <cassert>
#include <stdexcept>

namespace CoreEngine::GraphicsPipelineUtil
{
    ShaderPair CompileShaders(
        ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder,
        const std::wstring& vsPath,
        const std::wstring& psPath,
        const std::string& reflectionName,
        std::unique_ptr<ShaderReflectionData>& reflection)
    {
        ShaderPair shaders;
        shaders.vs = compiler.CompileShader(vsPath, L"vs_6_0");
        assert(shaders.vs != nullptr);

        shaders.ps = compiler.CompileShader(psPath, L"ps_6_0");
        assert(shaders.ps != nullptr);

        reflection = reflectionBuilder.BuildFromShaders(shaders.vs.Get(), shaders.ps.Get(), reflectionName);
        return shaders;
    }

    void BuildRootSignature(
        RootSignatureManager& rootSignature,
        ID3D12Device* device,
        const ShaderReflectionData& reflection,
        const RootSignatureConfig& config,
        const std::string& label)
    {
        const auto buildResult = rootSignature.Build(device, reflection, config);
        if (!buildResult.success) {
            const std::string name = label.empty() ? std::string() : label + " ";
            throw std::runtime_error("Failed to create " + name + "Root Signature: " + buildResult.errorMessage);
        }
    }

} // namespace CoreEngine::GraphicsPipelineUtil
