#include "pch.h"
#include "WaterConstantBufferSet.h"

#include <cassert>
#include <cstring>

void WaterConstantBufferSet::Initialize(ID3D12Device* device) {
    assert(device);

    CreateBuffer(device, kWaterCBSize, waterCBResource_, waterCBGpuAddress_, waterCBMapped_);
    CreateBuffer(device, kFrameCBSize, frameCBResource_, frameCBGpuAddress_, frameCBMapped_);
    CreateBuffer(device, kFrameCBSize, reflectionFrameCBResource_, reflectionFrameCBGpuAddress_, reflectionFrameCBMapped_);
}

D3D12_GPU_VIRTUAL_ADDRESS WaterConstantBufferSet::GetWaterCBGpuAddress() const {
    return waterCBGpuAddress_;
}

D3D12_GPU_VIRTUAL_ADDRESS WaterConstantBufferSet::GetFrameCBGpuAddress(bool useReflectionFrame) const {
    return useReflectionFrame ? reflectionFrameCBGpuAddress_ : frameCBGpuAddress_;
}

void WaterConstantBufferSet::UpdateWaterConstants(const WaterConstants& waterConstants) {
    if (!waterCBMapped_) {
        return;
    }

    // 波パラメータと経過時間を描画前に GPU へ反映する
    std::memcpy(waterCBMapped_, &waterConstants, sizeof(WaterConstants));
}

void WaterConstantBufferSet::UpdateFrameConstants(const WaterFrameConstants& frameConstants, bool useReflectionFrame) {
    uint8_t* targetMapped = useReflectionFrame ? reflectionFrameCBMapped_ : frameCBMapped_;
    if (!targetMapped) {
        return;
    }

    // 通常描画用または反射パス用のフレーム定数を切り替えて転送する
    std::memcpy(targetMapped, &frameConstants, sizeof(WaterFrameConstants));
}

void WaterConstantBufferSet::CreateBuffer(
    ID3D12Device* device,
    UINT bufferSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress,
    uint8_t*& mappedData) {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bufferSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    const HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
        resource.Reset();
        gpuAddress = 0;
        mappedData = nullptr;
        return;
    }
    assert(SUCCEEDED(hr));

    gpuAddress = resource->GetGPUVirtualAddress();

    // Upload Heap は常時マップしたまま使用する
    D3D12_RANGE readRange = { 0, 0 };
    resource->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
}
