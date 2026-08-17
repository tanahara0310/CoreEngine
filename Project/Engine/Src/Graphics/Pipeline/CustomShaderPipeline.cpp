#include "pch.h"
#include "CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
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
            // 検索キーは UTF-8 のテキストとして渡す（AssetDatabase の登録名も UTF-8）
            const std::string nameStr = Logger::GetInstance().PathToUtf8(std::filesystem::path(fileName));
            const std::filesystem::path resolved = AssetDatabase::GetInstance().FindAssetPath(nameStr);
            if (resolved.empty()) {
                return {};
            }
            return resolved.wstring();
        }

        /// @brief カスタムシェーダー向けの RootSignatureConfig を生成する
        /// CBV は RootDescriptor、テクスチャ SRV / サンプラーは DescriptorTable とする。
        RootSignatureConfig MakeForwardConfig()
        {
            RootSignatureConfig config = RootSignatureConfig::PerformanceOptimized();
            config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
            config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
            config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
            // gInstanceData は SetGraphicsRootShaderResourceView で渡すため RootDescriptor に設定する
            config.ConfigureResource("gInstanceData", BindingStrategy::RootDescriptor);
            config.ConfigureSampler("gSampler", SamplerConfig::Anisotropic());
            config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());
            config.ConfigureSampler("gLinearClamp", SamplerConfig::LinearClamp());
            return config;
        }
    }

    bool CustomShaderPipeline::Build(
        ID3D12Device* device,
        ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder,
        const ICustomShaderProvider& provider)
    {
        assert(device);

        const std::wstring vsPath = ResolveShaderPath(provider.GetVertexShaderPath());
        const std::wstring psPath = ResolveShaderPath(provider.GetPixelShaderPath());

        if (!vsPath.empty() && !psPath.empty()) {
            if (!BuildForwardPipeline(
                device,
                compiler,
                reflectionBuilder,
                vsPath,
                psPath,
                provider.GetCullMode(),
                provider.GetDepthWriteEnable(),
                provider.WritesMotionVector())) {
                return false;
            }
        }

        const std::wstring csPath = ResolveShaderPath(provider.GetComputeShaderPath());
        if (!csPath.empty()) {
            BuildComputePipeline(device, compiler, reflectionBuilder, csPath);
        }

        return hasForwardPSO_ || hasComputePSO_;
    }

    bool CustomShaderPipeline::BuildForwardPipeline(
        ID3D12Device* device,
        ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder,
        const std::wstring& vsPath,
        const std::wstring& psPath,
        D3D12_CULL_MODE cullMode,
        bool depthWriteEnable,
        bool writesMotionVector)
    {
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

        // シェーダーリフレクションから入力レイアウトとリソースバインディングを取得する
        auto reflectionData = reflectionBuilder.BuildFromShaders(vsBlob, psBlob, "CustomShader");

        // リフレクション結果から独自 RootSignature を構築する
        forwardRootSignatureMg_ = std::make_unique<RootSignatureManager>();
        const RootSignatureConfig config = MakeForwardConfig();
        const auto buildResult = forwardRootSignatureMg_->Build(device, *reflectionData, config);
        if (!buildResult.success) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "CustomShaderPipeline: Failed to build forward root signature: {}",
                buildResult.errorMessage);
            forwardRootSignatureMg_.reset();
            return false;
        }

        // 独自 RootSignature で PSO を構築する。
        // ★ビルダーは SetInputLayoutFromReflection() の後にコピーしてはいけない★
        // inputElementDescs_[i].SemanticName が semanticNameStorage_ の std::string を
        // 指しているため、コピーすると新しい方の desc が「コピー元の文字列」を指したままになり、
        // 元が破棄された瞬間にダングリングして PSO 生成が失敗する。
        // そのためビルダーは先に実体として受け取り、以降は参照のまま操作する。
        PipelineStateBuilder builder = forwardPsoMg_.CreateBuilder();
        builder.SetInputLayoutFromReflection(*reflectionData)
            .SetRasterizer(cullMode, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, depthWriteEnable)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

        if (writesMotionVector) {
            // SceneColor（オフスクリーン HDR）＋ GBuffer の MotionVector の 2 枚。
            // フォーマットは GBufferManager::kRenderTargetFormats と一致させること。
            const DXGI_FORMAT formats[2] = {
                DXGI_FORMAT_R16G16B16A16_FLOAT, // SceneColor
                DXGI_FORMAT_R16G16_FLOAT,       // MotionVector
            };
            builder.SetRenderTargetFormats(formats, 2);
        }

        const bool psoResult =
            builder.BuildAllBlendModes(device, vsBlob, psBlob, forwardRootSignatureMg_->GetRootSignature());

        if (!psoResult) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "CustomShaderPipeline: Failed to build forward PSO.");
            forwardRootSignatureMg_.reset();
            return false;
        }

        hasForwardPSO_ = true;
        return true;
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
        // 大気 LUT サンプラー（gLUTSampler）は端をクランプする。既定の WRAP だと LUT の
        // 端で反対側の値が巻き込まれ、地平線・天頂・太陽方位の境界にアーティファクトが出る。
        // この名前のサンプラーを持たないシェーダーでは無視されるため他パイプラインには無影響。
        csConfig.ConfigureSampler("gLUTSampler", SamplerConfig::LinearClamp());
        // スクリーンスペース系 CS（レンズフレア等）の汎用クランプサンプラー。
        // 画面外へはみ出す UV を端で止める（WRAP だと反対側の輝度が巻き込まれる）。
        csConfig.ConfigureSampler("gLinearClamp", SamplerConfig::LinearClamp());

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

    ID3D12RootSignature* CustomShaderPipeline::GetForwardRootSignature() const
    {
        if (!forwardRootSignatureMg_) {
            return nullptr;
        }
        return const_cast<RootSignatureManager*>(forwardRootSignatureMg_.get())->GetRootSignature();
    }

    ID3D12RootSignature* CustomShaderPipeline::GetComputeRootSignature() const
    {
        if (!computeRootSignatureMg_) {
            return nullptr;
        }
        return const_cast<RootSignatureManager*>(computeRootSignatureMg_.get())->GetRootSignature();
    }

    int CustomShaderPipeline::GetRootParamIndex(const std::string& resourceName) const
    {
        if (!forwardRootSignatureMg_) {
            return -1;
        }
        return forwardRootSignatureMg_->GetRootParameterIndex(resourceName);
    }

    int CustomShaderPipeline::GetComputeRootParamIndex(const std::string& resourceName) const
    {
        if (!computeRootSignatureMg_) {
            return -1;
        }
        return computeRootSignatureMg_->GetRootParameterIndex(resourceName);
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
