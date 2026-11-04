#include "pch.h"
#include "ShaderGraphPreview.h"

#ifdef CORE_EDITOR

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
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

        /// @brief 組み直したあとも持っておく古いパイプラインの数（描画中のフレームの数より多く）
        constexpr size_t kRetiredPipelines = 4;
    }

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
        if (engine_ && (pipeline_ || !retiredPipelines_.empty())) {
            if (GraphicsCore* const dx = engine_->GetService<GraphicsCore>()) {
                dx->WaitForGpuIdle();
            }
        }
        target_ = nullptr;
        pipeline_.reset();
        retiredPipelines_.clear();
        reflectionBuilder_.reset();
        compiler_.reset();
        engine_ = nullptr;
        drawn_ = false;
    }

    void ShaderGraphPreview::Request(const std::filesystem::path& shaderFile, uint64_t revision,
        const ShaderMaterial& values, const std::string& mainTexture)
    {
        requestedShader_ = shaderFile;
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
        if (mainTexture != mainTexturePath_) {
            mainTexturePath_ = mainTexture;
            mainTexture_ = mainTexture.empty() ? TextureManager::LoadedTexture{} : textures.Load(mainTexture);
        }
    }

    bool ShaderGraphPreview::EnsurePipeline()
    {
        if (requestedShader_.empty() || !engine_) {
            return false;
        }
        if (builtShader_ == requestedShader_ && builtRevision_ == requestedRevision_) {
            return pipeline_ != nullptr;
        }
        builtShader_ = requestedShader_;
        builtRevision_ = requestedRevision_;

        GraphicsCore* const dx = engine_->GetService<GraphicsCore>();
        if (!dx) {
            return false;
        }
        auto pipeline = std::make_unique<ShaderMaterialPipeline>();
        const ShaderMaterialPipeline::Target target{ kTargetName, false };
        if (!pipeline->Build(dx->GetDevice(), *compiler_, *reflectionBuilder_,
                Logger::GetInstance().PathToUtf8(requestedShader_), std::string(), target)) {
            error_ = "プレビューのパイプラインを組めません（コンソールを見てください）";
            return pipeline_ != nullptr;
        }
        if (pipeline_) {
            retiredPipelines_.push_back(std::move(pipeline_));
            if (retiredPipelines_.size() > kRetiredPipelines) {
                retiredPipelines_.erase(retiredPipelines_.begin());
            }
        }
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
        GraphicsCore* const dx = engine_->GetService<GraphicsCore>();
        Render* const render = engine_->GetService<Render>();
        RenderTargetManager* const targets = render ? render->GetRenderTargetManager() : nullptr;
        if (!dx || !targets) {
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

        const ShaderMaterialPipeline& pipeline = *pipeline_;
        const MaterialParamLayout& layout = pipeline.GetLayout();
        const D3D12_GPU_DESCRIPTOR_HANDLE white = whiteTexture_.gpuHandle;
        const D3D12_GPU_DESCRIPTOR_HANDLE main = mainTexture_.gpuHandle.ptr != 0 ? mainTexture_.gpuHandle : white;
        if (white.ptr == 0) {
            return;
        }

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

    D3D12_GPU_DESCRIPTOR_HANDLE ShaderGraphPreview::GetTexture() const
    {
        if (!drawn_ || !target_) {
            return {};
        }
        return target_->GetSRVHandle();
    }
}

#endif // CORE_EDITOR
