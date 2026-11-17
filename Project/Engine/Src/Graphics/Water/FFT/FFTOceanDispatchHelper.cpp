#include "pch.h"
#include "FFTOceanDispatchHelper.h"

#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Water/FFT/FFTOceanBindings.h"
#include "Math/IntegerMath.h"

namespace CoreEngine
{
    void FFTOceanDispatchHelper::DispatchEvolutionPass(
        ID3D12GraphicsCommandList* cmdList,
        FFTOceanPingPong& spectrumA,
        FFTOceanPingPong& spectrumB,
        CustomShaderPipeline& evolutionPipeline,
        const BindingTable& evolutionBindings,
        const DescriptorHandle& spectrumSrvHandle,
        D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
        uint32_t resolution)
    {
        // 時間発展が書くのは ping-pong の index 0 のみ。index 1 は後続の IFFT が
        // 追跡ステートから必要な状態へ遷移させるため、ここでは触らない
        // （以前は index 1 を毎回 COMMON へ落としており、カスケード×フレームごとに
        //   無意味なバリアを発行していた）。
        Barrier::Transition(
            cmdList, spectrumA[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(
            cmdList, spectrumB[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(evolutionPipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(evolutionPipeline.GetComputeRootSignature());

        namespace B = FFTOceanEvolutionBind;
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(evolutionBindings[B::gSpectrumSamples], spectrumSrvHandle.gpuHandle);
        binder.Set(evolutionBindings[B::gHeightDisplacementXOutput], spectrumA[0].uav.gpuHandle);
        binder.Set(evolutionBindings[B::gDisplacementZOutput], spectrumB[0].uav.gpuHandle);
        if (simulationConstantsAddress != 0) {
            binder.Set(evolutionBindings[B::FFTOceanSimulationConstants], simulationConstantsAddress);
        }
        binder.ValidateBeforeDraw(evolutionBindings);

        const UINT dispatchX = DivRoundUp(resolution, 8);
        const UINT dispatchY = DivRoundUp(resolution, 8);
        cmdList->Dispatch(dispatchX, dispatchY, 1);

        Barrier::UAV(cmdList, spectrumA[0]);
        Barrier::UAV(cmdList, spectrumB[0]);
    }

    void FFTOceanDispatchHelper::DispatchIFFTPass(
        ID3D12GraphicsCommandList* cmdList,
        FFTOceanGpuTexture& input,
        FFTOceanGpuTexture& output,
        CustomShaderPipeline& pipeline,
        const BindingTable& bindings,
        D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress,
        uint32_t resolution)
    {
        Barrier::Transition(cmdList, input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(pipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(pipeline.GetComputeRootSignature());

        namespace B = FFTOceanIFFTBind;
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings[B::gInputSpectrum], input.srv.gpuHandle);
        binder.Set(bindings[B::gOutputSpectrum], output.uav.gpuHandle);
        if (ifftConstantsGpuAddress != 0) {
            binder.Set(bindings[B::FFTOceanIFFTConstants], ifftConstantsGpuAddress);
        }
        binder.ValidateBeforeDraw(bindings);

        const UINT dispatchX = DivRoundUp(resolution, 8);
        const UINT dispatchY = DivRoundUp(resolution, 8);
        cmdList->Dispatch(dispatchX, dispatchY, 1);
        Barrier::UAV(cmdList, output);
    }

    void FFTOceanDispatchHelper::DispatchFinalizePass(
        ID3D12GraphicsCommandList* cmdList,
        FFTOceanGpuTexture& spectrumA,
        FFTOceanGpuTexture& spectrumB,
        CustomShaderPipeline& finalizePipeline,
        const BindingTable& finalizeBindings,
        D3D12_GPU_DESCRIPTOR_HANDLE displacementUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianUavHandle,
        D3D12_GPU_VIRTUAL_ADDRESS simulationConstantsAddress,
        uint32_t resolution)
    {
        Barrier::Transition(cmdList, spectrumA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, spectrumB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetPipelineState(finalizePipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(finalizePipeline.GetComputeRootSignature());

        namespace B = FFTOceanFinalizeBind;
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(finalizeBindings[B::gHeightDisplacementXSpectrum], spectrumA.srv.gpuHandle);
        binder.Set(finalizeBindings[B::gDisplacementZSpectrum], spectrumB.srv.gpuHandle);
        binder.Set(finalizeBindings[B::gDisplacementOutput], displacementUavHandle);
        binder.Set(finalizeBindings[B::gNormalOutput], normalUavHandle);
        binder.Set(finalizeBindings[B::gJacobianOutput], jacobianUavHandle);
        if (simulationConstantsAddress != 0) {
            binder.Set(finalizeBindings[B::FFTOceanSimulationConstants], simulationConstantsAddress);
        }
        binder.ValidateBeforeDraw(finalizeBindings);

        const UINT dispatchX = DivRoundUp(resolution, 8);
        const UINT dispatchY = DivRoundUp(resolution, 8);
        cmdList->Dispatch(dispatchX, dispatchY, 1);
    }
}
