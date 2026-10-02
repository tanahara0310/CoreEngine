#include "pch.h"
#include "WaterConstantBufferSet.h"

namespace CoreEngine
{

void WaterConstantBufferSet::Initialize(UploadRing& uploadRing) {
    waterConstants_.Initialize(uploadRing);
    frameConstants_.Initialize(uploadRing);
}

D3D12_GPU_VIRTUAL_ADDRESS WaterConstantBufferSet::GetWaterCBGpuAddress() const {
    return waterConstants_.Address();
}

D3D12_GPU_VIRTUAL_ADDRESS WaterConstantBufferSet::GetFrameCBGpuAddress() const {
    return frameConstants_.Address();
}

void WaterConstantBufferSet::UpdateWaterConstants(const WaterConstants& waterConstants) {
    waterConstants_.Set(waterConstants);
}

void WaterConstantBufferSet::UpdateFrameConstants(const WaterFrameConstants& frameConstants) {
    frameConstants_.Set(frameConstants);
}
}
