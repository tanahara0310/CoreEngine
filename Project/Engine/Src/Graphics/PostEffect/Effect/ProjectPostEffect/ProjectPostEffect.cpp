#include "pch.h"
#include "ProjectPostEffect.h"

#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#ifdef CORE_EDITOR
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Inspector/ShaderMaterialInspector.h"
#endif

#include <stdexcept>
#include <utility>

namespace CoreEngine
{
    namespace
    {
        /// @brief テクスチャを指していないときに差す白
        constexpr const char* kWhiteTexture = "white1x1.png";

        /// @brief スレッドグループの一辺（`POST_EFFECT_THREADS`）
        constexpr uint32_t kGroupSize = 8;

        /// @brief 最も近い画素・端で止めるサンプラー
        SamplerConfig PointClamp()
        {
            SamplerConfig config = SamplerConfig::Point();
            config.addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            config.addressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            config.addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            return config;
        }
    }

    ProjectPostEffect::ProjectPostEffect(ProjectPostEffectSettings settings)
        : settings_(std::move(settings))
    {
        SetEnabled(settings_.enabled);
    }

    bool ProjectPostEffect::IsValidStage(const std::string& stage)
    {
        return stage == kStageSceneHDR || stage == kStagePostTonemap || stage == kStageOverlay;
    }

    bool ProjectPostEffect::IsOverlay() const
    {
        return settings_.stage == kStageOverlay;
    }

    PostEffectStage ProjectPostEffect::GetStage() const
    {
        return settings_.stage == kStageSceneHDR ? PostEffectStage::SceneHDR : PostEffectStage::PostTonemap;
    }

    std::wstring ProjectPostEffect::GetComputeShaderPath() const
    {
        return material_ ? Logger::GetInstance().Utf8ToPath(material_->GetShaderPath()).wstring() : std::wstring();
    }

    void ProjectPostEffect::Initialize(GraphicsCore* dxCommon)
    {
        graphicsCore_ = dxCommon;
        material_ = ShaderMaterialLibrary::GetInstance().Get(settings_.material);
        if (!material_) {
            throw std::runtime_error("マテリアル " + settings_.material + " を読めません");
        }
        if (!BuildPipeline()) {
            throw std::runtime_error("シェーダ " + material_->GetShaderPath() + " を組めません");
        }
        white_ = TextureManager::GetInstance().Load(kWhiteTexture);
    }

    bool ProjectPostEffect::BuildPipeline()
    {
        Logger& log = Logger::GetInstance();
        const std::string shaderPath = material_ ? material_->GetShaderPath() : std::string();
        builtShaderPath_ = shaderPath;
        builtShaderRevision_ = ShaderMaterialLibrary::GetInstance().GetShaderRevision();
        if (shaderPath.empty() || !shaderProgramCache_ || !graphicsCore_) {
            log.Logf(LogLevel::Error, LogCategory::Graphics,
                "[PostEffect] {} のマテリアルにシェーダがありません", settings_.name);
            return false;
        }

        ShaderCompiler& compiler = shaderProgramCache_->GetCompiler();
        const Microsoft::WRL::ComPtr<IDxcBlob> blob = compiler.CompileShader(log.Utf8ToPath(shaderPath).wstring(), L"cs_6_0");
        if (!blob) {
            log.Logf(LogLevel::Error, LogCategory::Graphics,
                "[PostEffect] {} のシェーダ {} をコンパイルできません", settings_.name, shaderPath);
            return false;
        }
        std::unique_ptr<ShaderReflectionData> reflection =
            shaderProgramCache_->GetReflectionBuilder().BuildFromComputeShader(blob.Get(), settings_.name);
        if (!reflection) {
            return false;
        }

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
        config.SetDefaultUAVStrategy(BindingStrategy::DescriptorTable);
        config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
        config.ConfigureSampler("gSampler", SamplerConfig::LinearClamp());
        config.ConfigureSampler("gPointSampler", PointClamp());
        config.ConfigureSampler("gWrapSampler", SamplerConfig::Linear());

        auto rootSignature = std::make_unique<RootSignatureManager>();
        const auto result = rootSignature->Build(graphicsCore_->GetDevice(), *reflection, config);
        if (!result.success) {
            log.Logf(LogLevel::Error, LogCategory::Graphics,
                "[PostEffect] {} のルートシグネチャを作れません: {}", settings_.name, result.errorMessage);
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline = ComputePipelineUtil::Create(
            graphicsCore_->GetDevice(), rootSignature->GetRootSignature(), blob.Get(), settings_.name + "_CS");
        if (!pipeline) {
            return false;
        }

        MaterialParamLayout layout;
        ReadMaterialParamLayout(compiler.GetDxcUtils(), blob.Get(), layout, settings_.name);

        if (computePso_) {
            retiredPipelines_.push_back(std::move(computePso_));
        }
        if (rootSignatureManager_) {
            retiredRootSignatures_.push_back(std::move(rootSignatureManager_));
        }
        computePso_ = std::move(pipeline);
        rootSignatureManager_ = std::move(rootSignature);
        reflection_ = std::move(reflection);
        reflectionData_ = reflection_.get();
        layout_ = std::move(layout);

        inputSlot_ = rootSignatureManager_->GetRootSlot("gTexture");
        outputSlot_ = rootSignatureManager_->GetRootSlot("gOutput");
        frameSlot_ = rootSignatureManager_->GetRootSlot("gPostEffect");
        paramsSlot_ = layout_.bufferName.empty() ? RootSlot{} : rootSignatureManager_->GetRootSlot(layout_.bufferName);
        textureSlots_.clear();
        for (const std::string& name : layout_.textures) {
            textureSlots_.push_back(rootSignatureManager_->GetRootSlot(name));
        }
        return true;
    }

    void ProjectPostEffect::PrepareFrame(const PostEffectFrameContext& /*ctx*/)
    {
        if (!material_) {
            return;
        }
        const uint64_t revision = ShaderMaterialLibrary::GetInstance().GetShaderRevision();
        if (material_->GetShaderPath() != builtShaderPath_ || revision != builtShaderRevision_) {
            BuildPipeline();
        }
    }

    void ProjectPostEffect::ResetToDefault()
    {
        SetEnabled(settings_.enabled);
        if (material_) {
            material_->Revert();
        }
    }

    void ProjectPostEffect::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        if (!computePso_ || !rootSignatureManager_ || width == 0 || height == 0) {
            return;
        }

        FrameData frame{};
        frame.screenWidth = width;
        frame.screenHeight = height;
        frame.invScreenSize = { 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height) };
        frame.time = Time::TimeSinceStartup();
        frame.unscaledTime = Time::UnscaledTimeSinceStartup();
        frame.deltaTime = Time::DeltaTime();

        ID3D12GraphicsCommandList* const cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        if (inputSlot_.IsValid()) {
            cmdList->SetComputeRootDescriptorTable(inputSlot_.index, inputSrvHandle);
        }
        if (outputSlot_.IsValid()) {
            cmdList->SetComputeRootDescriptorTable(outputSlot_.index, outputUavHandle);
        }
        if (frameSlot_.IsValid()) {
            cmdList->SetComputeRootConstantBufferView(frameSlot_.index, UploadConstants(frame));
        }
        if (paramsSlot_.IsValid() && layout_.size > 0) {
            PackMaterialParams(layout_, [this](std::string_view name) { return material_->FindNumbers(name); }, packed_);
            cmdList->SetComputeRootConstantBufferView(paramsSlot_.index,
                UploadConstantBytes(packed_.data(), static_cast<uint32_t>(packed_.size())));
        }
        for (size_t index = 0; index < textureSlots_.size() && index < layout_.textures.size(); ++index) {
            if (!textureSlots_[index].IsValid()) {
                continue;
            }
            const TextureManager::LoadedTexture* const loaded = material_->FindLoadedTexture(layout_.textures[index]);
            const D3D12_GPU_DESCRIPTOR_HANDLE handle =
                (loaded && loaded->gpuHandle.ptr != 0) ? loaded->gpuHandle : white_.gpuHandle;
            cmdList->SetComputeRootDescriptorTable(textureSlots_[index].index, handle);
        }

        cmdList->Dispatch((width + kGroupSize - 1) / kGroupSize, (height + kGroupSize - 1) / kGroupSize, 1);
    }

    void ProjectPostEffect::DrawImGui()
    {
#ifdef CORE_EDITOR
        ImGui::PushID(settings_.name.c_str());
        ImGui::TextDisabled("Project.json のポストエフェクト（段 %s・シーンを開くと既定へ戻る）", settings_.stage.c_str());
        ImGui::TextWrapped("マテリアル: %s", settings_.material.c_str());
        if (material_) {
            UI::Separator();
            Editor::ShaderMaterialInspector::Draw(*material_, &layout_);
        }
        ImGui::PopID();
#endif // CORE_EDITOR
    }
}
