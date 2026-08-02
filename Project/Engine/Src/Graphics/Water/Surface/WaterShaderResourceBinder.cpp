#include "pch.h"
#include "WaterShaderResourceBinder.h"

#include "Graphics/Pipeline/CustomShaderPipeline.h"

namespace CoreEngine
{

void WaterShaderResourceBinder::Bind(
    ID3D12GraphicsCommandList* cmdList,
    const CustomShaderPipeline* pipeline,
    D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress,
    D3D12_GPU_VIRTUAL_ADDRESS frameCBGpuAddress,
    const WaterRenderResources& renderResources) {
    if (!cmdList || !pipeline || waterCBGpuAddress == 0 || frameCBGpuAddress == 0) {
        return;
    }

    // WaterConstants を b4 にバインドする
    const int waterConstantsSlot = pipeline->GetRootParamIndex("WaterConstants");
    if (waterConstantsSlot >= 0) {
        cmdList->SetGraphicsRootConstantBufferView(
            static_cast<UINT>(waterConstantsSlot),
            waterCBGpuAddress);
    }

    // WaterFrameConstants を b5 にバインドする
    const int frameConstantsSlot = pipeline->GetRootParamIndex("WaterFrameConstants");
    if (frameConstantsSlot >= 0) {
        cmdList->SetGraphicsRootConstantBufferView(
            static_cast<UINT>(frameConstantsSlot),
            frameCBGpuAddress);
    }

    // 反射テクスチャ SRV をバインドする
    if (renderResources.HasReflectionTexture()) {
        const int reflectionSlot = pipeline->GetRootParamIndex("gReflectionTexture");
        if (reflectionSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(reflectionSlot),
                renderResources.reflectionSRV);
        }
    }

    // シーン深度 SRV をバインドする
    if (renderResources.HasSceneDepth()) {
        const int sceneDepthSlot = pipeline->GetRootParamIndex("gSceneDepth");
        if (sceneDepthSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(sceneDepthSlot),
                renderResources.sceneDepthSRV);
        }
    }

    // シーンカラー SRV をバインドする
    if (renderResources.HasSceneColor()) {
        const int sceneColorSlot = pipeline->GetRootParamIndex("gSceneColor");
        if (sceneColorSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(sceneColorSlot),
                renderResources.sceneColorSRV);
        }
    }

    // レイトレーシング屈折カラー SRV をバインドする
    if (renderResources.HasRefractionColor()) {
        const int refractionColorSlot = pipeline->GetRootParamIndex("gRTWaterRefractionColor");
        if (refractionColorSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(refractionColorSlot),
                renderResources.refractionColorSRV);
        }
    }

    // FFT Ocean のテクスチャ群をバインドする
    if (renderResources.fftDisplacementSRV.ptr != 0) {
        const int fftDisplacementSlot = pipeline->GetRootParamIndex("gFFTOceanDisplacement");
        if (fftDisplacementSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(fftDisplacementSlot),
                renderResources.fftDisplacementSRV);
        }
    }

    if (renderResources.fftNormalSRV.ptr != 0) {
        const int fftNormalSlot = pipeline->GetRootParamIndex("gFFTOceanNormal");
        if (fftNormalSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(fftNormalSlot),
                renderResources.fftNormalSRV);
        }
    }

    if (renderResources.fftJacobianSRV.ptr != 0) {
        const int fftJacobianSlot = pipeline->GetRootParamIndex("gFFTOceanJacobian");
        if (fftJacobianSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(fftJacobianSlot),
                renderResources.fftJacobianSRV);
        }
    }

    if (renderResources.fftFoamSRV.ptr != 0) {
        const int fftFoamSlot = pipeline->GetRootParamIndex("gFFTOceanFoam");
        if (fftFoamSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(fftFoamSlot),
                renderResources.fftFoamSRV);
        }
    }

    // 大気散乱（Aerial Perspective）のリソース群をバインドする
    // （未接続のフレームはシェーダー側フラグ gAerialPerspectiveEnabled=0 で参照されない）
    if (renderResources.HasAtmosphere()) {
        const int atmosphereSlot = pipeline->GetRootParamIndex("gAtmosphereAP");
        if (atmosphereSlot >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(
                static_cast<UINT>(atmosphereSlot),
                renderResources.atmosphereCB);
        }

        const int cameraVolumeSlot = pipeline->GetRootParamIndex("gCameraVolumeLUT");
        if (cameraVolumeSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(cameraVolumeSlot),
                renderResources.cameraVolumeSRV);
        }

        const int skyViewSlot = pipeline->GetRootParamIndex("gSkyViewLUTAP");
        if (skyViewSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(skyViewSlot),
                renderResources.skyViewSRV);
        }
    }

    // 空アンビエント SH（水中インスキャッタの天空光）をバインドする
    // （未接続のフレームはシェーダー側フラグ gSkyAmbientEnabled=0 で参照されない）
    if (renderResources.skyIrradianceSRV.ptr != 0) {
        const int skyIrradianceSlot = pipeline->GetRootParamIndex("gWaterSkyIrradianceSH");
        if (skyIrradianceSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(skyIrradianceSlot),
                renderResources.skyIrradianceSRV);
        }
    }

    // 空スペキュラキューブマップ（平面反射への雲合成）をバインドする
    // （未接続のフレームはシェーダー側フラグ gSkyEnvReflectionEnabled=0 で参照されない）
    if (renderResources.skyEnvironmentSRV.ptr != 0) {
        const int skyEnvSlot = pipeline->GetRootParamIndex("gSkyEnvironmentMap");
        if (skyEnvSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(skyEnvSlot),
                renderResources.skyEnvironmentSRV);
        }
    }
}
}
