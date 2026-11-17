#include "pch.h"
#include "FFTOceanResourceFactory.h"

#include <stdexcept>
#include <string>

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UavTexture.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
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

        const D3D12_RESOURCE_DESC textureDesc =
            UavTexture::MakeDesc(resolution, resolution, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);

        auto createOne = [&](FFTOceanGpuTexture& tex, const char* label, uint32_t index) -> bool {
            const std::string name = std::string("FFTOceanSpectrum") + label;
            const std::string idx = std::to_string(index);
            return UavTexture::Create(device, *descriptorAllocator, tex, textureDesc,
                tex.srv, name + "_SRV_" + idx, tex.uav, name + "_UAV_" + idx);
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
        GpuResource& spectrumBuffer = outSet.defaultBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource>& spectrumUploadBuffer = outSet.uploadBuffer;
        void*& mappedSpectrumSamples = outSet.mapped;
        DescriptorHandle& spectrumSrvHandle = outSet.srv;

        if (!device || !descriptorAllocator || sampleStride == 0) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;

        const uint64_t sampleCount = static_cast<uint64_t>(resolution) * static_cast<uint64_t>(resolution);
        const uint64_t bufferSize = sampleStride * sampleCount;

        const D3D12_RESOURCE_DESC desc = ResourceFactory::MakeBufferDesc(bufferSize);

        try {
            spectrumBuffer.Reset(
                ResourceFactory::CreateTextureResource(
                    deviceRef,
                    desc,
                    D3D12_RESOURCE_STATE_COPY_DEST),
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

        return true;
    }
}
