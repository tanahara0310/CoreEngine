#include "pch.h"
#include "HiZOcclusionSystem.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Command/CommandManager.h"
#include "Graphics/RHI/Descriptor/DescriptorManager.h"
#include "Diagnostics/EngineStats.h"
#include "Graphics/RHI/Barrier/ResourceBarrierHelper.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        // 構築 CS 定数の 256B アライメントスロットサイズ
        constexpr uint32_t kCbSlotSize = 256;
    }

    // ---------------------------------------------------------------
    // CPU 側フレームフロー
    // ---------------------------------------------------------------

    void HiZOcclusionSystem::BeginFrame(uint32_t recordingFrameIndex)
    {
        ++frameCounter_;
        pendingQueries_.clear();

        if (recordingFrameIndex >= kFrameRing) {
            return;
        }

        // このリングスロットへ書いた GPU フレームは、同じ recordingFrameIndex が
        // 回ってくる前に CommandManager::WaitForFrame で完了が保証されている
        // （InstanceBatchManager の frameResources_ と同じリング前提）。
        if (!ringPending_[recordingFrameIndex] || !readback_[recordingFrameIndex]) {
            return;
        }

        const std::vector<uint32_t>& ids = ringIds_[recordingFrameIndex];
        const size_t count = ids.size();

        uint32_t* results = nullptr;
        const D3D12_RANGE readRange{ 0, sizeof(uint32_t) * count };
        if (SUCCEEDED(readback_[recordingFrameIndex]->Map(
                0, &readRange, reinterpret_cast<void**>(&results)))) {
            for (size_t i = 0; i < count; ++i) {
                const uint32_t id = ids[i];
                if (id < slots_.size() && slots_[id].allocated) {
                    slots_[id].visible = (results[i] != 0);
                    slots_[id].lastResultFrame = frameCounter_;
                }
            }
            const D3D12_RANGE writtenRange{ 0, 0 };
            readback_[recordingFrameIndex]->Unmap(0, &writtenRange);
        }
        ringPending_[recordingFrameIndex] = false;
    }

    uint32_t HiZOcclusionSystem::RegisterTarget()
    {
        if (!freeIds_.empty()) {
            const uint32_t id = freeIds_.back();
            freeIds_.pop_back();
            slots_[id] = TargetSlot{}; // 再利用時は「未測定・可視」へ戻す
            slots_[id].allocated = true;
            return id;
        }
        if (slots_.size() >= kMaxQueries) {
            return kInvalidId; // 満杯。未登録モデルは常に可視扱いになるだけで安全
        }
        slots_.emplace_back();
        slots_.back().allocated = true;
        return static_cast<uint32_t>(slots_.size() - 1);
    }

    void HiZOcclusionSystem::UnregisterTarget(uint32_t id)
    {
        if (id >= slots_.size() || !slots_[id].allocated) {
            return;
        }
        slots_[id] = TargetSlot{};
        freeIds_.push_back(id);
    }

    void HiZOcclusionSystem::SubmitBounds(uint32_t id, const BoundingBox& worldBounds)
    {
        if (id == kInvalidId || pendingQueries_.size() >= kMaxQueries) {
            return; // あふれた分は判定されず stale 経由で可視のまま維持される
        }
        PendingQuery query{};
        query.id = id;
        query.bounds.minX = worldBounds.min.x;
        query.bounds.minY = worldBounds.min.y;
        query.bounds.minZ = worldBounds.min.z;
        query.bounds.maxX = worldBounds.max.x;
        query.bounds.maxY = worldBounds.max.y;
        query.bounds.maxZ = worldBounds.max.z;
        pendingQueries_.push_back(query);
    }

    bool HiZOcclusionSystem::IsVisible(uint32_t id) const
    {
        if (!enabled_ || id >= slots_.size() || !slots_[id].allocated) {
            return true;
        }
        const TargetSlot& slot = slots_[id];
        // 未測定、または長く判定対象から外れている（フラスタム外だった等）場合は可視へ戻す
        if (slot.lastResultFrame == 0
            || frameCounter_ - slot.lastResultFrame > kResultStaleFrames) {
            return true;
        }
        return slot.visible;
    }

    void HiZOcclusionSystem::Shutdown()
    {
        // GPU リソースをデバイス破棄前に解放する（LeakChecker 対策）。
        // Shutdown 後に ~Model が UnregisterTarget を呼んでも slots_ が空なので安全。
        hiZTexture_.Reset();
        visibilityBuffer_.Reset();
        if (buildParamsBuffer_ && buildParamsMapped_) {
            buildParamsBuffer_->Unmap(0, nullptr);
        }
        buildParamsBuffer_.Reset();
        buildParamsMapped_ = nullptr;

        for (uint32_t i = 0; i < kFrameRing; ++i) {
            if (boundsUpload_[i] && boundsMapped_[i]) {
                boundsUpload_[i]->Unmap(0, nullptr);
            }
            boundsUpload_[i].Reset();
            boundsMapped_[i] = nullptr;
            if (cullParamsUpload_[i] && cullParamsMapped_[i]) {
                cullParamsUpload_[i]->Unmap(0, nullptr);
            }
            cullParamsUpload_[i].Reset();
            cullParamsMapped_[i] = nullptr;
            readback_[i].Reset();
            ringIds_[i].clear();
            ringPending_[i] = false;
        }

        buildPso_.Reset();
        cullPso_.Reset();
        buildRootSignatureMg_.reset();
        cullRootSignatureMg_.reset();
        buildReflectionData_.reset();
        cullReflectionData_.reset();
        reflectionBuilder_.reset();
        shaderCompiler_.reset();

        slots_.clear();
        freeIds_.clear();
        pendingQueries_.clear();
        descriptorManager_ = nullptr;
        dxCommon_ = nullptr;
        hiZWidth_ = hiZHeight_ = hiZMipCount_ = 0;
        depthWidth_ = depthHeight_ = 0;
        initialized_ = false;
        initializeFailed_ = false;
        collectEnabled_ = false;
    }

    // ---------------------------------------------------------------
    // 初期化
    // ---------------------------------------------------------------

    bool HiZOcclusionSystem::InitializeIfNeeded(GraphicsCore* dxCommon)
    {
        if (initialized_) {
            return true;
        }
        if (initializeFailed_ || !dxCommon) {
            return false;
        }

        dxCommon_ = dxCommon;
        descriptorManager_ = dxCommon->GetDescriptorManager();
        ID3D12Device* device = dxCommon->GetDevice();
        if (!device || !descriptorManager_) {
            initializeFailed_ = true;
            return false;
        }

        if (!BuildPipelines(device) || !CreateBuffers(device)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "HiZOcclusionSystem: 初期化に失敗したためオクルージョンカリングを無効化します");
            initializeFailed_ = true;
            return false;
        }

        slots_.reserve(256);
        pendingQueries_.reserve(256);
        initialized_ = true;
        return true;
    }

    bool HiZOcclusionSystem::BuildPipelines(ID3D12Device* device)
    {
        shaderCompiler_ = std::make_unique<ShaderCompiler>();
        reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();
        buildRootSignatureMg_ = std::make_unique<RootSignatureManager>();
        cullRootSignatureMg_ = std::make_unique<RootSignatureManager>();

        shaderCompiler_->Initialize();
        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());

        // RootSignature 構成は SkinningComputeDispatcher と同じ:
        // CBV は RootDescriptor、SRV/UAV は DescriptorTable
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);

        // ---- Hi-Z 構築 CS ----
        IDxcBlob* buildBlob = shaderCompiler_->CompileShader(
            L"Engine/Assets/Shaders/Culling/HiZBuild.CS.hlsl", L"cs_6_0");
        if (!buildBlob) {
            return false;
        }
        buildReflectionData_ = reflectionBuilder_->BuildFromComputeShader(buildBlob, "HiZBuild");
        if (!buildReflectionData_) {
            return false;
        }
        if (!buildRootSignatureMg_->Build(device, *buildReflectionData_, config).success) {
            return false;
        }
        buildPso_ = ComputePipelineUtil::Create(
            device, buildRootSignatureMg_->GetRootSignature(), buildBlob, "HiZ_Build");
        if (!buildPso_) {
            return false;
        }
        buildSourceIdx_ = buildReflectionData_->GetRootParameterIndexByName("gSourceDepth");
        buildDestIdx_ = buildReflectionData_->GetRootParameterIndexByName("gDestHiZ");
        buildParamsIdx_ = buildReflectionData_->GetRootParameterIndexByName("HiZBuildParams");
        if (buildSourceIdx_ < 0 || buildDestIdx_ < 0 || buildParamsIdx_ < 0) {
            return false;
        }

        // ---- 遮蔽判定 CS ----
        IDxcBlob* cullBlob = shaderCompiler_->CompileShader(
            L"Engine/Assets/Shaders/Culling/HiZOcclusionCull.CS.hlsl", L"cs_6_0");
        if (!cullBlob) {
            return false;
        }
        cullReflectionData_ = reflectionBuilder_->BuildFromComputeShader(cullBlob, "HiZOcclusionCull");
        if (!cullReflectionData_) {
            return false;
        }
        if (!cullRootSignatureMg_->Build(device, *cullReflectionData_, config).success) {
            return false;
        }
        cullPso_ = ComputePipelineUtil::Create(
            device, cullRootSignatureMg_->GetRootSignature(), cullBlob, "HiZ_OcclusionCull");
        if (!cullPso_) {
            return false;
        }
        cullBoundsIdx_ = cullReflectionData_->GetRootParameterIndexByName("gBounds");
        cullHiZIdx_ = cullReflectionData_->GetRootParameterIndexByName("gHiZ");
        cullVisibilityIdx_ = cullReflectionData_->GetRootParameterIndexByName("gVisibility");
        cullParamsIdx_ = cullReflectionData_->GetRootParameterIndexByName("HiZCullParams");
        return cullBoundsIdx_ >= 0 && cullHiZIdx_ >= 0
            && cullVisibilityIdx_ >= 0 && cullParamsIdx_ >= 0;
    }

    bool HiZOcclusionSystem::CreateBuffers(ID3D12Device* device)
    {
        // 構築 CS 定数（ミップ段ごとの 256B スロット、永続マップ）
        buildParamsBuffer_ = ResourceFactory::CreateBufferResource(
            device, kCbSlotSize * kMaxHiZMips);
        if (FAILED(buildParamsBuffer_->Map(
                0, nullptr, reinterpret_cast<void**>(&buildParamsMapped_)))) {
            return false;
        }

        // 可視フラグバッファ（Default ヒープ + UAV）
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = sizeof(uint32_t) * kMaxQueries;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            if (FAILED(device->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                    IID_PPV_ARGS(&visibilityBuffer_)))) {
                return false;
            }
            visibilityState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = kMaxQueries;
            uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
            descriptorManager_->CreateUAV(visibilityBuffer_.Get(), uavDesc,
                visibilityUavCpu_, visibilityUavGpu_, "HiZOcclusionVisibility");
        }

        // フレームリング: AABB アップロード + 判定 CS 定数 + Readback
        for (uint32_t i = 0; i < kFrameRing; ++i) {
            boundsUpload_[i] = ResourceFactory::CreateBufferResource(
                device, sizeof(BoundsGPU) * kMaxQueries);
            if (FAILED(boundsUpload_[i]->Map(
                    0, nullptr, reinterpret_cast<void**>(&boundsMapped_[i])))) {
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = kMaxQueries;
            srvDesc.Buffer.StructureByteStride = sizeof(BoundsGPU);
            descriptorManager_->CreateSRV(boundsUpload_[i].Get(), srvDesc,
                boundsSrvCpu_[i], boundsSrvGpu_[i], "HiZOcclusionBounds");

            cullParamsUpload_[i] = ResourceFactory::CreateBufferResource(
                device, sizeof(CullParamsGPU));
            if (FAILED(cullParamsUpload_[i]->Map(
                    0, nullptr, reinterpret_cast<void**>(&cullParamsMapped_[i])))) {
                return false;
            }

            readback_[i] = ResourceFactory::CreateBufferResource(
                device, sizeof(uint32_t) * kMaxQueries, D3D12_HEAP_TYPE_READBACK);
            ringIds_[i].reserve(256);
        }
        return true;
    }

    bool HiZOcclusionSystem::EnsureHiZResources(
        ID3D12Device* device, uint32_t depthWidth, uint32_t depthHeight)
    {
        if (depthWidth == 0 || depthHeight == 0) {
            return false;
        }
        if (hiZTexture_ && depthWidth_ == depthWidth && depthHeight_ == depthHeight) {
            return true;
        }

        depthWidth_ = depthWidth;
        depthHeight_ = depthHeight;
        hiZWidth_ = (std::max)(depthWidth >> 1, 1u);
        hiZHeight_ = (std::max)(depthHeight >> 1, 1u);

        // フルミップチェーン数を算出（最大辺が 1 になるまで）
        uint32_t mipCount = 1;
        for (uint32_t size = (std::max)(hiZWidth_, hiZHeight_); size > 1; size >>= 1) {
            ++mipCount;
        }
        hiZMipCount_ = (std::min)(mipCount, kMaxHiZMips);

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = hiZWidth_;
        desc.Height = hiZHeight_;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = static_cast<UINT16>(hiZMipCount_);
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // フレーム間は全ミップ UNORDERED_ACCESS で均一化する運用
        hiZTexture_ = ResourceFactory::CreateTextureResource(
            device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!hiZTexture_) {
            return false;
        }

        // ミップ単位 SRV / UAV（リサイズ時は既存スロットへ書き直す）
        for (uint32_t mip = 0; mip < hiZMipCount_; ++mip) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = mip;
            srvDesc.Texture2D.MipLevels = 1;
            descriptorManager_->CreateOrUpdateSRV(hiZTexture_.Get(), srvDesc,
                hiZMipSrvCpu_[mip], hiZMipSrvGpu_[mip], "HiZPyramidMipSRV");

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = mip;
            descriptorManager_->CreateOrUpdateUAV(hiZTexture_.Get(), uavDesc,
                hiZMipUavCpu_[mip], hiZMipUavGpu_[mip], "HiZPyramidMipUAV");
        }

        // 判定 CS 用フルチェーン SRV
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = hiZMipCount_;
            descriptorManager_->CreateOrUpdateSRV(hiZTexture_.Get(), srvDesc,
                hiZFullSrvCpu_, hiZFullSrvGpu_, "HiZPyramidFullSRV");
        }

        // 構築 CS 定数を再充填（dispatch d: mip(d) を書く。d=0 のソースはフル解像度深度）
        for (uint32_t mip = 0; mip < hiZMipCount_; ++mip) {
            BuildParamsGPU params{};
            params.destW = (std::max)(hiZWidth_ >> mip, 1u);
            params.destH = (std::max)(hiZHeight_ >> mip, 1u);
            if (mip == 0) {
                params.srcW = depthWidth_;
                params.srcH = depthHeight_;
            } else {
                params.srcW = (std::max)(hiZWidth_ >> (mip - 1), 1u);
                params.srcH = (std::max)(hiZHeight_ >> (mip - 1), 1u);
            }
            std::memcpy(buildParamsMapped_ + kCbSlotSize * mip, &params, sizeof(params));
        }
        return true;
    }

    // ---------------------------------------------------------------
    // GPU 実行
    // ---------------------------------------------------------------

    void HiZOcclusionSystem::ExecuteCulling(
        ID3D12GraphicsCommandList* cmdList,
        GraphicsCore* dxCommon,
        ID3D12Resource* depthResource,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSRV)
    {
        if (!enabled_ || !cmdList || !dxCommon || !depthResource || depthSRV.ptr == 0) {
            return;
        }
        if (!InitializeIfNeeded(dxCommon)) {
            return;
        }

        CommandManager* commandManager = dxCommon->GetCommandManager();
        if (!commandManager) {
            return;
        }
        const uint32_t frameIndex = commandManager->GetRecordingFrameIndex();
        if (frameIndex >= kFrameRing) {
            return;
        }

        const D3D12_RESOURCE_DESC depthDesc = depthResource->GetDesc();
        if (!EnsureHiZResources(dxCommon->GetDevice(),
                static_cast<uint32_t>(depthDesc.Width), depthDesc.Height)) {
            return;
        }

        const uint32_t queryCount =
            static_cast<uint32_t>((std::min)(pendingQueries_.size(),
                static_cast<size_t>(kMaxQueries)));
        EngineStats::GetInstance().RecordOcclusionTested(queryCount);
        if (queryCount == 0) {
            ringPending_[frameIndex] = false;
            return;
        }

        // 1) AABB と定数のアップロード（永続マップ済みリングスロットへ書くだけ）
        std::vector<uint32_t>& ids = ringIds_[frameIndex];
        ids.clear();
        for (uint32_t i = 0; i < queryCount; ++i) {
            boundsMapped_[frameIndex][i] = pendingQueries_[i].bounds;
            ids.push_back(pendingQueries_[i].id);
        }

        CullParamsGPU cullParams{};
        cullParams.viewProj = viewProj_;
        cullParams.hiZWidth = static_cast<float>(hiZWidth_);
        cullParams.hiZHeight = static_cast<float>(hiZHeight_);
        cullParams.mipCount = hiZMipCount_;
        cullParams.queryCount = queryCount;
        cullParams.depthBias = kDepthBias;
        cullParams.minRectTexels = kMinRectTexels;
        *cullParamsMapped_[frameIndex] = cullParams;

        // ディスクリプタヒープはパス実行順に依存しないよう自前でバインドする（パス分離契約 2）
        ID3D12DescriptorHeap* heaps[] = { dxCommon->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);

        // 2) Hi-Z ピラミッド構築（深度 → mip0 → … → 最終ミップ）
        BuildHiZPyramid(cmdList, depthSRV);

        // 3) 遮蔽判定 + Readback コピー
        DispatchCull(cmdList, frameIndex, queryCount);

        ringPending_[frameIndex] = true;
    }

    void HiZOcclusionSystem::BuildHiZPyramid(
        ID3D12GraphicsCommandList* cmdList, D3D12_GPU_DESCRIPTOR_HANDLE depthSRV)
    {
        cmdList->SetPipelineState(buildPso_.Get());
        cmdList->SetComputeRootSignature(buildRootSignatureMg_->GetRootSignature());

        // ミップ単位の遷移はリソース単位追跡の ResourceBarrierHelper では扱えないため、
        // ここで直接バリアを発行する。
        // 関数の入口で全ミップ UNORDERED_ACCESS、出口で全ミップ NON_PIXEL_SHADER_RESOURCE。
        auto transitionMip = [&](uint32_t mip,
            D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = hiZTexture_.Get();
            barrier.Transition.Subresource = mip;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            cmdList->ResourceBarrier(1, &barrier);
        };

        for (uint32_t mip = 0; mip < hiZMipCount_; ++mip) {
            if (mip == 0) {
                // 深度（NON_PIXEL_SHADER_RESOURCE 状態は RenderGraph の Read 宣言で保証済み）→ mip0
                cmdList->SetComputeRootDescriptorTable(
                    static_cast<UINT>(buildSourceIdx_), depthSRV);
            } else {
                // 読み側の 1 段上のミップだけ SRV 状態へ。書き込み先ミップは UAV のまま
                transitionMip(mip - 1,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                cmdList->SetComputeRootDescriptorTable(
                    static_cast<UINT>(buildSourceIdx_), hiZMipSrvGpu_[mip - 1]);
            }
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(buildDestIdx_), hiZMipUavGpu_[mip]);
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(buildParamsIdx_),
                buildParamsBuffer_->GetGPUVirtualAddress() + kCbSlotSize * mip);

            const uint32_t mipW = (std::max)(hiZWidth_ >> mip, 1u);
            const uint32_t mipH = (std::max)(hiZHeight_ >> mip, 1u);
            cmdList->Dispatch((mipW + 7) / 8, (mipH + 7) / 8, 1);

            // 次の段（および判定 CS）がこのミップを読むため書き込み完了を保証する
            ResourceBarrierHelper::UAV(cmdList, hiZTexture_.Get());
        }

        // 最終ミップも SRV 状態へ揃え、全ミップを判定 CS から読めるようにする
        transitionMip(hiZMipCount_ - 1,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void HiZOcclusionSystem::DispatchCull(
        ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex, uint32_t queryCount)
    {
        ResourceBarrierHelper::Transition(cmdList, visibilityBuffer_.Get(),
            visibilityState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(cullPso_.Get());
        cmdList->SetComputeRootSignature(cullRootSignatureMg_->GetRootSignature());
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(cullBoundsIdx_), boundsSrvGpu_[frameIndex]);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(cullHiZIdx_), hiZFullSrvGpu_);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(cullVisibilityIdx_), visibilityUavGpu_);
        cmdList->SetComputeRootConstantBufferView(
            static_cast<UINT>(cullParamsIdx_),
            cullParamsUpload_[frameIndex]->GetGPUVirtualAddress());

        cmdList->Dispatch((queryCount + 63) / 64, 1, 1);

        // 判定結果を Readback バッファへコピー（CPU は同リングインデックスの次回フレームで読む）
        ResourceBarrierHelper::UAV(cmdList, visibilityBuffer_.Get());
        ResourceBarrierHelper::Transition(cmdList, visibilityBuffer_.Get(),
            visibilityState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyBufferRegion(readback_[frameIndex].Get(), 0,
            visibilityBuffer_.Get(), 0, sizeof(uint32_t) * queryCount);
        ResourceBarrierHelper::Transition(cmdList, visibilityBuffer_.Get(),
            visibilityState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // Hi-Z を次フレームの構築に備えて全ミップ UNORDERED_ACCESS へ戻す
        for (uint32_t mip = 0; mip < hiZMipCount_; ++mip) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = hiZTexture_.Get();
            barrier.Transition.Subresource = mip;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            cmdList->ResourceBarrier(1, &barrier);
        }
    }
}
