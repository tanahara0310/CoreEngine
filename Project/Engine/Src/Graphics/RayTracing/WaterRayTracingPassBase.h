#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <string>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Graphics/Water/Simulation/WaterSurfaceModelProvider.h"
#include "GlobalRootSignatureManager.h"
#include "RayTracingOutputViewSet.h"
#include "ShaderTableBuilder.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    class WaterRayTracingPassBase {
    protected:
        struct alignas(16) WaterSurfaceConstants {
            float waterHeight = 0.0f;
            uint32_t activeWaveCount = 0;
            float time = 0.0f;
            uint32_t simulationType = kWaterSurfaceModelTypeGerstner;
            WaterWaveParam waves[kMaxWaterSurfaceWaveCount]{};
        };

        enum class DispatchGuardStatus : uint32_t {
            Ok = 0,
            NotInitialized,
            RayTracingUnsupported,
            NoBLAS,
            InvalidCommandList,
            CommandList4Unavailable,
            OutputAllocationFailed,
        };

        bool InitializeBase(
            DirectXCommon* dxCommon,
            DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr,
            const char* ownerName);

        bool EnsureSurfaceConstantBuffer(UINT bufferSize, const char* ownerName);

        bool EnsureOutputTextureBase(
            UINT width,
            UINT height,
            uint32_t viewIndex,
            const char* ownerName,
            const std::string& uavDebugName,
            const std::string& srvDebugName,
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT);

        void ReleaseOutputIfSizeMismatchBase(UINT width, UINT height, uint32_t viewIndex);

        D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRVHandleBase(uint32_t viewIndex) const;
        ID3D12Resource* GetOutputResourceBase(uint32_t viewIndex) const;
        D3D12_RESOURCE_STATES& GetOutputCurrentStateBase(uint32_t viewIndex);

        DispatchGuardStatus ValidateDispatchPreconditions(ID3D12GraphicsCommandList* cmdList) const;
        bool QueryCommandList4(
            ID3D12GraphicsCommandList* cmdList,
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
            const char* ownerName) const;
        void BeginOutputWrite(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* outputResource,
            D3D12_RESOURCE_STATES& outputCurrentState) const;
        void EndOutputWrite(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* outputResource,
            D3D12_RESOURCE_STATES& outputCurrentState,
            D3D12_RESOURCE_STATES finalState) const;
        bool PrepareDispatchResources(
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
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT);
        static constexpr UINT GetSurfaceConstantBufferSize()
        {
            return (sizeof(WaterSurfaceConstants) + 255) & ~255;
        }
        WaterSurfaceConstants BuildSurfaceConstants(const WaterSurfaceData& surfaceData) const;
        void UploadSurfaceConstants(const WaterSurfaceConstants& surfaceConstants, const char* ownerName) const;
        WaterSurfaceConstants UploadSurfaceDataForDispatch(const WaterSurfaceData& surfaceData, const char* ownerName) const;
        void SetSurfaceModelProviderBase(const IWaterSurfaceModelProvider* provider);
        const IWaterSurfaceModelProvider* GetSurfaceModelProviderBase() const;
        const WaterSurfaceData& ResolveSurfaceDataForDispatch(
            const WaterSurfaceData& fallbackSurfaceData,
            WaterSurfaceData& outResolvedSurfaceData,
            const char* ownerName) const;

        DirectXCommon* dxCommon_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        AccelerationStructureManager* asMgr_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        uint8_t* constantBufferMapped_ = nullptr;
        GlobalRootSignatureManager globalRootSigMgr_;
        Microsoft::WRL::ComPtr<ID3D12StateObject> stateObject_;
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> stateObjectProperties_;
        ShaderTableBuilder shaderTableBuilder_;
        RayTracingOutputViewSet outputViews_;
        const IWaterSurfaceModelProvider* surfaceModelProvider_ = nullptr;

        bool isInitialized_ = false;
    };
}
