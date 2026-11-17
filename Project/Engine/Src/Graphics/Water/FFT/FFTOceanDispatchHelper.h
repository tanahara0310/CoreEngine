#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Graphics/Water/FFT/FFTOceanGpuResources.h"

#include <cstdint>

namespace CoreEngine
{
    /// @brief FFT Ocean のGPUディスパッチ実処理を分離するヘルパー
    /// @details リソースは FFTOceanGpuTexture（本体＋ステート＋ビューの束）で受け取る。
    ///          束ねの表はパイプラインの持ち主（FFTOceanManager）が FFTOceanBindings.h の宣言表で引いて渡す。
    class FFTOceanDispatchHelper {
    public:
        /// @brief 時間発展パスを実行する
        /// @details 書き込み先は ping-pong の index 0（A/B とも）。index 1 には触れない。
        /// @param evolutionBindings FFTOceanEvolutionBind::kDecls で引いた表
        static void DispatchEvolutionPass(
            ID3D12GraphicsCommandList* cmdList,
            FFTOceanPingPong& spectrumA,
            FFTOceanPingPong& spectrumB,
            CustomShaderPipeline& evolutionPipeline,
            const BindingTable& evolutionBindings,
            const DescriptorHandle& spectrumSrvHandle,
            D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
            uint32_t resolution);

        /// @brief IFFT単一ステージを実行する（input を SRV、output を UAV へ遷移して1回 Dispatch）
        /// @param bindings FFTOceanIFFTBind::kDecls で引いた表
        static void DispatchIFFTPass(
            ID3D12GraphicsCommandList* cmdList,
            FFTOceanGpuTexture& input,
            FFTOceanGpuTexture& output,
            CustomShaderPipeline& pipeline,
            const BindingTable& bindings,
            D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress,
            uint32_t resolution);

        /// @brief 最終出力パスを実行する（出力先スライス UAV は呼び出し側がカスケードから選ぶ）
        /// @param finalizeBindings FFTOceanFinalizeBind::kDecls で引いた表
        static void DispatchFinalizePass(
            ID3D12GraphicsCommandList* cmdList,
            FFTOceanGpuTexture& spectrumA,
            FFTOceanGpuTexture& spectrumB,
            CustomShaderPipeline& finalizePipeline,
            const BindingTable& finalizeBindings,
            D3D12_GPU_DESCRIPTOR_HANDLE displacementUavHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE normalUavHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE jacobianUavHandle,
            D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
            uint32_t resolution);
    };
}
