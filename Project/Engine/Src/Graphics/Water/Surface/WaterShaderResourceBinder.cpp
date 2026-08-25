#include "pch.h"
#include "WaterShaderResourceBinder.h"

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Water/Surface/WaterBindings.h"

namespace CoreEngine
{

void WaterShaderResourceBinder::EnsureResolved(const CustomShaderPipeline* pipeline)
{
    const void* rs = pipeline->GetForwardRootSignature();
    if (rs == resolvedRootSignature_) {
        return;
    }

    const ShaderReflectionData* reflection = pipeline->GetForwardReflection();
    if (!reflection) {
        return;
    }

    // 水面固有リソースだけの契約。カメラ・ライト・IBL はエンジン側（ModelBind::kCustom）が
    // 持つので、宣言外のリソースがあっても警告しない。
    table_ = BindingTable::Resolve(
        *reflection, WaterBind::kDecls, "WaterSurface", /*warnUndeclared=*/false);
    resolvedRootSignature_ = rs;
}

void WaterShaderResourceBinder::Bind(
    ID3D12GraphicsCommandList* cmdList,
    const CustomShaderPipeline* pipeline,
    D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress,
    D3D12_GPU_VIRTUAL_ADDRESS frameCBGpuAddress,
    const WaterRenderResources& renderResources) {
    if (!cmdList || !pipeline || waterCBGpuAddress == 0 || frameCBGpuAddress == 0) {
        return;
    }

    EnsureResolved(pipeline);

    ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);

    // 水面本体の定数バッファ
    binder.Set(table_[WaterBind::WaterConstants], waterCBGpuAddress);
    binder.Set(table_[WaterBind::WaterFrameConstants], frameCBGpuAddress);

    // 反射 / シーン深度 / シーンカラー / レイトレ屈折カラー
    if (renderResources.HasReflectionTexture()) {
        binder.Set(table_[WaterBind::gReflectionTexture], renderResources.reflectionSRV);
    }
    if (renderResources.HasSceneDepth()) {
        binder.Set(table_[WaterBind::gSceneDepth], renderResources.sceneDepthSRV);
    }
    if (renderResources.HasSceneColor()) {
        binder.Set(table_[WaterBind::gSceneColor], renderResources.sceneColorSRV);
    }
    if (renderResources.HasRefractionColor()) {
        binder.Set(table_[WaterBind::gRTWaterRefractionColor], renderResources.refractionColorSRV);
    }

    // FFT Ocean のテクスチャ群（FFT を使わないシェーダーでは未解決スロットなので no-op）
    if (renderResources.fftDisplacementSRV.ptr != 0) {
        binder.Set(table_[WaterBind::gFFTOceanDisplacement], renderResources.fftDisplacementSRV);
    }
    if (renderResources.fftNormalSRV.ptr != 0) {
        binder.Set(table_[WaterBind::gFFTOceanNormal], renderResources.fftNormalSRV);
    }
    if (renderResources.fftJacobianSRV.ptr != 0) {
        binder.Set(table_[WaterBind::gFFTOceanJacobian], renderResources.fftJacobianSRV);
    }
    if (renderResources.fftFoamSRV.ptr != 0) {
        binder.Set(table_[WaterBind::gFFTOceanFoam], renderResources.fftFoamSRV);
    }

    // 大気散乱（Aerial Perspective）
    // （未接続のフレームはシェーダー側フラグ gAerialPerspectiveEnabled=0 で参照されない）
    if (renderResources.HasAtmosphere()) {
        binder.Set(table_[WaterBind::gAtmosphereAP], renderResources.atmosphereCB);
        binder.Set(table_[WaterBind::gCameraVolumeLUT], renderResources.cameraVolumeSRV);
        binder.Set(table_[WaterBind::gSkyViewLUTAP], renderResources.skyViewSRV);
    }

    // 空アンビエント SH（水中インスキャッタの天空光）
    // （未接続のフレームはシェーダー側フラグ gSkyAmbientEnabled=0 で参照されない）
    if (renderResources.skyIrradianceSRV.ptr != 0) {
        binder.Set(table_[WaterBind::gWaterSkyIrradianceSH], renderResources.skyIrradianceSRV);
    }

    // 空スペキュラキューブマップ（平面反射への雲合成）
    // （未接続のフレームはシェーダー側フラグ gSkyEnvReflectionEnabled=0 で参照されない）
    if (renderResources.skyEnvironmentSRV.ptr != 0) {
        binder.Set(table_[WaterBind::gSkyEnvironmentMap], renderResources.skyEnvironmentSRV);
    }
}
}
