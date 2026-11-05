#include "pch.h"
#include "ShaderGraphPreview.h"

#ifdef CORE_EDITOR

#include "Camera/CameraStructs.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Graphics/Material/SurfaceMaterialPipeline.h"
#include "Graphics/Model/ModelData.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/PostEffect/Effect/ProjectPostEffect/ProjectPostEffect.h"
#include "Graphics/Primitive/CubeMeshGenerator.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Primitive/SphereMeshGenerator.h"
#include "Graphics/Render/Model/VertexAnimation.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <cmath>
#include <cstring>
#include <numbers>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        constexpr const char* kTargetName = "ShaderGraphPreview";
        constexpr const char* kTarget3DName = "ShaderGraphPreview3D";
        constexpr const char* kWhiteTexture = "white1x1.png";

        /// @brief 3D の背景の色（そのまま画面へ出す値）
        constexpr float kBackground3D[4] = { 0.16f, 0.16f, 0.18f, 1.0f };

        /// @brief 3D のカメラ（原点を -Z から見る）
        constexpr float kCameraDistance = 2.6f;
        constexpr float kFieldOfView = std::numbers::pi_v<float> / 6.0f;

        /// @brief 3D のライト（光の進む向き・色・環境光の強さ）
        const Vector3 kLightDirection = { -0.45f, -0.6f, 0.65f };
        constexpr float kAmbient = 0.25f;

        /// @brief プレビューの定数（`ModelSurface.hlsli` の SurfacePreviewConstants と同じ並び）
        struct SurfacePreviewConstants
        {
            Matrix4x4 world;
            Matrix4x4 worldInverseTranspose;
            Matrix4x4 viewProjection;
            Vector3 lightDirection;
            float ambient;
            Vector3 lightColor;
            float padding;
        };
        static_assert(sizeof(SurfacePreviewConstants) % 16 == 0);

        /// @brief 形ごとの大きさ（どの向きでも枠に収まる）
        float ShapeScale(PreviewShape shape)
        {
            switch (shape) {
            case PreviewShape::Cube: return 0.7f;
            case PreviewShape::Plane: return 0.95f;
            default: return 1.0f;
            }
        }

        /// @brief 形と向きからワールド行列を作る（平面は表をカメラへ向ける）
        Matrix4x4 MakeWorld(const PreviewView& view)
        {
            const float scale = ShapeScale(view.shape);
            Matrix4x4 world = MathCore::Matrix::Scale({ scale, scale, scale });
            if (view.shape == PreviewShape::Plane) {
                world = world * MathCore::Matrix::RotationX(-std::numbers::pi_v<float> * 0.5f);
            }
            return world * MathCore::Matrix::RotationY(view.yaw) * MathCore::Matrix::RotationX(view.pitch);
        }

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

    const char* DisplayName(PreviewShape shape)
    {
        switch (shape) {
        case PreviewShape::Cube: return "立方体";
        case PreviewShape::Plane: return "平面";
        default: return "球";
        }
    }

    ShaderGraphPreview::ComputePipeline::ComputePipeline() = default;
    ShaderGraphPreview::ComputePipeline::~ComputePipeline() = default;
    ShaderGraphPreview::SurfacePipeline::SurfacePipeline() = default;
    ShaderGraphPreview::SurfacePipeline::~SurfacePipeline() = default;
    ShaderGraphPreview::PreviewMesh::PreviewMesh() = default;
    ShaderGraphPreview::PreviewMesh::~PreviewMesh() = default;

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
        if (engine_ && (pipeline_ || compute_ || surface_ || depth_ || !retiredPipelines_.empty() || !retiredComputes_.empty()
                || !retiredSurfaces_.empty())) {
            if (GraphicsCore* const dx = engine_->GetService<GraphicsCore>()) {
                dx->WaitForGpuIdle();
            }
        }
        target_ = nullptr;
        target3D_ = nullptr;
        drawnTarget_ = nullptr;
        pipeline_.reset();
        compute_.reset();
        surface_.reset();
        depth_.reset();
        retiredPipelines_.clear();
        retiredComputes_.clear();
        retiredSurfaces_.clear();
        reflectionBuilder_.reset();
        compiler_.reset();
        engine_ = nullptr;
        drawn_ = false;
    }

    void ShaderGraphPreview::Request(const std::filesystem::path& shaderFile, Target target, uint64_t revision,
        const ShaderMaterial& values, const std::string& mainTexture, const PreviewView& view)
    {
        requestedShader_ = shaderFile;
        requestedTarget_ = target;
        requestedRevision_ = revision;
        values_ = values;
        view_ = view;
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

    std::unique_ptr<ShaderGraphPreview::SurfacePipeline> ShaderGraphPreview::BuildSurfacePipeline(ID3D12Device* device)
    {
        Logger& log = Logger::GetInstance();
        const std::wstring path = requestedShader_.wstring();
        std::string error;
        const Microsoft::WRL::ComPtr<IDxcBlob> pixelBlob =
            compiler_->CompileShader(path, L"ps_6_0", kSurfacePreviewPixelEntry, &error);
        const Microsoft::WRL::ComPtr<IDxcBlob> vertexBlob =
            pixelBlob ? compiler_->CompileShader(path, L"vs_6_0", kSurfacePreviewVertexEntry, &error) : nullptr;
        if (!pixelBlob || !vertexBlob) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "[ShaderGraph] プレビューのシェーダをコンパイルできません: {}", error);
            return nullptr;
        }

        auto pipeline = std::make_unique<SurfacePipeline>();
        pipeline->reflection = reflectionBuilder_->BuildFromShaders(vertexBlob.Get(), pixelBlob.Get(), kTarget3DName);
        if (!pipeline->reflection) {
            return nullptr;
        }
        pipeline->rootSignature = std::make_unique<RootSignatureManager>();
        const auto result = pipeline->rootSignature->Build(device, *pipeline->reflection,
            MakeSurfaceMaterialRootSignatureConfig());
        if (!result.success) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "[ShaderGraph] プレビューのルートシグネチャを作れません: {}",
                result.errorMessage);
            return nullptr;
        }

        pipeline->pipelineStates = std::make_unique<PipelineStateManager>();
        PipelineStateBuilder builder = pipeline->pipelineStates->CreateBuilder();
        builder.SetDebugName(kTarget3DName)
            .SetInputLayoutFromReflection(*pipeline->reflection)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .SetRenderTargetFormat(DXGI_FORMAT_R16G16B16A16_FLOAT)
            .SetDepthStencilFormat(DXGI_FORMAT_D24_UNORM_S8_UINT);
        if (!builder.Build(device, vertexBlob.Get(), pixelBlob.Get(), pipeline->rootSignature->GetRootSignature())) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "[ShaderGraph] プレビューのパイプラインを作れません");
            return nullptr;
        }

        const std::string shaderName = kTarget3DName;
        ReadMaterialParamLayout(compiler_->GetDxcUtils(), pixelBlob.Get(), pipeline->layout, shaderName,
            kSurfaceMaterialRegisters);
        if (SurfaceShaderHasVertexFunction(log.PathToUtf8(requestedShader_))) {
            ReadMaterialParamLayout(compiler_->GetDxcUtils(), vertexBlob.Get(), pipeline->layout, shaderName,
                kSurfaceMaterialRegisters);
        }

        RootSignatureManager& root = *pipeline->rootSignature;
        pipeline->previewSlot = root.GetRootSlot("gSurfacePreview");
        pipeline->materialSlot = root.GetRootSlot("gMaterial");
        pipeline->cameraSlot = root.GetRootSlot("gCamera");
        pipeline->vertexAnimSlot = root.GetRootSlot("gVertexAnim");
        pipeline->paramsSlot = pipeline->layout.bufferName.empty() ? RootSlot{} : root.GetRootSlot(pipeline->layout.bufferName);
        constexpr const char* kModelTextures[] = { "gTexture", "gNormalMap", "gMetallicRoughnessMap", "gEmissiveMap", "gAOMap" };
        for (size_t index = 0; index < pipeline->modelTextureSlots.size(); ++index) {
            pipeline->modelTextureSlots[index] = root.GetRootSlot(kModelTextures[index]);
        }
        for (const std::string& name : pipeline->layout.textures) {
            pipeline->textureSlots.push_back(root.GetRootSlot(name));
        }
        return pipeline;
    }

    const ShaderGraphPreview::PreviewMesh& ShaderGraphPreview::GetMesh(PreviewShape shape)
    {
        std::unique_ptr<PreviewMesh>& mesh = meshes_[static_cast<size_t>(shape)];
        if (!mesh) {
            mesh = std::make_unique<PreviewMesh>();
            ModelData data;
            switch (shape) {
            case PreviewShape::Cube: data = CubeMeshGenerator().Generate(); break;
            case PreviewShape::Plane: data = PlaneMeshGenerator(1.0f, 1.0f, 16, 16).Generate(); break;
            default: data = SphereMeshGenerator(0.5f, 48, 24).Generate(); break;
            }
            mesh->vertices = std::move(data.vertices);
            mesh->indices.reserve(data.indices.size());
            for (const int32_t index : data.indices) {
                mesh->indices.push_back(static_cast<uint32_t>(index));
            }
        }
        return *mesh;
    }

    bool ShaderGraphPreview::EnsurePipeline()
    {
        if (requestedShader_.empty() || !engine_) {
            return false;
        }
        const bool compute = requestedTarget_ == Target::PostEffect;
        const bool surface = Is3D(requestedTarget_);
        if (builtShader_ == requestedShader_ && builtTarget_ == requestedTarget_ && builtRevision_ == requestedRevision_) {
            if (surface) {
                return surface_ != nullptr;
            }
            return compute ? compute_ != nullptr : pipeline_ != nullptr;
        }
        builtShader_ = requestedShader_;
        builtTarget_ = requestedTarget_;
        builtRevision_ = requestedRevision_;

        GraphicsCore* const dx = engine_->GetService<GraphicsCore>();
        if (!dx) {
            return false;
        }
        if (surface) {
            std::unique_ptr<SurfacePipeline> pipeline = BuildSurfacePipeline(dx->GetDevice());
            if (!pipeline) {
                error_ = "プレビューのパイプラインを組めません（コンソールを見てください）";
                return surface_ != nullptr;
            }
            Retire(surface_, retiredSurfaces_);
            surface_ = std::move(pipeline);
            error_.clear();
            return true;
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
        const D3D12_GPU_DESCRIPTOR_HANDLE white = whiteTexture_.gpuHandle;
        if (white.ptr == 0) {
            return;
        }
        if (Is3D(requestedTarget_)) {
            if (!target3D_) {
                RenderTargetDescriptor desc(kTarget3DName, kSize, kSize);
                desc.needsDepthStencil = false;
                desc.autoResize = false;
                std::memcpy(desc.clearColor, kBackground3D, sizeof(kBackground3D));
                target3D_ = targets->CreateRenderTarget(desc);
                if (!target3D_) {
                    return;
                }
            }
            if (!depth_) {
                GraphicsCore* const dx = engine_->GetService<GraphicsCore>();
                if (!dx) {
                    return;
                }
                depth_ = std::make_unique<SceneDepth>();
                depth_->Initialize(dx->GetDevice(), dx->GetDescriptorAllocator(), static_cast<int32_t>(kSize),
                    static_cast<int32_t>(kSize));
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE main = mainTexture_.gpuHandle.ptr != 0 ? mainTexture_.gpuHandle : white;
            RecordSurface(commandList, main, white);
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
        draw.time = Time::UnscaledTimeSinceStartup();
        draw.unscaledTime = Time::UnscaledTimeSinceStartup();
        draw.deltaTime = Time::UnscaledDeltaTime();
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
        drawnTarget_ = target_;
    }

    void ShaderGraphPreview::RecordSurface(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE main,
        D3D12_GPU_DESCRIPTOR_HANDLE white)
    {
        GraphicsCore* const dx = engine_->GetService<GraphicsCore>();
        if (!dx || !surface_ || !depth_ || !target3D_) {
            return;
        }
        const SurfacePipeline& pipeline = *surface_;
        UploadRing& ring = dx->GetUploadRing();

        const PreviewMesh& mesh = GetMesh(view_.shape);
        const uint32_t vertexBytes = static_cast<uint32_t>(mesh.vertices.size() * sizeof(VertexData));
        const uint32_t indexBytes = static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t));
        if (vertexBytes == 0 || indexBytes == 0) {
            return;
        }
        const UploadAllocation vertexMemory = ring.Allocate(vertexBytes, 16);
        const UploadAllocation indexMemory = ring.Allocate(indexBytes, 16);
        if (!vertexMemory.IsValid() || !indexMemory.IsValid()) {
            return;
        }
        std::memcpy(vertexMemory.cpu, mesh.vertices.data(), vertexBytes);
        std::memcpy(indexMemory.cpu, mesh.indices.data(), indexBytes);

        const Vector3 eye = { 0.0f, 0.0f, -kCameraDistance };
        SurfacePreviewConstants preview{};
        preview.world = MakeWorld(view_);
        preview.worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(preview.world));
        preview.viewProjection = MathCore::Matrix::LookAt(eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f })
            * MathCore::Rendering::PerspectiveFov(kFieldOfView, 1.0f, 0.1f, 10.0f);
        preview.lightDirection = Normalize(kLightDirection);
        preview.ambient = kAmbient;
        preview.lightColor = { 1.0f, 1.0f, 1.0f };
        const D3D12_GPU_VIRTUAL_ADDRESS previewAddress = ring.AllocateConstants(preview);

        MaterialConstants material{};
        material.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        material.uvTransform = MathCore::Matrix::Identity();
        material.metallic = 0.0f;
        material.roughness = 0.5f;
        material.occlusionStrength = 1.0f;
        material.useNormalMap = 0;
        material.emissiveFactor = { 0.0f, 0.0f, 0.0f };
        material.enableLighting = 1;
        material.enableDithering = 0;
        material.ditheringScale = 1.0f;
        material.alphaCutoff = 0.0f;
        material.vertexAnimation = 0;
        material.vertexAnimStrength = 1.0f;
        material.vertexAnimSpeed = 1.0f;
        const D3D12_GPU_VIRTUAL_ADDRESS materialAddress = ring.AllocateConstants(material);

        CameraForGPU camera{};
        camera.worldPosition = eye;
        const D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = ring.AllocateConstants(camera);

        VertexAnimationParams animation{};
        animation.time = std::fmod(Time::UnscaledTimeSinceStartup(), VertexAnimationParams::kWrapSeconds);
        animation.prevTime = (std::max)(0.0f, animation.time - Time::UnscaledDeltaTime());

        D3D12_GPU_VIRTUAL_ADDRESS paramsAddress = 0;
        if (pipeline.paramsSlot.IsValid() && pipeline.layout.size > 0) {
            std::vector<uint8_t> packed;
            PackMaterialParams(pipeline.layout, [this](std::string_view name) { return values_.FindNumbers(name); }, packed);
            paramsAddress = ring.AllocateConstants(packed.data(), static_cast<uint32_t>(packed.size()));
            if (paramsAddress == 0) {
                return;
            }
        }
        if (previewAddress == 0 || materialAddress == 0 || cameraAddress == 0) {
            return;
        }

        depth_->BeginDepthWrite(commandList);
        RenderTargetBeginDesc begin;
        begin.clear = true;
        begin.depth = DepthBinding::None;
        const RenderTargetBinding binding = target3D_->Begin(commandList, begin);
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = depth_->GetDSVHandle();
        commandList->OMSetRenderTargets(1, &binding.rtv, false, &dsv);

        commandList->SetGraphicsRootSignature(pipeline.rootSignature->GetRootSignature());
        commandList->SetPipelineState(pipeline.pipelineStates->GetPipelineState(BlendMode::kBlendModeNone));
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        vertexView.BufferLocation = vertexMemory.gpuAddress;
        vertexView.SizeInBytes = vertexBytes;
        vertexView.StrideInBytes = static_cast<UINT>(sizeof(VertexData));
        commandList->IASetVertexBuffers(0, 1, &vertexView);
        D3D12_INDEX_BUFFER_VIEW indexView{};
        indexView.BufferLocation = indexMemory.gpuAddress;
        indexView.SizeInBytes = indexBytes;
        indexView.Format = DXGI_FORMAT_R32_UINT;
        commandList->IASetIndexBuffer(&indexView);

        if (pipeline.previewSlot.IsValid()) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.previewSlot.index, previewAddress);
        }
        if (pipeline.materialSlot.IsValid()) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.materialSlot.index, materialAddress);
        }
        if (pipeline.cameraSlot.IsValid()) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.cameraSlot.index, cameraAddress);
        }
        if (pipeline.vertexAnimSlot.IsValid()) {
            commandList->SetGraphicsRoot32BitConstants(pipeline.vertexAnimSlot.index,
                static_cast<UINT>(sizeof(animation) / sizeof(uint32_t)), &animation, 0);
        }
        for (size_t index = 0; index < pipeline.modelTextureSlots.size(); ++index) {
            if (pipeline.modelTextureSlots[index].IsValid()) {
                commandList->SetGraphicsRootDescriptorTable(pipeline.modelTextureSlots[index].index, index == 0 ? main : white);
            }
        }
        if (paramsAddress != 0) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.paramsSlot.index, paramsAddress);
        }
        for (size_t index = 0; index < pipeline.textureSlots.size() && index < pipeline.layout.textures.size(); ++index) {
            if (!pipeline.textureSlots[index].IsValid()) {
                continue;
            }
            const TextureManager::LoadedTexture* const loaded = values_.FindLoadedTexture(pipeline.layout.textures[index]);
            const D3D12_GPU_DESCRIPTOR_HANDLE handle = loaded && loaded->gpuHandle.ptr != 0 ? loaded->gpuHandle : white;
            commandList->SetGraphicsRootDescriptorTable(pipeline.textureSlots[index].index, handle);
        }

        commandList->DrawIndexedInstanced(static_cast<UINT>(mesh.indices.size()), 1, 0, 0, 0);
        target3D_->End(commandList);
        drawn_ = true;
        drawnTarget_ = target3D_;
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
        frame.time = Time::UnscaledTimeSinceStartup();
        frame.unscaledTime = Time::UnscaledTimeSinceStartup();
        frame.deltaTime = Time::UnscaledDeltaTime();
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
        drawnTarget_ = target_;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ShaderGraphPreview::GetTexture() const
    {
        if (!drawn_ || !drawnTarget_) {
            return {};
        }
        return drawnTarget_->GetSRVHandle();
    }
}

#endif // CORE_EDITOR
