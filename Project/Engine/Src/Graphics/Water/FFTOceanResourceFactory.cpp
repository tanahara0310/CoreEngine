#include "pch.h"
#include "FFTOceanResourceFactory.h"

#include <stdexcept>
#include <string>

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief 定数バッファ要件の 256 バイト境界へ切り上げる
        constexpr UINT Align256(UINT value)
        {
            return (value + 255) & ~255;
        }

        /// @brief FFT 中間テクスチャ用の 2D リソース記述を作る（UAV 兼 SRV）
        D3D12_RESOURCE_DESC MakeTexture2DDesc(uint32_t resolution, DXGI_FORMAT format, uint32_t mipLevels = 1)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = resolution;
            desc.Height = resolution;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = static_cast<UINT16>(mipLevels);
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return desc;
        }
    }

    bool FFTOceanResourceFactory::CreateIntermediateTextures(
        ID3D12Device* device,
        DescriptorAllocator* descriptorAllocator,
        uint32_t resolution,
        FFTOceanPingPong& spectrumA,
        FFTOceanPingPong& spectrumB)
    {
        // ping-pong の 2 枚（A/B）を同一記述で作り、SRV と UAV を両方張る。
        // IFFT はバタフライ段ごとに読み書きを入れ替えるので、どちらの向きでも使える必要がある
        if (!device || !descriptorAllocator) {
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

        auto createOne = [&](FFTOceanGpuTexture& tex, const char* label, uint32_t index) -> bool {
            try {
                tex.resource = ResourceFactory::CreateTextureResource(
                    deviceRef,
                    textureDesc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                return false;
            }
            const std::string idx = std::to_string(index);
            tex.srv = descriptorAllocator->CreateSRV(
                tex.Get(), srvDesc, std::string("FFTOceanSpectrum") + label + "_SRV_" + idx);
            tex.uav = descriptorAllocator->CreateUAV(
                tex.Get(), uavDesc, std::string("FFTOceanSpectrum") + label + "_UAV_" + idx);
            tex.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            return true;
        };

        for (uint32_t i = 0; i < 2; ++i) {
            if (!createOne(spectrumA[i], "A", i) || !createOne(spectrumB[i], "B", i)) {
                return false;
            }
        }

        return true;
    }

    // 初期スペクトル h0 を置く StructuredBuffer と、その転送用アップロードバッファを作る。
    // 中身は CPU 側（FFTOceanSpectrumBuilder）が作って 1 度だけ流し込む
    bool FFTOceanResourceFactory::CreateSpectrumBuffers(
        ID3D12Device* device,
        DescriptorAllocator* descriptorAllocator,
        uint32_t resolution,
        uint32_t sampleStride,
        FFTOceanSpectrumBufferSet& outSet)
    {
        Microsoft::WRL::ComPtr<ID3D12Resource>& spectrumBuffer = outSet.defaultBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource>& spectrumUploadBuffer = outSet.uploadBuffer;
        void*& mappedSpectrumSamples = outSet.mapped;
        DescriptorHandle& spectrumSrvHandle = outSet.srv;
        D3D12_RESOURCE_STATES& spectrumBufferState = outSet.state;

        if (!device || !descriptorAllocator || sampleStride == 0) {
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

        // SRV は DEFAULT ヒープ側に作る。UPLOAD 側を読ませると時間発展CSが
        // 毎フレーム全サンプルを PCIe 経由で読むことになる（VB/IB UPLOAD 常駐事故と同型）。
        // CPU が書いた UPLOAD 側は spectrumBufferDirty 経由で次の Dispatch 時にコピーされる。
        spectrumSrvHandle = descriptorAllocator->CreateSRV(spectrumBuffer.Get(), srvDesc, "FFTOceanSpectrumSamplesSRV");

        spectrumBufferState = D3D12_RESOURCE_STATE_COPY_DEST;
        return true;
    }

    // シミュレーション定数バッファ。UPLOAD ヒープで常時 Map したまま毎フレーム書き換える
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
