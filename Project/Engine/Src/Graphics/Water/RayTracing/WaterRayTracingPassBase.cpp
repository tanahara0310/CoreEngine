#include "pch.h"
#include "WaterRayTracingPassBase.h"

#include <algorithm>
#include <cstring>

#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    const char* WaterRayTracingPassBase::ToString(DispatchStatus status)
    {
        switch (status) {
        case DispatchStatus::None: return "None";
        case DispatchStatus::NotInitialized: return "NotInitialized";
        case DispatchStatus::RayTracingUnsupported: return "RayTracingUnsupported";
        case DispatchStatus::NoBLAS: return "NoBLAS";
        case DispatchStatus::OutputAllocationFailed: return "OutputAllocationFailed";
        case DispatchStatus::CommandList4Unavailable: return "CommandList4Unavailable";
        case DispatchStatus::Dispatched: return "Dispatched";
        default: return "Unknown";
        }
    }

    bool WaterRayTracingPassBase::InitializeBase(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr,
        const char* ownerName,
        const char* outputDebugName)
    {
        ownerName_ = ownerName ? ownerName : "WaterRayTracingPassBase";
        outputDebugName_ = outputDebugName ? outputDebugName : "RTWaterOutput";

        dxCommon_ = dxCommon;
        descriptorManager_ = descriptorManager;
        asMgr_ = asMgr;

        if (!dxCommon_ || !descriptorManager_ || !asMgr_ || !asMgr_->IsSupported()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: DXR unsupported. initialization skipped.",
                ownerName_);
            return false;
        }

        return true;
    }

    bool WaterRayTracingPassBase::EnsureSurfaceConstantBuffer(UINT bufferSize)
    {
        if (constantBuffer_) {
            return true;
        }

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bufferSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&constantBuffer_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: constant buffer allocation failed. hr={:#x}",
                ownerName_,
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_RANGE readRange = { 0, 0 };
        hr = constantBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&constantBufferMapped_));
        if (FAILED(hr) || !constantBufferMapped_) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: constant buffer map failed. hr={:#x}",
                ownerName_,
                static_cast<uint32_t>(hr));
            constantBuffer_.Reset();
            constantBufferMapped_ = nullptr;
            return false;
        }

        std::memset(constantBufferMapped_, 0, bufferSize);
        return true;
    }

    bool WaterRayTracingPassBase::EnsureOutputTextureBase(
        UINT width,
        UINT height,
        uint32_t viewIndex,
        DXGI_FORMAT format)
    {
        const std::string suffix = "_v" + std::to_string(viewIndex);
        return outputViews_.EnsureTexture(
            dxCommon_,
            descriptorManager_,
            width,
            height,
            viewIndex,
            ownerName_,
            std::string(outputDebugName_) + "_UAV" + suffix,
            std::string(outputDebugName_) + "_SRV" + suffix,
            format);
    }

    void WaterRayTracingPassBase::ReleaseOutputIfSizeMismatchBase(UINT width, UINT height, uint32_t viewIndex)
    {
        outputViews_.ReleaseIfSizeMismatch(width, height, viewIndex);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterRayTracingPassBase::GetOutputSRVHandleBase(uint32_t viewIndex) const
    {
        return outputViews_.GetSRVHandle(viewIndex);
    }

    ID3D12Resource* WaterRayTracingPassBase::GetOutputResourceBase(uint32_t viewIndex) const
    {
        return outputViews_.GetResource(viewIndex);
    }

    D3D12_RESOURCE_STATES& WaterRayTracingPassBase::GetOutputCurrentStateBase(uint32_t viewIndex)
    {
        return outputViews_.GetCurrentState(viewIndex);
    }

    WaterRayTracingPassBase::DispatchGuardStatus WaterRayTracingPassBase::ValidateDispatchPreconditions(
        ID3D12GraphicsCommandList* cmdList) const
    {
        if (!isInitialized_) {
            return DispatchGuardStatus::NotInitialized;
        }
        if (!asMgr_ || !asMgr_->IsSupported()) {
            return DispatchGuardStatus::RayTracingUnsupported;
        }
        if (asMgr_->GetBLASCount() == 0) {
            return DispatchGuardStatus::NoBLAS;
        }
        if (!cmdList) {
            return DispatchGuardStatus::InvalidCommandList;
        }
        return DispatchGuardStatus::Ok;
    }

    bool WaterRayTracingPassBase::QueryCommandList4(
        ID3D12GraphicsCommandList* cmdList,
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>& cmdList4) const
    {
        cmdList4.Reset();
        if (!cmdList) {
            return false;
        }

        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "{}: QueryInterface(ID3D12GraphicsCommandList4) failed.",
                ownerName_);
            return false;
        }
        return true;
    }

    void WaterRayTracingPassBase::ReportDispatchGuardFailure(
        DispatchGuardStatus guardStatus,
        ID3D12GraphicsCommandList* cmdList)
    {
        switch (guardStatus) {
        case DispatchGuardStatus::NotInitialized:
            lastDiagnostics_.status = DispatchStatus::NotInitialized;
            break;
        case DispatchGuardStatus::RayTracingUnsupported:
            lastDiagnostics_.status = DispatchStatus::RayTracingUnsupported;
            break;
        case DispatchGuardStatus::NoBLAS:
            lastDiagnostics_.status = DispatchStatus::NoBLAS;
            break;
        case DispatchGuardStatus::InvalidCommandList:
        case DispatchGuardStatus::CommandList4Unavailable:
            lastDiagnostics_.status = DispatchStatus::CommandList4Unavailable;
            break;
        case DispatchGuardStatus::OutputAllocationFailed:
            lastDiagnostics_.status = DispatchStatus::OutputAllocationFailed;
            break;
        default:
            lastDiagnostics_.status = DispatchStatus::RayTracingUnsupported;
            break;
        }

        Logger::GetInstance().Warnf(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "{}: dispatch skipped. status={} initialized={} cmdList={} asMgr={} supported={}",
            ownerName_,
            ToString(lastDiagnostics_.status),
            isInitialized_,
            cmdList != nullptr,
            asMgr_ != nullptr,
            asMgr_ ? asMgr_->IsSupported() : false);
    }

    void WaterRayTracingPassBase::BeginDiagnostics(
        uint32_t viewIndex,
        UINT width,
        UINT height,
        const WaterSurfaceData& surfaceData,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV)
    {
        lastDiagnostics_ = {};
        lastDiagnostics_.viewIndex = viewIndex;
        lastDiagnostics_.waterHeight = surfaceData.waterHeight;
        lastDiagnostics_.activeWaveCount = surfaceData.activeWaveCount;
        lastDiagnostics_.width = width;
        lastDiagnostics_.height = height;
        lastDiagnostics_.sceneDepthSrv = sceneDepthSRV.ptr;
        lastDiagnostics_.sceneColorSrv = sceneColorSRV.ptr;
        lastDiagnostics_.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;
    }

    bool WaterRayTracingPassBase::BeginDispatch(
        ID3D12GraphicsCommandList* cmdList,
        UINT width,
        UINT height,
        uint32_t viewIndex,
        DispatchResources& outResources,
        DXGI_FORMAT format)
    {
        DispatchGuardStatus guardStatus = ValidateDispatchPreconditions(cmdList);
        if (guardStatus == DispatchGuardStatus::Ok) {
            if (!EnsureOutputTextureBase(width, height, viewIndex, format)) {
                guardStatus = DispatchGuardStatus::OutputAllocationFailed;
            } else if (!EnsureSurfaceConstantBuffer(GetSurfaceConstantBufferSize())) {
                guardStatus = DispatchGuardStatus::OutputAllocationFailed;
            } else if (!QueryCommandList4(cmdList, outResources.cmdList4)) {
                guardStatus = DispatchGuardStatus::CommandList4Unavailable;
            }
        }

        if (guardStatus != DispatchGuardStatus::Ok) {
            ReportDispatchGuardFailure(guardStatus, cmdList);
            return false;
        }

        outResources.outputSrvHandle = GetOutputSRVHandleBase(viewIndex);
        outResources.outputUavHandle = outputViews_.GetUAVHandle(viewIndex);
        outResources.outputResource = GetOutputResourceBase(viewIndex);
        outResources.outputCurrentState = &GetOutputCurrentStateBase(viewIndex);
        lastDiagnostics_.outputSrv = outResources.outputSrvHandle.ptr;
        return true;
    }

    void WaterRayTracingPassBase::BeginOutputWrite(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* outputResource,
        D3D12_RESOURCE_STATES& outputCurrentState) const
    {
        ResourceBarrierHelper::Transition(
            cmdList,
            outputResource,
            outputCurrentState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        outputCurrentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    void WaterRayTracingPassBase::EndOutputWrite(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* outputResource,
        D3D12_RESOURCE_STATES& outputCurrentState,
        D3D12_RESOURCE_STATES finalState) const
    {
        ResourceBarrierHelper::UAV(cmdList, outputResource);
        ResourceBarrierHelper::Transition(
            cmdList,
            outputResource,
            outputCurrentState,
            finalState);
        outputCurrentState = finalState;
    }

    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::BuildSurfaceConstants(
        const WaterSurfaceData& surfaceData) const
    {
        WaterSurfaceConstants surfaceConstants{};
        surfaceConstants.waterHeight = surfaceData.waterHeight;
        surfaceConstants.activeWaveCount = (std::min)(surfaceData.activeWaveCount, kMaxWaterSurfaceWaveCount);
        surfaceConstants.time = surfaceData.time;
        surfaceConstants.simulationType = surfaceData.simulationType;
        for (uint32_t waveIndex = 0; waveIndex < surfaceConstants.activeWaveCount; ++waveIndex) {
            surfaceConstants.waves[waveIndex] = surfaceData.waves[waveIndex];
        }
        return surfaceConstants;
    }

    void WaterRayTracingPassBase::UploadSurfaceConstants(const WaterSurfaceConstants& surfaceConstants) const
    {
        if (!constantBufferMapped_) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: surface constant upload skipped. constant buffer is not mapped.",
                ownerName_);
            return;
        }

        std::memcpy(constantBufferMapped_, &surfaceConstants, sizeof(surfaceConstants));
    }

    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::UploadSurfaceDataForDispatch(
        const WaterSurfaceData& surfaceData) const
    {
        const WaterSurfaceConstants surfaceConstants = BuildSurfaceConstants(surfaceData);
        UploadSurfaceConstants(surfaceConstants);
        return surfaceConstants;
    }

    void WaterRayTracingPassBase::SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider)
    {
        surfaceModelProvider_ = provider;
    }

    std::shared_ptr<const IWaterSurfaceModelProvider> WaterRayTracingPassBase::GetSurfaceModelProvider() const
    {
        return surfaceModelProvider_.lock();
    }

    const WaterSurfaceData& WaterRayTracingPassBase::ResolveSurfaceDataForDispatch(
        const WaterSurfaceData& fallbackSurfaceData,
        WaterSurfaceData& outResolvedSurfaceData) const
    {
        const std::shared_ptr<const IWaterSurfaceModelProvider> surfaceModelProvider = surfaceModelProvider_.lock();
        if (!surfaceModelProvider) {
            return fallbackSurfaceData;
        }

        if (!surfaceModelProvider->TryGetSurfaceData(outResolvedSurfaceData)) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: surface model provider returned no data. fallback path is used. provider='{}' type={}",
                ownerName_,
                surfaceModelProvider->GetProviderName(),
                static_cast<uint32_t>(surfaceModelProvider->GetSimulationType()));
            return fallbackSurfaceData;
        }

        // 以前はここで毎フレーム Infof を出していたが、3 マネージャ分が常時流れて
        // ログを埋めるだけだったため撤去した（解決結果は GetLastDiagnostics で参照できる）。
        return outResolvedSurfaceData;
    }
}
