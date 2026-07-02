#include "pch.h"
#include "FFTOceanResourceFactory.h"

#include <stdexcept>
#include <string>

#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        constexpr UINT Align256(UINT value)
        {
            return (value + 255) & ~255;
        }

        D3D12_RESOURCE_DESC MakeTexture2DDesc(uint32_t resolution, DXGI_FORMAT format)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = resolution;
            desc.Height = resolution;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return desc;
        }
    }

    bool FFTOceanResourceFactory::CreateOutputTextures(
        ID3D12Device* device,
        DescriptorManager* descriptorManager,
        uint32_t resolution,
        Microsoft::WRL::ComPtr<ID3D12Resource>& displacementTexture,
        Microsoft::WRL::ComPtr<ID3D12Resource>& normalTexture,
        Microsoft::WRL::ComPtr<ID3D12Resource>& jacobianTexture,
        D3D12_CPU_DESCRIPTOR_HANDLE& displacementSrvCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& displacementSrvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE& displacementUavCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& displacementUavHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE& normalSrvCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& normalSrvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE& normalUavCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& normalUavHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE& jacobianSrvCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& jacobianSrvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE& jacobianUavCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& jacobianUavHandle,
        D3D12_RESOURCE_STATES& displacementState,
        D3D12_RESOURCE_STATES& normalState,
        D3D12_RESOURCE_STATES& jacobianState)
    {
        if (!device || !descriptorManager) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;

        const D3D12_RESOURCE_DESC textureDesc = MakeTexture2DDesc(resolution, DXGI_FORMAT_R16G16B16A16_FLOAT);
        try {
            displacementTexture = ResourceFactory::CreateTextureResource(
                deviceRef,
                textureDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            jacobianTexture = ResourceFactory::CreateTextureResource(
                deviceRef,
                textureDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            normalTexture = ResourceFactory::CreateTextureResource(
                deviceRef,
                textureDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        descriptorManager->CreateSRV(
            displacementTexture.Get(),
            srvDesc,
            displacementSrvCpuHandle,
            displacementSrvHandle,
            "FFTOceanDisplacementSRV");
        descriptorManager->CreateSRV(
            normalTexture.Get(),
            srvDesc,
            normalSrvCpuHandle,
            normalSrvHandle,
            "FFTOceanNormalSRV");
        descriptorManager->CreateSRV(
            jacobianTexture.Get(),
            srvDesc,
            jacobianSrvCpuHandle,
            jacobianSrvHandle,
            "FFTOceanJacobianSRV");

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = textureDesc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        descriptorManager->CreateUAV(
            displacementTexture.Get(),
            uavDesc,
            displacementUavCpuHandle,
            displacementUavHandle,
            "FFTOceanDisplacementUAV");
        descriptorManager->CreateUAV(
            normalTexture.Get(),
            uavDesc,
            normalUavCpuHandle,
            normalUavHandle,
            "FFTOceanNormalUAV");
        descriptorManager->CreateUAV(
            jacobianTexture.Get(),
            uavDesc,
            jacobianUavCpuHandle,
            jacobianUavHandle,
            "FFTOceanJacobianUAV");

        displacementState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        normalState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        jacobianState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return true;
    }

    bool FFTOceanResourceFactory::CreateIntermediateTextures(
        ID3D12Device* device,
        DescriptorManager* descriptorManager,
        uint32_t resolution,
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>& spectrumTextureA,
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>& spectrumTextureB,
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2>& spectrumASrvCpuHandle,
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>& spectrumASrvHandle,
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2>& spectrumAUavCpuHandle,
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>& spectrumAUavHandle,
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2>& spectrumBSrvCpuHandle,
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>& spectrumBSrvHandle,
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2>& spectrumBUavCpuHandle,
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>& spectrumBUavHandle,
        std::array<D3D12_RESOURCE_STATES, 2>& spectrumAState,
        std::array<D3D12_RESOURCE_STATES, 2>& spectrumBState)
    {
        if (!device || !descriptorManager) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;

        const D3D12_RESOURCE_DESC textureDesc = MakeTexture2DDesc(resolution, DXGI_FORMAT_R32G32B32A32_FLOAT);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = textureDesc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        for (uint32_t i = 0; i < 2; ++i) {
            try {
                spectrumTextureA[i] = ResourceFactory::CreateTextureResource(
                    deviceRef,
                    textureDesc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                spectrumTextureB[i] = ResourceFactory::CreateTextureResource(
                    deviceRef,
                    textureDesc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                return false;
            }

            descriptorManager->CreateSRV(
                spectrumTextureA[i].Get(),
                srvDesc,
                spectrumASrvCpuHandle[i],
                spectrumASrvHandle[i],
                std::string("FFTOceanSpectrumA_SRV_") + std::to_string(i));
            descriptorManager->CreateUAV(
                spectrumTextureA[i].Get(),
                uavDesc,
                spectrumAUavCpuHandle[i],
                spectrumAUavHandle[i],
                std::string("FFTOceanSpectrumA_UAV_") + std::to_string(i));

            descriptorManager->CreateSRV(
                spectrumTextureB[i].Get(),
                srvDesc,
                spectrumBSrvCpuHandle[i],
                spectrumBSrvHandle[i],
                std::string("FFTOceanSpectrumB_SRV_") + std::to_string(i));
            descriptorManager->CreateUAV(
                spectrumTextureB[i].Get(),
                uavDesc,
                spectrumBUavCpuHandle[i],
                spectrumBUavHandle[i],
                std::string("FFTOceanSpectrumB_UAV_") + std::to_string(i));

            spectrumAState[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            spectrumBState[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        return true;
    }

    bool FFTOceanResourceFactory::CreateSpectrumBuffers(
        ID3D12Device* device,
        DescriptorManager* descriptorManager,
        uint32_t resolution,
        uint32_t sampleStride,
        Microsoft::WRL::ComPtr<ID3D12Resource>& spectrumBuffer,
        Microsoft::WRL::ComPtr<ID3D12Resource>& spectrumUploadBuffer,
        void*& mappedSpectrumSamples,
        D3D12_CPU_DESCRIPTOR_HANDLE& spectrumSrvCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& spectrumSrvHandle,
        D3D12_RESOURCE_STATES& spectrumBufferState,
        bool& spectrumBufferDirty)
    {
        if (!device || !descriptorManager || sampleStride == 0) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;

        const uint64_t sampleCount = static_cast<uint64_t>(resolution) * static_cast<uint64_t>(resolution);
        const uint64_t bufferSize = sampleStride * sampleCount;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bufferSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        try {
            spectrumBuffer = ResourceFactory::CreateTextureResource(
                deviceRef,
                desc,
                D3D12_RESOURCE_STATE_COPY_DEST);
            spectrumUploadBuffer = ResourceFactory::CreateBufferResource(
                deviceRef,
                static_cast<size_t>(bufferSize),
                D3D12_HEAP_TYPE_UPLOAD);
        }
        catch (const std::exception&) {
            return false;
        }

        D3D12_RANGE readRange{ 0, 0 };
        const HRESULT hr = spectrumUploadBuffer->Map(0, &readRange, &mappedSpectrumSamples);
        if (FAILED(hr) || !mappedSpectrumSamples) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanResourceFactory: upload spectrum buffer map failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        std::memset(mappedSpectrumSamples, 0, static_cast<size_t>(bufferSize));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = static_cast<UINT>(sampleCount);
        srvDesc.Buffer.StructureByteStride = sampleStride;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        descriptorManager->CreateSRV(
            spectrumUploadBuffer.Get(),
            srvDesc,
            spectrumSrvCpuHandle,
            spectrumSrvHandle,
            "FFTOceanSpectrumSamplesSRV");

        spectrumBufferState = D3D12_RESOURCE_STATE_COPY_DEST;
        spectrumBufferDirty = true;
        return true;
    }

    bool FFTOceanResourceFactory::CreateSimulationConstantBuffer(
        ID3D12Device* device,
        uint32_t constantSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& simulationConstantsBuffer,
        void*& mappedSimulationConstants)
    {
        if (!device || constantSize == 0) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;
        try {
            simulationConstantsBuffer = ResourceFactory::CreateBufferResource(
                deviceRef,
                Align256(constantSize),
                D3D12_HEAP_TYPE_UPLOAD);
        }
        catch (const std::exception&) {
            return false;
        }

        D3D12_RANGE readRange{ 0, 0 };
        void* mapped = nullptr;
        const HRESULT mapHr = simulationConstantsBuffer->Map(0, &readRange, &mapped);
        if (FAILED(mapHr) || !mapped) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanResourceFactory: simulation constant buffer map failed. hr={:#x}",
                static_cast<uint32_t>(mapHr));
            return false;
        }

        std::memset(mapped, 0, constantSize);
        mappedSimulationConstants = mapped;
        return true;
    }

    bool FFTOceanResourceFactory::CreateIFFTConstantBuffer(
        ID3D12Device* device,
        uint32_t constantSize,
        uint32_t maxPassCount,
        Microsoft::WRL::ComPtr<ID3D12Resource>& ifftConstantsBuffer,
        uint8_t*& mappedIFFTConstantsData)
    {
        if (!device || constantSize == 0 || maxPassCount == 0) {
            return false;
        }

        const size_t ifftBufferBytes = static_cast<size_t>(Align256(constantSize)) * maxPassCount;
        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;
        try {
            ifftConstantsBuffer = ResourceFactory::CreateBufferResource(
                deviceRef,
                ifftBufferBytes,
                D3D12_HEAP_TYPE_UPLOAD);
        }
        catch (const std::exception&) {
            return false;
        }

        D3D12_RANGE readRange{ 0, 0 };
        void* mapped = nullptr;
        const HRESULT mapHr = ifftConstantsBuffer->Map(0, &readRange, &mapped);
        if (FAILED(mapHr) || !mapped) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanResourceFactory: IFFT constant buffer map failed. hr={:#x}",
                static_cast<uint32_t>(mapHr));
            return false;
        }

        std::memset(mapped, 0, ifftBufferBytes);
        mappedIFFTConstantsData = static_cast<uint8_t*>(mapped);
        return true;
    }
}
