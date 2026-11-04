#include "pch.h"
#include "SurfaceMaterialPipeline.h"

#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/Model/ModelBindings.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

#include <iterator>

namespace CoreEngine
{
    namespace
    {
        /// @brief モデル描画と同じ差し方（CBV はルート・SRV は表・インスタンスはルート SRV・頂点アニメーションはルート定数）
        RootSignatureConfig MakeConfig()
        {
            RootSignatureConfig config;
            config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
            config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
            config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
            config.ConfigureResource("gInstanceData", BindingStrategy::RootDescriptor);
            config.ConfigureResource("gVertexAnim", BindingStrategy::RootConstants);
            config.ConfigureSampler("gSampler", SamplerConfig::Anisotropic());
            config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());
            config.ConfigureSampler("gPointSampler", SamplerConfig::Point());
            config.ConfigureSampler("gClampSampler", SamplerConfig::LinearClamp());
            return config;
        }
    }

    ID3D12RootSignature* SurfaceMaterialVariant::GetRootSignature() const
    {
        return rootSignature ? rootSignature->GetRootSignature() : nullptr;
    }

    ID3D12PipelineState* SurfaceMaterialVariant::GetPipelineState(BlendMode mode) const
    {
        return pipelineStates ? pipelineStates->GetPipelineState(mode) : nullptr;
    }

    SurfaceMaterialPipeline::SurfaceMaterialPipeline(ID3D12Device* device, ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder, std::string shaderPath)
        : device_(device), compiler_(&compiler), reflectionBuilder_(&reflectionBuilder), shaderPath_(std::move(shaderPath))
    {
    }

    SurfaceMaterialPipeline::~SurfaceMaterialPipeline() = default;

    size_t SurfaceMaterialPipeline::VariantIndex(Pass pass, bool skinned)
    {
        static_assert(std::size(kSurfaceMaterialStages) == kVariantCount);
        return static_cast<size_t>(pass) * 2 + (skinned ? 1 : 0);
    }

    bool SurfaceMaterialPipeline::Build()
    {
        return GetVariant(Pass::GBuffer, false) != nullptr;
    }

    bool SurfaceMaterialPipeline::BuildVariantsUsedBy(const SurfaceMaterialPipeline& previous)
    {
        bool allBuilt = true;
        for (const Pass pass : { Pass::GBuffer, Pass::Forward }) {
            for (const bool skinned : { false, true }) {
                if (previous.attempted_[VariantIndex(pass, skinned)] && !GetVariant(pass, skinned)) {
                    allBuilt = false;
                }
            }
        }
        return allBuilt;
    }

    const SurfaceMaterialVariant* SurfaceMaterialPipeline::GetVariant(Pass pass, bool skinned)
    {
        const size_t index = VariantIndex(pass, skinned);
        if (!attempted_[index]) {
            attempted_[index] = true;
            BuildVariant(pass, skinned);
        }
        return variants_[index].get();
    }

    Microsoft::WRL::ComPtr<IDxcBlob> SurfaceMaterialPipeline::CompilePixelShader(Pass pass, bool skinned, std::string& error)
    {
        if (pass == Pass::Forward && forwardPixelShader_) {
            return forwardPixelShader_;
        }
        const SurfaceMaterialStage& spec = kSurfaceMaterialStages[VariantIndex(pass, skinned)];
        Microsoft::WRL::ComPtr<IDxcBlob> blob = compiler_->CompileShader(
            Logger::GetInstance().Utf8ToPath(shaderPath_).wstring(), L"ps_6_0", spec.entryPoint, &error);
        if (pass == Pass::Forward) {
            forwardPixelShader_ = blob;
        }
        return blob;
    }

    bool SurfaceMaterialPipeline::BuildVariant(Pass pass, bool skinned)
    {
        Logger& log = Logger::GetInstance();
        const SurfaceMaterialStage& spec = kSurfaceMaterialStages[VariantIndex(pass, skinned)];

        std::string error;
        const Microsoft::WRL::ComPtr<IDxcBlob> pixelBlob = CompilePixelShader(pass, skinned, error);
        const Microsoft::WRL::ComPtr<IDxcBlob> vertexBlob = pixelBlob
            ? compiler_->CompileShader(log.Utf8ToPath(spec.vertexShader).wstring(), L"vs_6_0", L"main", &error)
            : nullptr;
        if (!pixelBlob || !vertexBlob) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "3D モデルのマテリアルのシェーダ {} をコンパイルできません: {}",
                shaderPath_, error);
            return false;
        }

        const std::string shaderName = std::string(spec.debugName) + " " + shaderPath_;
        const std::unique_ptr<ShaderReflectionData> reflection =
            reflectionBuilder_->BuildFromShaders(vertexBlob.Get(), pixelBlob.Get(), shaderName);
        if (!reflection) {
            return false;
        }

        auto variant = std::make_unique<SurfaceMaterialVariant>();
        variant->rootSignature = std::make_unique<RootSignatureManager>();
        const auto rootResult = variant->rootSignature->Build(device_, *reflection, MakeConfig());
        if (!rootResult.success) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "3D モデルのマテリアルのシェーダ {} のルートシグネチャを作れません: {}",
                shaderPath_, rootResult.errorMessage);
            return false;
        }

        variant->pipelineStates = std::make_unique<PipelineStateManager>();
        PipelineStateBuilder builder = variant->pipelineStates->CreateBuilder();
        builder.SetDebugName(spec.debugName)
            .SetInputLayoutFromReflection(*reflection)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        bool built = false;
        if (pass == Pass::GBuffer) {
            builder.SetRenderTargetFormats(GBufferManager::kRenderTargetFormats,
                static_cast<UINT>(std::size(GBufferManager::kRenderTargetFormats)));
            built = builder.BuildGBuffer(device_, vertexBlob.Get(), pixelBlob.Get(), variant->GetRootSignature());
        } else {
            built = builder.BuildAllBlendModes(device_, vertexBlob.Get(), pixelBlob.Get(), variant->GetRootSignature());
        }
        if (!built) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "3D モデルのマテリアルのシェーダ {} のパイプラインを作れません",
                shaderPath_);
            return false;
        }

        ReadMaterialParamLayout(compiler_->GetDxcUtils(), pixelBlob.Get(), layout_, shaderName, kSurfaceMaterialRegisters);

        variant->layout = &layout_;
        variant->modelBindings = BindingTable::Resolve(*reflection, ModelBind::kCustom, shaderName, /*warnUndeclared=*/false);
        variant->paramsSlot = layout_.bufferName.empty() ? RootSlot{} : variant->rootSignature->GetRootSlot(layout_.bufferName);
        variant->textureSlots.reserve(layout_.textures.size());
        for (const std::string& name : layout_.textures) {
            variant->textureSlots.push_back(variant->rootSignature->GetRootSlot(name));
        }
        variants_[VariantIndex(pass, skinned)] = std::move(variant);
        return true;
    }

    SurfaceMaterialPipelineCache::SurfaceMaterialPipelineCache(ID3D12Device* device, ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder)
        : device_(device), compiler_(&compiler), reflectionBuilder_(&reflectionBuilder)
    {
    }

    SurfaceMaterialPipelineCache::~SurfaceMaterialPipelineCache() = default;

    SurfaceMaterialPipeline* SurfaceMaterialPipelineCache::Get(const std::string& shaderPath)
    {
        if (shaderPath.empty()) {
            return nullptr;
        }
        Entry& entry = entries_[shaderPath];
        const bool engineShader = shaderPath == kErrorSurfaceShader;
        const uint64_t revision = ShaderMaterialLibrary::GetInstance().GetShaderRevision();
        if (entry.shaderRevision == revision || (engineShader && entry.shaderRevision != 0)) {
            return entry.pipeline.get();
        }
        entry.shaderRevision = revision;

        if (!IsSurfaceShaderPath(shaderPath)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Shader,
                "{} は 3D モデル用のシェーダではありません（名前を「名前.Surface.hlsl」にします）", shaderPath);
            return nullptr;
        }

        auto pipeline = std::make_unique<SurfaceMaterialPipeline>(device_, *compiler_, *reflectionBuilder_, shaderPath);
        const bool built = pipeline->Build() && (!entry.pipeline || pipeline->BuildVariantsUsedBy(*entry.pipeline));
        if (!built) {
            return entry.pipeline.get();
        }
        if (entry.pipeline) {
            retired_.push_back(std::move(entry.pipeline));
        }
        entry.pipeline = std::move(pipeline);
        return entry.pipeline.get();
    }
}
