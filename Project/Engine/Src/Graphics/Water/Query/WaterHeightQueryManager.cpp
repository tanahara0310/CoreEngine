#include "pch.h"
#include "WaterHeightQueryManager.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <iterator>

#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        namespace QueryBind {
            enum Slot : size_t {
                WaterHeightQueryConstants, WaterSurfaceData, gQueryDisplacement, gQueryResults, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "WaterHeightQueryConstants", ShaderBindingType::CBV, BindingUsage::Required },
                { "WaterSurfaceData",          ShaderBindingType::CBV, BindingUsage::Required },
                { "gQueryDisplacement",        ShaderBindingType::SRV, BindingUsage::Required },
                { "gQueryResults",             ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        constexpr UINT64 kResultBytes = sizeof(WaterQuery::GpuResult) * WaterQuery::kMaxPoints;
        constexpr UINT64 kReadbackBytes = kResultBytes * WaterQuery::kMaxSurfaces;
        constexpr uint32_t kThreadGroupSize = 64;
    }

    bool WaterHeightQueryManager::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        ShaderProgramCache* shaderProgramCache,
        WaterQuery* query)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        shaderProgramCache_ = shaderProgramCache;
        query_ = query;
        if (!dxCommon_ || !descriptorAllocator_ || !shaderProgramCache_ || !query_) {
            return false;
        }
        isInitialized_ = CreatePipeline() && CreateBuffers();
        return isInitialized_;
    }

    bool WaterHeightQueryManager::CreatePipeline()
    {
        Logger& log = Logger::GetInstance();
        // 海の範囲マップをヒープから番号で引く（cs_6_6）
        const ShaderProgram* program = shaderProgramCache_->GetOrCreateCompute(
            L"Engine/Assets/Shaders/Water/Query/WaterHeightQuery.CS.hlsl", "WaterHeightQuery", L"cs_6_6");
        if (!program) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterHeightQueryManager: 水面の高さの問い合わせのシェーダーをコンパイルできませんでした");
            return false;
        }

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
        const auto buildResult = rootSignature_.Build(dxCommon_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterHeightQueryManager: ルートシグネチャを作れませんでした: {}", buildResult.errorMessage);
            return false;
        }

        try {
            bindings_ = BindingTable::Resolve(program->GetReflection(), QueryBind::kDecls, "WaterHeightQuery");
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterHeightQueryManager: シェーダーの結線が宣言と合いません: {}", e.what());
            return false;
        }

        pipelineState_ = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), rootSignature_.GetRootSignature(), program->GetCS(), "WaterHeightQuery");
        return pipelineState_ != nullptr;
    }

    bool WaterHeightQueryManager::CreateBuffers()
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kResultBytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if (FAILED(device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&buffer)))) {
            return false;
        }
        results_.Reset(std::move(buffer), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = static_cast<UINT>(kResultBytes / sizeof(uint32_t));
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        resultsUav_ = descriptorAllocator_->CreateUAV(results_.Get(), uavDesc, "WaterHeightQueryResultsUAV");
        if (!resultsUav_.IsValid()) {
            return false;
        }

        try {
            for (auto& readback : readback_) {
                readback = ResourceFactory::CreateBufferResource(
                    device, static_cast<size_t>(kReadbackBytes), D3D12_HEAP_TYPE_READBACK);
            }
        }
        catch (const std::exception&) {
            return false;
        }
        resultScratch_.resize(static_cast<size_t>(WaterQuery::kMaxPoints) * WaterQuery::kMaxSurfaces);
        return true;
    }

    void WaterHeightQueryManager::CollectResults()
    {
        if (!isInitialized_) {
            return;
        }
        // 古い枠から順に返す（新しい結果ほど後に入る）
        const uint32_t framesInFlight = dxCommon_->Frame().FramesInFlight();
        const uint32_t current = dxCommon_->Frame().FrameIndex();
        for (uint32_t offset = 1; offset <= framesInFlight; ++offset) {
            CollectFrame((current + offset) % framesInFlight);
        }
    }

    void WaterHeightQueryManager::CollectFrame(uint32_t frameIndex)
    {
        PendingFrame& frame = pendingFrames_[frameIndex];
        if (!frame.pending) {
            return;
        }
        const FrameSync& sync = dxCommon_->Frame();
        const uint64_t fence = sync.FenceValueOf(frameIndex);
        if (fence == frame.fenceBeforeSubmit || sync.CompletedValue() < fence) {
            return;
        }
        frame.pending = false;

        const size_t count = frame.submitted.size() * frame.surfaceCount;
        if (count == 0) {
            return;
        }
        void* mapped = nullptr;
        const D3D12_RANGE readRange{ 0, sizeof(WaterQuery::GpuResult) * count };
        if (FAILED(readback_[frameIndex]->Map(0, &readRange, &mapped))) {
            return;
        }
        std::memcpy(resultScratch_.data(), mapped, sizeof(WaterQuery::GpuResult) * count);
        const D3D12_RANGE writtenRange{ 0, 0 };
        readback_[frameIndex]->Unmap(0, &writtenRange);

        query_->ApplyResults(
            frame.submitted, std::span<const WaterQuery::GpuResult>(resultScratch_.data(), count), frame.surfaceCount);
    }

    void WaterHeightQueryManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        const WaterSurfaceData* primarySurface,
        std::span<const WaterSurfaceData> otherSurfaces,
        D3D12_GPU_DESCRIPTOR_HANDLE fallbackDisplacementSRV)
    {
        if (!isInitialized_ || !cmdList) {
            return;
        }
        CollectResults();

        const uint32_t frameIndex = dxCommon_->Frame().FrameIndex();
        PendingFrame& frame = pendingFrames_[frameIndex];
        // この枠に投げたまま GPU へ届かなかった記録は捨てる
        frame.pending = false;

        // 範囲のある水面だけ評価する（主の水が先頭）
        surfaceScratch_.clear();
        if (primarySurface && primarySurface->regionValid != 0) {
            surfaceScratch_.push_back(primarySurface);
        }
        for (const WaterSurfaceData& surface : otherSurfaces) {
            if (surface.regionValid != 0) {
                surfaceScratch_.push_back(&surface);
            }
        }

        // 水面の無いフレームは、届く前の結果も含めて捨てる
        if (surfaceScratch_.empty()) {
            for (PendingFrame& other : pendingFrames_) {
                other.pending = false;
            }
            query_->ClearResults();
            return;
        }
        if (surfaceScratch_.size() > WaterQuery::kMaxSurfaces) {
            if (!warnedSurfaceLimit_) {
                Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "WaterHeightQueryManager: 水面が {} 枚を超えたので、超えた分の水面の高さは調べません", WaterQuery::kMaxSurfaces);
                warnedSurfaceLimit_ = true;
            }
            surfaceScratch_.resize(WaterQuery::kMaxSurfaces);
        }

        const uint32_t pointCount = query_->GetPointCount();
        if (pointCount == 0) {
            return;
        }

        QueryConstants constants{};
        constants.pointCount = pointCount;
        query_->Snapshot(frame.submitted);
        for (uint32_t i = 0; i < pointCount; ++i) {
            Vector4& packed = constants.points[i / 2];
            if ((i & 1u) == 0) {
                packed.x = frame.submitted[i].x;
                packed.y = frame.submitted[i].z;
            } else {
                packed.z = frame.submitted[i].x;
                packed.w = frame.submitted[i].z;
            }
        }

        UploadRing& uploadRing = dxCommon_->GetUploadRing();
        const D3D12_GPU_VIRTUAL_ADDRESS queryConstants = uploadRing.AllocateConstants(constants);

        cmdList->SetPipelineState(pipelineState_.Get());
        cmdList->SetComputeRootSignature(rootSignature_.GetRootSignature());
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);

        // 水面ごとに評価し、読み戻し用のバッファの水面ごとの場所へ写す。CPU は GPU がこのフレームを終えてから読む
        const UINT64 surfaceResultBytes = sizeof(WaterQuery::GpuResult) * pointCount;
        uint32_t surfaceCount = 0;
        for (const WaterSurfaceData* const surface : surfaceScratch_) {
            // Gerstner の波面のときも、変位テクスチャのスロットには有効な SRV を差す
            WaterRayTracingPassBase::FFTOceanInput fftOceanInput = surface->fftOcean;
            if (fftOceanInput.displacementSRV.ptr == 0) {
                fftOceanInput.displacementSRV = fallbackDisplacementSRV;
            }
            if (fftOceanInput.displacementSRV.ptr == 0) {
                continue;
            }
            const D3D12_GPU_VIRTUAL_ADDRESS surfaceConstants = uploadRing.AllocateConstants(
                WaterRayTracingPassBase::BuildSurfaceConstants(*surface, fftOceanInput));

            Barrier::Transition(cmdList, results_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            binder.Set(bindings_[QueryBind::WaterHeightQueryConstants], queryConstants);
            binder.Set(bindings_[QueryBind::WaterSurfaceData], surfaceConstants);
            binder.Set(bindings_[QueryBind::gQueryDisplacement], fftOceanInput.displacementSRV);
            binder.Set(bindings_[QueryBind::gQueryResults], resultsUav_.gpuHandle);
            binder.ValidateBeforeDraw(bindings_);
            cmdList->Dispatch((pointCount + kThreadGroupSize - 1) / kThreadGroupSize, 1, 1);

            Barrier::Transition(cmdList, results_, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmdList->CopyBufferRegion(
                readback_[frameIndex].Get(), surfaceResultBytes * surfaceCount, results_.Get(), 0, surfaceResultBytes);
            ++surfaceCount;
        }
        Barrier::Transition(cmdList, results_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (surfaceCount == 0) {
            return;
        }

        frame.pending = true;
        frame.surfaceCount = surfaceCount;
        frame.fenceBeforeSubmit = dxCommon_->Frame().FenceValueOf(frameIndex);
    }
}
