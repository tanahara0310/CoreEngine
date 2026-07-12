#include "pch.h"
#include "WaterRenderResources.h"

bool WaterRenderResources::HasReflectionTexture() const {
    return reflectionSRV.ptr != 0;
}

bool WaterRenderResources::HasSceneDepth() const {
    return sceneDepthSRV.ptr != 0;
}

bool WaterRenderResources::HasSceneColor() const {
    return sceneColorSRV.ptr != 0;
}

bool WaterRenderResources::HasRefractionColor() const {
    return refractionColorSRV.ptr != 0;
}

bool WaterRenderResources::HasFFTOceanTextureSRVs() const {
    return fftDisplacementSRV.ptr != 0 && fftNormalSRV.ptr != 0;
}

void WaterRenderResources::SetFFTOceanTextureSRVs(
    D3D12_GPU_DESCRIPTOR_HANDLE displacementSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE normalSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE jacobianSrvHandle) {
    // FFT Ocean の描画に必要な 3 種の SRV を一括で更新する
    fftDisplacementSRV = displacementSrvHandle;
    fftNormalSRV = normalSrvHandle;
    fftJacobianSRV = jacobianSrvHandle;
}
