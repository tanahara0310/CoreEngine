#include "pch.h"
#include "ShaderGraphPreview.h"

#ifdef CORE_EDITOR

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/PostEffect/Effect/ProjectPostEffect/ProjectPostEffect.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <array>
#include <cstring>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        constexpr const char* kTargetName = "ShaderGraphPreview";
        constexpr const char* kWhiteTexture = "white1x1.png";

        /// @brief ポストエフェクトで画面の代わりに読む既定の画像
        constexpr const char* kCheckerTexture = "uvChecker.png";

        /// @brief コンピュートシェーダのスレッドグループの一辺（`POST_EFFECT_THREADS`）
        constexpr uint32_t kGroupSize = 8;

        /// @brief 組み直したあとも持っておく古いパイプラインの数（描画中のフレームの数より多く）
        constexpr size_t kRetiredPipelines = 4;

        template <class T>
        void Retire(std::unique_ptr<T>& current, std::vector<std::unique_ptr<T>>& retired)
        {
            if (!current) {
                return;
            }
            retired.push_back(std::move(current));
            if (retired.size() > kRetiredPipelines) {
                retired.erase(retired.begin());
            }
        }
    }

    ShaderGraphPreview::ComputePipeline::ComputePipeline() = default;
    ShaderGraphPreview::ComputePipeline::~ComputePipeline() = default;

    ShaderGraphPreview::ShaderGraphPreview() = default;
    ShaderGraphPreview::~ShaderGraphPreview() = default;

    void ShaderGraphPreview::Initialize(EngineSystem* engine)
    {
        engine_ = engine;
        compiler_ = std::make_unique<ShaderCompiler>();
        compiler_->Initialize();
        reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();
        reflectionBuilder_->Initialize(compiler_->GetDxcUtils());
    }

    void ShaderGraphPreview::Finalize()
    {
        // 描いている途中のフレームが使い終わるのを待ってからパイプラインを捨てる
        if (engine_ && (pipeline_ || compute_ || !retiredPipelines_.empty() || !retiredComputes_.empty())) {
            if (GraphicsCore* const dx = engine_->GetService<GraphicsCore>()) {
                dx->WaitForGpuIdle();
            }
        }
        target_ = nullptr;
        pipeline_.reset();
        compute_.reset();
        retiredPipelines_.clear();
        retiredComputes_.clear();
        reflectionBuilder_.reset();
        compiler_.reset();
        engine_ = nullptr;
        drawn_ = false;
    }

    void ShaderGraphPreview::Request(const std::filesystem::path& shaderFile, Target target, uint64_t revision,
        const ShaderMaterial& values, const std::string& mainTexture)
    {
        requestedShader_ = shaderFile;
        requestedTarget_ = target;
        requestedRevision_ = revision;
        values_ = values;
        requested_ = true;

        TextureManager& textures = TextureManager::GetInstance();
        if (!textures.IsInitialized()) {
            return;
        }
        if (whiteTexture_.gpuHandle.ptr == 0) {
            whiteTexture_ = textures.Load(kWhiteTexture);
        }
        if (target == Target::PostEffect && checkerTexture_.gpuHandle.ptr == 0) {
            checkerTexture_ = textures.Load(kCheckerTexture);
        }
        if (mainTexture != mainTexturePath_) {
            mainTexturePath_ = mainTexture;
            mainTexture_ = mainTexture.empty() ? TextureManager::LoadedTexture{} : textures.Load(mainTexture);
        }
    }

    std::unique_ptr<ShaderGraphPreview::ComputePipeline> ShaderGraphPreview::BuildComputePipeline(ID3D12Device* device)
    {
        Logger& log = Logger::GetInstance();
        const Microsoft::WRL::ComPtr<IDxcBlob> blob = compiler_->CompileShader(requestedShader_.wstring(), L"cs_6_0");
        if (!blob) {
            return nullptr;
        }
        auto pipeline = std::make_unique<ComputePipeline>();
        pipeline->reflection = reflectionBuilder_->BuildFromComputeShader(blob.Get(), kTargetName);
        if (!pipeline->reflection) {
            return nullptr;
        }
        pipeline->rootSignature = std::make_unique<RootSignatureManager>();
        const auto result = pipeline->rootSignature->Build(device, *pipeline->reflection,
            ProjectPostEffect::MakeRootSignatureConfig());
        if (!result.success) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "[ShaderGraph] プレビューのルートシグネチャを作れません: {}",
                result.errorMessage);
            return nullptr;
        }
        pipeline->pipelineState = ComputePipelineUtil::Create(device, pipeline->rootSignature->GetRootSignature(),
            blob.Get(), std::string(kTargetName) + "_CS");
        if (!pipeline->pipelineState) {
            return nullptr;
        }
        ReadMaterialParamLayout(compiler_->GetDxcUtils(), blob.Get(), pipeline->layout, kTargetName);
        RootSignatureManager& root = *pipeline->rootSignature;
        pipeline->inputSlot = root.GetRootSlot("gTexture");
        pipeline->outputSlot = root.GetRootSlot("gOutput");
        pipeline->frameSlot = root.GetRootSlot("gPostEffect");
        pipeline->paramsSlot = pipeline->layout.bufferName.empty() ? RootSlot{} : root.GetRootSlot(pipeline->layout.bufferName);
        for (const std::string& name : pipeline->layout.textures) {
            pipeline->textureSlots.push_back(root.GetRootSlot(name));
        }
        return pipeline;
    }

    bool ShaderGraphPreview::EnsurePipeline()
    {
        if (requestedShader_.empty() || !engine_) {
            return false;
        }
        const bool compute = requestedTarget_ == Target::PostEffect;
        if (builtShader_ == requestedShader_ && builtTarget_ == requestedTarget_ && builtRevision_ == requestedRevision_) {
            return compute ? compute_ != nullptr : pipeline_ != nullptr;
        }
        builtShader_ = requestedShader_;
        builtTarget_ = requestedTarget_;
        builtRevision_ = requestedRevision_;

        GraphicsCore* const dx = engine_->GetService<GraphicsCore>();
        if (!dx) {
            return false;
        }
        if (compute) {
            std::unique_ptr<ComputePipeline> pipeline = BuildComputePipeline(dx->GetDevice());
            if (!pipeline) {
                error_ = "プレビューのパイプラインを組めません（コンソールを見てください）";
                return compute_ != nullptr;
            }
            Retire(compute_, retiredComputes_);
            compute_ = std::move(pipeline);
            error_.clear();
            return true;
        }

        auto pipeline = std::make_unique<ShaderMaterialPipeline>();
        const ShaderMaterialPipeline::Target target{ kTargetName, false };
        if (!pipeline->Build(dx->GetDevice(), *compiler_, *reflectionBuilder_,
                Logger::GetInstance().PathToUtf8(requestedShader_), std::string(), target)) {
            error_ = "プレビューのパイプラインを組めません（コンソールを見てください）";
            return pipeline_ != nullptr;
        }
        Retire(pipeline_, retiredPipelines_);
        pipeline_ = std::move(pipeline);
        error_.clear();
        return true;
    }

    void ShaderGraphPreview::Record(ID3D12GraphicsCommandList* commandList)
    {
        if (!requested_ || !commandList || !engine_) {
            return;
        }
        requested_ = false;
        if (!EnsurePipeline()) {
            return;
        }
        Render* const render = engine_->GetService<Render>();
        RenderTargetManager* const targets = render ? render->GetRenderTargetManager() : nullptr;
        if (!targets) {
            return;
        }
        if (!target_) {
            RenderTargetDescriptor desc(kTargetName, kSize, kSize);
            desc.needsDepthStencil = false;
            desc.autoResize = false;
            desc.clearColor[0] = desc.clearColor[1] = desc.clearColor[2] = desc.clearColor[3] = 0.0f;
            target_ = targets->CreateRenderTarget(desc);
            if (!target_) {
                return;
            }
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE white = whiteTexture_.gpuHandle;
        if (white.ptr == 0) {
            return;
        }
        if (requestedTarget_ == Target::PostEffect) {
            D3D12_GPU_DESCRIPTOR_HANDLE input = checkerTexture_.gpuHandle.ptr != 0 ? checkerTexture_.gpuHandle : white;
            if (mainTexture_.gpuHandle.ptr != 0) {
                input = mainTexture_.gpuHandle;
            }
            RecordCompute(commandList, input, white);
        } else {
            const D3D12_GPU_DESCRIPTOR_HANDLE main = mainTexture_.gpuHandle.ptr != 0 ? mainTexture_.gpuHandle : white;
            RecordSprite(commandList, main, white);
        }
    }

    void ShaderGraphPreview::RecordSprite(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE main,
        D3D12_GPU_DESCRIPTOR_HANDLE white)
    {
        GraphicsCore* const dx = engine_->GetService<GraphicsCore>();
        if (!dx || !pipeline_) {
            return;
        }
        const ShaderMaterialPipeline& pipeline = *pipeline_;
        const MaterialParamLayout& layout = pipeline.GetLayout();
        UploadRing& ring = dx->GetUploadRing();

        // 板の 4 頂点（画面いっぱい・左上が UV の 0,0）
        std::array<VertexData, 4> vertices{};
        vertices[0].position = { -1.0f, -1.0f, 0.0f, 1.0f };
        vertices[0].texcoord = { 0.0f, 1.0f };
        vertices[1].position = { -1.0f, 1.0f, 0.0f, 1.0f };
        vertices[1].texcoord = { 0.0f, 0.0f };
        vertices[2].position = { 1.0f, -1.0f, 0.0f, 1.0f };
        vertices[2].texcoord = { 1.0f, 1.0f };
        vertices[3].position = { 1.0f, 1.0f, 0.0f, 1.0f };
        vertices[3].texcoord = { 1.0f, 0.0f };
        for (VertexData& vertex : vertices) {
            vertex.normal = { 0.0f, 0.0f, -1.0f };
        }
        const UploadAllocation vertexMemory = ring.Allocate(static_cast<uint32_t>(sizeof(vertices)), 16);
        if (!vertexMemory.IsValid()) {
            return;
        }
        std::memcpy(vertexMemory.cpu, vertices.data(), sizeof(vertices));

        SpriteRenderer::TransformationMatrix transform{};
        transform.WVP = MathCore::Matrix::Identity();
        transform.world = MathCore::Matrix::Identity();
        const D3D12_GPU_VIRTUAL_ADDRESS transformAddress = ring.AllocateConstants(transform);

        SpriteDrawData draw{};
        draw.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        draw.textureSize = { static_cast<float>(kSize), static_cast<float>(kSize) };
        draw.time = Time::TimeSinceStartup();
        draw.unscaledTime = Time::UnscaledTimeSinceStartup();
        draw.deltaTime = Time::DeltaTime();
        const D3D12_GPU_VIRTUAL_ADDRESS drawAddress = ring.AllocateConstants(draw);

        D3D12_GPU_VIRTUAL_ADDRESS paramsAddress = 0;
        if (pipeline.GetParamsSlot().IsValid() && layout.size > 0) {
            std::vector<uint8_t> packed;
            PackMaterialParams(layout, [this](std::string_view name) { return values_.FindNumbers(name); }, packed);
            paramsAddress = ring.AllocateConstants(packed.data(), static_cast<uint32_t>(packed.size()));
            if (paramsAddress == 0) {
                return;
            }
        }
        if (transformAddress == 0 || drawAddress == 0) {
            return;
        }

        RenderTargetBeginDesc begin;
        begin.clear = true;
        begin.depth = DepthBinding::None;
        target_->Begin(commandList, begin);

        commandList->SetGraphicsRootSignature(pipeline.GetRootSignature());
        commandList->SetPipelineState(pipeline.GetPipelineState(BlendMode::kBlendModeNone));
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        vertexView.BufferLocation = vertexMemory.gpuAddress;
        vertexView.SizeInBytes = static_cast<UINT>(sizeof(vertices));
        vertexView.StrideInBytes = static_cast<UINT>(sizeof(VertexData));
        commandList->IASetVertexBuffers(0, 1, &vertexView);

        if (pipeline.GetTransformSlot().IsValid()) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.GetTransformSlot().index, transformAddress);
        }
        if (pipeline.GetDrawSlot().IsValid()) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.GetDrawSlot().index, drawAddress);
        }
        if (pipeline.GetTextureSlot().IsValid()) {
            commandList->SetGraphicsRootDescriptorTable(pipeline.GetTextureSlot().index, main);
        }
        if (paramsAddress != 0) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.GetParamsSlot().index, paramsAddress);
        }
        const std::vector<RootSlot>& textureSlots = pipeline.GetExtraTextureSlots();
        for (size_t index = 0; index < textureSlots.size() && index < layout.textures.size(); ++index) {
            if (!textureSlots[index].IsValid()) {
                continue;
            }
            const TextureManager::LoadedTexture* const loaded = values_.FindLoadedTexture(layout.textures[index]);
            const D3D12_GPU_DESCRIPTOR_HANDLE handle = loaded && loaded->gpuHandle.ptr != 0 ? loaded->gpuHandle : white;
            commandList->SetGraphicsRootDescriptorTable(textureSlots[index].index, handle);
        }

        commandList->DrawInstanced(4, 1, 0, 0);
        target_->End(commandList);
        drawn_ = true;
    }

    void ShaderGraphPreview::RecordCompute(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE input,
        D3D12_GPU_DESCRIPTOR_HANDLE white)
    {
        GraphicsCore* const dx = engine_->GetService<GraphicsCore>();
        auto* const offscreen = dynamic_cast<OffscreenRenderTarget*>(target_);
        if (!dx || !compute_ || !offscreen) {
            return;
        }
        const ComputePipeline& pipeline = *compute_;
        UploadRing& ring = dx->GetUploadRing();

        ProjectPostEffect::FrameData frame{};
        frame.screenWidth = kSize;
        frame.screenHeight = kSize;
        frame.invScreenSize = { 1.0f / static_cast<float>(kSize), 1.0f / static_cast<float>(kSize) };
        frame.time = Time::TimeSinceStartup();
        frame.unscaledTime = Time::UnscaledTimeSinceStartup();
        frame.deltaTime = Time::DeltaTime();
        const D3D12_GPU_VIRTUAL_ADDRESS frameAddress = ring.AllocateConstants(frame);
        if (frameAddress == 0) {
            return;
        }
        D3D12_GPU_VIRTUAL_ADDRESS paramsAddress = 0;
        if (pipeline.paramsSlot.IsValid() && pipeline.layout.size > 0) {
            std::vector<uint8_t> packed;
            PackMaterialParams(pipeline.layout, [this](std::string_view name) { return values_.FindNumbers(name); }, packed);
            paramsAddress = ring.AllocateConstants(packed.data(), static_cast<uint32_t>(packed.size()));
            if (paramsAddress == 0) {
                return;
            }
        }

        offscreen->BeginCS(commandList);
        commandList->SetComputeRootSignature(pipeline.rootSignature->GetRootSignature());
        commandList->SetPipelineState(pipeline.pipelineState.Get());
        if (pipeline.inputSlot.IsValid()) {
            commandList->SetComputeRootDescriptorTable(pipeline.inputSlot.index, input);
        }
        if (pipeline.outputSlot.IsValid()) {
            commandList->SetComputeRootDescriptorTable(pipeline.outputSlot.index, offscreen->GetUAVHandle());
        }
        if (pipeline.frameSlot.IsValid()) {
            commandList->SetComputeRootConstantBufferView(pipeline.frameSlot.index, frameAddress);
        }
        if (paramsAddress != 0) {
            commandList->SetComputeRootConstantBufferView(pipeline.paramsSlot.index, paramsAddress);
        }
        for (size_t index = 0; index < pipeline.textureSlots.size() && index < pipeline.layout.textures.size(); ++index) {
            if (!pipeline.textureSlots[index].IsValid()) {
                continue;
            }
            const TextureManager::LoadedTexture* const loaded = values_.FindLoadedTexture(pipeline.layout.textures[index]);
            const D3D12_GPU_DESCRIPTOR_HANDLE handle = loaded && loaded->gpuHandle.ptr != 0 ? loaded->gpuHandle : white;
            commandList->SetComputeRootDescriptorTable(pipeline.textureSlots[index].index, handle);
        }
        commandList->Dispatch((kSize + kGroupSize - 1) / kGroupSize, (kSize + kGroupSize - 1) / kGroupSize, 1);
        offscreen->End(commandList);
        drawn_ = true;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ShaderGraphPreview::GetTexture() const
    {
        if (!drawn_ || !target_) {
            return {};
        }
        return target_->GetSRVHandle();
    }
}

#endif // CORE_EDITOR
