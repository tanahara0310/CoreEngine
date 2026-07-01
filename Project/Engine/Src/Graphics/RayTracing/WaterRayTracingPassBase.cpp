#include "pch.h"
#include "WaterRayTracingPassBase.h"

#include <algorithm>
#include <cstring>

#include "AccelerationStructureManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    bool WaterRayTracingPassBase::InitializeBase(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr,
        const char* ownerName)
    {
        dxCommon_ = dxCommon;
        descriptorManager_ = descriptorManager;
        asMgr_ = asMgr;

        if (!dxCommon_ || !descriptorManager_ || !asMgr_ || !asMgr_->IsSupported()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: DXR unsupported. initialization skipped.",
                ownerName ? ownerName : "WaterRayTracingPassBase");
            return false;
        }

        return true;
    }

    bool WaterRayTracingPassBase::EnsureSurfaceConstantBuffer(UINT bufferSize, const char* ownerName)
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
                ownerName ? ownerName : "WaterRayTracingPassBase",
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
                ownerName ? ownerName : "WaterRayTracingPassBase",
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
        const char* ownerName,
        const std::string& uavDebugName,
        const std::string& srvDebugName,
        DXGI_FORMAT format)
    {
        return outputViews_.EnsureTexture(
            dxCommon_,
            descriptorManager_,
            width,
            height,
            viewIndex,
            ownerName,
            uavDebugName,
            srvDebugName,
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
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
        const char* ownerName) const
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
                ownerName ? ownerName : "WaterRayTracingPassBase");
            return false;
        }
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

    bool WaterRayTracingPassBase::PrepareDispatchResources(
        ID3D12GraphicsCommandList* cmdList,
        UINT width,
        UINT height,
        uint32_t viewIndex,
        UINT constantBufferSize,
        const char* ownerName,
        const std::string& uavDebugName,
        const std::string& srvDebugName,
        DispatchGuardStatus& outStatus,
        D3D12_GPU_DESCRIPTOR_HANDLE& outOutputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& outOutputUavHandle,
        ID3D12Resource*& outOutputResource,
        D3D12_RESOURCE_STATES*& outOutputCurrentState,
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>& outCmdList4,
        DXGI_FORMAT format)
    {
        outStatus = ValidateDispatchPreconditions(cmdList);
        if (outStatus != DispatchGuardStatus::Ok) {
            return false;
        }

        if (!EnsureOutputTextureBase(
            width,
            height,
            viewIndex,
            ownerName,
            uavDebugName,
            srvDebugName,
            format)) {
            outStatus = DispatchGuardStatus::OutputAllocationFailed;
            return false;
        }

        if (!EnsureSurfaceConstantBuffer(constantBufferSize, ownerName)) {
            outStatus = DispatchGuardStatus::OutputAllocationFailed;
            return false;
        }

        if (!QueryCommandList4(cmdList, outCmdList4, ownerName)) {
            outStatus = DispatchGuardStatus::CommandList4Unavailable;
            return false;
        }

        outOutputSrvHandle = GetOutputSRVHandleBase(viewIndex);
        outOutputUavHandle = outputViews_.GetUAVHandle(viewIndex);
        outOutputResource = GetOutputResourceBase(viewIndex);
        outOutputCurrentState = &GetOutputCurrentStateBase(viewIndex);
        outStatus = DispatchGuardStatus::Ok;
        return true;
    }

    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::BuildSurfaceConstants(
        const WaterSurfaceData& surfaceData) const
    {
        WaterSurfaceConstants surfaceConstants{};
        surfaceConstants.waterHeight = surfaceData.waterHeight;
        surfaceConstants.activeWaveCount = (std::min)(surfaceData.activeWaveCount, kMaxWaterSurfaceWaveCount);
        surfaceConstants.time = surfaceData.time;
        for (uint32_t waveIndex = 0; waveIndex < surfaceConstants.activeWaveCount; ++waveIndex) {
            surfaceConstants.waves[waveIndex] = surfaceData.waves[waveIndex];
        }
        return surfaceConstants;
    }

    void WaterRayTracingPassBase::UploadSurfaceConstants(
        const WaterSurfaceConstants& surfaceConstants,
        const char* ownerName) const
    {
        if (!constantBufferMapped_) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: surface constant upload skipped. constant buffer is not mapped.",
                ownerName ? ownerName : "WaterRayTracingPassBase");
            return;
        }

        std::memcpy(constantBufferMapped_, &surfaceConstants, sizeof(surfaceConstants));
    }

    void WaterRayTracingPassBase::SetSurfaceModelProviderBase(const IWaterSurfaceModelProvider* provider)
    {
        surfaceModelProvider_ = provider;
    }

    const IWaterSurfaceModelProvider* WaterRayTracingPassBase::GetSurfaceModelProviderBase() const
    {
        return surfaceModelProvider_;
    }

    const WaterSurfaceData& WaterRayTracingPassBase::ResolveSurfaceDataForDispatch(
        const WaterSurfaceData& fallbackSurfaceData,
        WaterSurfaceData& outResolvedSurfaceData,
        const char* ownerName) const
    {
        if (!surfaceModelProvider_) {
            return fallbackSurfaceData;
        }

        if (!surfaceModelProvider_->TryGetSurfaceData(outResolvedSurfaceData)) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: surface model provider returned no data. fallback path is used. provider='{}' type={}",
                ownerName ? ownerName : "WaterRayTracingPassBase",
                surfaceModelProvider_->GetProviderName(),
                static_cast<uint32_t>(surfaceModelProvider_->GetSimulationType()));
            return fallbackSurfaceData;
        }

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "{}: surface model provider resolved. provider='{}' type={} waterHeight={:.3f} activeWaveCount={} time={:.3f}",
            ownerName ? ownerName : "WaterRayTracingPassBase",
            surfaceModelProvider_->GetProviderName(),
            static_cast<uint32_t>(surfaceModelProvider_->GetSimulationType()),
            outResolvedSurfaceData.waterHeight,
            outResolvedSurfaceData.activeWaveCount,
            outResolvedSurfaceData.time);
        return outResolvedSurfaceData;
    }
}
