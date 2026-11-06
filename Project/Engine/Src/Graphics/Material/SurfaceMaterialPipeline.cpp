#include "pch.h"
#include "SurfaceMaterialPipeline.h"

#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RayTracing/VertexAnimationDeformer.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/Model/ModelBindings.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>

namespace CoreEngine
{
    RootSignatureConfig MakeSurfaceMaterialRootSignatureConfig()
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

    namespace
    {
        /// @brief レイトレの影の形を作るコンピュートシェーダの差し方（頂点の並びと定数はルート・テクスチャは表）
        RootSignatureConfig MakeDeformConfig()
        {
            RootSignatureConfig config;
            config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
            config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
            config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
            config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
            config.ConfigureResource("gVertexAnim", BindingStrategy::RootConstants);
            config.ConfigureResource("gDeform", BindingStrategy::RootConstants);
            config.ConfigureResource("gSourceVertices", BindingStrategy::RootDescriptor);
            config.ConfigureResource("gIndices", BindingStrategy::RootDescriptor);
            config.ConfigureResource("gOutputVertices", BindingStrategy::RootDescriptor);
            config.ConfigureSampler("gSampler", SamplerConfig::Anisotropic());
            config.ConfigureSampler("gPointSampler", SamplerConfig::Point());
            config.ConfigureSampler("gClampSampler", SamplerConfig::LinearClamp());
            return config;
        }
    }

    namespace
    {
        /// @brief シェーダの文字に `#define 名前` の行があるか
        bool ShaderTextDefines(std::string_view text, std::string_view name)
        {
            constexpr std::string_view kDirective = "define";
            const auto isSpace = [](char c) { return c == ' ' || c == '\t'; };
            const auto isNameChar = [](char c) {
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            };
            size_t lineStart = 0;
            while (lineStart < text.size()) {
                size_t lineEnd = text.find('\n', lineStart);
                if (lineEnd == std::string_view::npos) {
                    lineEnd = text.size();
                }
                const std::string_view line = text.substr(lineStart, lineEnd - lineStart);
                lineStart = lineEnd + 1;

                size_t at = 0;
                while (at < line.size() && isSpace(line[at])) {
                    ++at;
                }
                if (at >= line.size() || line[at] != '#') {
                    continue;
                }
                ++at;
                while (at < line.size() && isSpace(line[at])) {
                    ++at;
                }
                if (line.substr(at, kDirective.size()) != kDirective) {
                    continue;
                }
                at += kDirective.size();
                if (at >= line.size() || !isSpace(line[at])) {
                    continue;
                }
                while (at < line.size() && isSpace(line[at])) {
                    ++at;
                }
                if (line.substr(at, name.size()) != name) {
                    continue;
                }
                at += name.size();
                if (at >= line.size() || !isNameChar(line[at])) {
                    return true;
                }
            }
            return false;
        }

        /// @brief シェーダのファイルを読む（綴りはプロジェクトかエンジンの根から辿る。読めなければ空）
        std::string ReadShaderText(const std::string& shaderPath)
        {
            std::ifstream file(ProjectPaths::Resolve(shaderPath), std::ios::binary);
            if (!file) {
                return {};
            }
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        }
    }

    bool SurfaceShaderTextHasVertexFunction(std::string_view text)
    {
        return ShaderTextDefines(text, "SURFACE_VERTEX");
    }

    bool SurfaceShaderHasVertexFunction(const std::string& shaderPath)
    {
        return SurfaceShaderTextHasVertexFunction(ReadShaderText(shaderPath));
    }

    bool SurfaceShaderTextHasOwnVertexShader(std::string_view text)
    {
        return ShaderTextDefines(text, "SURFACE_VERTEX") || ShaderTextDefines(text, "SURFACE_OBJECT_SPACE")
            || ShaderTextDefines(text, "SURFACE_VERTEX_COLOR");
    }

    bool SurfaceShaderHasOwnVertexShader(const std::string& shaderPath)
    {
        return SurfaceShaderTextHasOwnVertexShader(ReadShaderText(shaderPath));
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
        const std::string text = ReadShaderText(shaderPath_);
        hasVertexFunction_ = SurfaceShaderTextHasVertexFunction(text);
        hasOwnVertexShader_ = SurfaceShaderTextHasOwnVertexShader(text);
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

    Microsoft::WRL::ComPtr<IDxcBlob> SurfaceMaterialPipeline::CompileVertexShader(Pass pass, bool skinned, std::string& error)
    {
        Logger& log = Logger::GetInstance();
        if (!hasOwnVertexShader_) {
            const SurfaceMaterialStage& spec = kSurfaceMaterialStages[VariantIndex(pass, skinned)];
            return compiler_->CompileShader(log.Utf8ToPath(spec.vertexShader).wstring(), L"vs_6_0", L"main", &error);
        }
        Microsoft::WRL::ComPtr<IDxcBlob>& blob = ownVertexShaders_[skinned ? 1 : 0];
        if (!blob) {
            blob = compiler_->CompileShader(log.Utf8ToPath(shaderPath_).wstring(), L"vs_6_0",
                skinned ? kSurfaceSkinnedVertexEntry : kSurfaceStaticVertexEntry, &error);
        }
        return blob;
    }

    bool SurfaceMaterialPipeline::BuildVariant(Pass pass, bool skinned)
    {
        Logger& log = Logger::GetInstance();
        const SurfaceMaterialStage& spec = kSurfaceMaterialStages[VariantIndex(pass, skinned)];

        std::string error;
        const Microsoft::WRL::ComPtr<IDxcBlob> pixelBlob = CompilePixelShader(pass, skinned, error);
        const Microsoft::WRL::ComPtr<IDxcBlob> vertexBlob = pixelBlob ? CompileVertexShader(pass, skinned, error) : nullptr;
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
        const auto rootResult = variant->rootSignature->Build(device_, *reflection, MakeSurfaceMaterialRootSignatureConfig());
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
        if (hasOwnVertexShader_) {
            ReadMaterialParamLayout(compiler_->GetDxcUtils(), vertexBlob.Get(), layout_, shaderName, kSurfaceMaterialRegisters);
        }

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

    const SurfaceDeformVariant* SurfaceMaterialPipeline::GetDeformVariant()
    {
        if (!hasVertexFunction_) {
            return nullptr;
        }
        if (!deformAttempted_) {
            deformAttempted_ = true;
            BuildDeformVariant();
        }
        return deform_.get();
    }

    bool SurfaceMaterialPipeline::BuildDeformVariant()
    {
        Logger& log = Logger::GetInstance();
        std::string error;
        const Microsoft::WRL::ComPtr<IDxcBlob> blob = compiler_->CompileShader(
            log.Utf8ToPath(shaderPath_).wstring(), L"cs_6_0", kSurfaceDeformEntry, &error);
        if (!blob) {
            log.Logf(LogLevel::Error, LogCategory::Shader,
                "3D モデルのマテリアルのシェーダ {} の影の形を作るシェーダをコンパイルできません: {}", shaderPath_, error);
            return false;
        }

        const std::string shaderName = "SurfaceDeform " + shaderPath_;
        const std::unique_ptr<ShaderReflectionData> reflection = reflectionBuilder_->BuildFromComputeShader(blob.Get(), shaderName);
        if (!reflection) {
            return false;
        }

        auto variant = std::make_unique<SurfaceDeformVariant>();
        variant->rootSignature = std::make_unique<RootSignatureManager>();
        const auto rootResult = variant->rootSignature->Build(device_, *reflection, MakeDeformConfig());
        if (!rootResult.success) {
            log.Logf(LogLevel::Error, LogCategory::Shader,
                "3D モデルのマテリアルのシェーダ {} の影の形を作るルートシグネチャを作れません: {}", shaderPath_,
                rootResult.errorMessage);
            return false;
        }
        variant->pipelineState = ComputePipelineUtil::Create(
            device_, variant->rootSignature->GetRootSignature(), blob.Get(), shaderName);
        if (!variant->pipelineState) {
            return false;
        }
        try {
            variant->bindings = BindingTable::Resolve(*reflection, VertexAnimationDeformBind::kSurfaceDecls, shaderName,
                /*warnUndeclared=*/false);
        }
        catch (const std::exception& e) {
            log.Logf(LogLevel::Error, LogCategory::Shader,
                "3D モデルのマテリアルのシェーダ {} の影の形を作るシェーダの資源が足りません: {}", shaderPath_, e.what());
            return false;
        }

        ReadMaterialParamLayout(compiler_->GetDxcUtils(), blob.Get(), layout_, shaderName, kSurfaceMaterialRegisters);
        variant->layout = &layout_;
        variant->mainTextureSlot = variant->rootSignature->GetRootSlot("gTexture");
        variant->vertexColorSlot = variant->rootSignature->GetRootSlot("gVertexColors");
        variant->paramsSlot = layout_.bufferName.empty() ? RootSlot{} : variant->rootSignature->GetRootSlot(layout_.bufferName);
        variant->textureSlots.reserve(layout_.textures.size());
        for (const std::string& name : layout_.textures) {
            variant->textureSlots.push_back(variant->rootSignature->GetRootSlot(name));
        }
        deform_ = std::move(variant);
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
        if (entry.pipeline) {
            retired_.push_back(std::move(entry.pipeline));
        }
        if (built) {
            entry.pipeline = std::move(pipeline);
        }
        return entry.pipeline.get();
    }
}
