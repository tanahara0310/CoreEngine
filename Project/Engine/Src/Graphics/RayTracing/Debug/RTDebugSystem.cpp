#include "pch.h"
#include "RTDebugSystem.h"

#ifdef CORE_EDITOR

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <format>
#include <iterator>

#include "Camera/View/ViewInfo.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RTInstanceMask.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector4.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        // 数えた値を書くバッファの並び（RTDebugVisualize.CS.hlsl・RTDebugScene.CS.hlsl と同じ）
        constexpr UINT kDataBytes = 1024;
        constexpr UINT kDataView = 0;
        constexpr UINT kDataScan = 64;
        constexpr UINT kDataStatsBytes = 64;
        constexpr UINT kDataSceneTotals = 128;
        constexpr UINT kDataTraceHeader = 192;
        constexpr UINT kDataTraceRays = 256;
        constexpr UINT kTraceRayStride = 64;
        constexpr UINT kStatsMin = 12;

        constexpr uint32_t kViewSlot = 0;
        constexpr uint32_t kSceneSlot = 1;
        constexpr uint32_t kThreadGroupSize = 8;

        /// @brief RTDebugVisualize.CS.hlsl の RTDebugVisualizeConstants
        struct alignas(16) VisualizeConstants {
            uint32_t width;          // offset  0
            uint32_t height;         // offset  4
            uint32_t mode;           // offset  8
            uint32_t component;      // offset 12
            float rangeMin;          // offset 16
            float rangeMax;          // offset 20
            uint32_t componentMask;  // offset 24
            uint32_t writeOutput;    // offset 28
            uint32_t probeX;         // offset 32
            uint32_t probeY;         // offset 36
            uint32_t probeEnabled;   // offset 40
            uint32_t statsOffset;    // offset 44
            uint32_t alphaMarksMiss; // offset 48
            uint32_t pad0;           // offset 52
            uint32_t pad1;           // offset 56
            uint32_t pad2;           // offset 60
        };
        static_assert(sizeof(VisualizeConstants) == 64, "VisualizeConstants と HLSL の cbuffer の大きさが違う");

        constexpr Cb::Field kVisualizeConstantsFields[] = {
            CB_FIELD(VisualizeConstants, width), CB_FIELD(VisualizeConstants, height),
            CB_FIELD(VisualizeConstants, mode), CB_FIELD(VisualizeConstants, component),
            CB_FIELD(VisualizeConstants, rangeMin), CB_FIELD(VisualizeConstants, rangeMax),
            CB_FIELD(VisualizeConstants, componentMask), CB_FIELD(VisualizeConstants, writeOutput),
            CB_FIELD(VisualizeConstants, probeX), CB_FIELD(VisualizeConstants, probeY),
            CB_FIELD(VisualizeConstants, probeEnabled), CB_FIELD(VisualizeConstants, statsOffset),
            CB_FIELD(VisualizeConstants, alphaMarksMiss), CB_FIELD(VisualizeConstants, pad0),
            CB_FIELD(VisualizeConstants, pad1), CB_FIELD(VisualizeConstants, pad2),
        };
        CB_VERIFY_LAYOUT(VisualizeConstants, kVisualizeConstantsFields);
        CB_BIND_HLSL(VisualizeConstants, kVisualizeConstantsFields, "RTDebugVisualizeConstants");

        /// @brief RTDebugScene.CS.hlsl の RTDebugSceneConstants
        struct alignas(16) SceneConstants {
            Matrix4x4 invViewProj;        // offset   0
            float cameraX;                // offset  64
            float cameraY;                // offset  68
            float cameraZ;                // offset  72
            uint32_t mode;                // offset  76
            uint32_t width;               // offset  80
            uint32_t height;              // offset  84
            uint32_t instanceMask;        // offset  88
            uint32_t alphaTest;           // offset  92
            uint32_t instanceTableIndex;  // offset  96
            uint32_t subMeshTableIndex;   // offset 100
            uint32_t rowCount;            // offset 104
            uint32_t traceOnly;           // offset 108
            uint32_t traceX;              // offset 112
            uint32_t traceY;              // offset 116
            uint32_t lightCount;          // offset 120
            float shadowBias;             // offset 124
            Vector4 lightDirections[4];   // offset 128
            float shadowDistance;         // offset 192
            float reflectionDistance;     // offset 196
            float coneSpread;             // offset 200
            float pad0;                   // offset 204
        };
        static_assert(sizeof(SceneConstants) == 208, "SceneConstants と HLSL の cbuffer の大きさが違う");

        constexpr Cb::Field kSceneConstantsFields[] = {
            CB_FIELD(SceneConstants, invViewProj),
            CB_FIELD(SceneConstants, cameraX), CB_FIELD(SceneConstants, cameraY),
            CB_FIELD(SceneConstants, cameraZ), CB_FIELD(SceneConstants, mode),
            CB_FIELD(SceneConstants, width), CB_FIELD(SceneConstants, height),
            CB_FIELD(SceneConstants, instanceMask), CB_FIELD(SceneConstants, alphaTest),
            CB_FIELD(SceneConstants, instanceTableIndex), CB_FIELD(SceneConstants, subMeshTableIndex),
            CB_FIELD(SceneConstants, rowCount), CB_FIELD(SceneConstants, traceOnly),
            CB_FIELD(SceneConstants, traceX), CB_FIELD(SceneConstants, traceY),
            CB_FIELD(SceneConstants, lightCount), CB_FIELD(SceneConstants, shadowBias),
            CB_FIELD(SceneConstants, lightDirections),
            CB_FIELD(SceneConstants, shadowDistance), CB_FIELD(SceneConstants, reflectionDistance),
            CB_FIELD(SceneConstants, coneSpread), CB_FIELD(SceneConstants, pad0),
        };
        CB_VERIFY_LAYOUT(SceneConstants, kSceneConstantsFields);
        CB_BIND_HLSL(SceneConstants, kSceneConstantsFields, "RTDebugSceneConstants");

        namespace VisualizeBind {
            enum Slot : size_t { RTDebugVisualizeConstants, gSource, gOutput, gDebugData, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "RTDebugVisualizeConstants", ShaderBindingType::CBV, BindingUsage::Required },
                { "gSource",                   ShaderBindingType::SRV, BindingUsage::Required },
                { "gOutput",                   ShaderBindingType::UAV, BindingUsage::Required },
                { "gDebugData",                ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        namespace SceneBind {
            enum Slot : size_t { RTDebugSceneConstants, gScene, gSceneDepth, gRowInfo, gSceneOutput, gDebugData, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "RTDebugSceneConstants", ShaderBindingType::CBV, BindingUsage::Required },
                { "gScene",                ShaderBindingType::SRV, BindingUsage::Required },
                { "gSceneDepth",           ShaderBindingType::SRV, BindingUsage::Required },
                { "gRowInfo",              ShaderBindingType::SRV, BindingUsage::Required },
                { "gSceneOutput",          ShaderBindingType::UAV, BindingUsage::Required },
                { "gDebugData",            ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        constexpr const char* kSceneModeNames[] = {
            "物（TLAS の行）", "サブメッシュ", "三角形", "距離", "法線", "ベースカラー",
            "インスタンスの印", "表と裏", "動き", "ラスタとの深度の差", "三角形の候補の数", "アルファを調べた数",
        };
        static_assert(std::size(kSceneModeNames) == static_cast<size_t>(RTDebugSceneMode::Count),
            "kSceneModeNames と RTDebugSceneMode の並びがずれている");

        /// @brief 並びを保つ uint から float へ戻す（RTDebugVisualize.CS.hlsl の FloatToOrderedKey の逆）
        float OrderedKeyToFloat(uint32_t key)
        {
            const uint32_t bits = (key & 0x80000000u) ? (key & 0x7FFFFFFFu) : ~key;
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        float AsFloat(uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        /// @brief テクスチャの幅と高さ
        bool GetTextureSize(GpuResource* resource, uint32_t& width, uint32_t& height)
        {
            if (!resource || !resource->IsValid()) {
                return false;
            }
            const D3D12_RESOURCE_DESC desc = resource->Desc();
            if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                return false;
            }
            width = static_cast<uint32_t>(desc.Width);
            height = desc.Height;
            return width > 0 && height > 0;
        }

        /// @brief 申告した成分のビット（何も申告していなければ RGBA 全部）
        uint32_t DeclaredComponentMask(const RTDebugChannelDesc& channel)
        {
            uint32_t mask = 0;
            for (uint32_t c = 0; c < 4; ++c) {
                if (channel.components[c]) {
                    mask |= 1u << c;
                }
            }
            if (channel.alphaMarksMiss) {
                mask &= ~0x8u;
            }
            return mask != 0 ? mask : 0xFu;
        }
    }

    const char* ToDisplayName(RTDebugSceneMode mode)
    {
        const size_t index = static_cast<size_t>(mode);
        return index < std::size(kSceneModeNames) ? kSceneModeNames[index] : "?";
    }

    RTDebugSceneModeInfo GetSceneModeInfo(RTDebugSceneMode mode)
    {
        switch (mode) {
        case RTDebugSceneMode::Instance:
            return { RTDebugViewMode::Id, 0.0f, 1.0f, "当たった物（TLAS の行）ごとの色。同じ色の所は同じインスタンス" };
        case RTDebugSceneMode::SubMesh:
            return { RTDebugViewMode::Id, 0.0f, 1.0f, "当たったサブメッシュ（BLAS の形 = GeometryIndex）ごとの色" };
        case RTDebugSceneMode::Triangle:
            return { RTDebugViewMode::Id, 0.0f, 1.0f, "当たった三角形（PrimitiveIndex）ごとの色。形の細かさが見える" };
        case RTDebugSceneMode::Distance:
            return { RTDebugViewMode::FalseColor, 0.0f, 200.0f, "カメラから当たった所までの距離（m）" };
        case RTDebugSceneMode::Normal:
            return { RTDebugViewMode::Normal, -1.0f, 1.0f, "当たった三角形の面の法線（レイが来た側を向く。頂点の法線ではない）" };
        case RTDebugSceneMode::BaseColor:
            return { RTDebugViewMode::Color, 0.0f, 1.0f, "材質の表から引いたベースカラー（影と映り込みのレイが見る色）" };
        case RTDebugSceneMode::Mask:
            return { RTDebugViewMode::Bits, 0.0f, 1.0f,
                "インスタンスの印のビットの帯（赤 = 影を落とす・緑 = 映り込む・青 = 止まっている形・黄 = 動く形）" };
        case RTDebugSceneMode::FrontFace:
            return { RTDebugViewMode::Signed, 0.0f, 1.0f, "赤 = 三角形の表から当たった・青 = 裏から当たった" };
        case RTDebugSceneMode::Motion:
            return { RTDebugViewMode::FalseColor, 0.0f, 2.0f, "青 = 止まっている・緑 = 動く形の決まった物・赤 = 毎フレーム BLAS を作る物" };
        case RTDebugSceneMode::DepthDifference:
            return { RTDebugViewMode::Signed, 0.01f, 0.5f,
                "RT の距離 − ラスタの深度の距離（m）。赤 = RT の方が遠い（RT に無い物がある）・青 = RT の方が近い" };
        case RTDebugSceneMode::Candidates:
            return { RTDebugViewMode::Heat, 0.0f, 32.0f, "不透明な物も含めて、レイが調べた三角形の候補の数（重なりが多いほど重い）" };
        case RTDebugSceneMode::AlphaTests:
            return { RTDebugViewMode::Heat, 0.0f, 8.0f, "アルファで抜けるかを材質の表で調べた候補の数" };
        default:
            return {};
        }
    }

    void RTDebugSystem::Initialize(GraphicsCore* graphics, ShaderProgramCache* shaderProgramCache)
    {
        graphics_ = graphics;
        descriptorAllocator_ = graphics ? graphics->GetDescriptorAllocator() : nullptr;
        shaderProgramCache_ = shaderProgramCache;
    }

    bool RTDebugSystem::EnsureCreated()
    {
        if (created_ || createFailed_) {
            return created_;
        }
        if (!graphics_ || !descriptorAllocator_ || !shaderProgramCache_) {
            createFailed_ = true;
            return false;
        }
        created_ =
            CreatePipeline(shaderProgramCache_, L"RTDebugVisualize.CS.hlsl", "RTDebugVisualize",
                VisualizeBind::kDecls, VisualizeBind::Count, false, visualizePipeline_)
            && CreatePipeline(shaderProgramCache_, L"RTDebugScene.CS.hlsl", "RTDebugScene",
                SceneBind::kDecls, SceneBind::Count, true, scenePipeline_)
            && CreateBuffers();
        createFailed_ = !created_;
        return created_;
    }

    bool RTDebugSystem::CreatePipeline(ShaderProgramCache* cache, const wchar_t* path, const char* label,
        const ShaderBindingDecl* decls, size_t declCount, bool directHeap, ComputePipeline& out)
    {
        Logger& log = Logger::GetInstance();
        const ShaderProgram* program = cache->GetOrCreateCompute(path, label, L"cs_6_6");
        if (!program) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTDebugSystem: {} のシェーダーをコンパイルできませんでした", label);
            return false;
        }

        RootSignatureConfig config;
        config.SetFlags(directHeap ? D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED : D3D12_ROOT_SIGNATURE_FLAG_NONE);
        if (directHeap) {
            // 物のテクスチャを繰り返して引く（Include/RayTracing/RTSceneTables.hlsli）
            config.ConfigureSampler("gLinearWrap", SamplerConfig::Linear());
        }
        const auto buildResult = out.rootSignature.Build(graphics_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTDebugSystem: {} のルートシグネチャを作れませんでした: {}", label, buildResult.errorMessage);
            return false;
        }

        try {
            out.bindings = BindingTable::Resolve(program->GetReflection(), decls, declCount, label);
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTDebugSystem: {} のシェーダーの結線が宣言と合いません: {}", label, e.what());
            return false;
        }

        out.pipelineState = ComputePipelineUtil::Create(
            graphics_->GetDevice(), out.rootSignature.GetRootSignature(), program->GetCS(), label);
        return out.pipelineState != nullptr;
    }

    bool RTDebugSystem::CreateBuffers()
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device = graphics_->GetDevice();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kDataBytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&buffer)))) {
            return false;
        }
        buffer->SetName(L"RTDebugData");
        data_.Reset(std::move(buffer), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = kDataBytes / sizeof(uint32_t);
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        dataUav_ = descriptorAllocator_->CreateUAV(data_.Get(), uavDesc, "RTDebugDataUAV");
        if (!dataUav_.IsValid()) {
            return false;
        }

        try {
            // 初期値（最小は最大の値から始める）
            dataInit_ = ResourceFactory::CreateBufferResource(device, kDataBytes, D3D12_HEAP_TYPE_UPLOAD);
            void* mapped = nullptr;
            if (FAILED(dataInit_->Map(0, nullptr, &mapped)) || !mapped) {
                return false;
            }
            std::memset(mapped, 0, kDataBytes);
            const uint32_t maxKey = 0xFFFFFFFFu;
            std::memcpy(static_cast<uint8_t*>(mapped) + kDataView + kStatsMin, &maxKey, sizeof(maxKey));
            std::memcpy(static_cast<uint8_t*>(mapped) + kDataScan + kStatsMin, &maxKey, sizeof(maxKey));
            dataInit_->Unmap(0, nullptr);

            for (auto& readback : readback_) {
                readback = ResourceFactory::CreateBufferResource(device, kDataBytes, D3D12_HEAP_TYPE_READBACK);
            }
        }
        catch (const std::exception&) {
            return false;
        }
        return true;
    }

    uint64_t RTDebugSystem::CurrentFrame() const
    {
        return graphics_ ? graphics_->Frame().FrameNumber() : 0;
    }

    bool RTDebugSystem::IsRequested(uint64_t requestFrame) const
    {
        return requestFrame != 0 && CurrentFrame() <= requestFrame + kRequestKeepFrames;
    }

    void RTDebugSystem::RequestView(const RTDebugViewRequest& request)
    {
        viewRequest_ = request;
        viewRequestFrame_ = CurrentFrame();
    }

    void RTDebugSystem::RequestScene(const RTDebugSceneRequest& request)
    {
        sceneRequest_ = request;
        sceneRequestFrame_ = CurrentFrame();
    }

    void RTDebugSystem::RequestTrace(uint32_t x, uint32_t y)
    {
        traceX_ = x;
        traceY_ = y;
        traceRequestFrame_ = CurrentFrame();
    }

    void RTDebugSystem::RequestScan()
    {
        scanRequestFrame_ = CurrentFrame();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RTDebugSystem::GetViewSRV() const
    {
        return outputs_.HasTexture(kViewSlot) ? outputs_.GetSRVHandle(kViewSlot) : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    uint32_t RTDebugSystem::GetViewWidth() const
    {
        return outputs_.HasTexture(kViewSlot) ? outputs_.GetWidth(kViewSlot) : 0;
    }

    uint32_t RTDebugSystem::GetViewHeight() const
    {
        return outputs_.HasTexture(kViewSlot) ? outputs_.GetHeight(kViewSlot) : 0;
    }

    bool RTDebugSystem::ResolveSceneSource(RTDebugSource& out)
    {
        if (sceneWrittenFrame_ == 0 || sceneWrittenFrame_ != CurrentFrame() || !outputs_.HasTexture(kSceneSlot)) {
            return false;
        }
        out.resource = &outputs_.Resource(kSceneSlot);
        out.srv = outputs_.GetSRVHandle(kSceneSlot);
        return true;
    }

    void RTDebugSystem::ResetData(ID3D12GraphicsCommandList* cmdList, UINT offset, UINT bytes)
    {
        Barrier::Transition(cmdList, data_, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyBufferRegion(data_.Get(), offset, dataInit_.Get(), offset, bytes);
        Barrier::Transition(cmdList, data_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    RTDebugSystem::PendingFrame& RTDebugSystem::BeginPendingFrame()
    {
        const uint32_t frameIndex = graphics_->Frame().FrameIndex();
        PendingFrame& frame = pendingFrames_[frameIndex];
        const uint64_t current = CurrentFrame();
        if (frame.frame != current) {
            // この枠の前の結果を読んでから使い直す
            ReadFrame(frameIndex);
            frame = PendingFrame{};
            frame.frame = current;
        }
        return frame;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RTDebugSystem::UploadRowInfo(const RenderContext& context, uint32_t& outRowCount)
    {
        outRowCount = 0;
        const RTScene* scene = context.rtScene;
        const auto& instances = scene->GetInstances();
        const auto& rows = scene->GetRows();
        const uint32_t rowCount = static_cast<uint32_t>((std::min)(instances.size(), rows.size()));

        rowInfoScratch_.assign(static_cast<size_t>(rowCount) * 4, 0u);
        for (uint32_t i = 0; i < rowCount; ++i) {
            const auto& instance = instances[i];
            uint32_t flags = 0;
            if ((instance.flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) == 0) {
                flags |= kRowFlagAlphaTested;
            }
            if (rows[i].dynamicOwner) {
                flags |= kRowFlagDynamicShape;
            }
            if ((instance.instanceMask & RTInstanceMask::kMovingSolid) != 0) {
                flags |= kRowFlagMoving;
            }
            rowInfoScratch_[i * 4 + 0] = instance.instanceMask;
            rowInfoScratch_[i * 4 + 1] = flags;
        }

        const uint32_t slot = graphics_->Frame().FrameIndex();
        auto& buffer = rowInfoBuffers_[slot];
        if (!buffer || rowInfoCapacities_[slot] < rowCount) {
            if (buffer) {
                graphics_->DeferredRelease().PushForCurrentFrame(std::move(buffer));
            }
            const uint32_t capacity = (std::max)(rowCount + rowCount / 2, 64u);
            try {
                buffer = ResourceFactory::CreateBufferResource(
                    graphics_->GetDevice(), static_cast<size_t>(capacity) * 16, D3D12_HEAP_TYPE_UPLOAD);
            }
            catch (const std::exception&) {
                rowInfoCapacities_[slot] = 0;
                return {};
            }
            rowInfoCapacities_[slot] = capacity;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = capacity;
            srvDesc.Buffer.StructureByteStride = 16;
            descriptorAllocator_->EnsureSRV(rowInfoSrvs_[slot], buffer.Get(), srvDesc, "RTDebugRowInfoSRV");
        }

        void* mapped = nullptr;
        if (FAILED(buffer->Map(0, nullptr, &mapped)) || !mapped) {
            return {};
        }
        if (!rowInfoScratch_.empty()) {
            std::memcpy(mapped, rowInfoScratch_.data(), rowInfoScratch_.size() * sizeof(uint32_t));
        }
        buffer->Unmap(0, nullptr);
        outRowCount = rowCount;
        return rowInfoSrvs_[slot].gpuHandle;
    }

    void RTDebugSystem::ExecuteScene(const RenderContext& context)
    {
        if (!graphics_ || !context.cmdList) {
            return;
        }
        const bool scene = IsRequested(sceneRequestFrame_);
        const bool trace = IsRequested(traceRequestFrame_);
        if ((!scene && !trace) || !EnsureCreated()) {
            return;
        }
        AccelerationStructureManager* asMgr = context.accelerationStructureManager;
        if (!asMgr || !asMgr->IsSupported() || !asMgr->IsTLASReady() || !context.rtScene
            || !context.frameViews || !context.frameBlackboard) {
            return;
        }
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
        if (!view.isValid) {
            return;
        }
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrv{};
        if (!context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, depthSrv)) {
            return;
        }

        const UINT width = static_cast<UINT>((std::max)(graphics_->GetClientWidth(), 1));
        const UINT height = static_cast<UINT>((std::max)(graphics_->GetClientHeight(), 1));
        RayTracingOutputViewSet::TextureOptions options;
        options.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        if (!outputs_.EnsureTexture(graphics_, descriptorAllocator_, width, height, kSceneSlot,
                "RTDebugSystem", "RTDebugScene", options)) {
            return;
        }
        uint32_t rowCount = 0;
        const D3D12_GPU_DESCRIPTOR_HANDLE rowInfo = UploadRowInfo(context, rowCount);
        if (rowInfo.ptr == 0) {
            return;
        }

        SceneConstants constants{};
        constants.invViewProj = view.invViewProjection;
        constants.cameraX = view.position.x;
        constants.cameraY = view.position.y;
        constants.cameraZ = view.position.z;
        constants.mode = static_cast<uint32_t>(sceneRequest_.mode);
        constants.width = width;
        constants.height = height;
        constants.instanceMask = sceneRequest_.instanceMask & 0xFFu;
        constants.alphaTest = sceneRequest_.alphaTest ? 1u : 0u;
        const RTHitTableIndices tables = context.rtScene->GetHitTableIndices();
        constants.instanceTableIndex = tables.instanceTable;
        constants.subMeshTableIndex = tables.subMeshTable;
        constants.rowCount = rowCount;
        constants.traceX = traceX_;
        constants.traceY = traceY_;
        if (context.lightManager) {
            for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && constants.lightCount < 4; ++li) {
                const Light* light = context.lightManager->GetDirectionalLight(li);
                if (!light || !light->enabled) {
                    continue;
                }
                constants.lightDirections[constants.lightCount++] =
                    Vector4{ light->direction.x, light->direction.y, light->direction.z, 1.0f };
            }
        }
        constants.shadowBias = 0.05f;
        constants.shadowDistance = 1000.0f;
        if (const RayTracingShadowManager* rtShadow = FindRTEffect<RayTracingShadowManager>(context)) {
            constants.shadowBias = rtShadow->GetSettings().shadowBias;
            constants.shadowDistance = rtShadow->GetSettings().maxRayDistance;
        }
        constants.reflectionDistance = 1000.0f;
        constants.coneSpread = 2.0f / ((std::max)(std::abs(view.projection.m[1][1]), 1.0e-4f) * static_cast<float>(height));

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        PendingFrame& frame = BeginPendingFrame();
        UploadRing& uploadRing = graphics_->GetUploadRing();
        GpuResource& output = outputs_.Resource(kSceneSlot);

        cmdList->SetPipelineState(scenePipeline_.pipelineState.Get());
        cmdList->SetComputeRootSignature(scenePipeline_.rootSignature.GetRootSignature());
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        const BindingTable& bindings = scenePipeline_.bindings;

        auto dispatch = [&](bool traceOnly) {
            constants.traceOnly = traceOnly ? 1u : 0u;
            binder.Set(bindings[SceneBind::RTDebugSceneConstants], uploadRing.AllocateConstants(constants));
            binder.Set(bindings[SceneBind::gScene], asMgr->GetTLASSRVHandle());
            binder.Set(bindings[SceneBind::gSceneDepth], depthSrv);
            binder.Set(bindings[SceneBind::gRowInfo], rowInfo);
            binder.Set(bindings[SceneBind::gSceneOutput], outputs_.GetUAVHandle(kSceneSlot));
            binder.Set(bindings[SceneBind::gDebugData], dataUav_.gpuHandle);
            binder.ValidateBeforeDraw(bindings);
            if (traceOnly) {
                cmdList->Dispatch(1, 1, 1);
            } else {
                cmdList->Dispatch((width + kThreadGroupSize - 1) / kThreadGroupSize,
                    (height + kThreadGroupSize - 1) / kThreadGroupSize, 1);
            }
            Barrier::UAV(cmdList, data_);
        };

        if (scene) {
            ResetData(cmdList, kDataSceneTotals, kDataStatsBytes);
            Barrier::Transition(cmdList, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            dispatch(false);
            Barrier::Transition(cmdList, output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            sceneWrittenFrame_ = CurrentFrame();
            frame.scene = true;
            frame.sceneMode = sceneRequest_.mode;
        }
        if (trace && traceX_ < width && traceY_ < height) {
            ResetData(cmdList, kDataTraceHeader, kDataBytes - kDataTraceHeader);
            Barrier::Transition(cmdList, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            dispatch(true);
            Barrier::Transition(cmdList, output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            frame.trace = true;
        }
    }

    bool RTDebugSystem::DispatchVisualize(const RenderContext& context, const RTDebugChannelDesc& channel,
        uint32_t variant, const RTDebugViewRequest& request, bool writeOutput, UINT statsOffset,
        uint32_t& outWidth, uint32_t& outHeight)
    {
        RTDebugSource source;
        if (!channel.resolve || !channel.resolve(context, variant, source) || source.srv.ptr == 0) {
            return false;
        }
        uint32_t width = 0;
        uint32_t height = 0;
        if (!GetTextureSize(source.resource, width, height)) {
            return false;
        }
        if (writeOutput) {
            RayTracingOutputViewSet::TextureOptions options;
            options.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (!outputs_.EnsureTexture(graphics_, descriptorAllocator_, width, height, kViewSlot,
                    "RTDebugSystem", "RTDebugView", options)) {
                return false;
            }
        }
        if (!outputs_.HasTexture(kViewSlot)) {
            // 数えるだけのときも出力のスロットに有効な UAV を差す
            RayTracingOutputViewSet::TextureOptions options;
            options.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (!outputs_.EnsureTexture(graphics_, descriptorAllocator_, 8, 8, kViewSlot,
                    "RTDebugSystem", "RTDebugView", options)) {
                return false;
            }
        }

        VisualizeConstants constants{};
        constants.width = width;
        constants.height = height;
        constants.mode = static_cast<uint32_t>(request.mode);
        constants.component = (std::min)(request.component, 3u);
        constants.rangeMin = request.rangeMin;
        constants.rangeMax = request.rangeMax;
        constants.componentMask = request.componentMask & 0xFu;
        constants.writeOutput = writeOutput ? 1u : 0u;
        constants.probeEnabled = request.probeEnabled ? 1u : 0u;
        constants.probeX = request.probeX;
        constants.probeY = request.probeY;
        constants.statsOffset = statsOffset;
        constants.alphaMarksMiss = request.alphaMarksMiss ? 1u : 0u;

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        GpuResource& output = outputs_.Resource(kViewSlot);
        {
            BarrierBatch batch(cmdList);
            batch.Transition(*source.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        cmdList->SetPipelineState(visualizePipeline_.pipelineState.Get());
        cmdList->SetComputeRootSignature(visualizePipeline_.rootSignature.GetRootSignature());
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        const BindingTable& bindings = visualizePipeline_.bindings;
        binder.Set(bindings[VisualizeBind::RTDebugVisualizeConstants], graphics_->GetUploadRing().AllocateConstants(constants));
        binder.Set(bindings[VisualizeBind::gSource], source.srv);
        binder.Set(bindings[VisualizeBind::gOutput], outputs_.GetUAVHandle(kViewSlot));
        binder.Set(bindings[VisualizeBind::gDebugData], dataUav_.gpuHandle);
        binder.ValidateBeforeDraw(bindings);
        cmdList->Dispatch((width + kThreadGroupSize - 1) / kThreadGroupSize,
            (height + kThreadGroupSize - 1) / kThreadGroupSize, 1);
        Barrier::UAV(cmdList, data_);
        Barrier::Transition(cmdList, output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        outWidth = width;
        outHeight = height;
        return true;
    }

    bool RTDebugSystem::NextScanTarget(const RenderContext& context, int& outChannel, uint32_t& outVariant,
        RTDebugSource& outSource)
    {
        const auto& channels = RTDebugRegistry::Get().Channels();
        size_t total = 0;
        for (const RTDebugChannelDesc& channel : channels) {
            total += (std::max)(channel.variantCount, 1u);
        }
        if (total == 0) {
            return false;
        }
        for (size_t attempt = 0; attempt < total; ++attempt) {
            const size_t cursor = (scanCursor_ + attempt) % total;
            size_t base = 0;
            for (size_t c = 0; c < channels.size(); ++c) {
                const size_t count = (std::max)(channels[c].variantCount, 1u);
                if (cursor < base + count) {
                    const uint32_t variant = static_cast<uint32_t>(cursor - base);
                    RTDebugSource source;
                    if (channels[c].resolve(context, variant, source) && source.resource && source.srv.ptr != 0) {
                        outChannel = static_cast<int>(c);
                        outVariant = variant;
                        outSource = source;
                        scanCursor_ = cursor + 1;
                        return true;
                    }
                    break;
                }
                base += count;
            }
        }
        return false;
    }

    void RTDebugSystem::ExecuteVisualize(const RenderContext& context)
    {
        if (!graphics_ || !context.cmdList) {
            return;
        }
        const auto& channels = RTDebugRegistry::Get().Channels();
        const bool view = IsRequested(viewRequestFrame_) && viewRequest_.channel >= 0
            && static_cast<size_t>(viewRequest_.channel) < channels.size();
        const bool scan = IsRequested(scanRequestFrame_);
        if ((view || scan) && !EnsureCreated()) {
            return;
        }
        if (!created_) {
            return;
        }
        ID3D12GraphicsCommandList* cmdList = context.cmdList;

        if (view) {
            const RTDebugChannelDesc& channel = channels[static_cast<size_t>(viewRequest_.channel)];
            const uint32_t variant = (std::min)(viewRequest_.variant, (std::max)(channel.variantCount, 1u) - 1u);
            PendingFrame& frame = BeginPendingFrame();
            ResetData(cmdList, kDataView, kDataStatsBytes);
            uint32_t width = 0;
            uint32_t height = 0;
            if (DispatchVisualize(context, channel, variant, viewRequest_, true, kDataView, width, height)) {
                frame.view = true;
                frame.viewChannel = viewRequest_.channel;
                frame.viewVariant = variant;
                frame.viewWidth = width;
                frame.viewHeight = height;
                frame.viewProbe = viewRequest_.probeEnabled && viewRequest_.probeX < width && viewRequest_.probeY < height;
                frame.probeX = viewRequest_.probeX;
                frame.probeY = viewRequest_.probeY;
                viewFrame_ = CurrentFrame();
                viewChannel_ = viewRequest_.channel;
                viewVariant_ = variant;
            }
        }

        if (scan) {
            int channelIndex = -1;
            uint32_t variant = 0;
            RTDebugSource source;
            if (NextScanTarget(context, channelIndex, variant, source)) {
                const RTDebugChannelDesc& channel = channels[static_cast<size_t>(channelIndex)];
                RTDebugViewRequest request;
                request.mode = RTDebugViewMode::Color;
                request.componentMask = DeclaredComponentMask(channel);
                request.alphaMarksMiss = channel.alphaMarksMiss;
                PendingFrame& frame = BeginPendingFrame();
                ResetData(cmdList, kDataScan, kDataStatsBytes);
                uint32_t width = 0;
                uint32_t height = 0;
                if (DispatchVisualize(context, channel, variant, request, false, kDataScan, width, height)) {
                    frame.scan = true;
                    frame.scanChannel = channelIndex;
                    frame.scanVariant = variant;
                    frame.scanWidth = width;
                    frame.scanHeight = height;
                }
            }
        }

        // 今フレームに積んだ分を読み戻しへ写す
        const uint32_t frameIndex = graphics_->Frame().FrameIndex();
        PendingFrame& frame = pendingFrames_[frameIndex];
        if (frame.frame == CurrentFrame() && !frame.pending && (frame.view || frame.scan || frame.scene || frame.trace)) {
            Barrier::Transition(cmdList, data_, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmdList->CopyBufferRegion(readback_[frameIndex].Get(), 0, data_.Get(), 0, kDataBytes);
            Barrier::Transition(cmdList, data_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            frame.pending = true;
            frame.fenceBeforeSubmit = graphics_->Frame().FenceValueOf(frameIndex);
        }
    }

    void RTDebugSystem::CollectResults()
    {
        if (!created_) {
            return;
        }
        // 古い枠から順に読む（新しい結果ほど後に入る）
        const uint32_t framesInFlight = graphics_->Frame().FramesInFlight();
        const uint32_t current = graphics_->Frame().FrameIndex();
        for (uint32_t offset = 1; offset <= framesInFlight; ++offset) {
            ReadFrame((current + offset) % framesInFlight);
        }
    }

    void RTDebugSystem::ReadFrame(uint32_t frameIndex)
    {
        PendingFrame& frame = pendingFrames_[frameIndex];
        if (!frame.pending) {
            return;
        }
        const FrameSync& sync = graphics_->Frame();
        const uint64_t fence = sync.FenceValueOf(frameIndex);
        if (fence == frame.fenceBeforeSubmit || sync.CompletedValue() < fence) {
            return;
        }
        frame.pending = false;

        uint32_t data[kDataBytes / sizeof(uint32_t)] = {};
        void* mapped = nullptr;
        const D3D12_RANGE readRange{ 0, kDataBytes };
        if (FAILED(readback_[frameIndex]->Map(0, &readRange, &mapped)) || !mapped) {
            return;
        }
        std::memcpy(data, mapped, kDataBytes);
        const D3D12_RANGE writtenRange{ 0, 0 };
        readback_[frameIndex]->Unmap(0, &writtenRange);

        auto word = [&data](UINT byteOffset) { return data[byteOffset / sizeof(uint32_t)]; };

        if (frame.view) {
            RTDebugImageStats stats;
            stats.valid = true;
            stats.frame = frame.frame;
            stats.channel = frame.viewChannel;
            stats.variant = frame.viewVariant;
            stats.width = frame.viewWidth;
            stats.height = frame.viewHeight;
            stats.nanCount = word(kDataView + 0);
            stats.infCount = word(kDataView + 4);
            stats.finiteCount = word(kDataView + 8);
            const uint32_t minKey = word(kDataView + 12);
            const uint32_t maxKey = word(kDataView + 16);
            stats.hasRange = minKey != 0xFFFFFFFFu && maxKey != 0u;
            if (stats.hasRange) {
                stats.minValue = OrderedKeyToFloat(minKey);
                stats.maxValue = OrderedKeyToFloat(maxKey);
            }
            stats.probeValid = frame.viewProbe;
            stats.probeX = frame.probeX;
            stats.probeY = frame.probeY;
            for (UINT c = 0; c < 4; ++c) {
                stats.probe[c] = AsFloat(word(kDataView + 20 + c * 4));
            }
            viewStats_ = stats;
        }

        if (frame.scan) {
            RTDebugScanEntry entry;
            entry.channel = frame.scanChannel;
            entry.variant = frame.scanVariant;
            entry.frame = frame.frame;
            entry.width = frame.scanWidth;
            entry.height = frame.scanHeight;
            entry.nanCount = word(kDataScan + 0);
            entry.infCount = word(kDataScan + 4);
            entry.finiteCount = word(kDataScan + 8);
            auto it = std::find_if(scanResults_.begin(), scanResults_.end(), [&](const RTDebugScanEntry& e) {
                return e.channel == entry.channel && e.variant == entry.variant;
            });
            if (it != scanResults_.end()) {
                *it = entry;
            } else {
                scanResults_.push_back(entry);
            }
        }

        if (frame.scene) {
            RTDebugSceneStats stats;
            stats.valid = true;
            stats.frame = frame.frame;
            stats.mode = frame.sceneMode;
            stats.candidates = word(kDataSceneTotals + 0);
            stats.alphaTests = word(kDataSceneTotals + 4);
            stats.hitPixels = word(kDataSceneTotals + 8);
            stats.pixels = word(kDataSceneTotals + 12);
            sceneStats_ = stats;
        }

        if (frame.trace) {
            RTDebugTraceResult result;
            result.valid = true;
            result.frame = frame.frame;
            result.hitPosition = Vector3{ AsFloat(word(kDataTraceHeader + 0)), AsFloat(word(kDataTraceHeader + 4)),
                AsFloat(word(kDataTraceHeader + 8)) };
            const uint32_t rayCount = (std::min)(word(kDataTraceHeader + 12), kMaxTracedRays);
            result.hitNormal = Vector3{ AsFloat(word(kDataTraceHeader + 16)), AsFloat(word(kDataTraceHeader + 20)),
                AsFloat(word(kDataTraceHeader + 24)) };
            result.hit = word(kDataTraceHeader + 28) != 0;
            for (UINT c = 0; c < 4; ++c) {
                result.baseColor[c] = AsFloat(word(kDataTraceHeader + 32 + c * 4));
            }
            result.uv[0] = AsFloat(word(kDataTraceHeader + 48));
            result.uv[1] = AsFloat(word(kDataTraceHeader + 52));
            result.x = word(kDataTraceHeader + 56);
            result.y = word(kDataTraceHeader + 60);
            result.rays.reserve(rayCount);
            for (uint32_t i = 0; i < rayCount; ++i) {
                const UINT base = kDataTraceRays + i * kTraceRayStride;
                RTDebugTracedRay ray;
                ray.origin = Vector3{ AsFloat(word(base + 0)), AsFloat(word(base + 4)), AsFloat(word(base + 8)) };
                ray.tMax = AsFloat(word(base + 12));
                ray.direction = Vector3{ AsFloat(word(base + 16)), AsFloat(word(base + 20)), AsFloat(word(base + 24)) };
                ray.hitT = AsFloat(word(base + 28));
                ray.row = word(base + 32);
                ray.geometry = word(base + 36);
                ray.primitive = word(base + 40);
                const uint32_t flags = word(base + 44);
                ray.hit = (flags & 1u) != 0;
                ray.frontFace = (flags & 2u) != 0;
                ray.kind = word(base + 48);
                ray.instanceMask = word(base + 52);
                ray.candidates = word(base + 56);
                ray.alphaTests = word(base + 60);
                result.rays.push_back(ray);
            }
            traceResult_ = std::move(result);
        }
    }

    // ──────────────────────────────────────────────────────────
    // 中間結果とチェックの申告
    // ──────────────────────────────────────────────────────────

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "デバッグ",
        .stage = "見え方",
        .name = "カメラのレイ",
        .description = "「見え方」タブで選んだ値。見え方のタブを開いている間だけ作る",
        .components = { "値", "補足 1", "補足 2", "当たったか（0 = 当たっていない）" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Id,
        .alphaMarksMiss = true,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return context.rtDebug && context.rtDebug->ResolveSceneSource(out);
        })

    namespace
    {
        /// @brief 物の名前（見つからなければ "?"）
        std::string ObjectName(const RTDebugQuery& query, ObjectId id)
        {
            if (!query.objects || !id.IsValid()) {
                return "?";
            }
            const GameObject* object = query.objects->FindObject(id);
            return object ? object->GetName() : "?";
        }

        /// @brief 行列に NaN・Inf があるか
        bool HasNonFinite(const float (&transform)[3][4])
        {
            for (const auto& row : transform) {
                for (float v : row) {
                    if (!std::isfinite(v)) {
                        return true;
                    }
                }
            }
            return false;
        }

        /// @brief 3×3 部分の行列式
        float Determinant3x3(const float (&m)[3][4])
        {
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        }

        /// @brief 一覧に出す行の数の上限（多いときは件数だけ出す）
        constexpr size_t kMaxListedRows = 8;
    }

    RT_DEBUG_CHECK_REGISTER(
        .name = "TLAS",
        .run = [](const RTDebugQuery& query, std::vector<RTDebugCheckResult>& out) {
            AccelerationStructureManager* asMgr = query.domain ? query.domain->GetAccelerationStructureManager() : nullptr;
            RTScene* scene = query.domain ? query.domain->GetRTScene() : nullptr;
            if (!asMgr || !asMgr->IsSupported()) {
                out.push_back({ RTDebugSeverity::Info, "DXR の Tier 1.1 に対応していない GPU（レイトレーシングは使わない）" });
                return;
            }
            if (!asMgr->IsTLASReady()) {
                out.push_back({ RTDebugSeverity::Error, "TLAS ができていない（どの効果も撃たずに戻る）" });
            } else {
                out.push_back({ RTDebugSeverity::Ok, std::format("TLAS がある（インスタンス {} 個）", asMgr->GetTLASInstanceCount()) });
            }
            if (!scene) {
                return;
            }
            if (RTDebug::IsSceneFrozen()) {
                out.push_back({ RTDebugSeverity::Info, "凍結中：TLAS と材質の表を作り直していない" });
            } else if (scene->GetLastBuildFrame() + 2 < query.frame) {
                out.push_back({ RTDebugSeverity::Warning,
                    std::format("TLAS を {} フレーム作っていない", query.frame - scene->GetLastBuildFrame()) });
            }
            if (scene->GetInstances().size() != asMgr->GetTLASInstanceCount()) {
                out.push_back({ RTDebugSeverity::Error, std::format("RTScene の行の数（{}）と TLAS のインスタンス数（{}）が合わない",
                    scene->GetInstances().size(), asMgr->GetTLASInstanceCount()) });
            }
        })

    RT_DEBUG_CHECK_REGISTER(
        .name = "材質の表",
        .run = [](const RTDebugQuery& query, std::vector<RTDebugCheckResult>& out) {
            RTScene* scene = query.domain ? query.domain->GetRTScene() : nullptr;
            if (!scene || scene->GetInstances().empty()) {
                return;
            }
            if (!scene->HasHitShadingTables()) {
                out.push_back({ RTDebugSeverity::Warning, "材質の表が無い（アルファで抜ける所を調べず、水面の映り込みは色を引けない）" });
                return;
            }
            const auto& instances = scene->GetHitInstances();
            const auto& subMeshes = scene->GetHitSubMeshes();
            if (instances.size() != scene->GetInstances().size()) {
                out.push_back({ RTDebugSeverity::Error, std::format("インスタンス表の行の数（{}）と TLAS の行の数（{}）が合わない",
                    instances.size(), scene->GetInstances().size()) });
            }
            const uint32_t heapSize = (query.graphics && query.graphics->GetDescriptorAllocator())
                ? query.graphics->GetDescriptorAllocator()->SRV().Capacity() : UINT32_MAX;
            auto badIndex = [heapSize](uint32_t index) { return index != kRTHitNoTexture && index >= heapSize; };
            size_t badRows = 0;
            std::string listed;
            for (size_t i = 0; i < instances.size(); ++i) {
                const RTHitInstance& row = instances[i];
                bool bad = badIndex(row.vertexBufferIndex) || badIndex(row.indexBufferIndex)
                    || row.vertexBufferIndex == kRTHitNoTexture || row.indexBufferIndex == kRTHitNoTexture
                    || row.subMeshCount == 0 || static_cast<size_t>(row.firstSubMesh) + row.subMeshCount > subMeshes.size();
                if (!bad) {
                    for (uint32_t s = 0; s < row.subMeshCount; ++s) {
                        const RTHitSubMesh& subMesh = subMeshes[row.firstSubMesh + s];
                        if (badIndex(subMesh.baseColorTextureIndex) || badIndex(subMesh.emissiveTextureIndex)
                            || badIndex(subMesh.metallicRoughnessTextureIndex) || badIndex(subMesh.normalTextureIndex)
                            || badIndex(subMesh.occlusionTextureIndex)) {
                            bad = true;
                            break;
                        }
                    }
                }
                if (bad) {
                    if (badRows < kMaxListedRows) {
                        listed += std::format("{}{}", listed.empty() ? "" : "・", i);
                    }
                    ++badRows;
                }
            }
            if (badRows > 0) {
                out.push_back({ RTDebugSeverity::Error, std::format(
                    "ヒープの外や空の番号を指す行が {} 行（行 {}）", badRows, listed) });
            } else {
                out.push_back({ RTDebugSeverity::Ok, std::format(
                    "表の番号は全部ヒープの中（インスタンス {} 行・サブメッシュ {} 行）", instances.size(), subMeshes.size()) });
            }
        })

    RT_DEBUG_CHECK_REGISTER(
        .name = "行列",
        .run = [](const RTDebugQuery& query, std::vector<RTDebugCheckResult>& out) {
            RTScene* scene = query.domain ? query.domain->GetRTScene() : nullptr;
            if (!scene) {
                return;
            }
            const auto& instances = scene->GetInstances();
            const auto& rows = scene->GetRows();
            size_t nonFinite = 0;
            size_t degenerate = 0;
            std::string listed;
            for (size_t i = 0; i < instances.size(); ++i) {
                const bool bad = HasNonFinite(instances[i].transform);
                const bool flat = !bad && std::abs(Determinant3x3(instances[i].transform)) < 1.0e-12f;
                if (!bad && !flat) {
                    continue;
                }
                (bad ? nonFinite : degenerate) += 1;
                if (nonFinite + degenerate <= kMaxListedRows) {
                    const ObjectId owner = i < rows.size() ? rows[i].owner : ObjectId{};
                    listed += std::format("{}{}（{}）", listed.empty() ? "" : "・", ObjectName(query, owner), bad ? "NaN" : "つぶれ");
                }
            }
            if (nonFinite > 0) {
                out.push_back({ RTDebugSeverity::Error, std::format("行列に NaN か Inf がある物が {} 個：{}", nonFinite, listed) });
            }
            if (degenerate > 0) {
                out.push_back({ RTDebugSeverity::Warning, std::format("拡大率が 0 でつぶれた物が {} 個：{}", degenerate, listed) });
            }
            if (nonFinite == 0 && degenerate == 0) {
                out.push_back({ RTDebugSeverity::Ok, "どの物の行列も有限でつぶれていない" });
            }
        })

    RT_DEBUG_CHECK_REGISTER(
        .name = "加速構造のメモリ",
        .run = [](const RTDebugQuery& query, std::vector<RTDebugCheckResult>& out) {
            AccelerationStructureManager* asMgr = query.domain ? query.domain->GetAccelerationStructureManager() : nullptr;
            if (!asMgr || !asMgr->IsSupported()) {
                return;
            }
            constexpr double kMiB = 1024.0 * 1024.0;
            out.push_back({ RTDebugSeverity::Info, std::format("BLAS {:.1f} MB・TLAS {:.2f} MB・スクラッチ {:.1f} MB",
                static_cast<double>(asMgr->GetBLASTotalBytes()) / kMiB,
                static_cast<double>(asMgr->GetTLASResultBytes()) / kMiB,
                static_cast<double>(asMgr->GetScratchTotalBytes()) / kMiB) });
            constexpr size_t kManyRetired = 256;
            const size_t retired = asMgr->GetRetiredResourceCount();
            out.push_back({ retired > kManyRetired ? RTDebugSeverity::Warning : RTDebugSeverity::Ok,
                std::format("解放待ちのリソース {} 個", retired) });
        })

    RT_DEBUG_CHECK_REGISTER(
        .name = "中間結果の NaN と Inf",
        .run = [](const RTDebugQuery& query, std::vector<RTDebugCheckResult>& out) {
            if (!query.debug) {
                return;
            }
            const auto& channels = RTDebugRegistry::Get().Channels();
            const auto& results = query.debug->GetScanResults();
            if (query.debug->GetScanRequestFrame() == 0 || results.empty()) {
                out.push_back({ RTDebugSeverity::Info, "まだ数えていない（「中間結果を見回る」を入れると 1 フレームに 1 枚ずつ数える）" });
                return;
            }
            size_t clean = 0;
            for (const RTDebugScanEntry& entry : results) {
                if (entry.channel < 0 || static_cast<size_t>(entry.channel) >= channels.size()) {
                    continue;
                }
                if (entry.nanCount == 0 && entry.infCount == 0) {
                    ++clean;
                    continue;
                }
                const RTDebugChannelDesc& channel = channels[static_cast<size_t>(entry.channel)];
                const std::string variant = channel.variantCount > 1
                    ? std::format("（{} {}）", channel.variantLabel, entry.variant) : std::string();
                out.push_back({ RTDebugSeverity::Error, std::format("{} / {} / {}{}：NaN {} 画素・Inf {} 画素（{}×{}）",
                    channel.effect, channel.stage, channel.name, variant, entry.nanCount, entry.infCount,
                    entry.width, entry.height) });
            }
            if (clean > 0) {
                out.push_back({ RTDebugSeverity::Ok, std::format("{} 枚は NaN も Inf も無い", clean) });
            }
        })
}

#endif // CORE_EDITOR
