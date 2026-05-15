#include "CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <stdexcept>
#include <filesystem>

namespace CoreEngine
{
    namespace {
        /// @brief wstring ファイル名を AssetDatabase で解決してフルパス wstring を返す
        /// 解決できない場合は空文字列を返す
        std::wstring ResolveShaderPath(const std::wstring& fileName)
        {
            if (fileName.empty()) {
                return {};
            }
            // filesystem::path 経由で安全に string 変換する
            const std::string nameStr = std::filesystem::path(fileName).string();
            const std::string resolved = AssetDatabase::GetInstance().FindAssetPath(nameStr);
            if (resolved.empty()) {
                return {};
            }
            return std::filesystem::path(resolved).wstring();
        }
    }

    bool CustomShaderPipeline::Build(
        ID3D12Device* device,
        ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder,
        const ICustomShaderProvider& provider,
        ID3D12RootSignature* existingRootSignature)
    {
        assert(device);
        assert(existingRootSignature);

        const std::wstring vsPath = ResolveShaderPath(provider.GetVertexShaderPath());
        const std::wstring psPath = ResolveShaderPath(provider.GetPixelShaderPath());

        // VS・PS の少なくとも一方が指定されている場合のみフォワード PSO を構築する
        if (!vsPath.empty() && !psPath.empty()) {
            IDxcBlob* vsBlob = compiler.CompileShader(vsPath, L"vs_6_0");
            if (!vsBlob) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                    "CustomShaderPipeline: Failed to compile vertex shader: {}",
                    std::filesystem::path(vsPath).string());
                return false;
            }

            IDxcBlob* psBlob = compiler.CompileShader(psPath, L"ps_6_0");
            if (!psBlob) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                    "CustomShaderPipeline: Failed to compile pixel shader: {}",
                    std::filesystem::path(psPath).string());
                return false;
            }

            // リフレクションから入力レイアウトを取得して PSO を構築する
            // RootSignature は既定のフォワードパスのものを再利用する
            auto reflectionData = reflectionBuilder.BuildFromShaders(vsBlob, psBlob, "CustomShader");

            const bool result = forwardPsoMg_.CreateBuilder()
                .SetInputLayoutFromReflection(*reflectionData)
                .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
                .SetDepthStencil(true, true)
                .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
                .BuildAllBlendModes(device, vsBlob, psBlob, existingRootSignature);

            if (!result) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                    "CustomShaderPipeline: Failed to build forward PSO.");
                return false;
            }

            hasForwardPSO_ = true;
        }

        // CS が指定されている場合はコンピュートパイプラインを構築する
        const std::wstring csPath = ResolveShaderPath(provider.GetComputeShaderPath());
        if (!csPath.empty()) {
            BuildComputePipeline(device, compiler, reflectionBuilder, csPath);
        }

        return hasForwardPSO_ || hasComputePSO_;
    }

    void CustomShaderPipeline::BuildComputePipeline(
        ID3D12Device* device,
        ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder,
        const std::wstring& csPath)
    {
        IDxcBlob* csBlob = compiler.CompileShader(csPath, L"cs_6_0");
        if (!csBlob) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "CustomShaderPipeline: Failed to compile compute shader: {}",
                std::filesystem::path(csPath).string());
            return;
        }

        auto csReflection = reflectionBuilder.BuildFromComputeShader(csBlob, "CustomComputeShader");

        // コンピュート用 RootSignature を構築する
        computeRootSignatureMg_ = std::make_unique<RootSignatureManager>();
        RootSignatureConfig csConfig = RootSignatureConfig::PerformanceOptimized();
        csConfig.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        csConfig.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);

        auto buildResult = computeRootSignatureMg_->Build(device, *csReflection, csConfig);
        if (!buildResult.success) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "CustomShaderPipeline: Failed to build compute root signature: {}",
                buildResult.errorMessage);
            computeRootSignatureMg_.reset();
            return;
        }

        // コンピュートパイプラインステートを構築する
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = computeRootSignatureMg_->GetRootSignature();
        desc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };

        const HRESULT hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&computePSO_));
        if (FAILED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "CustomShaderPipeline: CreateComputePipelineState failed. HRESULT={:#x}",
                static_cast<uint32_t>(hr));
            computeRootSignatureMg_.reset();
            return;
        }

        hasComputePSO_ = true;
    }

    ID3D12PipelineState* CustomShaderPipeline::GetForwardPSO(BlendMode mode) const
    {
        if (!hasForwardPSO_) {
            return nullptr;
        }
        return const_cast<PipelineStateManager&>(forwardPsoMg_).GetPipelineState(mode);
    }

    ID3D12PipelineState* CustomShaderPipeline::GetComputePSO() const
    {
        return computePSO_.Get();
    }

    bool CustomShaderPipeline::HasForwardPSO() const
    {
        return hasForwardPSO_;
    }

    bool CustomShaderPipeline::HasComputePSO() const
    {
        return hasComputePSO_;
    }

} // namespace CoreEngine
