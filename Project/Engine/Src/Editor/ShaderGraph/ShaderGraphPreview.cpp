#include "pch.h"
#include "ShaderGraphPreview.h"

#ifdef CORE_EDITOR

#include "Camera/CameraStructs.h"
#include "Editor/ShaderGraph/ShaderGraphAsset.h"
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
        constexpr const char* kWhiteTexture = "white1x1.png";

        /// @brief 3D の背景の色（そのまま画面へ出す値）
        constexpr float kBackground3D[4] = { 0.16f, 0.16f, 0.18f, 1.0f };

        /// @brief 3D のカメラ（原点を -Z から見る）
        constexpr float kCameraDistance = 2.6f;
        constexpr float kFieldOfView = std::numbers::pi_v<float> / 6.0f;
        constexpr float kNearZ = 0.1f;
        constexpr float kFarZ = 10.0f;

        /// @brief 画面の深度を距離へ直す値（プレビューは透視投影・描き先は正方形）
        SceneDepth::ShaderParams MakeSceneDepthParams(float nearZ, float farZ, uint32_t size)
        {
            SceneDepth::ShaderParams params{};
            params.nearZ = nearZ;
            params.farZ = farZ;
            params.screenWidth = static_cast<float>(size);
            params.screenHeight = static_cast<float>(size);
            params.orthographic = 0.0f;
            return params;
        }

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

        /// @brief 原点の深さで、枠の高さちょうどになる大きさ
        float FillScale()
        {
            return 2.0f * kCameraDistance * std::tan(kFieldOfView * 0.5f);
        }

        /// @brief 形と向きからワールド行列を作る（平面は表をカメラへ向ける）
        /// @param rotate false なら向きを変えず、平面は枠いっぱいに広げる
        Matrix4x4 MakeWorld(PreviewShape shape, const PreviewView& view, bool rotate)
        {
            const float scale = (!rotate && shape == PreviewShape::Plane) ? FillScale() : ShapeScale(shape);
            Matrix4x4 world = MathCore::Matrix::Scale({ scale, scale, scale });
            if (shape == PreviewShape::Plane) {
                world = world * MathCore::Matrix::RotationX(-std::numbers::pi_v<float> * 0.5f);
            }
            if (rotate) {
                world = world * MathCore::Matrix::RotationY(view.yaw) * MathCore::Matrix::RotationX(view.pitch);
            }
            return world;
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

        /// @brief 描く範囲をタイル 1 枚にする
        void SetTileViewport(ID3D12GraphicsCommandList* commandList, float x, float y, float size)
        {
            const D3D12_VIEWPORT viewport{ x, y, size, size, 0.0f, 1.0f };
            const D3D12_RECT scissor{ static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x + size),
                static_cast<LONG>(y + size) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);
        }

        /// @brief スプライトの標準の頂点シェーダのパス
        std::wstring StandardVertexShader()
        {
            return Logger::GetInstance().Utf8ToPath(kStandardMaterialVertexShader).wstring();
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

    // ---------------------------------------------------------------- 裏のコンパイル

    struct ShaderGraphCompileWorker::Job
    {
        uint64_t key = 0;
        Target target = Target::Sprite;
        std::string hlsl;
        std::filesystem::path file;
        PreparedShaderCompile vertex;
        PreparedShaderCompile pixel;
        PreparedShaderCompile compute;
    };

    ShaderGraphCompileWorker::ShaderGraphCompileWorker() = default;

    ShaderGraphCompileWorker::~ShaderGraphCompileWorker()
    {
        Stop();
    }

    void ShaderGraphCompileWorker::Start()
    {
        if (thread_.joinable()) {
            return;
        }
        thread_ = std::jthread([this](std::stop_token stop) { Run(stop); });
    }

    void ShaderGraphCompileWorker::Stop()
    {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        thread_ = std::jthread();
        std::lock_guard lock(mutex_);
        pending_.reset();
        finished_.reset();
    }

    void ShaderGraphCompileWorker::Submit(uint64_t key, std::string hlsl, Target target, const std::filesystem::path& file,
        const ShaderCompiler& prepareCompiler)
    {
        auto job = std::make_unique<Job>();
        job->key = key;
        job->target = target;
        job->hlsl = std::move(hlsl);
        job->file = file;
        const std::wstring path = file.wstring();
        if (target == Target::PostEffect) {
            job->compute = prepareCompiler.Prepare(path, L"cs_6_0", L"main");
        } else if (Is3D(target)) {
            job->vertex = prepareCompiler.Prepare(path, L"vs_6_0", kSurfacePreviewVertexEntry);
            job->pixel = prepareCompiler.Prepare(path, L"ps_6_0", kSurfacePreviewPixelEntry);
        } else {
            job->vertex = prepareCompiler.Prepare(StandardVertexShader(), L"vs_6_0", L"main");
            job->pixel = prepareCompiler.Prepare(path, L"ps_6_0", L"main");
        }
        {
            std::lock_guard lock(mutex_);
            pending_ = std::move(job);
        }
        wake_.notify_one();
    }

    bool ShaderGraphCompileWorker::TakeFinished(CompiledShaders& result)
    {
        std::lock_guard lock(mutex_);
        if (!finished_) {
            return false;
        }
        result = std::move(*finished_);
        finished_.reset();
        return true;
    }

    void ShaderGraphCompileWorker::Run(std::stop_token stop)
    {
        ShaderCompiler compiler;
        compiler.Initialize();
        while (!stop.stop_requested()) {
            std::unique_ptr<Job> job;
            {
                std::unique_lock lock(mutex_);
                if (!wake_.wait(lock, stop, [this] { return pending_ != nullptr; })) {
                    return;
                }
                job = std::move(pending_);
            }

            CompiledShaders result;
            result.key = job->key;
            result.target = job->target;
            if (!WriteFileReplacing(job->file, job->hlsl)) {
                result.error = "プレビューのシェーダを書けません";
            }
            const auto compile = [&compiler, &result](const PreparedShaderCompile& prepared,
                Microsoft::WRL::ComPtr<IDxcBlob>& blob) {
                if (!prepared.IsValid() || !result.error.empty()) {
                    return;
                }
                std::string error;
                blob = compiler.CompilePrepared(prepared, &error);
                if (!blob) {
                    result.error = error.empty() ? std::string("コンパイルできません") : error;
                }
            };
            compile(job->vertex, result.vertex);
            compile(job->pixel, result.pixel);
            compile(job->compute, result.compute);

            std::lock_guard lock(mutex_);
            finished_ = std::move(result);
        }
    }

    // ---------------------------------------------------------------- プレビュー

    ShaderGraphPreview::ComputePipeline::ComputePipeline() = default;
    ShaderGraphPreview::ComputePipeline::~ComputePipeline() = default;
    ShaderGraphPreview::SurfacePipeline::SurfacePipeline() = default;
    ShaderGraphPreview::SurfacePipeline::~SurfacePipeline() = default;
    ShaderGraphPreview::PreviewMesh::PreviewMesh() = default;
    ShaderGraphPreview::PreviewMesh::~PreviewMesh() = default;

    ShaderGraphPreview::ShaderGraphPreview(std::string name, uint32_t size)
        : name_(std::move(name)), size_(size)
    {
    }

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
        compiled_.reset();
        tiles_.clear();
        requestedKey_ = 0;
        builtKey_ = 0;
        failedKey_ = 0;
        drawnKey_ = 0;
        engine_ = nullptr;
        drawn_ = false;
    }

    void ShaderGraphPreview::LoadTextures(Target target, const std::string& mainTexture)
    {
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

    void ShaderGraphPreview::Request(const std::filesystem::path& shaderFile, Target target, uint64_t revision,
        const ShaderMaterial& values, const std::string& mainTexture, const PreviewView& view)
    {
        requestedShader_ = shaderFile;
        requestedTarget_ = target;
        requestedRevision_ = revision;
        requestedKey_ = 0;
        values_ = values;
        view_ = view;
        requested_ = true;
        tiles_.assign(1, DrawTile{ 0.0f, 0.0f, static_cast<float>(size_), view.shape, true });
        LoadTextures(target, mainTexture);
    }

    void ShaderGraphPreview::SetCompiled(CompiledShaders shaders)
    {
        compiled_ = std::move(shaders);
    }

    void ShaderGraphPreview::RequestTiles(uint64_t key, Target target, const std::vector<NodePreviewTile>& tiles,
        const ShaderMaterial& values, const std::string& mainTexture, const PreviewView& view)
    {
        requestedKey_ = key;
        requestedTarget_ = target;
        values_ = values;
        view_ = view;
        requested_ = true;
        tiles_.clear();
        for (const NodePreviewTile& tile : tiles) {
            DrawTile draw;
            draw.x = static_cast<float>((tile.tile % kNodePreviewColumns) * kNodePreviewTile);
            draw.y = static_cast<float>((tile.tile / kNodePreviewColumns) * kNodePreviewTile);
            draw.size = static_cast<float>(kNodePreviewTile);
            draw.shape = tile.sphere ? PreviewShape::Sphere : PreviewShape::Plane;
            draw.rotate = tile.sphere;
            tiles_.push_back(draw);
        }
        LoadTextures(target, mainTexture);
    }

    CompiledShaders ShaderGraphPreview::CompileRequested()
    {
        CompiledShaders shaders;
        shaders.target = requestedTarget_;
        const std::wstring path = requestedShader_.wstring();
        std::string error;
        if (requestedTarget_ == Target::PostEffect) {
            shaders.compute = compiler_->CompileShader(path, L"cs_6_0", L"main", &error);
        } else if (Is3D(requestedTarget_)) {
            shaders.pixel = compiler_->CompileShader(path, L"ps_6_0", kSurfacePreviewPixelEntry, &error);
            if (shaders.pixel) {
                shaders.vertex = compiler_->CompileShader(path, L"vs_6_0", kSurfacePreviewVertexEntry, &error);
            }
        } else {
            shaders.vertex = compiler_->CompileShader(StandardVertexShader(), L"vs_6_0", L"main", &error);
            if (shaders.vertex) {
                shaders.pixel = compiler_->CompileShader(path, L"ps_6_0", L"main", &error);
            }
        }
        const bool compiled = requestedTarget_ == Target::PostEffect ? shaders.compute != nullptr
                                                                     : (shaders.vertex && shaders.pixel);
        if (!compiled) {
            shaders.error = error.empty() ? std::string("コンパイルできません") : error;
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader,
                "[ShaderGraph] プレビューのシェーダをコンパイルできません: {}", shaders.error);
        }
        return shaders;
    }

    bool ShaderGraphPreview::BuildPipeline(const CompiledShaders& shaders, bool hasVertexFunction)
    {
        GraphicsCore* const dx = engine_ ? engine_->GetService<GraphicsCore>() : nullptr;
        if (!dx || !shaders.error.empty()) {
            return false;
        }
        ID3D12Device* const device = dx->GetDevice();
        if (shaders.target == Target::PostEffect) {
            std::unique_ptr<ComputePipeline> pipeline = shaders.compute
                ? BuildComputePipeline(device, shaders.compute.Get()) : nullptr;
            if (!pipeline) {
                return false;
            }
            Retire(compute_, retiredComputes_);
            compute_ = std::move(pipeline);
            return true;
        }
        if (!shaders.vertex || !shaders.pixel) {
            return false;
        }
        if (Is3D(shaders.target)) {
            std::unique_ptr<SurfacePipeline> pipeline =
                BuildSurfacePipeline(device, shaders.vertex.Get(), shaders.pixel.Get(), hasVertexFunction);
            if (!pipeline) {
                return false;
            }
            Retire(surface_, retiredSurfaces_);
            surface_ = std::move(pipeline);
            return true;
        }
        auto pipeline = std::make_unique<ShaderMaterialPipeline>();
        const ShaderMaterialPipeline::Target target{ name_.c_str(), false };
        if (!pipeline->BuildFromBlobs(device, compiler_->GetDxcUtils(), *reflectionBuilder_, shaders.vertex.Get(),
                shaders.pixel.Get(), name_, target)) {
            return false;
        }
        Retire(pipeline_, retiredPipelines_);
        pipeline_ = std::move(pipeline);
        return true;
    }

    std::unique_ptr<ShaderGraphPreview::ComputePipeline> ShaderGraphPreview::BuildComputePipeline(ID3D12Device* device,
        IDxcBlob* blob)
    {
        Logger& log = Logger::GetInstance();
        auto pipeline = std::make_unique<ComputePipeline>();
        pipeline->reflection = reflectionBuilder_->BuildFromComputeShader(blob, name_);
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
            blob, name_ + "_CS");
        if (!pipeline->pipelineState) {
            return nullptr;
        }
        ReadMaterialParamLayout(compiler_->GetDxcUtils(), blob, pipeline->layout, name_);
        RootSignatureManager& root = *pipeline->rootSignature;
        pipeline->inputSlot = root.GetRootSlot("gTexture");
        pipeline->outputSlot = root.GetRootSlot("gOutput");
        pipeline->frameSlot = root.GetRootSlot("gPostEffect");
        pipeline->paramsSlot = pipeline->layout.bufferName.empty() ? RootSlot{} : root.GetRootSlot(pipeline->layout.bufferName);
        for (const std::string& name : pipeline->layout.textures) {
            pipeline->textureSlots.push_back(root.GetRootSlot(name));
        }
        pipeline->sceneDepthSlot = root.GetRootSlot("gSceneDepthTexture");
        pipeline->sceneDepthParamsSlot = root.GetRootSlot("gSceneDepthParams");
        return pipeline;
    }

    std::unique_ptr<ShaderGraphPreview::SurfacePipeline> ShaderGraphPreview::BuildSurfacePipeline(ID3D12Device* device,
        IDxcBlob* vertexBlob, IDxcBlob* pixelBlob, bool hasVertexFunction)
    {
        Logger& log = Logger::GetInstance();
        const std::string shaderName = name_ + "3D";
        auto pipeline = std::make_unique<SurfacePipeline>();
        pipeline->reflection = reflectionBuilder_->BuildFromShaders(vertexBlob, pixelBlob, shaderName);
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
        builder.SetDebugName(shaderName)
            .SetInputLayoutFromReflection(*pipeline->reflection)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .SetRenderTargetFormat(DXGI_FORMAT_R16G16B16A16_FLOAT)
            .SetDepthStencilFormat(DXGI_FORMAT_D24_UNORM_S8_UINT);
        if (!builder.Build(device, vertexBlob, pixelBlob, pipeline->rootSignature->GetRootSignature())) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "[ShaderGraph] プレビューのパイプラインを作れません");
            return nullptr;
        }

        ReadMaterialParamLayout(compiler_->GetDxcUtils(), pixelBlob, pipeline->layout, shaderName, kSurfaceMaterialRegisters);
        if (hasVertexFunction) {
            ReadMaterialParamLayout(compiler_->GetDxcUtils(), vertexBlob, pipeline->layout, shaderName,
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
        pipeline->sceneDepthSlot = root.GetRootSlot("gSceneDepthTexture");
        pipeline->sceneDepthParamsSlot = root.GetRootSlot("gSceneDepthParams");
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
        if (!engine_) {
            return false;
        }
        const auto hasPipeline = [this](Target target) {
            if (target == Target::PostEffect) {
                return compute_ != nullptr;
            }
            return Is3D(target) ? surface_ != nullptr : pipeline_ != nullptr;
        };

        // 裏でコンパイルしたシェーダ（ノードのプレビュー）は印で比べる
        if (requestedKey_ != 0) {
            return builtKey_ == requestedKey_ && builtTarget_ == requestedTarget_ && hasPipeline(requestedTarget_);
        }

        if (requestedShader_.empty()) {
            return false;
        }
        if (builtShader_ == requestedShader_ && builtTarget_ == requestedTarget_ && builtRevision_ == requestedRevision_) {
            return hasPipeline(requestedTarget_);
        }
        builtShader_ = requestedShader_;
        builtTarget_ = requestedTarget_;
        builtRevision_ = requestedRevision_;

        const CompiledShaders shaders = CompileRequested();
        const bool hasVertexFunction = Is3D(requestedTarget_)
            && SurfaceShaderHasVertexFunction(Logger::GetInstance().PathToUtf8(requestedShader_));
        if (!BuildPipeline(shaders, hasVertexFunction)) {
            error_ = "プレビューのパイプラインを組めません（コンソールを見てください）";
            return hasPipeline(requestedTarget_);
        }
        error_.clear();
        return true;
    }

    void ShaderGraphPreview::Record(ID3D12GraphicsCommandList* commandList)
    {
        if (!commandList || !engine_) {
            return;
        }
        // 裏から届いたシェーダは、描くかどうかに関わらず組んでおく
        if (compiled_) {
            const CompiledShaders shaders = std::move(*compiled_);
            compiled_.reset();
            if (BuildPipeline(shaders, false)) {
                builtKey_ = shaders.key;
                builtTarget_ = shaders.target;
            } else {
                failedKey_ = shaders.key;
            }
        }
        if (!requested_) {
            return;
        }
        requested_ = false;
        if (tiles_.empty() || !EnsurePipeline()) {
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
                RenderTargetDescriptor desc(name_ + "3D", size_, size_);
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
                depth_->Initialize(dx->GetDevice(), dx->GetDescriptorAllocator(), static_cast<int32_t>(size_),
                    static_cast<int32_t>(size_));
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE main = mainTexture_.gpuHandle.ptr != 0 ? mainTexture_.gpuHandle : white;
            RecordSurface(commandList, main, white);
            return;
        }
        if (!target_) {
            RenderTargetDescriptor desc(name_, size_, size_);
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

        // 板の 4 頂点（描く範囲いっぱい・左上が UV の 0,0）
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

        const float tileSize = tiles_.front().size;
        SpriteDrawData draw{};
        draw.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        draw.textureSize = { tileSize, tileSize };
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
        const D3D12_GPU_VIRTUAL_ADDRESS depthParamsAddress = pipeline.GetSceneDepthParamsSlot().IsValid()
            ? ring.AllocateConstants(MakeSceneDepthParams(0.1f, 10.0f, size_)) : 0;

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
        // プレビューに画面の深度は無いので、いちばん遠い（白 = 1）を読ませる
        if (pipeline.GetSceneDepthSlot().IsValid()) {
            commandList->SetGraphicsRootDescriptorTable(pipeline.GetSceneDepthSlot().index, white);
        }
        if (depthParamsAddress != 0) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.GetSceneDepthParamsSlot().index, depthParamsAddress);
        }

        for (const DrawTile& tile : tiles_) {
            SetTileViewport(commandList, tile.x, tile.y, tile.size);
            commandList->DrawInstanced(4, 1, 0, 0);
        }
        target_->End(commandList);
        drawn_ = true;
        drawnTarget_ = target_;
        drawnKey_ = requestedKey_;
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

        // 形ごとの頂点と番号（タイルで使う形だけ）
        struct MeshMemory
        {
            D3D12_VERTEX_BUFFER_VIEW vertexView{};
            D3D12_INDEX_BUFFER_VIEW indexView{};
            UINT indexCount = 0;
        };
        std::array<std::optional<MeshMemory>, 3> meshMemory;
        const auto uploadMesh = [&](PreviewShape shape) -> const MeshMemory* {
            std::optional<MeshMemory>& slot = meshMemory[static_cast<size_t>(shape)];
            if (slot) {
                return &*slot;
            }
            const PreviewMesh& mesh = GetMesh(shape);
            const uint32_t vertexBytes = static_cast<uint32_t>(mesh.vertices.size() * sizeof(VertexData));
            const uint32_t indexBytes = static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t));
            if (vertexBytes == 0 || indexBytes == 0) {
                return nullptr;
            }
            const UploadAllocation vertexMemory = ring.Allocate(vertexBytes, 16);
            const UploadAllocation indexMemory = ring.Allocate(indexBytes, 16);
            if (!vertexMemory.IsValid() || !indexMemory.IsValid()) {
                return nullptr;
            }
            std::memcpy(vertexMemory.cpu, mesh.vertices.data(), vertexBytes);
            std::memcpy(indexMemory.cpu, mesh.indices.data(), indexBytes);
            MeshMemory memory;
            memory.vertexView.BufferLocation = vertexMemory.gpuAddress;
            memory.vertexView.SizeInBytes = vertexBytes;
            memory.vertexView.StrideInBytes = static_cast<UINT>(sizeof(VertexData));
            memory.indexView.BufferLocation = indexMemory.gpuAddress;
            memory.indexView.SizeInBytes = indexBytes;
            memory.indexView.Format = DXGI_FORMAT_R32_UINT;
            memory.indexCount = static_cast<UINT>(mesh.indices.size());
            slot = memory;
            return &*slot;
        };

        const Vector3 eye = { 0.0f, 0.0f, -kCameraDistance };
        const Matrix4x4 viewProjection = MathCore::Matrix::LookAt(eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f })
            * MathCore::Rendering::PerspectiveFov(kFieldOfView, 1.0f, kNearZ, kFarZ);

        // タイルごとの形の向き（同じ形と向きは 1 つの定数を使い回す）
        std::array<std::array<D3D12_GPU_VIRTUAL_ADDRESS, 2>, 3> previewAddresses{};
        const auto previewConstants = [&](PreviewShape shape, bool rotate) {
            D3D12_GPU_VIRTUAL_ADDRESS& address = previewAddresses[static_cast<size_t>(shape)][rotate ? 1 : 0];
            if (address != 0) {
                return address;
            }
            SurfacePreviewConstants preview{};
            preview.world = MakeWorld(shape, view_, rotate);
            preview.worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(preview.world));
            preview.viewProjection = viewProjection;
            preview.lightDirection = Normalize(kLightDirection);
            preview.ambient = kAmbient;
            preview.lightColor = { 1.0f, 1.0f, 1.0f };
            address = ring.AllocateConstants(preview);
            return address;
        };

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
        if (materialAddress == 0 || cameraAddress == 0) {
            return;
        }
        const D3D12_GPU_VIRTUAL_ADDRESS depthParamsAddress = pipeline.sceneDepthParamsSlot.IsValid()
            ? ring.AllocateConstants(MakeSceneDepthParams(kNearZ, kFarZ, size_)) : 0;

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
        if (pipeline.sceneDepthSlot.IsValid()) {
            commandList->SetGraphicsRootDescriptorTable(pipeline.sceneDepthSlot.index, white);
        }
        if (depthParamsAddress != 0) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.sceneDepthParamsSlot.index, depthParamsAddress);
        }

        for (const DrawTile& tile : tiles_) {
            const MeshMemory* const mesh = uploadMesh(tile.shape);
            const D3D12_GPU_VIRTUAL_ADDRESS previewAddress = previewConstants(tile.shape, tile.rotate);
            if (!mesh || previewAddress == 0) {
                continue;
            }
            SetTileViewport(commandList, tile.x, tile.y, tile.size);
            commandList->IASetVertexBuffers(0, 1, &mesh->vertexView);
            commandList->IASetIndexBuffer(&mesh->indexView);
            if (pipeline.previewSlot.IsValid()) {
                commandList->SetGraphicsRootConstantBufferView(pipeline.previewSlot.index, previewAddress);
            }
            commandList->DrawIndexedInstanced(mesh->indexCount, 1, 0, 0, 0);
        }
        target3D_->End(commandList);
        drawn_ = true;
        drawnTarget_ = target3D_;
        drawnKey_ = requestedKey_;
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
        frame.screenWidth = size_;
        frame.screenHeight = size_;
        frame.invScreenSize = { 1.0f / static_cast<float>(size_), 1.0f / static_cast<float>(size_) };
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
        const D3D12_GPU_VIRTUAL_ADDRESS depthParamsAddress = pipeline.sceneDepthParamsSlot.IsValid()
            ? ring.AllocateConstants(MakeSceneDepthParams(0.1f, 10.0f, size_)) : 0;

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
        if (pipeline.sceneDepthSlot.IsValid()) {
            commandList->SetComputeRootDescriptorTable(pipeline.sceneDepthSlot.index, white);
        }
        if (depthParamsAddress != 0) {
            commandList->SetComputeRootConstantBufferView(pipeline.sceneDepthParamsSlot.index, depthParamsAddress);
        }
        for (size_t index = 0; index < pipeline.textureSlots.size() && index < pipeline.layout.textures.size(); ++index) {
            if (!pipeline.textureSlots[index].IsValid()) {
                continue;
            }
            const TextureManager::LoadedTexture* const loaded = values_.FindLoadedTexture(pipeline.layout.textures[index]);
            const D3D12_GPU_DESCRIPTOR_HANDLE handle = loaded && loaded->gpuHandle.ptr != 0 ? loaded->gpuHandle : white;
            commandList->SetComputeRootDescriptorTable(pipeline.textureSlots[index].index, handle);
        }
        commandList->Dispatch((size_ + kGroupSize - 1) / kGroupSize, (size_ + kGroupSize - 1) / kGroupSize, 1);
        offscreen->End(commandList);
        drawn_ = true;
        drawnTarget_ = target_;
        drawnKey_ = requestedKey_;
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
